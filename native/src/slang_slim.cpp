#include "slang_slim.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
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

struct TargetInfo
{
    slang_slim_target publicTarget;
    SlangCompileTarget slangTarget;
    const char* profile;
    bool text;
};

bool getTargetInfo(slang_slim_target target, TargetInfo& out) noexcept
{
    switch (target)
    {
    case SLANG_SLIM_TARGET_HLSL:
        out = {target, SLANG_HLSL, "sm_6_0", true};
        return true;
    case SLANG_SLIM_TARGET_SPIRV:
        out = {target, SLANG_SPIRV, "spirv_1_3", false};
        return true;
    case SLANG_SLIM_TARGET_METAL:
        out = {target, SLANG_METAL, "metallib_2_3", true};
        return true;
    default:
        return false;
    }
}

bool getStage(slang_slim_stage stage, SlangStage& out) noexcept
{
    switch (stage)
    {
    case SLANG_SLIM_STAGE_VERTEX:
        out = SLANG_STAGE_VERTEX;
        return true;
    case SLANG_SLIM_STAGE_FRAGMENT:
        out = SLANG_STAGE_FRAGMENT;
        return true;
    case SLANG_SLIM_STAGE_COMPUTE:
        out = SLANG_STAGE_COMPUTE;
        return true;
    default:
        return false;
    }
}

