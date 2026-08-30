#include "slang_slim.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <exception>
#include <future>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using Slang::ComPtr;

bool sameGuid(const SlangUUID& left, const SlangUUID& right) noexcept
{
    return std::memcmp(&left, &right, sizeof(SlangUUID)) == 0;
}

slang_slim_status mapSlangStatus(SlangResult status) noexcept
{
    if (SLANG_SUCCEEDED(status))
        return SLANG_SLIM_STATUS_OK;
    if (status == SLANG_E_INVALID_ARG || status == SLANG_E_INVALID_HANDLE)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    if (status == SLANG_E_OUT_OF_MEMORY)
        return SLANG_SLIM_STATUS_OUT_OF_MEMORY;
    if (status == SLANG_E_NOT_IMPLEMENTED || status == SLANG_E_NOT_AVAILABLE)
        return SLANG_SLIM_STATUS_UNSUPPORTED;
    if (status == SLANG_E_NOT_FOUND)
        return SLANG_SLIM_STATUS_NOT_FOUND;
    if (status == SLANG_E_CANNOT_OPEN)
        return SLANG_SLIM_STATUS_IO_ERROR;
    return SLANG_SLIM_STATUS_COMPILE_ERROR;
}

SlangResult createSlangGlobalSession(ComPtr<slang::IGlobalSession>& outSession) noexcept
{
    SlangGlobalSessionDesc desc = {};
    desc.structureSize = sizeof(SlangGlobalSessionDesc);
    desc.apiVersion = SLANG_API_VERSION;
    desc.minLanguageVersion = SLANG_LANGUAGE_VERSION_2025;
    desc.enableGLSL = false;
    return slang_createGlobalSession2(&desc, outSession.writeRef());
}

void shutdownSlangWorkerAtExit() noexcept;

thread_local slang::IGlobalSession* gCurrentSlangGlobalSession = nullptr;
// Slang's per-linkage source/file caches can retain references into a custom
// file system after the linkage is released. Keep VFS-backed sessions alive
// for the worker lifetime so a later compile cannot observe dangling cache
// state. Disk-only sessions follow normal RAII and do not accumulate here.
thread_local std::vector<ComPtr<slang::ISession>>* gRetainedSlangSessions = nullptr;

class SlangWorker final
{
public:
    template<typename Function>
    void run(Function&& function)
    {
        ensureStarted();
        if (isWorkerThread() && gCurrentSlangGlobalSession)
        {
            function(gCurrentSlangGlobalSession);
            return;
        }
        using FunctionType = std::decay_t<Function>;
        auto task = std::make_shared<std::packaged_task<void()>>(
            [this, function = FunctionType(std::forward<Function>(function))]() mutable {
                function(m_globalSession);
            });
        auto completion = task->get_future();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.emplace_back([task] { (*task)(); });
        }
        m_condition.notify_one();
        completion.get();
    }

    void shutdown() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_condition.notify_one();
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    void ensureStarted()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_started)
            return;
        m_thread = std::thread([this] { workerMain(); });
        m_started = true;
    }

    void workerMain() noexcept
    {
        ComPtr<slang::IGlobalSession> globalSession;
        gRetainedSlangSessions = &m_retainedSessions;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_workerThreadId = std::this_thread::get_id();
        }
        if (SLANG_SUCCEEDED(createSlangGlobalSession(globalSession)))
        {
            m_globalSession = globalSession;
            gCurrentSlangGlobalSession = globalSession;
            std::atexit(shutdownSlangWorkerAtExit);
        }

        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
                if (m_stopping && m_tasks.empty())
                    break;
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
            task();
        }
        gCurrentSlangGlobalSession = nullptr;
        gRetainedSlangSessions = nullptr;
        m_globalSession = nullptr;
        globalSession.detach();
    }

    bool isWorkerThread()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_workerThreadId == std::this_thread::get_id();
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<std::function<void()>> m_tasks;
    std::thread m_thread;
    std::thread::id m_workerThreadId;
    slang::IGlobalSession* m_globalSession = nullptr;
    std::vector<ComPtr<slang::ISession>> m_retainedSessions;
    bool m_started = false;
    bool m_stopping = false;
};

SlangWorker* gSlangWorker = nullptr;

void shutdownSlangWorkerAtExit() noexcept
{
    if (gSlangWorker)
        gSlangWorker->shutdown();
}

SlangWorker& getSlangWorker()
{
    static SlangWorker* worker = [] {
        auto* value = new SlangWorker();
        gSlangWorker = value;
        return value;
    }();
    return *worker;
}

template<typename Function>
void runSlangWorker(Function&& function)
{
    auto& worker = getSlangWorker();
    worker.run(std::forward<Function>(function));
}

std::string normalizePath(const char* path)
{
    std::string input = path ? path : "";
    for (char& character : input)
    {
        if (character == '\\')
            character = '/';
    }

    std::string prefix;
    std::size_t cursor = 0;
    if (input.size() >= 2 && input[1] == ':')
    {
        prefix = input.substr(0, 2);
        cursor = 2;
    }

    const bool absolute = cursor < input.size() && input[cursor] == '/';
    while (cursor < input.size() && input[cursor] == '/')
        ++cursor;

    std::vector<std::string> components;
    while (cursor <= input.size())
    {
        const auto next = input.find('/', cursor);
        const auto end = next == std::string::npos ? input.size() : next;
        const std::string component = input.substr(cursor, end - cursor);
        if (component.empty() || component == ".")
        {
            // Empty path components and current-directory components have no
            // effect after normalizing separators.
        }
        else if (component == ".." && !components.empty() && components.back() != "..")
        {
            components.pop_back();
        }
        else if (component != ".." || !absolute)
        {
            components.push_back(component);
        }

        if (next == std::string::npos)
            break;
        cursor = next + 1;
    }

    std::string result = prefix;
    if (absolute)
        result.push_back('/');
    for (std::size_t index = 0; index < components.size(); ++index)
    {
        if ((absolute || !prefix.empty()) && result.size() > 0 && result.back() != '/')
            result.push_back('/');
        else if (!result.empty() && result.back() != '/' && index != 0)
            result.push_back('/');
        result += components[index];
    }

    if (result.empty())
        return ".";
    return result;
}

