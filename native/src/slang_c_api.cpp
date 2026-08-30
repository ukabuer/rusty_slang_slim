#include <slang-com-ptr.h>
#include <slang.h>

#include "slang_c_api.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <future>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using Slang::ComPtr;

/* Keep the C projection mechanically honest as Slang evolves. The fields
 * below are the ABI-visible subset of the corresponding C++ records; all
 * translation into the namespaced Slang records happens in one place. */
static_assert(sizeof(SlangTargetDesc) == sizeof(slang::TargetDesc));
static_assert(offsetof(SlangTargetDesc, structureSize) == offsetof(slang::TargetDesc, structureSize));
static_assert(offsetof(SlangTargetDesc, format) == offsetof(slang::TargetDesc, format));
static_assert(offsetof(SlangTargetDesc, profile) == offsetof(slang::TargetDesc, profile));
static_assert(offsetof(SlangTargetDesc, compilerOptionEntries) ==
              offsetof(slang::TargetDesc, compilerOptionEntries));
static_assert(sizeof(SlangSessionDesc) == sizeof(slang::SessionDesc));
static_assert(offsetof(SlangSessionDesc, structureSize) == offsetof(slang::SessionDesc, structureSize));
static_assert(offsetof(SlangSessionDesc, targets) == offsetof(slang::SessionDesc, targets));
static_assert(offsetof(SlangSessionDesc, fileSystem) == offsetof(slang::SessionDesc, fileSystem));
static_assert(offsetof(SlangSessionDesc, compilerOptionEntries) ==
              offsetof(slang::SessionDesc, compilerOptionEntries));

bool sameGuid(const SlangUUID& left, const SlangUUID& right) noexcept
{
    return std::memcmp(&left, &right, sizeof(SlangUUID)) == 0;
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

class RawFileSystem final : public ISlangFileSystem
{
public:
    RawFileSystem(SlangLoadFileFunc callback, void* callbackUserData)
        : m_callback(callback)
        , m_callbackUserData(callbackUserData)
    {}

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(
        SlangUUID const& uuid,
        void** outObject) override
    {
        if (!outObject)
            return SLANG_E_INVALID_ARG;
        *outObject = nullptr;
        if (sameGuid(uuid, ISlangFileSystem::getTypeGuid()))
            *outObject = static_cast<ISlangFileSystem*>(this);
        else if (sameGuid(uuid, ISlangCastable::getTypeGuid()))
            *outObject = static_cast<ISlangCastable*>(this);
        else if (sameGuid(uuid, ISlangUnknown::getTypeGuid()))
            *outObject = static_cast<ISlangUnknown*>(this);
        else
            return SLANG_E_NO_INTERFACE;
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
        if (!m_callback)
            return SLANG_E_NOT_FOUND;
        return m_callback(m_callbackUserData, path, outBlob);
    }

private:
    std::atomic<uint32_t> m_refCount = 1;
    SlangLoadFileFunc m_callback = nullptr;
    void* m_callbackUserData = nullptr;
};

bool convertRawCompilerOptionEntry(
    const CompilerOptionEntry& source,
    slang::CompilerOptionEntry& destination) noexcept
{
    destination.name = static_cast<slang::CompilerOptionName>(source.name);
    destination.value.kind = static_cast<slang::CompilerOptionValueKind>(source.value.kind);
    destination.value.intValue0 = source.value.intValue0;
    destination.value.intValue1 = source.value.intValue1;
    destination.value.stringValue0 = source.value.stringValue0;
    destination.value.stringValue1 = source.value.stringValue1;
    return true;
}

} // namespace

/*
 * Opaque owners for the Slang-shaped raw ABI. The native interface deliberately
 * keeps the upstream COM objects behind these handles: callers see the same
 * ownership boundaries as Slang, while no C++ vtable crosses the FFI boundary.
 */
struct GlobalSession
{
    Slang::ComPtr<slang::IGlobalSession> native;
};

struct Session
{
    Slang::ComPtr<slang::IGlobalSession> global;
    Slang::ComPtr<slang::ISession> native;
    Slang::ComPtr<ISlangFileSystem> fileSystem;
};

struct ComponentType
{
    Slang::ComPtr<slang::IComponentType> native;
    Slang::ComPtr<slang::ISession> session;
    slang::IModule* module = nullptr;
};

struct ProgramLayout
{
    Slang::ComPtr<slang::IComponentType> owner;
    slang::ProgramLayout* native = nullptr;
};