template<typename T>
bool hasFullStruct(const T* value) noexcept
{
    return value && value->struct_size >= sizeof(T);
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
        if (sameGuid(uuid, ISlangFileSystem::getTypeGuid()) ||
            sameGuid(uuid, ISlangCastable::getTypeGuid()) ||
            sameGuid(uuid, ISlangUnknown::getTypeGuid()))
        {
            *outObject = static_cast<ISlangFileSystem*>(this);
            addRef();
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
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
        if (sameGuid(uuid, ISlangFileSystem::getTypeGuid()) ||
            sameGuid(uuid, ISlangCastable::getTypeGuid()) ||
            sameGuid(uuid, ISlangUnknown::getTypeGuid()))
            return static_cast<ISlangFileSystem*>(this);
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
            if (!callbackBlob.data || callbackBlob.size == 0)
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
        if (!data || size == 0)
            return SLANG_E_CANNOT_OPEN;
        *outBlob = slang_createBlob(data, size);
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

struct TargetOutput
{
    TargetInfo info = {};
    ComPtr<slang::IBlob> reflection;
    std::vector<ComPtr<slang::IBlob>> code;
};
} // namespace

struct slang_slim_compiler
{
    ComPtr<slang::IGlobalSession> globalSession;
};

struct slang_slim_compilation
{
    // Declare the file system before the Slang objects. Destruction then
    // releases the session before deleting the object the session references.
    std::unique_ptr<NativeFileSystem> fileSystem;
    ComPtr<slang::ISession> session;
    ComPtr<slang::IBlob> sourceBlob;
    ComPtr<slang::IModule> module;
    ComPtr<slang::IComponentType> program;
    ComPtr<slang::IComponentType> linkedProgram;

    std::string moduleName;
    std::string sourcePath;
    std::string diagnostics;
    std::vector<std::string> entryPointNames;
    std::vector<slang::TargetDesc> targetDescs;
    std::vector<DefineStorage> defineStorage;
    std::vector<slang::PreprocessorMacroDesc> defineDescs;
    std::vector<TargetOutput> outputs;
};

namespace
{
slang_slim_status validateDescriptor(const slang_slim_compile_desc* desc) noexcept
{
    if (!hasFullStruct(desc) || !desc->source || desc->source_size == 0 ||
        !desc->entry_points || desc->entry_point_count == 0 || !desc->targets ||
        desc->target_count == 0 || desc->target_count > 3)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;

    if (desc->define_count != 0 && !desc->defines)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    if (desc->virtual_file_count != 0 && !desc->virtual_files)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    if (desc->load_file == nullptr && desc->load_file_user_data != nullptr)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;

    for (std::size_t index = 0; index < desc->entry_point_count; ++index)
    {
        const auto& entryPoint = desc->entry_points[index];
        if (!hasFullStruct(&entryPoint) || !entryPoint.name || entryPoint.name[0] == '\0')
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        SlangStage ignoredStage;
        if (!getStage(entryPoint.stage, ignoredStage))
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (std::strcmp(entryPoint.name, desc->entry_points[previous].name) == 0)
                return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        }
    }

    for (std::size_t index = 0; index < desc->target_count; ++index)
    {
        const auto& target = desc->targets[index];
        if (!hasFullStruct(&target))
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        TargetInfo ignoredInfo;
        if (!getTargetInfo(target.target, ignoredInfo))
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
#if defined(__ANDROID__)
        if (target.target != SLANG_SLIM_TARGET_SPIRV)
            return SLANG_SLIM_STATUS_UNSUPPORTED;
#endif
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (target.target == desc->targets[previous].target)
                return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
        }
    }

    for (std::size_t index = 0; index < desc->define_count; ++index)
    {
        const auto& define = desc->defines[index];
        if (!hasFullStruct(&define) || !define.name || define.name[0] == '\0' || !define.value)
            return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    }

    for (std::size_t index = 0; index < desc->virtual_file_count; ++index)
    {
        const auto& file = desc->virtual_files[index];
        if (!hasFullStruct(&file) || !file.path || file.path[0] == '\0' || !file.data ||
            file.size == 0)
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
    slang_slim_compiler* compiler,
    const slang_slim_compile_desc* desc,
    slang_slim_compilation& result)
{
    result.moduleName = desc->module_name ? desc->module_name : "slang_slim_module";
    result.sourcePath = desc->source_path ? desc->source_path : "slang_slim_input.hlsl";
    result.entryPointNames.reserve(desc->entry_point_count);
    for (std::size_t index = 0; index < desc->entry_point_count; ++index)
        result.entryPointNames.emplace_back(desc->entry_points[index].name);

    result.targetDescs.resize(desc->target_count);
    result.outputs.resize(desc->target_count);
    for (std::size_t index = 0; index < desc->target_count; ++index)
    {
        TargetInfo info;
        getTargetInfo(desc->targets[index].target, info);
        result.outputs[index].info = info;
        auto& targetDesc = result.targetDescs[index];
        targetDesc.structureSize = sizeof(slang::TargetDesc);
        targetDesc.format = info.slangTarget;
        targetDesc.profile = compiler->globalSession->findProfile(info.profile);
        if (targetDesc.profile == SLANG_PROFILE_UNKNOWN)
        {
            result.diagnostics += "target profile is unavailable: ";
            result.diagnostics += info.profile;
            result.diagnostics.push_back('\n');
            return SLANG_SLIM_STATUS_UNSUPPORTED;
        }
    }

    result.defineStorage.reserve(desc->define_count);
    for (std::size_t index = 0; index < desc->define_count; ++index)
    {
        result.defineStorage.push_back(
            {desc->defines[index].name, desc->defines[index].value});
    }
    result.defineDescs.resize(result.defineStorage.size());
    for (std::size_t index = 0; index < result.defineStorage.size(); ++index)
    {
        result.defineDescs[index].name = result.defineStorage[index].name.c_str();
        result.defineDescs[index].value = result.defineStorage[index].value.c_str();
    }

    if (desc->virtual_file_count != 0 || desc->load_file != nullptr)
    {
        result.fileSystem = std::make_unique<NativeFileSystem>(
            desc->virtual_files,
            desc->virtual_file_count,
            desc->load_file,
            desc->load_file_user_data);
    }

    slang::SessionDesc sessionDesc = {};
    sessionDesc.structureSize = sizeof(slang::SessionDesc);
    sessionDesc.targets = result.targetDescs.data();
    sessionDesc.targetCount = static_cast<SlangInt>(result.targetDescs.size());
    sessionDesc.preprocessorMacros = result.defineDescs.data();
    sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(result.defineDescs.size());
    sessionDesc.fileSystem = result.fileSystem.get();
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    sessionDesc.allowGLSLSyntax = false;

    if (mapSlangStatus(compiler->globalSession->createSession(
            sessionDesc,
            result.session.writeRef())) != SLANG_SLIM_STATUS_OK)
    {
        result.diagnostics += "failed to create Slang session\n";
        return SLANG_SLIM_STATUS_INTERNAL_ERROR;
    }

    result.sourceBlob.attach(slang_createBlob(desc->source, desc->source_size));
    if (!result.sourceBlob)
    {
        result.diagnostics += "failed to copy source bytes\n";
        return SLANG_SLIM_STATUS_OUT_OF_MEMORY;
    }

    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = result.session->loadModuleFromSource(
        result.moduleName.c_str(),
        result.sourcePath.c_str(),
        result.sourceBlob,
        diagnostics.writeRef());
    appendDiagnostics(result.diagnostics, diagnostics);
    result.module.attach(module);
    if (!result.module)
        return SLANG_SLIM_STATUS_COMPILE_ERROR;

    std::vector<ComPtr<slang::IEntryPoint>> entryPoints;
    entryPoints.reserve(desc->entry_point_count);
    std::vector<slang::IComponentType*> components;
    components.reserve(desc->entry_point_count + 1);
    components.push_back(result.module);
    for (std::size_t index = 0; index < desc->entry_point_count; ++index)
    {
        SlangStage stage;
        getStage(desc->entry_points[index].stage, stage);
        slang::IEntryPoint* entryPoint = nullptr;
        diagnostics.setNull();
        const SlangResult status = result.module->findAndCheckEntryPoint(
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
    slang::IComponentType* program = nullptr;
    if (SLANG_FAILED(result.session->createCompositeComponentType(
            components.data(),
            static_cast<SlangInt>(components.size()),
            &program,
            diagnostics.writeRef())))
    {
        appendDiagnostics(result.diagnostics, diagnostics);
        return SLANG_SLIM_STATUS_COMPILE_ERROR;
    }
    appendDiagnostics(result.diagnostics, diagnostics);
    result.program.attach(program);

    diagnostics.setNull();
    if (SLANG_FAILED(result.program->link(result.linkedProgram.writeRef(), diagnostics.writeRef())))
    {
        appendDiagnostics(result.diagnostics, diagnostics);
        return SLANG_SLIM_STATUS_COMPILE_ERROR;
    }
    appendDiagnostics(result.diagnostics, diagnostics);

    for (std::size_t targetIndex = 0; targetIndex < result.outputs.size(); ++targetIndex)
    {
        auto& output = result.outputs[targetIndex];
        diagnostics.setNull();
        slang::ProgramLayout* layout = result.linkedProgram->getLayout(
            static_cast<SlangInt>(targetIndex),
            diagnostics.writeRef());
        appendDiagnostics(result.diagnostics, diagnostics);
        if (!layout)
            return SLANG_SLIM_STATUS_COMPILE_ERROR;

        diagnostics.setNull();
        if (SLANG_FAILED(layout->toJson(output.reflection.writeRef())))
        {
            appendDiagnostics(result.diagnostics, diagnostics);
            return SLANG_SLIM_STATUS_COMPILE_ERROR;
        }
        appendDiagnostics(result.diagnostics, diagnostics);

        output.code.resize(desc->entry_point_count);
        for (std::size_t entryIndex = 0; entryIndex < desc->entry_point_count; ++entryIndex)
        {
            diagnostics.setNull();
            const SlangResult status = result.linkedProgram->getEntryPointCode(
                static_cast<SlangInt>(entryIndex),
                static_cast<SlangInt>(targetIndex),
                output.code[entryIndex].writeRef(),
                diagnostics.writeRef());
            appendDiagnostics(result.diagnostics, diagnostics);
            if (SLANG_FAILED(status) || !output.code[entryIndex])
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
        SlangGlobalSessionDesc desc = {};
        desc.structureSize = sizeof(SlangGlobalSessionDesc);
        desc.apiVersion = SLANG_API_VERSION;
        desc.minLanguageVersion = SLANG_LANGUAGE_VERSION_2025;
        desc.enableGLSL = false;
        const slang_slim_status status = mapSlangStatus(
            slang_createGlobalSession2(&desc, compiler->globalSession.writeRef()));
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
    if (!compiler || !compiler->globalSession)
        return nullptr;
    return compiler->globalSession->getBuildTagString();
}

SLANG_SLIM_API int32_t slang_slim_compiler_supports_target(
    const slang_slim_compiler* compiler,
    slang_slim_target target)
{
    if (!compiler || !compiler->globalSession)
        return 0;
    TargetInfo info;
    if (!getTargetInfo(target, info))
        return 0;
#if defined(__ANDROID__)
    if (target != SLANG_SLIM_TARGET_SPIRV)
        return 0;
#endif
    if (compiler->globalSession->findProfile(info.profile) == SLANG_PROFILE_UNKNOWN)
        return 0;
    // Slang's target-support query also checks optional downstream tools. The
    // slim build intentionally omits those tools (for example SPIR-V
    // validators), even though the in-process emitters remain available. The
    // fixed profile check is therefore the authoritative capability query;
    // an actual compile still reports backend failures.
    return 1;
}

SLANG_SLIM_API slang_slim_status slang_slim_compile(
    const slang_slim_compiler* compiler,
    const slang_slim_compile_desc* desc,
    slang_slim_compilation** outCompilation)
{
    if (!outCompilation)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;
    *outCompilation = nullptr;
    if (!compiler || !compiler->globalSession)
        return SLANG_SLIM_STATUS_INVALID_ARGUMENT;

    const slang_slim_status validationStatus = validateDescriptor(desc);
    if (validationStatus != SLANG_SLIM_STATUS_OK)
        return validationStatus;

    try
    {
        auto compilation = std::make_unique<slang_slim_compilation>();
        *outCompilation = compilation.get();
        slang_slim_status status = SLANG_SLIM_STATUS_INTERNAL_ERROR;
        try
        {
            status = compileImpl(const_cast<slang_slim_compiler*>(compiler), desc, *compilation);
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
    if (entryPointIndex >= output.code.size() || !output.code[entryPointIndex])
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
    if (!reflection)
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