struct TargetSpec
{
    slang_slim_target publicTarget = 0;
    slang_slim_compile_target format = SLANG_SLIM_COMPILE_TARGET_UNKNOWN;
    const char* profile = nullptr;
    slang_slim_target_flags flags = SLANG_SLIM_TARGET_FLAGS_DEFAULT;
    bool flagsSpecified = false;
    slang_slim_floating_point_mode floatingPointMode =
        SLANG_SLIM_FLOATING_POINT_MODE_DEFAULT;
    slang_slim_line_directive_mode lineDirectiveMode = SLANG_SLIM_LINE_DIRECTIVE_MODE_DEFAULT;
    uint32_t forceGLSLScalarBufferLayout = 0;
    const slang_slim_compiler_option_entry* compilerOptions = nullptr;
    std::size_t compilerOptionCount = 0;
};

struct TargetInfo
{
    slang_slim_target publicTarget = 0;
    slang_slim_compile_target format = SLANG_SLIM_COMPILE_TARGET_UNKNOWN;
    SlangCompileTarget slangTarget = SLANG_TARGET_UNKNOWN;
    const char* profile = nullptr;
    bool text = false;
};

const char* defaultProfileForFormat(slang_slim_compile_target format) noexcept
{
    switch (format)
    {
    case SLANG_SLIM_COMPILE_TARGET_HLSL:
        return "sm_6_0";
    case SLANG_SLIM_COMPILE_TARGET_SPIRV:
        return "spirv_1_3";
    case SLANG_SLIM_COMPILE_TARGET_METAL:
    case SLANG_SLIM_COMPILE_TARGET_METAL_LIB:
        return "metallib_2_3";
    default:
        return nullptr;
    }
}

bool targetFormatProducesText(slang_slim_compile_target format) noexcept
{
    switch (format)
    {
    case SLANG_SLIM_COMPILE_TARGET_GLSL:
    case SLANG_SLIM_COMPILE_TARGET_HLSL:
    case SLANG_SLIM_COMPILE_TARGET_SPIRV_ASM:
    case SLANG_SLIM_COMPILE_TARGET_C_SOURCE:
    case SLANG_SLIM_COMPILE_TARGET_CPP_SOURCE:
    case SLANG_SLIM_COMPILE_TARGET_CUDA_SOURCE:
    case SLANG_SLIM_COMPILE_TARGET_PTX:
    case SLANG_SLIM_COMPILE_TARGET_HOST_CPP_SOURCE:
    case SLANG_SLIM_COMPILE_TARGET_METAL:
    case SLANG_SLIM_COMPILE_TARGET_METAL_LIB_ASM:
    case SLANG_SLIM_COMPILE_TARGET_WGSL:
    case SLANG_SLIM_COMPILE_TARGET_WGSL_SPIRV_ASM:
    case SLANG_SLIM_COMPILE_TARGET_CPP_HEADER:
    case SLANG_SLIM_COMPILE_TARGET_CUDA_HEADER:
    case SLANG_SLIM_COMPILE_TARGET_HOST_LLVM_IR:
    case SLANG_SLIM_COMPILE_TARGET_SHADER_LLVM_IR:
        return true;
    default:
        return false;
    }
}

bool targetAllowedOnThisPlatform(slang_slim_compile_target format) noexcept
{
#if defined(__ANDROID__)
    return format == SLANG_SLIM_COMPILE_TARGET_SPIRV;
#else
    (void)format;
    return true;
#endif
}

bool fitsSlangInt(std::size_t value) noexcept
{
    return value <= static_cast<std::size_t>(std::numeric_limits<SlangInt>::max());
}

bool getLegacyTargetSpec(slang_slim_target target, TargetSpec& out) noexcept
{
    out = {};
    out.publicTarget = target;
    switch (target)
    {
    case SLANG_SLIM_TARGET_HLSL:
        out.format = SLANG_SLIM_COMPILE_TARGET_HLSL;
        return true;
    case SLANG_SLIM_TARGET_SPIRV:
        out.format = SLANG_SLIM_COMPILE_TARGET_SPIRV;
        return true;
    case SLANG_SLIM_TARGET_METAL:
        out.format = SLANG_SLIM_COMPILE_TARGET_METAL;
        return true;
    default:
        return false;
    }
}

template<typename T>
bool hasStructPrefix(const T* value, std::size_t requiredSize) noexcept
{
    return value && value->struct_size >= requiredSize;
}

bool readTargetField(
    const uint8_t* bytes,
    std::size_t structSize,
    std::size_t offset,
    void* destination,
    std::size_t size) noexcept
{
    if (offset > structSize || size > structSize - offset)
        return false;
    std::memcpy(destination, bytes + offset, size);
    return true;
}

struct TargetArrayView
{
    const uint8_t* data = nullptr;
    std::size_t stride = 0;
};

struct CompilerOptionArrayView
{
    const slang_slim_compiler_option_entry* data = nullptr;
    std::size_t count = 0;
};

bool validateCompilerOptionArray(
    const slang_slim_compiler_option_entry* options,
    std::size_t count) noexcept
{
    if (count != 0 && !options)
        return false;
    if (count > std::numeric_limits<uint32_t>::max())
        return false;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& option = options[index];
        if (option.value.kind > SLANG_SLIM_COMPILER_OPTION_VALUE_STRING)
            return false;
        if (option.value.kind == SLANG_SLIM_COMPILER_OPTION_VALUE_STRING &&
            !option.value.string_value0)
            return false;
    }
    return true;
}

bool getCompilerOptionArray(
    const slang_slim_compile_desc* desc,
    CompilerOptionArrayView& out) noexcept
{
    out = {};
    if (!desc)
        return false;
    const std::size_t requiredSize =
        offsetof(slang_slim_compile_desc, compiler_option_count) +
        sizeof(desc->compiler_option_count);
    if (!hasStructPrefix(desc, requiredSize))
        return true;
    out.data = desc->compiler_options;
    out.count = desc->compiler_option_count;
    return validateCompilerOptionArray(out.data, out.count);
}

bool getTargetArrayView(const slang_slim_compile_desc* desc, TargetArrayView& out) noexcept
{
    out = {};
    if (!desc || !desc->targets || desc->target_count == 0)
        return false;

    const auto* data = reinterpret_cast<const uint8_t*>(desc->targets);
    uint32_t firstStructSize = 0;
    std::memcpy(&firstStructSize, data, sizeof(firstStructSize));
    const std::size_t minimumSize =
        offsetof(slang_slim_target_desc, target) + sizeof(slang_slim_target);
    if (firstStructSize < minimumSize)
        return false;

    const std::size_t stride = firstStructSize;
    if (desc->target_count > std::numeric_limits<std::size_t>::max() / stride)
        return false;
    for (std::size_t index = 0; index < desc->target_count; ++index)
    {
        const auto* element = data + index * stride;
        uint32_t structSize = 0;
        std::memcpy(&structSize, element, sizeof(structSize));
        if (structSize != firstStructSize || structSize < minimumSize)
            return false;
    }

    out.data = data;
    out.stride = stride;
    return true;
}