namespace
{
template<typename T>
std::size_t rawStructureSize(const T* value) noexcept
{
    if (!value || value->structureSize == 0)
        return sizeof(T);
    return value->structureSize;
}

template<typename T>
bool rawHasPrefix(const T* value, std::size_t requiredSize) noexcept
{
    return value && rawStructureSize(value) >= requiredSize;
}

template<typename T, typename Field>
bool rawReadField(
    const T* value,
    std::size_t offset,
    Field& destination) noexcept
{
    if (!rawHasPrefix(value, offset + sizeof(Field)))
        return false;
    std::memcpy(&destination, reinterpret_cast<const uint8_t*>(value) + offset, sizeof(Field));
    return true;
}

template<typename T>
bool fitsRawSlangInt(T value) noexcept
{
    return value >= 0 && static_cast<uint64_t>(value) <=
        static_cast<uint64_t>(std::numeric_limits<SlangInt>::max());
}

bool copyRawCompilerOptions(
    const CompilerOptionEntry* options,
    uint32_t count,
    std::vector<slang::CompilerOptionEntry>& destination) noexcept
{
    if (count != 0 && !options)
        return false;
    try
    {
        destination.reserve(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            slang::CompilerOptionEntry entry = {};
            if (!convertRawCompilerOptionEntry(options[index], entry))
                return false;
            destination.push_back(entry);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

struct RawSessionStorage
{
    slang::SessionDesc nativeDesc = {};
    std::vector<slang::TargetDesc> targets;
    std::vector<std::vector<slang::CompilerOptionEntry>> targetOptions;
    std::vector<std::string> searchPaths;
    std::vector<const char*> searchPathPointers;
    std::vector<std::string> macroNames;
    std::vector<std::string> macroValues;
    std::vector<slang::PreprocessorMacroDesc> macros;
    std::vector<slang::CompilerOptionEntry> compilerOptions;
    Slang::ComPtr<ISlangFileSystem> fileSystem;
};

SlangResult buildRawSessionDesc(
    const SlangSessionDesc* desc,
    RawSessionStorage& storage)
{
    storage.nativeDesc.structureSize = sizeof(slang::SessionDesc);
    storage.nativeDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

    if (!desc)
        return SLANG_OK;

    const SlangTargetDesc* rawTargets = nullptr;
    int64_t rawTargetCount = 0;
    if (rawHasPrefix(desc, offsetof(SlangSessionDesc, targets) + sizeof(rawTargets)))
        rawReadField(desc, offsetof(SlangSessionDesc, targets), rawTargets);
    if (rawHasPrefix(desc, offsetof(SlangSessionDesc, targetCount) + sizeof(rawTargetCount)))
        rawReadField(desc, offsetof(SlangSessionDesc, targetCount), rawTargetCount);
    if (!fitsRawSlangInt(rawTargetCount))
        return SLANG_E_INVALID_ARG;
    if (rawTargetCount != 0 && !rawTargets)
        return SLANG_E_INVALID_ARG;

    const std::size_t targetCount = static_cast<std::size_t>(rawTargetCount);
    storage.targets.resize(targetCount);
    storage.targetOptions.resize(targetCount);
    for (std::size_t index = 0; index < targetCount; ++index)
    {
        const auto& raw = rawTargets[index];
        if (!rawHasPrefix(rawTargets + index, offsetof(SlangTargetDesc, format) + sizeof(raw.format)))
            return SLANG_E_INVALID_ARG;
        auto& target = storage.targets[index];
        target.structureSize = sizeof(slang::TargetDesc);
        target.format = static_cast<SlangCompileTarget>(raw.format);
        target.flags = kDefaultTargetFlags;
        target.floatingPointMode = SLANG_FLOATING_POINT_MODE_DEFAULT;
        target.lineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_DEFAULT;

        if (rawHasPrefix(&raw, offsetof(SlangTargetDesc, profile) + sizeof(raw.profile)))
            target.profile = static_cast<SlangProfileID>(raw.profile);
        if (rawHasPrefix(&raw, offsetof(SlangTargetDesc, flags) + sizeof(raw.flags)))
            target.flags = static_cast<SlangTargetFlags>(raw.flags);
        if (rawHasPrefix(
                &raw,
                offsetof(SlangTargetDesc, floatingPointMode) +
                    sizeof(raw.floatingPointMode)))
        {
            target.floatingPointMode = static_cast<SlangFloatingPointMode>(raw.floatingPointMode);
        }
        if (rawHasPrefix(
                &raw,
                offsetof(SlangTargetDesc, lineDirectiveMode) +
                    sizeof(raw.lineDirectiveMode)))
        {
            target.lineDirectiveMode = static_cast<SlangLineDirectiveMode>(raw.lineDirectiveMode);
        }
        if (rawHasPrefix(
                &raw,
                offsetof(SlangTargetDesc, forceGLSLScalarBufferLayout) +
                    sizeof(raw.forceGLSLScalarBufferLayout)) &&
            raw.forceGLSLScalarBufferLayout != 0)
        {
            target.forceGLSLScalarBufferLayout = true;
        }

        const CompilerOptionEntry* options = nullptr;
        uint32_t optionCount = 0;
        if (rawHasPrefix(
                &raw,
                offsetof(SlangTargetDesc, compilerOptionEntries) +
                    sizeof(options)))
        {
            rawReadField(&raw, offsetof(SlangTargetDesc, compilerOptionEntries), options);
        }
        if (rawHasPrefix(
                &raw,
                offsetof(SlangTargetDesc, compilerOptionEntryCount) +
                    sizeof(optionCount)))
        {
            rawReadField(
                &raw,
                offsetof(SlangTargetDesc, compilerOptionEntryCount),
                optionCount);
        }
        if (!copyRawCompilerOptions(options, optionCount, storage.targetOptions[index]))
            return SLANG_E_INVALID_ARG;
        if (!storage.targetOptions[index].empty())
        {
            target.compilerOptionEntries = storage.targetOptions[index].data();
            target.compilerOptionEntryCount = static_cast<uint32_t>(
                storage.targetOptions[index].size());
        }
    }
    if (!storage.targets.empty())
    {
        storage.nativeDesc.targets = storage.targets.data();
        storage.nativeDesc.targetCount = static_cast<SlangInt>(storage.targets.size());
    }

    if (rawHasPrefix(desc, offsetof(SlangSessionDesc, flags) + sizeof(desc->flags)))
    {
        rawReadField(desc, offsetof(SlangSessionDesc, flags), storage.nativeDesc.flags);
    }
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, defaultMatrixLayoutMode) +
                sizeof(desc->defaultMatrixLayoutMode)))
    {
        rawReadField(
            desc,
            offsetof(SlangSessionDesc, defaultMatrixLayoutMode),
            storage.nativeDesc.defaultMatrixLayoutMode);
    }

    const char* const* rawSearchPaths = nullptr;
    int64_t rawSearchPathCount = 0;
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, searchPaths) + sizeof(rawSearchPaths)))
    {
        rawReadField(desc, offsetof(SlangSessionDesc, searchPaths), rawSearchPaths);
    }
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, searchPathCount) + sizeof(rawSearchPathCount)))
    {
        rawReadField(
            desc,
            offsetof(SlangSessionDesc, searchPathCount),
            rawSearchPathCount);
    }
    if (!fitsRawSlangInt(rawSearchPathCount) ||
        (rawSearchPathCount != 0 && !rawSearchPaths))
        return SLANG_E_INVALID_ARG;
    storage.searchPaths.reserve(static_cast<std::size_t>(rawSearchPathCount));
    for (int64_t index = 0; index < rawSearchPathCount; ++index)
    {
        if (!rawSearchPaths[index])
            return SLANG_E_INVALID_ARG;
        storage.searchPaths.emplace_back(rawSearchPaths[index]);
    }
    storage.searchPathPointers.reserve(storage.searchPaths.size());
    for (const auto& path : storage.searchPaths)
        storage.searchPathPointers.push_back(path.c_str());
    if (!storage.searchPathPointers.empty())
    {
        storage.nativeDesc.searchPaths = storage.searchPathPointers.data();
        storage.nativeDesc.searchPathCount = static_cast<SlangInt>(
            storage.searchPathPointers.size());
    }

    const SlangPreprocessorMacroDesc* rawMacros = nullptr;
    int64_t rawMacroCount = 0;
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, preprocessorMacros) + sizeof(rawMacros)))
    {
        rawReadField(
            desc,
            offsetof(SlangSessionDesc, preprocessorMacros),
            rawMacros);
    }
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, preprocessorMacroCount) +
                sizeof(rawMacroCount)))
    {
        rawReadField(
            desc,
            offsetof(SlangSessionDesc, preprocessorMacroCount),
            rawMacroCount);
    }
    if (!fitsRawSlangInt(rawMacroCount) || (rawMacroCount != 0 && !rawMacros))
        return SLANG_E_INVALID_ARG;
    storage.macroNames.reserve(static_cast<std::size_t>(rawMacroCount));
    storage.macroValues.reserve(static_cast<std::size_t>(rawMacroCount));
    storage.macros.reserve(static_cast<std::size_t>(rawMacroCount));
    for (int64_t index = 0; index < rawMacroCount; ++index)
    {
        if (!rawMacros[index].name || !rawMacros[index].value)
            return SLANG_E_INVALID_ARG;
        storage.macroNames.emplace_back(rawMacros[index].name);
        storage.macroValues.emplace_back(rawMacros[index].value);
        storage.macros.push_back({
            storage.macroNames.back().c_str(),
            storage.macroValues.back().c_str(),
        });
    }
    if (!storage.macros.empty())
    {
        storage.nativeDesc.preprocessorMacros = storage.macros.data();
        storage.nativeDesc.preprocessorMacroCount = static_cast<SlangInt>(storage.macros.size());
    }

    ISlangFileSystem* rawFileSystem = nullptr;
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, fileSystem) + sizeof(rawFileSystem)))
    {
        rawReadField(desc, offsetof(SlangSessionDesc, fileSystem), rawFileSystem);
    }
    if (rawFileSystem)
    {
        storage.fileSystem = rawFileSystem;
        storage.nativeDesc.fileSystem = storage.fileSystem.get();
    }

    uint8_t enableEffectAnnotations = 0;
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, enableEffectAnnotations) +
                sizeof(enableEffectAnnotations)))
    {
        rawReadField(
            desc,
            offsetof(SlangSessionDesc, enableEffectAnnotations),
            enableEffectAnnotations);
        storage.nativeDesc.enableEffectAnnotations = enableEffectAnnotations != 0;
    }
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, allowGLSLSyntax) +
                sizeof(desc->allowGLSLSyntax)))
        {
        storage.nativeDesc.allowGLSLSyntax = desc->allowGLSLSyntax != 0;
    }

    const CompilerOptionEntry* sessionOptions = nullptr;
    uint32_t sessionOptionCount = 0;
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, compilerOptionEntries) + sizeof(sessionOptions)))
    {
        rawReadField(desc, offsetof(SlangSessionDesc, compilerOptionEntries), sessionOptions);
    }
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, compilerOptionEntryCount) +
                sizeof(sessionOptionCount)))
    {
        rawReadField(
            desc,
            offsetof(SlangSessionDesc, compilerOptionEntryCount),
            sessionOptionCount);
    }
    if (!copyRawCompilerOptions(sessionOptions, sessionOptionCount, storage.compilerOptions))
        return SLANG_E_INVALID_ARG;
    if (!storage.compilerOptions.empty())
    {
        storage.nativeDesc.compilerOptionEntries = storage.compilerOptions.data();
        storage.nativeDesc.compilerOptionEntryCount = static_cast<uint32_t>(
            storage.compilerOptions.size());
    }
    if (rawHasPrefix(
            desc,
            offsetof(SlangSessionDesc, skipSPIRVValidation) +
                sizeof(desc->skipSPIRVValidation)))
    {
        uint8_t skipSpirvValidation = 0;
        rawReadField(
            desc,
            offsetof(SlangSessionDesc, skipSPIRVValidation),
            skipSpirvValidation);
        storage.nativeDesc.skipSPIRVValidation = skipSpirvValidation != 0;
    }

    return SLANG_OK;
}