bool getTargetSpec(const void* targetData, TargetSpec& out) noexcept
{
    out = {};
    if (!targetData)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(targetData);
    uint32_t structSize = 0;
    std::memcpy(&structSize, bytes, sizeof(structSize));

    slang_slim_target legacyTarget = 0;
    const std::size_t minimumSize =
        offsetof(slang_slim_target_desc, target) + sizeof(legacyTarget);
    if (structSize < minimumSize)
        return false;
    if (!readTargetField(bytes, structSize, offsetof(slang_slim_target_desc, target), &legacyTarget, sizeof(legacyTarget)))
        return false;
    out.publicTarget = legacyTarget;
    out.flags = SLANG_SLIM_TARGET_FLAGS_DEFAULT;

    slang_slim_compile_target format = SLANG_SLIM_COMPILE_TARGET_UNKNOWN;
    if (structSize >= offsetof(slang_slim_target_desc, format) + sizeof(format))
    {
        readTargetField(bytes, structSize, offsetof(slang_slim_target_desc, format), &format, sizeof(format));
    }
    if (format != SLANG_SLIM_COMPILE_TARGET_UNKNOWN)
    {
        out.format = format;
    }
    else if (!getLegacyTargetSpec(legacyTarget, out))
    {
        return false;
    }
    out.flags = SLANG_SLIM_TARGET_FLAGS_DEFAULT;

    if (structSize >= offsetof(slang_slim_target_desc, profile) + sizeof(out.profile))
        readTargetField(bytes, structSize, offsetof(slang_slim_target_desc, profile), &out.profile, sizeof(out.profile));
    if (structSize >= offsetof(slang_slim_target_desc, flags) + sizeof(out.flags))
    {
        readTargetField(bytes, structSize, offsetof(slang_slim_target_desc, flags), &out.flags, sizeof(out.flags));
        out.flagsSpecified = true;
    }
    if (structSize >= offsetof(slang_slim_target_desc, floating_point_mode) + sizeof(out.floatingPointMode))
        readTargetField(
            bytes,
            structSize,
            offsetof(slang_slim_target_desc, floating_point_mode),
            &out.floatingPointMode,
            sizeof(out.floatingPointMode));
    if (structSize >= offsetof(slang_slim_target_desc, line_directive_mode) + sizeof(out.lineDirectiveMode))
        readTargetField(
            bytes,
            structSize,
            offsetof(slang_slim_target_desc, line_directive_mode),
            &out.lineDirectiveMode,
            sizeof(out.lineDirectiveMode));
    if (structSize >=
        offsetof(slang_slim_target_desc, force_glsl_scalar_buffer_layout) +
            sizeof(out.forceGLSLScalarBufferLayout))
        readTargetField(
            bytes,
            structSize,
            offsetof(slang_slim_target_desc, force_glsl_scalar_buffer_layout),
            &out.forceGLSLScalarBufferLayout,
            sizeof(out.forceGLSLScalarBufferLayout));

    if (structSize >=
        offsetof(slang_slim_target_desc, compiler_option_count) +
            sizeof(out.compilerOptionCount))
    {
        readTargetField(
            bytes,
            structSize,
            offsetof(slang_slim_target_desc, compiler_options),
            &out.compilerOptions,
            sizeof(out.compilerOptions));
        readTargetField(
            bytes,
            structSize,
            offsetof(slang_slim_target_desc, compiler_option_count),
            &out.compilerOptionCount,
            sizeof(out.compilerOptionCount));
        if (!validateCompilerOptionArray(out.compilerOptions, out.compilerOptionCount))
            return false;
    }

    return true;
}

bool getStage(slang_slim_stage stage, bool legacyEncoding, SlangStage& out) noexcept
{
    if (stage == SLANG_SLIM_STAGE_NONE || stage >= SLANG_SLIM_STAGE_COUNT_OF)
        return false;

    // The original v0.1 slice used 2 and 3 for fragment and compute. Those
    // values overlap Slang's hull and domain stages, so only interpret them as
    // legacy aliases when the request also uses the original short target
    // descriptor prefix. Full descriptors always use Slang's stage numbers.
    if (legacyEncoding && stage == SLANG_SLIM_STAGE_FRAGMENT_LEGACY)
        out = SLANG_STAGE_FRAGMENT;
    else if (legacyEncoding && stage == SLANG_SLIM_STAGE_COMPUTE_LEGACY)
        out = SLANG_STAGE_COMPUTE;
    else
        out = static_cast<SlangStage>(stage);
    return true;
}

void appendBytes(std::string& destination, const void* data, std::size_t size)
{
    if (!data || size == 0)
        return;

    const auto* bytes = static_cast<const char*>(data);
    if (!destination.empty() && destination.back() != '\n')
        destination.push_back('\n');
    destination.append(bytes, size);
    while (!destination.empty() && destination.back() == '\0')
        destination.pop_back();
}

void appendDiagnostics(std::string& destination, slang::IBlob* diagnostics)
{
    if (!diagnostics)
        return;
    appendBytes(destination, diagnostics->getBufferPointer(), diagnostics->getBufferSize());
}

slang_slim_blob makeBlobView(slang::IBlob* blob, bool text) noexcept
{
    slang_slim_blob view = {nullptr, 0};
    if (!blob)
        return view;

    view.data = static_cast<const uint8_t*>(blob->getBufferPointer());
    view.size = blob->getBufferSize();
    if (text && view.size != 0 && view.data[view.size - 1] == '\0')
        --view.size;
    return view;
}

slang_slim_blob makeBlobView(const std::vector<uint8_t>& blob, bool text) noexcept
{
    slang_slim_blob view = {blob.data(), blob.size()};
    if (text && view.size != 0 && view.data[view.size - 1] == '\0')
        --view.size;
    return view;
}

bool copyBlob(slang::IBlob* blob, std::vector<uint8_t>& destination)
{
    destination.clear();
    if (!blob)
        return false;
    const auto* data = static_cast<const uint8_t*>(blob->getBufferPointer());
    const std::size_t size = blob->getBufferSize();
    if (size != 0 && !data)
        return false;
    if (size != 0)
        destination.assign(data, data + size);
    return true;
}