SlangResult exportRawBlob(
    const Slang::ComPtr<slang::IBlob>& source,
    ISlangBlob** outBlob)
{
    if (!outBlob)
        return SLANG_OK;
    *outBlob = nullptr;
    if (!source)
        return SLANG_OK;
    *outBlob = source.get();
    (*outBlob)->addRef();
    return SLANG_OK;
}

std::unique_ptr<ComponentType> makeRawComponent(
    const Slang::ComPtr<slang::IComponentType>& native,
    slang::ISession* session,
    slang::IModule* module = nullptr)
{
    auto handle = std::make_unique<ComponentType>();
    handle->native = native;
    handle->session = session;
    handle->module = module;
    return handle;
}

template<typename Handle, typename Reset>
void destroyRawHandle(Handle* handle, Reset&& reset) noexcept
{
    if (!handle)
        return;
    try
    {
        runSlangWorker([&](slang::IGlobalSession*) {
            reset(handle);
            delete handle;
        });
    }
    catch (...)
    {
        // If the worker is already shutting down, leaking the opaque handle is
        // safer than releasing an upstream COM object on an arbitrary thread.
    }
}
} // namespace

extern "C"
{
SLANG_C_API_API SlangResult slang_create_global_session(
    const SlangGlobalSessionDesc* desc,
    IGlobalSession** outGlobalSession)
{
    if (!desc || !outGlobalSession)
        return SLANG_E_INVALID_ARG;
    *outGlobalSession = nullptr;
    try
    {
        auto handle = std::make_unique<GlobalSession>();
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            SlangGlobalSessionDesc nativeDesc = {};
            nativeDesc.structureSize = sizeof(SlangGlobalSessionDesc);
            nativeDesc.apiVersion = SLANG_API_VERSION;
            nativeDesc.minLanguageVersion = SLANG_LANGUAGE_VERSION_2025;
            if (!rawHasPrefix(
                    desc,
                    offsetof(SlangGlobalSessionDesc, apiVersion) +
                        sizeof(desc->apiVersion)))
            {
                status = SLANG_E_INVALID_ARG;
                return;
            }
            if (desc->apiVersion != 0)
                nativeDesc.apiVersion = desc->apiVersion;
            if (rawHasPrefix(
                    desc,
                    offsetof(SlangGlobalSessionDesc, minLanguageVersion) +
                        sizeof(desc->minLanguageVersion)) &&
                desc->minLanguageVersion != 0)
                nativeDesc.minLanguageVersion = desc->minLanguageVersion;
            if (rawHasPrefix(
                    desc,
                    offsetof(SlangGlobalSessionDesc, enableGLSL) +
                        sizeof(desc->enableGLSL)))
                nativeDesc.enableGLSL = desc->enableGLSL;
            status = slang_createGlobalSession2(&nativeDesc, handle->native.writeRef());
        });
        if (status != SLANG_OK)
            return status;
        *outGlobalSession = handle.release();
        return SLANG_OK;
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

SLANG_C_API_API void slang_global_session_destroy(IGlobalSession* globalSession)
{
    destroyRawHandle(globalSession, [](GlobalSession* value) {
        value->native.setNull();
    });
}

SLANG_C_API_API const char* slang_global_session_get_build_tag(
    const IGlobalSession* globalSession)
{
    if (!globalSession || !globalSession->native)
        return nullptr;
    try
    {
        const char* result = nullptr;
        runSlangWorker([&](slang::IGlobalSession*) {
            result = globalSession->native->getBuildTagString();
        });
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

SLANG_C_API_API SlangProfileID slang_global_session_find_profile(
    const IGlobalSession* globalSession,
    const char* name)
{
    if (!globalSession || !globalSession->native || !name)
        return SLANG_PROFILE_UNKNOWN;
    try
    {
        SlangProfileID result = SLANG_PROFILE_UNKNOWN;
        runSlangWorker([&](slang::IGlobalSession*) {
            result = globalSession->native->findProfile(name);
        });
        return result;
    }
    catch (...)
    {
        return SLANG_PROFILE_UNKNOWN;
    }
}

SLANG_C_API_API SlangResult slang_global_session_check_compile_target_support(
    const IGlobalSession* globalSession,
    SlangCompileTarget target)
{
    if (!globalSession || !globalSession->native)
        return SLANG_E_INVALID_ARG;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            status = globalSession->native->checkCompileTargetSupport(
                static_cast<SlangCompileTarget>(target));
        });
        return status;
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

SLANG_C_API_API SlangResult slang_global_session_create_session(
    const IGlobalSession* globalSession,
    const SlangSessionDesc* desc,
    ISession** outSession)
{
    if (!globalSession || !globalSession->native || !desc || !outSession)
        return SLANG_E_INVALID_ARG;
    *outSession = nullptr;
    try
    {
        auto handle = std::make_unique<Session>();
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            RawSessionStorage storage;
            status = buildRawSessionDesc(desc, storage);
            if (status != SLANG_OK)
                return;
            status = globalSession->native->createSession(
                storage.nativeDesc,
                handle->native.writeRef());
            if (status != SLANG_OK)
                return;
            handle->global = globalSession->native;
            handle->fileSystem = storage.fileSystem;
            if (gRetainedSlangSessions && handle->fileSystem)
                gRetainedSlangSessions->emplace_back(handle->native.get());
        });
        if (status != SLANG_OK)
            return status;
        *outSession = handle.release();
        return SLANG_OK;
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

SLANG_C_API_API SlangResult slang_file_system_create(
    const SlangFileSystemDesc* desc,
    ISlangFileSystem** outFileSystem)
{
    if (!outFileSystem)
        return SLANG_E_INVALID_ARG;
    *outFileSystem = nullptr;
    try
    {
        SlangLoadFileFunc callback = nullptr;
        void* callbackUserData = nullptr;
        if (desc)
        {
            if (!rawHasPrefix(
                    desc,
                    offsetof(SlangFileSystemDesc, loadFile) +
                        sizeof(desc->loadFile)))
                return SLANG_E_INVALID_ARG;
            rawReadField(desc, offsetof(SlangFileSystemDesc, loadFile), callback);
            rawReadField(
                desc,
                offsetof(SlangFileSystemDesc, loadFileUserData),
                callbackUserData);
        }
        if (!callback && callbackUserData != nullptr)
            return SLANG_E_INVALID_ARG;
        auto native = std::make_unique<RawFileSystem>(callback, callbackUserData);
        *outFileSystem = static_cast<ISlangFileSystem*>(native.release());
        return SLANG_OK;
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

SLANG_C_API_API void slang_file_system_destroy(ISlangFileSystem* fileSystem)
{
    if (!fileSystem)
        return;
    try
    {
        runSlangWorker([&](slang::IGlobalSession*) { fileSystem->release(); });
    }
    catch (...)
    {
        // If the worker is already shutting down, leaking the adapter is safer
        // than releasing an upstream interface on an arbitrary thread.
    }
}

SLANG_C_API_API SlangResult slang_create_blob(
    const void* data,
    size_t size,
    ISlangBlob** outBlob)
{
    if (!outBlob || (!data && size != 0))
        return SLANG_E_INVALID_ARG;
    *outBlob = nullptr;
    static const uint8_t emptyByte = 0;
    *outBlob = slang_createBlob(data ? data : &emptyByte, size);
    return *outBlob ? SLANG_OK : SLANG_E_OUT_OF_MEMORY;
}

SLANG_C_API_API void slang_session_destroy(ISession* session)
{
    destroyRawHandle(session, [](Session* value) {
        value->native.setNull();
        value->fileSystem.setNull();
        value->global.setNull();
    });
}

SLANG_C_API_API SlangResult slang_session_load_module_from_source(
    ISession* session,
    const char* moduleName,
    const char* path,
    ISlangBlob* source,
    ISlangBlob** outDiagnostics,
    IModule** outModule)
{
    if (!session || !session->native || !moduleName || !path || !source || !outModule)
        return SLANG_E_INVALID_ARG;
    *outModule = nullptr;
    if (outDiagnostics)
        *outDiagnostics = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            Slang::ComPtr<slang::IBlob> diagnostics;
            slang::IModule* rawModule = session->native->loadModuleFromSource(
                moduleName,
                path,
                source,
                diagnostics.writeRef());
            exportRawBlob(diagnostics, outDiagnostics);
            if (!rawModule)
            {
                status = SLANG_FAIL;
                return;
            }
            Slang::ComPtr<slang::IModule> module(Slang::INIT_ATTACH, rawModule);
            Slang::ComPtr<slang::IComponentType> component(
                static_cast<slang::IComponentType*>(module.get()));
            auto handle = makeRawComponent(component, session->native.get(), module.get());
            *outModule = handle.release();
            status = SLANG_OK;
        });
        return status;
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

SLANG_C_API_API SlangResult slang_session_create_composite_component_type(
    ISession* session,
    IComponentType* const* componentTypes,
    SlangInt componentTypeCount,
    IComponentType** outComponentType,
    ISlangBlob** outDiagnostics)
{
    if (!session || !session->native || !outComponentType || componentTypeCount < 0 ||
        !fitsRawSlangInt(componentTypeCount) ||
        (componentTypeCount != 0 && !componentTypes))
        return SLANG_E_INVALID_ARG;
    *outComponentType = nullptr;
    if (outDiagnostics)
        *outDiagnostics = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            std::vector<slang::IComponentType*> nativeComponents;
            nativeComponents.reserve(static_cast<std::size_t>(componentTypeCount));
            for (SlangInt index = 0; index < componentTypeCount; ++index)
            {
                if (!componentTypes[index] || !componentTypes[index]->native)
                {
                    status = SLANG_E_INVALID_ARG;
                    return;
                }
                nativeComponents.push_back(componentTypes[index]->native.get());
            }
            Slang::ComPtr<slang::IBlob> diagnostics;
            slang::IComponentType* rawComponent = nullptr;
            status = session->native->createCompositeComponentType(
                nativeComponents.data(),
                static_cast<SlangInt>(nativeComponents.size()),
                &rawComponent,
                diagnostics.writeRef());
            exportRawBlob(diagnostics, outDiagnostics);
            if (status != SLANG_OK || !rawComponent)
                return;
            Slang::ComPtr<slang::IComponentType> component(Slang::INIT_ATTACH, rawComponent);
            auto handle = makeRawComponent(component, session->native.get());
            *outComponentType = handle.release();
        });
        return status;
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

SLANG_C_API_API SlangResult slang_module_find_and_check_entry_point(
    IModule* module,
    const char* name,
    SlangStage stage,
    IEntryPoint** outEntryPoint,
    ISlangBlob** outDiagnostics)
{
    if (!module || !module->native || !module->module || !name || !outEntryPoint)
        return SLANG_E_INVALID_ARG;
    *outEntryPoint = nullptr;
    if (outDiagnostics)
        *outDiagnostics = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            Slang::ComPtr<slang::IBlob> diagnostics;
            slang::IEntryPoint* rawEntryPoint = nullptr;
            status = module->module->findAndCheckEntryPoint(
                name,
                static_cast<SlangStage>(stage),
                &rawEntryPoint,
                diagnostics.writeRef());
            exportRawBlob(diagnostics, outDiagnostics);
            if (status != SLANG_OK || !rawEntryPoint)
                return;
            Slang::ComPtr<slang::IEntryPoint> entryPoint(Slang::INIT_ATTACH, rawEntryPoint);
            Slang::ComPtr<slang::IComponentType> component(
                static_cast<slang::IComponentType*>(entryPoint.get()));
            auto handle = makeRawComponent(component, module->session.get());
            *outEntryPoint = handle.release();
        });
        return status;
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

SLANG_C_API_API const char* slang_module_get_name(const IModule* module)
{
    if (!module || !module->module)
        return nullptr;
    try
    {
        const char* result = nullptr;
        runSlangWorker([&](slang::IGlobalSession*) { result = module->module->getName(); });
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

SLANG_C_API_API const char* slang_module_get_file_path(const IModule* module)
{
    if (!module || !module->module)
        return nullptr;
    try
    {
        const char* result = nullptr;
        runSlangWorker([&](slang::IGlobalSession*) { result = module->module->getFilePath(); });
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

SLANG_C_API_API void slang_component_type_destroy(IComponentType* componentType)
{
    destroyRawHandle(componentType, [](ComponentType* value) {
        value->native.setNull();
        value->session.setNull();
        value->module = nullptr;
    });
}

SLANG_C_API_API SlangResult slang_component_type_link(
    IComponentType* componentType,
    IComponentType** outLinkedComponentType,
    ISlangBlob** outDiagnostics)
{
    if (!componentType || !componentType->native || !outLinkedComponentType)
        return SLANG_E_INVALID_ARG;
    *outLinkedComponentType = nullptr;
    if (outDiagnostics)
        *outDiagnostics = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            Slang::ComPtr<slang::IBlob> diagnostics;
            Slang::ComPtr<slang::IComponentType> linked;
            status = componentType->native->link(linked.writeRef(), diagnostics.writeRef());
            exportRawBlob(diagnostics, outDiagnostics);
            if (status != SLANG_OK || !linked)
                return;
            auto handle = makeRawComponent(linked, componentType->session.get());
            *outLinkedComponentType = handle.release();
        });
        return status;
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

SLANG_C_API_API SlangResult slang_component_type_get_target_code(
    IComponentType* componentType,
    SlangInt targetIndex,
    ISlangBlob** outCode,
    ISlangBlob** outDiagnostics)
{
    if (!componentType || !componentType->native || targetIndex < 0 ||
        !fitsRawSlangInt(targetIndex) || !outCode)
        return SLANG_E_INVALID_ARG;
    *outCode = nullptr;
    if (outDiagnostics)
        *outDiagnostics = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            Slang::ComPtr<slang::IBlob> code;
            Slang::ComPtr<slang::IBlob> diagnostics;
            status = componentType->native->getTargetCode(
                static_cast<SlangInt>(targetIndex),
                code.writeRef(),
                diagnostics.writeRef());
            exportRawBlob(code, outCode);
            exportRawBlob(diagnostics, outDiagnostics);
        });
        return status;
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

SLANG_C_API_API SlangResult slang_component_type_get_entry_point_code(
    IComponentType* componentType,
    SlangInt entryPointIndex,
    SlangInt targetIndex,
    ISlangBlob** outCode,
    ISlangBlob** outDiagnostics)
{
    if (!componentType || !componentType->native || entryPointIndex < 0 || targetIndex < 0 ||
        !fitsRawSlangInt(entryPointIndex) || !fitsRawSlangInt(targetIndex) || !outCode)
        return SLANG_E_INVALID_ARG;
    *outCode = nullptr;
    if (outDiagnostics)
        *outDiagnostics = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            Slang::ComPtr<slang::IBlob> code;
            Slang::ComPtr<slang::IBlob> diagnostics;
            status = componentType->native->getEntryPointCode(
                static_cast<SlangInt>(entryPointIndex),
                static_cast<SlangInt>(targetIndex),
                code.writeRef(),
                diagnostics.writeRef());
            exportRawBlob(code, outCode);
            exportRawBlob(diagnostics, outDiagnostics);
        });
        return status;
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

SLANG_C_API_API SlangResult slang_component_type_get_layout(
    IComponentType* componentType,
    SlangInt targetIndex,
    ProgramLayout** outLayout,
    ISlangBlob** outDiagnostics)
{
    if (!componentType || !componentType->native || targetIndex < 0 ||
        !fitsRawSlangInt(targetIndex) || !outLayout)
        return SLANG_E_INVALID_ARG;
    *outLayout = nullptr;
    if (outDiagnostics)
        *outDiagnostics = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            Slang::ComPtr<slang::IBlob> diagnostics;
            slang::ProgramLayout* nativeLayout = componentType->native->getLayout(
                static_cast<SlangInt>(targetIndex),
                diagnostics.writeRef());
            exportRawBlob(diagnostics, outDiagnostics);
            if (!nativeLayout)
            {
                status = SLANG_FAIL;
                return;
            }
            auto handle = std::make_unique<ProgramLayout>();
            handle->owner = componentType->native;
            handle->native = nativeLayout;
            *outLayout = handle.release();
            status = SLANG_OK;
        });
        return status;
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

SLANG_C_API_API void slang_program_layout_destroy(ProgramLayout* layout)
{
    destroyRawHandle(layout, [](ProgramLayout* value) {
        value->native = nullptr;
        value->owner.setNull();
    });
}

SLANG_C_API_API SlangResult slang_program_layout_to_json(
    ProgramLayout* layout,
    ISlangBlob** outJson)
{
    if (!layout || !layout->native || !outJson)
        return SLANG_E_INVALID_ARG;
    *outJson = nullptr;
    try
    {
        SlangResult status = SLANG_FAIL;
        runSlangWorker([&](slang::IGlobalSession*) {
            Slang::ComPtr<slang::IBlob> json;
            status = layout->native->toJson(json.writeRef());
            exportRawBlob(json, outJson);
        });
        return status;
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

SLANG_C_API_API void slang_blob_destroy(ISlangBlob* blob)
{
    if (!blob)
        return;
    try
    {
        runSlangWorker([&](slang::IGlobalSession*) { blob->release(); });
    }
    catch (...)
    {
        // If the worker is already shutting down, leaking the blob is safer
        // than releasing an upstream interface on an arbitrary thread.
    }
}

SLANG_C_API_API const void* slang_blob_get_buffer_pointer(
    ISlangBlob* blob)
{
    return blob ? blob->getBufferPointer() : nullptr;
}

SLANG_C_API_API size_t slang_blob_get_buffer_size(ISlangBlob* blob)
{
    return blob ? blob->getBufferSize() : 0;
}

SLANG_C_API_API uint32_t slang_abi_version(void)
{
    return SLANG_C_API_ABI_VERSION;
}

}