slang_slim_blob makeStringView(const std::string& value) noexcept
{
    if (value.empty())
        return {nullptr, 0};
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}

class NativeFileSystem final : public ISlangFileSystem
{
public:
    struct File
    {
        std::string path;
        std::vector<uint8_t> data;
    };

    NativeFileSystem(
        const slang_slim_virtual_file* files,
        std::size_t fileCount,
        slang_slim_load_file_fn callback,
        void* callbackUserData)
        : m_callback(callback)
        , m_callbackUserData(callbackUserData)
    {
        m_files.reserve(fileCount);
        for (std::size_t index = 0; index < fileCount; ++index)
        {
            const auto& file = files[index];
            File copy;
            copy.path = normalizePath(file.path);
            if (file.size != 0)
                copy.data.assign(file.data, file.data + file.size);
            m_files.push_back(std::move(copy));
        }
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(
        SlangUUID const& uuid,
        void** outObject) override
    {
        if (!outObject)
            return SLANG_E_INVALID_ARG;
        *outObject = nullptr;
        if (sameGuid(uuid, ISlangFileSystem::getTypeGuid()))
        {
            *outObject = static_cast<ISlangFileSystem*>(this);
        }
        else if (sameGuid(uuid, ISlangCastable::getTypeGuid()))
        {
            *outObject = static_cast<ISlangCastable*>(this);
        }
        else if (sameGuid(uuid, ISlangUnknown::getTypeGuid()))
        {
            *outObject = static_cast<ISlangUnknown*>(this);
        }
        else
        {
            return SLANG_E_NO_INTERFACE;
        }
        addRef();
        return SLANG_OK;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override
    {
        return m_refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        const uint32_t remaining = m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& uuid) override
    {
        if (sameGuid(uuid, ISlangFileSystem::getTypeGuid()))
            return static_cast<ISlangFileSystem*>(this);
        if (sameGuid(uuid, ISlangCastable::getTypeGuid()))
            return static_cast<ISlangCastable*>(this);
        if (sameGuid(uuid, ISlangUnknown::getTypeGuid()))
            return static_cast<ISlangUnknown*>(this);
        return nullptr;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(
        const char* path,
        ISlangBlob** outBlob) override
    {
        if (!path || !outBlob)
            return SLANG_E_INVALID_ARG;
        *outBlob = nullptr;

        try
        {
            const std::string normalizedPath = normalizePath(path);
            for (const File& file : m_files)
            {
                if (file.path == normalizedPath)
                    return createBlob(file.data, outBlob);
            }

            if (!m_callback)
                return SLANG_E_NOT_FOUND;

            slang_slim_blob callbackBlob = {nullptr, 0};
            const slang_slim_status callbackStatus =
                m_callback(m_callbackUserData, normalizedPath.c_str(), &callbackBlob);
            if (callbackStatus != SLANG_SLIM_STATUS_OK)
                return callbackStatus == SLANG_SLIM_STATUS_NOT_FOUND ? SLANG_E_NOT_FOUND
                                                                       : SLANG_E_CANNOT_OPEN;
            if (callbackBlob.size != 0 && !callbackBlob.data)
                return SLANG_E_CANNOT_OPEN;

            return createBlob(callbackBlob.data, callbackBlob.size, outBlob);
        }
        catch (const std::bad_alloc&)
        {
            return SLANG_E_OUT_OF_MEMORY;
        }
        catch (...)
        {
            return SLANG_FAIL;
        }
    }

private:
    static SlangResult createBlob(
        const std::vector<uint8_t>& data,
        ISlangBlob** outBlob) noexcept
    {
        return createBlob(data.data(), data.size(), outBlob);
    }

    static SlangResult createBlob(
        const void* data,
        std::size_t size,
        ISlangBlob** outBlob) noexcept
    {
        if (!data && size != 0)
            return SLANG_E_CANNOT_OPEN;
        static const uint8_t emptyByte = 0;
        *outBlob = slang_createBlob(data ? data : &emptyByte, size);
        return *outBlob ? SLANG_OK : SLANG_E_OUT_OF_MEMORY;
    }

    std::atomic<uint32_t> m_refCount = 1;
    std::vector<File> m_files;
    slang_slim_load_file_fn m_callback = nullptr;
    void* m_callbackUserData = nullptr;
};

struct DefineStorage
{
    std::string name;
    std::string value;
};

bool convertCompilerOptionEntry(
    const slang_slim_compiler_option_entry& source,
    slang::CompilerOptionEntry& destination) noexcept
{
    if (source.value.kind > SLANG_SLIM_COMPILER_OPTION_VALUE_STRING)
        return false;
    destination.name = static_cast<slang::CompilerOptionName>(source.name);
    destination.value.kind = static_cast<slang::CompilerOptionValueKind>(source.value.kind);
    destination.value.intValue0 = source.value.int_value0;
    destination.value.intValue1 = source.value.int_value1;
    destination.value.stringValue0 = source.value.string_value0;
    destination.value.stringValue1 = source.value.string_value1;
    return source.value.kind != SLANG_SLIM_COMPILER_OPTION_VALUE_STRING ||
           source.value.string_value0 != nullptr;
}

struct TargetOutput
{
    TargetInfo info;
    std::string profileStorage;
    std::vector<uint8_t> reflection;
    std::vector<std::vector<uint8_t>> code;
};
} // namespace

struct slang_slim_compiler
{
    std::string buildTag;
};

struct slang_slim_compilation
{
    std::string moduleName;
    std::string sourcePath;
    std::string diagnostics;
    std::vector<std::string> entryPointNames;
    std::vector<TargetOutput> outputs;
};

namespace
{
bool usesLegacyStageEncoding(const slang_slim_compile_desc* desc) noexcept
{
    TargetArrayView targets;
    if (!getTargetArrayView(desc, targets))
        return false;
    return targets.stride < sizeof(slang_slim_target_desc);
}

slang_slim_status validateDescriptor(const slang_slim_compile_desc* desc)
{
    const std::size_t requiredCompileDescSize =
        offsetof(slang_slim_compile_desc, load_file_user_data) +
        sizeof(desc->load_file_user_data);
    if (!hasStructPrefix(desc, requiredCompileDescSize) || !desc->source ||
        desc->source_size == 0 ||
        !desc->entry_points || desc->entry_point_count == 0 || !desc->targets ||
        desc->target_count == 0 || !fitsSlangInt(desc->entry_point_count) ||
        !fitsSlangInt(desc->target_count) || !fitsSlangInt(desc->define_count) ||
        !fitsSlangInt(desc->virtual_file_count))
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;

    if (desc->define_count != 0 && !desc->defines)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    if (desc->virtual_file_count != 0 && !desc->virtual_files)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    if (desc->load_file == nullptr && desc->load_file_user_data != nullptr)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;

    const bool hasSearchPathFields = hasStructPrefix(
        desc,
        offsetof(slang_slim_compile_desc, search_path_count) +
            sizeof(desc->search_path_count));
    if (hasSearchPathFields)
    {
        if ((desc->search_path_count != 0 && !desc->search_paths) ||
            !fitsSlangInt(desc->search_path_count))
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        for (std::size_t index = 0; index < desc->search_path_count; ++index)
        {
            if (!desc->search_paths[index])
                return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        }
    }

    const bool legacyStageEncoding = usesLegacyStageEncoding(desc);
    for (std::size_t index = 0; index < desc->entry_point_count; ++index)
    {
        const auto& entryPoint = desc->entry_points[index];
        const std::size_t requiredEntryPointDescSize =
            offsetof(slang_slim_entry_point_desc, stage) + sizeof(entryPoint.stage);
        if (!hasStructPrefix(&entryPoint, requiredEntryPointDescSize) || !entryPoint.name ||
            entryPoint.name[0] == '\0')
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        SlangStage ignoredStage;
        if (!getStage(entryPoint.stage, legacyStageEncoding, ignoredStage))
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (std::strcmp(entryPoint.name, desc->entry_points[previous].name) == 0)
                return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        }
    }

    TargetArrayView targets;
    if (!getTargetArrayView(desc, targets))
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    for (std::size_t index = 0; index < desc->target_count; ++index)
    {
        TargetSpec ignoredSpec;
        if (!getTargetSpec(targets.data + index * targets.stride, ignoredSpec))
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    }

    for (std::size_t index = 0; index < desc->define_count; ++index)
    {
        const auto& define = desc->defines[index];
        const std::size_t requiredDefineDescSize =
            offsetof(slang_slim_define_desc, value) + sizeof(define.value);
        if (!hasStructPrefix(&define, requiredDefineDescSize) || !define.name ||
            define.name[0] == '\0' || !define.value)
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    }

    CompilerOptionArrayView compilerOptions;
    if (!getCompilerOptionArray(desc, compilerOptions))
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;

    for (std::size_t index = 0; index < desc->virtual_file_count; ++index)
    {
        const auto& file = desc->virtual_files[index];
        const std::size_t requiredVirtualFileSize =
            offsetof(slang_slim_virtual_file, size) + sizeof(file.size);
        if (!hasStructPrefix(&file, requiredVirtualFileSize) || !file.path ||
            file.path[0] == '\0' || (file.size != 0 && !file.data))
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        const std::string normalizedPath = normalizePath(file.path);
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (normalizedPath == normalizePath(desc->virtual_files[previous].path))
                return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        }
    }

    return SLANG_SLIM_STATUS_OK;
}

slang_slim_status compileImpl(
    const slang_slim_compile_desc* desc,
    slang_slim_compilation& result,
    slang::IGlobalSession* globalSession)
{
    if (!globalSession)
    {
        result.diagnostics += "failed to create Slang compilation session\n";
        return SLANG_SLIM_STATUS_INTERNAL_ERROR;
    }

    ComPtr<NativeFileSystem> fileSystem;
    ComPtr<slang::ISession> session;
    ComPtr<slang::IBlob> sourceBlob;
    ComPtr<slang::IModule> module;
    ComPtr<slang::IComponentType> program;
    ComPtr<slang::IComponentType> linkedProgram;
    std::vector<std::string> searchPaths;
    std::vector<const char*> searchPathPointers;
    std::vector<slang::TargetDesc> targetDescs;
    std::vector<std::vector<slang::CompilerOptionEntry>> targetOptionEntries;
    std::vector<slang::CompilerOptionEntry> sessionOptionEntries;
    std::vector<DefineStorage> defineStorage;
    std::vector<slang::PreprocessorMacroDesc> defineDescs;

    result.moduleName = desc->module_name ? desc->module_name : "slang_slim_module";
    result.sourcePath = desc->source_path ? desc->source_path : "slang_slim_input.hlsl";
    const bool legacyStageEncoding = usesLegacyStageEncoding(desc);
    result.entryPointNames.reserve(desc->entry_point_count);
    for (std::size_t index = 0; index < desc->entry_point_count; ++index)
        result.entryPointNames.emplace_back(desc->entry_points[index].name);

    targetDescs.resize(desc->target_count);
    targetOptionEntries.resize(desc->target_count);
    result.outputs.resize(desc->target_count);
    TargetArrayView targets;
    getTargetArrayView(desc, targets);
    for (std::size_t index = 0; index < desc->target_count; ++index)
    {
        TargetSpec spec;
        getTargetSpec(targets.data + index * targets.stride, spec);
        if (!targetAllowedOnThisPlatform(spec.format))
        {
            result.diagnostics += "target format is unavailable on this platform: ";
            result.diagnostics += std::to_string(spec.format);
            result.diagnostics.push_back('\n');
            return SLANG_SLIM_STATUS_UNSUPPORTED;
        }
        auto& output = result.outputs[index];
        TargetInfo info;
        info.publicTarget = spec.publicTarget;
        info.format = spec.format;
        info.slangTarget = static_cast<SlangCompileTarget>(spec.format);
        const char* profile = spec.profile ? spec.profile : defaultProfileForFormat(spec.format);
        if (profile)
        {
            output.profileStorage = profile;
            info.profile = output.profileStorage.c_str();
        }
        info.text = targetFormatProducesText(spec.format);
        output.info = info;
        auto& targetDesc = targetDescs[index];
        targetDesc.structureSize = sizeof(slang::TargetDesc);
        targetDesc.format = info.slangTarget;
        targetDesc.profile = info.profile ? globalSession->findProfile(info.profile)
                                          : SLANG_PROFILE_UNKNOWN;
        // Zero means "use Slang's default" for optional target settings so
        // legacy descriptors retain the exact behavior of the first ABI slice.
        if (spec.flagsSpecified)
            targetDesc.flags = static_cast<SlangTargetFlags>(spec.flags);
        if (spec.floatingPointMode != SLANG_SLIM_FLOATING_POINT_MODE_DEFAULT)
            targetDesc.floatingPointMode =
                static_cast<SlangFloatingPointMode>(spec.floatingPointMode);
        if (spec.lineDirectiveMode != SLANG_SLIM_LINE_DIRECTIVE_MODE_DEFAULT)
            targetDesc.lineDirectiveMode =
                static_cast<SlangLineDirectiveMode>(spec.lineDirectiveMode);
        if (spec.forceGLSLScalarBufferLayout != 0)
            targetDesc.forceGLSLScalarBufferLayout = true;
        auto& optionEntries = targetOptionEntries[index];
        optionEntries.reserve(spec.compilerOptionCount);
        for (std::size_t optionIndex = 0; optionIndex < spec.compilerOptionCount;
             ++optionIndex)
        {
            slang::CompilerOptionEntry optionEntry = {};
            if (!convertCompilerOptionEntry(
                    spec.compilerOptions[optionIndex],
                    optionEntry))
            {
                result.diagnostics += "invalid target compiler option\n";
                return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
            }
            optionEntries.push_back(optionEntry);
        }
        if (!optionEntries.empty())
        {
            targetDesc.compilerOptionEntries = optionEntries.data();
            targetDesc.compilerOptionEntryCount = static_cast<uint32_t>(optionEntries.size());
        }
        if (targetDesc.profile == SLANG_PROFILE_UNKNOWN)
        {
            result.diagnostics += "target profile is unavailable: ";
            if (!info.profile)
                result.diagnostics += "<unspecified>";
            else
                result.diagnostics += info.profile;
            result.diagnostics.push_back('\n');
            return SLANG_SLIM_STATUS_UNSUPPORTED;
        }
    }

    defineStorage.reserve(desc->define_count);
    for (std::size_t index = 0; index < desc->define_count; ++index)
    {
        defineStorage.push_back({desc->defines[index].name, desc->defines[index].value});
    }
    defineDescs.resize(defineStorage.size());
    for (std::size_t index = 0; index < defineStorage.size(); ++index)
    {
        defineDescs[index].name = defineStorage[index].name.c_str();
        defineDescs[index].value = defineStorage[index].value.c_str();
    }

    if (hasStructPrefix(
            desc,
            offsetof(slang_slim_compile_desc, search_path_count) +
                sizeof(desc->search_path_count)) &&
        desc->search_path_count != 0)
    {
        searchPaths.reserve(desc->search_path_count);
        for (std::size_t index = 0; index < desc->search_path_count; ++index)
            searchPaths.emplace_back(desc->search_paths[index] ? desc->search_paths[index] : "");
        searchPathPointers.reserve(searchPaths.size());
        for (const auto& path : searchPaths)
            searchPathPointers.push_back(path.c_str());
    }

    if (desc->virtual_file_count != 0 || desc->load_file != nullptr)
    {
        fileSystem.attach(new NativeFileSystem(
            desc->virtual_files,
            desc->virtual_file_count,
            desc->load_file,
            desc->load_file_user_data));
    }

    slang::SessionDesc sessionDesc = {};
    sessionDesc.structureSize = sizeof(slang::SessionDesc);
    sessionDesc.targets = targetDescs.data();
    sessionDesc.targetCount = static_cast<SlangInt>(targetDescs.size());
    sessionDesc.preprocessorMacros = defineDescs.data();
    sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(defineDescs.size());
    sessionDesc.fileSystem = fileSystem.get();
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    sessionDesc.allowGLSLSyntax = false;

    if (!searchPathPointers.empty())
    {
        sessionDesc.searchPaths = searchPathPointers.data();
        sessionDesc.searchPathCount = static_cast<SlangInt>(searchPathPointers.size());
    }

    CompilerOptionArrayView sessionOptions;
    if (!getCompilerOptionArray(desc, sessionOptions))
    {
        result.diagnostics += "invalid session compiler options\n";
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    }
    sessionOptionEntries.reserve(sessionOptions.count);
    for (std::size_t optionIndex = 0; optionIndex < sessionOptions.count; ++optionIndex)
    {
        slang::CompilerOptionEntry optionEntry = {};
        if (!convertCompilerOptionEntry(sessionOptions.data[optionIndex], optionEntry))
        {
            result.diagnostics += "invalid session compiler option\n";
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        }
        sessionOptionEntries.push_back(optionEntry);
    }
    if (!sessionOptionEntries.empty())
    {
        sessionDesc.compilerOptionEntries = sessionOptionEntries.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(sessionOptionEntries.size());
    }

    if (hasStructPrefix(
            desc,
            offsetof(slang_slim_compile_desc, default_matrix_layout_mode) +
                sizeof(desc->default_matrix_layout_mode)) &&
        desc->default_matrix_layout_mode != SLANG_SLIM_MATRIX_LAYOUT_MODE_UNKNOWN)
    {
        sessionDesc.defaultMatrixLayoutMode = static_cast<SlangMatrixLayoutMode>(
            desc->default_matrix_layout_mode);
    }

    if (hasStructPrefix(
            desc,
            offsetof(slang_slim_compile_desc, session_flags) + sizeof(desc->session_flags)) &&
        desc->session_flags != SLANG_SLIM_SESSION_FLAGS_NONE)
    {
        sessionDesc.flags = static_cast<slang::SessionFlags>(desc->session_flags);
    }
    if (hasStructPrefix(
            desc,
            offsetof(slang_slim_compile_desc, allow_glsl_syntax) +
                sizeof(desc->allow_glsl_syntax)) &&
        desc->allow_glsl_syntax != 0)
    {
        sessionDesc.allowGLSLSyntax = true;
    }
    if (hasStructPrefix(
            desc,
            offsetof(slang_slim_compile_desc, skip_spirv_validation) +
                sizeof(desc->skip_spirv_validation)) &&
        desc->skip_spirv_validation != 0)
    {
        sessionDesc.skipSPIRVValidation = true;
    }
    if (hasStructPrefix(
            desc,
            offsetof(slang_slim_compile_desc, enable_effect_annotations) +
                sizeof(desc->enable_effect_annotations)) &&
        desc->enable_effect_annotations != 0)
    {
        sessionDesc.enableEffectAnnotations = true;
    }

    if (mapSlangStatus(globalSession->createSession(
            sessionDesc,
            session.writeRef())) != SLANG_SLIM_STATUS_OK)
    {
        result.diagnostics += "failed to create Slang session\n";
        return SLANG_SLIM_STATUS_INTERNAL_ERROR;
    }

    if (gRetainedSlangSessions && session && fileSystem)
        gRetainedSlangSessions->emplace_back(session.get());

    sourceBlob.attach(slang_createBlob(desc->source, desc->source_size));
    if (!sourceBlob)
    {
        result.diagnostics += "failed to copy source bytes\n";
        return SLANG_SLIM_STATUS_OUT_OF_MEMORY;
    }

    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* moduleRaw = session->loadModuleFromSource(
        result.moduleName.c_str(),
        result.sourcePath.c_str(),
        sourceBlob,
        diagnostics.writeRef());
    appendDiagnostics(result.diagnostics, diagnostics);
    module.attach(moduleRaw);
    if (!module)
        return SLANG_SLIM_STATUS_COMPILE_ERROR;
    std::vector<ComPtr<slang::IEntryPoint>> entryPoints;
    entryPoints.reserve(desc->entry_point_count);
    std::vector<slang::IComponentType*> components;
    components.reserve(desc->entry_point_count + 1);
    components.push_back(module);
    for (std::size_t index = 0; index < desc->entry_point_count; ++index)
    {
        SlangStage stage;
        getStage(desc->entry_points[index].stage, legacyStageEncoding, stage);
        slang::IEntryPoint* entryPoint = nullptr;
        diagnostics.setNull();
        const SlangResult status = module->findAndCheckEntryPoint(
            desc->entry_points[index].name,
            stage,
            &entryPoint,
            diagnostics.writeRef());
        appendDiagnostics(result.diagnostics, diagnostics);
        if (SLANG_FAILED(status) || !entryPoint)
            return SLANG_SLIM_STATUS_COMPILE_ERROR;
        entryPoints.emplace_back(Slang::INIT_ATTACH, entryPoint);
        components.push_back(entryPoints.back());
    }

    diagnostics.setNull();
    slang::IComponentType* programRaw = nullptr;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components.data(),
            static_cast<SlangInt>(components.size()),
            &programRaw,
            diagnostics.writeRef())))
    {
        appendDiagnostics(result.diagnostics, diagnostics);
        return SLANG_SLIM_STATUS_COMPILE_ERROR;
    }
    appendDiagnostics(result.diagnostics, diagnostics);
    program.attach(programRaw);

    diagnostics.setNull();
    if (SLANG_FAILED(program->link(linkedProgram.writeRef(), diagnostics.writeRef())))
    {
        appendDiagnostics(result.diagnostics, diagnostics);
        return SLANG_SLIM_STATUS_COMPILE_ERROR;
    }
    appendDiagnostics(result.diagnostics, diagnostics);

    for (std::size_t targetIndex = 0; targetIndex < result.outputs.size(); ++targetIndex)
    {
        auto& output = result.outputs[targetIndex];
        diagnostics.setNull();
        slang::ProgramLayout* layout = linkedProgram->getLayout(
            static_cast<SlangInt>(targetIndex),
            diagnostics.writeRef());
        appendDiagnostics(result.diagnostics, diagnostics);
        if (!layout)
            return SLANG_SLIM_STATUS_COMPILE_ERROR;

        diagnostics.setNull();
        ComPtr<slang::IBlob> reflection;
        if (SLANG_FAILED(layout->toJson(reflection.writeRef())) ||
            !copyBlob(reflection, output.reflection))
        {
            appendDiagnostics(result.diagnostics, diagnostics);
            return SLANG_SLIM_STATUS_COMPILE_ERROR;
        }
        appendDiagnostics(result.diagnostics, diagnostics);

        output.code.resize(desc->entry_point_count);
        for (std::size_t entryIndex = 0; entryIndex < desc->entry_point_count; ++entryIndex)
        {
            diagnostics.setNull();
            ComPtr<slang::IBlob> code;
            const SlangResult status = linkedProgram->getEntryPointCode(
                static_cast<SlangInt>(entryIndex),
                static_cast<SlangInt>(targetIndex),
                code.writeRef(),
                diagnostics.writeRef());
            appendDiagnostics(result.diagnostics, diagnostics);
            if (SLANG_FAILED(status) || !copyBlob(code, output.code[entryIndex]))
                return SLANG_SLIM_STATUS_COMPILE_ERROR;
        }
    }

    return SLANG_SLIM_STATUS_OK;
}
} // namespace

extern "C"
{
SLANG_SLIM_API uint32_t slang_slim_abi_version(void)
{
    return SLANG_SLIM_ABI_VERSION;
}

SLANG_SLIM_API slang_slim_status slang_slim_compiler_create(slang_slim_compiler** outCompiler)
{
    if (!outCompiler)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    *outCompiler = nullptr;

    try
    {
        auto compiler = std::make_unique<slang_slim_compiler>();
        slang_slim_status status = SLANG_SLIM_STATUS_INTERNAL_ERROR;
        runSlangWorker([&](slang::IGlobalSession* globalSession) {
            if (!globalSession)
                return;
            const char* buildTag = globalSession->getBuildTagString();
            if (buildTag)
                compiler->buildTag = buildTag;
            status = SLANG_SLIM_STATUS_OK;
        });
        if (status != SLANG_SLIM_STATUS_OK)
            return status;
        *outCompiler = compiler.release();
        return SLANG_SLIM_STATUS_OK;
    }
    catch (const std::bad_alloc&)
    {
        return SLANG_SLIM_STATUS_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return SLANG_SLIM_STATUS_INTERNAL_ERROR;
    }
}

SLANG_SLIM_API void slang_slim_compiler_destroy(slang_slim_compiler* compiler)
{
    delete compiler;
}

SLANG_SLIM_API const char* slang_slim_compiler_build_tag(const slang_slim_compiler* compiler)
{
    if (!compiler || compiler->buildTag.empty())
        return nullptr;
    return compiler->buildTag.c_str();
}

SLANG_SLIM_API int32_t slang_slim_compiler_supports_target(
    const slang_slim_compiler* compiler,
    slang_slim_target target)
{
    if (!compiler)
        return 0;
    try
    {
        int32_t supported = 0;
        runSlangWorker([&](slang::IGlobalSession* globalSession) {
            if (!globalSession)
                return;
            TargetSpec spec;
            if (!getLegacyTargetSpec(target, spec) || !targetAllowedOnThisPlatform(spec.format))
                return;
            const char* profile = defaultProfileForFormat(spec.format);
            // Slang's support probe also requires optional downstream tools
            // (for example SPIR-V validators) that are intentionally omitted
            // from the slim build. The profile is the capability signal here;
            // an actual compile still reports backend failures.
            if (profile && globalSession->findProfile(profile) != SLANG_PROFILE_UNKNOWN)
                supported = 1;
        });
        return supported;
    }
    catch (...)
    {
        return 0;
    }
}

SLANG_SLIM_API int32_t slang_slim_compiler_supports_target_format(
    const slang_slim_compiler* compiler,
    slang_slim_compile_target format,
    const char* profile)
{
    if (!compiler || format == SLANG_SLIM_COMPILE_TARGET_UNKNOWN)
        return 0;
    try
    {
        int32_t supported = 0;
        runSlangWorker([&](slang::IGlobalSession* globalSession) {
            if (!globalSession || !targetAllowedOnThisPlatform(format))
                return;
            const char* selectedProfile = profile ? profile : defaultProfileForFormat(format);
            if (selectedProfile && selectedProfile[0] != '\0' &&
                globalSession->findProfile(selectedProfile) != SLANG_PROFILE_UNKNOWN)
                supported = 1;
        });
        return supported;
    }
    catch (...)
    {
        return 0;
    }
}

SLANG_SLIM_API slang_slim_status slang_slim_compile(
    const slang_slim_compiler* compiler,
    const slang_slim_compile_desc* desc,
    slang_slim_compilation** outCompilation)
{
    if (!outCompilation)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    *outCompilation = nullptr;
    if (!compiler)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;

    slang_slim_status validationStatus = SLANG_SLIM_STATUS_INTERNAL_ERROR;
    try
    {
        validationStatus = validateDescriptor(desc);
    }
    catch (const std::bad_alloc&)
    {
        return SLANG_SLIM_STATUS_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return SLANG_SLIM_STATUS_INTERNAL_ERROR;
    }
    if (validationStatus != SLANG_SLIM_STATUS_OK)
        return validationStatus;

    try
    {
        auto compilation = std::make_unique<slang_slim_compilation>();
        *outCompilation = compilation.get();
        slang_slim_status status = SLANG_SLIM_STATUS_INTERNAL_ERROR;
        try
        {
            runSlangWorker([&](slang::IGlobalSession* globalSession) {
                status = compileImpl(desc, *compilation, globalSession);
            });
        }
        catch (const std::bad_alloc&)
        {
            status = SLANG_SLIM_STATUS_OUT_OF_MEMORY;
        }
        catch (...)
        {
            status = SLANG_SLIM_STATUS_INTERNAL_ERROR;
        }
        compilation.release();
        return status;
    }
    catch (const std::bad_alloc&)
    {
        return SLANG_SLIM_STATUS_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return SLANG_SLIM_STATUS_INTERNAL_ERROR;
    }
}

SLANG_SLIM_API void slang_slim_compilation_destroy(slang_slim_compilation* compilation)
{
    delete compilation;
}

SLANG_SLIM_API size_t slang_slim_compilation_target_count(
    const slang_slim_compilation* compilation)
{
    return compilation ? compilation->outputs.size() : 0;
}

SLANG_SLIM_API size_t slang_slim_compilation_entry_point_count(
    const slang_slim_compilation* compilation)
{
    return compilation ? compilation->entryPointNames.size() : 0;
}

SLANG_SLIM_API slang_slim_target slang_slim_compilation_target(
    const slang_slim_compilation* compilation,
    size_t targetIndex)
{
    if (!compilation || targetIndex >= compilation->outputs.size())
        return 0;
    return compilation->outputs[targetIndex].info.publicTarget;
}

SLANG_SLIM_API slang_slim_compile_target slang_slim_compilation_target_format(
    const slang_slim_compilation* compilation,
    size_t targetIndex)
{
    if (!compilation || targetIndex >= compilation->outputs.size())
        return SLANG_SLIM_COMPILE_TARGET_UNKNOWN;
    return compilation->outputs[targetIndex].info.format;
}

SLANG_SLIM_API const char* slang_slim_compilation_target_profile(
    const slang_slim_compilation* compilation,
    size_t targetIndex)
{
    if (!compilation || targetIndex >= compilation->outputs.size())
        return nullptr;
    return compilation->outputs[targetIndex].info.profile;
}

SLANG_SLIM_API const char* slang_slim_compilation_entry_point_name(
    const slang_slim_compilation* compilation,
    size_t entryPointIndex)
{
    if (!compilation || entryPointIndex >= compilation->entryPointNames.size())
        return nullptr;
    return compilation->entryPointNames[entryPointIndex].c_str();
}

SLANG_SLIM_API slang_slim_status slang_slim_compilation_get_code(
    const slang_slim_compilation* compilation,
    size_t targetIndex,
    size_t entryPointIndex,
    slang_slim_blob* outCode)
{
    if (!outCode)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    *outCode = {nullptr, 0};
    if (!compilation || targetIndex >= compilation->outputs.size() ||
        entryPointIndex >= compilation->entryPointNames.size())
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    const auto& output = compilation->outputs[targetIndex];
    if (entryPointIndex >= output.code.size() || output.code[entryPointIndex].empty())
        return SLANG_SLIM_STATUS_NOT_FOUND;
    *outCode = makeBlobView(output.code[entryPointIndex], output.info.text);
    return outCode->data && outCode->size != 0 ? SLANG_SLIM_STATUS_OK
                                                : SLANG_SLIM_STATUS_NOT_FOUND;
}

SLANG_SLIM_API slang_slim_status slang_slim_compilation_get_reflection_json(
    const slang_slim_compilation* compilation,
    size_t targetIndex,
    slang_slim_blob* outJson)
{
    if (!outJson)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    *outJson = {nullptr, 0};
    if (!compilation || targetIndex >= compilation->outputs.size())
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    const auto& reflection = compilation->outputs[targetIndex].reflection;
    if (reflection.empty())
        return SLANG_SLIM_STATUS_NOT_FOUND;
    *outJson = makeBlobView(reflection, true);
    return outJson->data && outJson->size != 0 ? SLANG_SLIM_STATUS_OK
                                                : SLANG_SLIM_STATUS_NOT_FOUND;
}

SLANG_SLIM_API slang_slim_status slang_slim_compilation_get_diagnostics(
    const slang_slim_compilation* compilation,
    slang_slim_blob* outDiagnostics)
{
    if (!outDiagnostics)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    *outDiagnostics = {nullptr, 0};
    if (!compilation)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    *outDiagnostics = makeStringView(compilation->diagnostics);
    return SLANG_SLIM_STATUS_OK;
}
}
