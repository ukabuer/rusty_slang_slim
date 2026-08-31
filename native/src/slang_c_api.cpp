#include <slang-com-ptr.h>
#include <slang.h>
#include <core/slang-blob.h>

#include "slang_c_api.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace
{
/* Keep the C projection mechanically honest as Slang evolves. The fields
 * below are the ABI-visible subset of the corresponding C++ records; all
 * translation into the namespaced Slang records happens in one place. */
static_assert(sizeof(SlangTargetDesc) == sizeof(slang::TargetDesc));
static_assert(offsetof(SlangTargetDesc, structureSize) == offsetof(slang::TargetDesc, structureSize));
static_assert(offsetof(SlangTargetDesc, format) == offsetof(slang::TargetDesc, format));
static_assert(offsetof(SlangTargetDesc, profile) == offsetof(slang::TargetDesc, profile));
static_assert(offsetof(SlangTargetDesc, flags) == offsetof(slang::TargetDesc, flags));
static_assert(offsetof(SlangTargetDesc, floatingPointMode) ==
              offsetof(slang::TargetDesc, floatingPointMode));
static_assert(offsetof(SlangTargetDesc, lineDirectiveMode) ==
              offsetof(slang::TargetDesc, lineDirectiveMode));
static_assert(offsetof(SlangTargetDesc, forceGLSLScalarBufferLayout) ==
              offsetof(slang::TargetDesc, forceGLSLScalarBufferLayout));
static_assert(offsetof(SlangTargetDesc, compilerOptionEntries) ==
              offsetof(slang::TargetDesc, compilerOptionEntries));
static_assert(offsetof(SlangTargetDesc, compilerOptionEntryCount) ==
              offsetof(slang::TargetDesc, compilerOptionEntryCount));
static_assert(sizeof(CompilerOptionValue) == sizeof(slang::CompilerOptionValue));
static_assert(offsetof(CompilerOptionValue, kind) == offsetof(slang::CompilerOptionValue, kind));
static_assert(offsetof(CompilerOptionValue, intValue0) ==
              offsetof(slang::CompilerOptionValue, intValue0));
static_assert(offsetof(CompilerOptionValue, intValue1) ==
              offsetof(slang::CompilerOptionValue, intValue1));
static_assert(offsetof(CompilerOptionValue, stringValue0) ==
              offsetof(slang::CompilerOptionValue, stringValue0));
static_assert(offsetof(CompilerOptionValue, stringValue1) ==
              offsetof(slang::CompilerOptionValue, stringValue1));
static_assert(sizeof(CompilerOptionEntry) == sizeof(slang::CompilerOptionEntry));
static_assert(offsetof(CompilerOptionEntry, name) == offsetof(slang::CompilerOptionEntry, name));
static_assert(offsetof(CompilerOptionEntry, value) == offsetof(slang::CompilerOptionEntry, value));
static_assert(sizeof(SlangSessionDesc) == sizeof(slang::SessionDesc));
static_assert(offsetof(SlangSessionDesc, structureSize) == offsetof(slang::SessionDesc, structureSize));
static_assert(offsetof(SlangSessionDesc, targets) == offsetof(slang::SessionDesc, targets));
static_assert(offsetof(SlangSessionDesc, targetCount) == offsetof(slang::SessionDesc, targetCount));
static_assert(offsetof(SlangSessionDesc, flags) == offsetof(slang::SessionDesc, flags));
static_assert(offsetof(SlangSessionDesc, defaultMatrixLayoutMode) ==
              offsetof(slang::SessionDesc, defaultMatrixLayoutMode));
static_assert(offsetof(SlangSessionDesc, searchPaths) == offsetof(slang::SessionDesc, searchPaths));
static_assert(offsetof(SlangSessionDesc, searchPathCount) ==
              offsetof(slang::SessionDesc, searchPathCount));
static_assert(offsetof(SlangSessionDesc, preprocessorMacros) ==
              offsetof(slang::SessionDesc, preprocessorMacros));
static_assert(offsetof(SlangSessionDesc, preprocessorMacroCount) ==
              offsetof(slang::SessionDesc, preprocessorMacroCount));
static_assert(offsetof(SlangSessionDesc, fileSystem) == offsetof(slang::SessionDesc, fileSystem));
static_assert(offsetof(SlangSessionDesc, enableEffectAnnotations) ==
              offsetof(slang::SessionDesc, enableEffectAnnotations));
static_assert(offsetof(SlangSessionDesc, allowGLSLSyntax) ==
              offsetof(slang::SessionDesc, allowGLSLSyntax));
static_assert(offsetof(SlangSessionDesc, compilerOptionEntries) ==
              offsetof(slang::SessionDesc, compilerOptionEntries));
static_assert(offsetof(SlangSessionDesc, compilerOptionEntryCount) ==
              offsetof(slang::SessionDesc, compilerOptionEntryCount));
static_assert(offsetof(SlangSessionDesc, skipSPIRVValidation) ==
              offsetof(slang::SessionDesc, skipSPIRVValidation));

bool sameGuid(const SlangUUID& left, const SlangUUID& right) noexcept
{
    return std::memcmp(&left, &right, sizeof(SlangUUID)) == 0;
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
/* Keep signed count/index values representable before converting them to a
 * size_t for the temporary pointer array. This is an ABI-safety guard, not a
 * Slang target or feature policy. */
template<typename T>
bool fitsRawSlangInt(T value) noexcept
{
    return value >= 0 && static_cast<uint64_t>(value) <=
        static_cast<uint64_t>(std::numeric_limits<SlangInt>::max());
}

/*
 * The public C header projects Slang's namespaced SessionDesc into a C
 * record. Keep this conversion deliberately shallow: Slang owns/copies the
 * pointed-to descriptors during createSession, just as it does for a native
 * C++ caller. The only work here is copying the versioned prefix into a
 * correctly typed C++ object, which avoids crossing the FFI boundary with a
 * C++ reference or vtable.
 */
slang::SessionDesc makeNativeSessionDesc(const SlangSessionDesc* source) noexcept
{
    slang::SessionDesc destination = {};
    if (!source)
        return destination;

    const std::size_t sourceSize = source->structureSize;
    const std::size_t copySize = sourceSize < sizeof(destination) ? sourceSize : sizeof(destination);
    std::memcpy(&destination, source, copySize);
    return destination;
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
        reset(handle);
        delete handle;
    }
    catch (...)
    {
        // C++ exceptions must not cross the stable C ABI. The opaque handle is
        // intentionally leaked if an unexpected destructor throws.
    }
}
} // namespace

extern "C"
{
SLANG_C_API SlangResult slang_create_global_session(
    const SlangGlobalSessionDesc* desc,
    IGlobalSession** outGlobalSession)
{
    if (!desc || !outGlobalSession)
        return SLANG_E_INVALID_ARG;
    *outGlobalSession = nullptr;
    try
    {
        auto handle = std::make_unique<GlobalSession>();
        const SlangResult status =
            slang_createGlobalSession2(desc, handle->native.writeRef());
        if (SLANG_FAILED(status))
            return status;
        *outGlobalSession = handle.release();
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

SLANG_C_API void slang_global_session_destroy(IGlobalSession* globalSession)
{
    destroyRawHandle(globalSession, [](GlobalSession* value) {
        value->native.setNull();
    });
}

SLANG_C_API const char* slang_global_session_get_build_tag(
    const IGlobalSession* globalSession)
{
    if (!globalSession || !globalSession->native)
        return nullptr;
    try
    {
        const char* result = nullptr;
        result = globalSession->native->getBuildTagString();
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

SLANG_C_API SlangProfileID slang_global_session_find_profile(
    const IGlobalSession* globalSession,
    const char* name)
{
    if (!globalSession || !globalSession->native || !name)
        return SLANG_PROFILE_UNKNOWN;
    try
    {
        SlangProfileID result = SLANG_PROFILE_UNKNOWN;
        result = globalSession->native->findProfile(name);
        return result;
    }
    catch (...)
    {
        return SLANG_PROFILE_UNKNOWN;
    }
}

SLANG_C_API SlangResult slang_global_session_check_compile_target_support(
    const IGlobalSession* globalSession,
    SlangCompileTarget target)
{
    if (!globalSession || !globalSession->native)
        return SLANG_E_INVALID_ARG;
    try
    {
        const SlangResult status = globalSession->native->checkCompileTargetSupport(
            static_cast<SlangCompileTarget>(target));
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

SLANG_C_API SlangResult slang_global_session_create_session(
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
        const auto nativeDesc = makeNativeSessionDesc(desc);
        const SlangResult status = globalSession->native->createSession(
            nativeDesc,
            handle->native.writeRef());
        if (SLANG_FAILED(status))
            return status;
        handle->global = globalSession->native;
        *outSession = handle.release();
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

SLANG_C_API SlangResult slang_file_system_create(
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
            callback = desc->loadFile;
            callbackUserData = desc->loadFileUserData;
        }
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

SLANG_C_API void slang_file_system_destroy(ISlangFileSystem* fileSystem)
{
    if (!fileSystem)
        return;
    try
    {
        fileSystem->release();
    }
    catch (...)
    {
        // C++ exceptions must not cross the stable C ABI.
    }
}

SLANG_C_API SlangResult slang_create_blob(
    const void* data,
    size_t size,
    ISlangBlob** outBlob)
{
    if (!outBlob || (!data && size != 0))
        return SLANG_E_INVALID_ARG;
    *outBlob = nullptr;
    Slang::ComPtr<ISlangBlob> blob;
    const SlangResult status = Slang::RawBlob::tryCreate(data, size, blob);
    if (SLANG_FAILED(status))
        return status;
    *outBlob = blob.detach();
    return SLANG_OK;
}

SLANG_C_API void slang_session_destroy(ISession* session)
{
    destroyRawHandle(session, [](Session* value) {
        value->native.setNull();
        value->global.setNull();
    });
}

SLANG_C_API SlangResult slang_session_load_module_from_source(
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
        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* rawModule = session->native->loadModuleFromSource(
            moduleName,
            path,
            source,
            diagnostics.writeRef());
        exportRawBlob(diagnostics, outDiagnostics);
        if (!rawModule)
            return SLANG_FAIL;
        Slang::ComPtr<slang::IModule> module(Slang::INIT_ATTACH, rawModule);
        Slang::ComPtr<slang::IComponentType> component(
            static_cast<slang::IComponentType*>(module.get()));
        auto handle = makeRawComponent(component, session->native.get(), module.get());
        *outModule = handle.release();
        status = SLANG_OK;
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

SLANG_C_API SlangResult slang_session_create_composite_component_type(
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
        std::vector<slang::IComponentType*> nativeComponents;
        nativeComponents.reserve(static_cast<std::size_t>(componentTypeCount));
        for (SlangInt index = 0; index < componentTypeCount; ++index)
        {
            if (!componentTypes[index] || !componentTypes[index]->native)
            {
                status = SLANG_E_INVALID_ARG;
                return status;
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
        if (SLANG_FAILED(status) || !rawComponent)
            return status;
        Slang::ComPtr<slang::IComponentType> component(Slang::INIT_ATTACH, rawComponent);
        auto handle = makeRawComponent(component, session->native.get());
        *outComponentType = handle.release();
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

SLANG_C_API SlangResult slang_module_find_and_check_entry_point(
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
        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IEntryPoint* rawEntryPoint = nullptr;
        status = module->module->findAndCheckEntryPoint(
            name,
            static_cast<SlangStage>(stage),
            &rawEntryPoint,
            diagnostics.writeRef());
        exportRawBlob(diagnostics, outDiagnostics);
        if (SLANG_FAILED(status) || !rawEntryPoint)
            return status;
        Slang::ComPtr<slang::IEntryPoint> entryPoint(Slang::INIT_ATTACH, rawEntryPoint);
        Slang::ComPtr<slang::IComponentType> component(
            static_cast<slang::IComponentType*>(entryPoint.get()));
        auto handle = makeRawComponent(component, module->session.get());
        *outEntryPoint = handle.release();
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

SLANG_C_API const char* slang_module_get_name(const IModule* module)
{
    if (!module || !module->module)
        return nullptr;
    try
    {
        const char* result = nullptr;
        result = module->module->getName();
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

SLANG_C_API const char* slang_module_get_file_path(const IModule* module)
{
    if (!module || !module->module)
        return nullptr;
    try
    {
        const char* result = nullptr;
        result = module->module->getFilePath();
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

SLANG_C_API void slang_component_type_destroy(IComponentType* componentType)
{
    destroyRawHandle(componentType, [](ComponentType* value) {
        value->native.setNull();
        value->session.setNull();
        value->module = nullptr;
    });
}

SLANG_C_API SlangResult slang_component_type_link(
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
        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IComponentType> linked;
        status = componentType->native->link(linked.writeRef(), diagnostics.writeRef());
        exportRawBlob(diagnostics, outDiagnostics);
        if (SLANG_FAILED(status) || !linked)
            return status;
        auto handle = makeRawComponent(linked, componentType->session.get());
        *outLinkedComponentType = handle.release();
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

SLANG_C_API SlangResult slang_component_type_get_target_code(
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
        Slang::ComPtr<slang::IBlob> code;
        Slang::ComPtr<slang::IBlob> diagnostics;
        status = componentType->native->getTargetCode(
            static_cast<SlangInt>(targetIndex),
            code.writeRef(),
            diagnostics.writeRef());
        exportRawBlob(code, outCode);
        exportRawBlob(diagnostics, outDiagnostics);
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

SLANG_C_API SlangResult slang_component_type_get_entry_point_code(
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
        Slang::ComPtr<slang::IBlob> code;
        Slang::ComPtr<slang::IBlob> diagnostics;
        status = componentType->native->getEntryPointCode(
            static_cast<SlangInt>(entryPointIndex),
            static_cast<SlangInt>(targetIndex),
            code.writeRef(),
            diagnostics.writeRef());
        exportRawBlob(code, outCode);
        exportRawBlob(diagnostics, outDiagnostics);
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

SLANG_C_API SlangResult slang_component_type_get_layout(
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
        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::ProgramLayout* nativeLayout = componentType->native->getLayout(
            static_cast<SlangInt>(targetIndex),
            diagnostics.writeRef());
        exportRawBlob(diagnostics, outDiagnostics);
        if (!nativeLayout)
            return SLANG_FAIL;
        auto handle = std::make_unique<ProgramLayout>();
        handle->owner = componentType->native;
        handle->native = nativeLayout;
        *outLayout = handle.release();
        status = SLANG_OK;
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

SLANG_C_API void slang_program_layout_destroy(ProgramLayout* layout)
{
    destroyRawHandle(layout, [](ProgramLayout* value) {
        value->native = nullptr;
        value->owner.setNull();
    });
}

SLANG_C_API SlangReflection* slang_program_layout_get_reflection(ProgramLayout* layout)
{
    if (!layout || !layout->native)
        return nullptr;
    return reinterpret_cast<SlangReflection*>(layout->native);
}

/*
 * Reflection bridge
 * -----------------
 *
 * The public C++ reflection records are intentionally layout-compatible with
 * Slang's opaque C names.  Keep the casts in this translation unit so the
 * packaged header never exposes a C++ vtable.  These wrappers call the C++
 * methods rather than the deprecated spReflection_* entry points; the latter
 * are implementation details of the currently pinned Slang release and may
 * disappear from a future slang-deprecated.h.
 */

SLANG_C_API SlangResult slang_reflection_to_json(
    SlangReflection* reflection,
    ISlangBlob** outBlob)
{
    return reinterpret_cast<slang::ShaderReflection*>(reflection)->toJson(outBlob);
}

SLANG_C_API unsigned slang_reflection_get_parameter_count(SlangReflection* reflection)
{
    return reinterpret_cast<slang::ShaderReflection*>(reflection)->getParameterCount();
}

SLANG_C_API SlangReflectionParameter* slang_reflection_get_parameter_by_index(
    SlangReflection* reflection,
    unsigned index)
{
    return reinterpret_cast<SlangReflectionParameter*>(
        reinterpret_cast<slang::ShaderReflection*>(reflection)->getParameterByIndex(index));
}

SLANG_C_API SlangUInt slang_reflection_get_entry_point_count(SlangReflection* reflection)
{
    return reinterpret_cast<slang::ShaderReflection*>(reflection)->getEntryPointCount();
}

SLANG_C_API SlangReflectionEntryPoint* slang_reflection_get_entry_point_by_index(
    SlangReflection* reflection,
    SlangUInt index)
{
    return reinterpret_cast<SlangReflectionEntryPoint*>(
        reinterpret_cast<slang::ShaderReflection*>(reflection)->getEntryPointByIndex(index));
}

SLANG_C_API SlangReflectionEntryPoint* slang_reflection_find_entry_point_by_name(
    SlangReflection* reflection,
    const char* name)
{
    return reinterpret_cast<SlangReflectionEntryPoint*>(
        reinterpret_cast<slang::ShaderReflection*>(reflection)->findEntryPointByName(name));
}

SLANG_C_API SlangReflectionType* slang_reflection_find_type_by_name(
    SlangReflection* reflection,
    const char* name)
{
    return reinterpret_cast<SlangReflectionType*>(
        reinterpret_cast<slang::ShaderReflection*>(reflection)->findTypeByName(name));
}

SLANG_C_API SlangReflectionTypeLayout* slang_reflection_get_type_layout(
    SlangReflection* reflection,
    SlangReflectionType* reflectionType,
    SlangLayoutRules rules)
{
    return reinterpret_cast<SlangReflectionTypeLayout*>(
        reinterpret_cast<slang::ShaderReflection*>(reflection)->getTypeLayout(
            reinterpret_cast<slang::TypeReflection*>(reflectionType),
            static_cast<slang::LayoutRules>(rules)));
}

SLANG_C_API SlangReflectionTypeLayout* slang_reflection_get_global_params_type_layout(
    SlangReflection* reflection)
{
    return reinterpret_cast<SlangReflectionTypeLayout*>(
        reinterpret_cast<slang::ShaderReflection*>(reflection)->getGlobalParamsTypeLayout());
}

SLANG_C_API SlangReflectionVariableLayout* slang_reflection_get_global_params_var_layout(
    SlangReflection* reflection)
{
    return reinterpret_cast<SlangReflectionVariableLayout*>(
        reinterpret_cast<slang::ShaderReflection*>(reflection)->getGlobalParamsVarLayout());
}

SLANG_C_API const char* slang_reflection_entry_point_get_name(
    SlangReflectionEntryPoint* entryPoint)
{
    return reinterpret_cast<slang::EntryPointReflection*>(entryPoint)->getName();
}

SLANG_C_API unsigned slang_reflection_entry_point_get_parameter_count(
    SlangReflectionEntryPoint* entryPoint)
{
    return reinterpret_cast<slang::EntryPointReflection*>(entryPoint)->getParameterCount();
}

SLANG_C_API SlangReflectionVariableLayout*
slang_reflection_entry_point_get_parameter_by_index(
    SlangReflectionEntryPoint* entryPoint,
    unsigned index)
{
    return reinterpret_cast<SlangReflectionVariableLayout*>(
        reinterpret_cast<slang::EntryPointReflection*>(entryPoint)->getParameterByIndex(index));
}

SLANG_C_API SlangStage slang_reflection_entry_point_get_stage(
    SlangReflectionEntryPoint* entryPoint)
{
    return reinterpret_cast<slang::EntryPointReflection*>(entryPoint)->getStage();
}

SLANG_C_API void slang_reflection_entry_point_get_compute_thread_group_size(
    SlangReflectionEntryPoint* entryPoint,
    SlangUInt axisCount,
    SlangUInt* outSizeAlongAxis)
{
    reinterpret_cast<slang::EntryPointReflection*>(entryPoint)->getComputeThreadGroupSize(
        axisCount,
        outSizeAlongAxis);
}

SLANG_C_API SlangReflectionVariableLayout* slang_reflection_entry_point_get_var_layout(
    SlangReflectionEntryPoint* entryPoint)
{
    return reinterpret_cast<SlangReflectionVariableLayout*>(
        reinterpret_cast<slang::EntryPointReflection*>(entryPoint)->getVarLayout());
}

SLANG_C_API SlangReflectionVariableLayout* slang_reflection_entry_point_get_result_var_layout(
    SlangReflectionEntryPoint* entryPoint)
{
    return reinterpret_cast<SlangReflectionVariableLayout*>(
        reinterpret_cast<slang::EntryPointReflection*>(entryPoint)->getResultVarLayout());
}

SLANG_C_API SlangTypeKind slang_reflection_type_get_kind(SlangReflectionType* type)
{
    return static_cast<SlangTypeKind>(
        reinterpret_cast<slang::TypeReflection*>(type)->getKind());
}

SLANG_C_API unsigned slang_reflection_type_get_field_count(SlangReflectionType* type)
{
    return reinterpret_cast<slang::TypeReflection*>(type)->getFieldCount();
}

SLANG_C_API SlangReflectionVariable* slang_reflection_type_get_field_by_index(
    SlangReflectionType* type,
    unsigned index)
{
    return reinterpret_cast<SlangReflectionVariable*>(
        reinterpret_cast<slang::TypeReflection*>(type)->getFieldByIndex(index));
}

SLANG_C_API size_t slang_reflection_type_get_element_count(SlangReflectionType* type)
{
    return reinterpret_cast<slang::TypeReflection*>(type)->getElementCount();
}

SLANG_C_API SlangReflectionType* slang_reflection_type_get_element_type(
    SlangReflectionType* type)
{
    return reinterpret_cast<SlangReflectionType*>(
        reinterpret_cast<slang::TypeReflection*>(type)->getElementType());
}

SLANG_C_API unsigned slang_reflection_type_get_row_count(SlangReflectionType* type)
{
    return reinterpret_cast<slang::TypeReflection*>(type)->getRowCount();
}

SLANG_C_API unsigned slang_reflection_type_get_column_count(SlangReflectionType* type)
{
    return reinterpret_cast<slang::TypeReflection*>(type)->getColumnCount();
}

SLANG_C_API SlangScalarType slang_reflection_type_get_scalar_type(SlangReflectionType* type)
{
    return static_cast<SlangScalarType>(
        reinterpret_cast<slang::TypeReflection*>(type)->getScalarType());
}

SLANG_C_API SlangResourceShape slang_reflection_type_get_resource_shape(
    SlangReflectionType* type)
{
    return reinterpret_cast<slang::TypeReflection*>(type)->getResourceShape();
}

SLANG_C_API SlangResourceAccess slang_reflection_type_get_resource_access(
    SlangReflectionType* type)
{
    return reinterpret_cast<slang::TypeReflection*>(type)->getResourceAccess();
}

SLANG_C_API SlangReflectionType* slang_reflection_type_get_resource_result_type(
    SlangReflectionType* type)
{
    return reinterpret_cast<SlangReflectionType*>(
        reinterpret_cast<slang::TypeReflection*>(type)->getResourceResultType());
}

SLANG_C_API const char* slang_reflection_type_get_name(SlangReflectionType* type)
{
    return reinterpret_cast<slang::TypeReflection*>(type)->getName();
}

SLANG_C_API SlangReflectionType* slang_reflection_type_layout_get_type(
    SlangReflectionTypeLayout* type)
{
    return reinterpret_cast<SlangReflectionType*>(
        reinterpret_cast<slang::TypeLayoutReflection*>(type)->getType());
}

SLANG_C_API SlangTypeKind slang_reflection_type_layout_get_kind(
    SlangReflectionTypeLayout* type)
{
    return static_cast<SlangTypeKind>(
        reinterpret_cast<slang::TypeLayoutReflection*>(type)->getKind());
}

SLANG_C_API size_t slang_reflection_type_layout_get_size(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category)
{
    return reinterpret_cast<slang::TypeLayoutReflection*>(type)->getSize(category);
}

SLANG_C_API size_t slang_reflection_type_layout_get_stride(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category)
{
    return reinterpret_cast<slang::TypeLayoutReflection*>(type)->getStride(category);
}

SLANG_C_API int32_t slang_reflection_type_layout_get_alignment(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category)
{
    return reinterpret_cast<slang::TypeLayoutReflection*>(type)->getAlignment(category);
}

SLANG_C_API uint32_t slang_reflection_type_layout_get_field_count(
    SlangReflectionTypeLayout* type)
{
    return reinterpret_cast<slang::TypeLayoutReflection*>(type)->getFieldCount();
}

SLANG_C_API SlangReflectionVariableLayout* slang_reflection_type_layout_get_field_by_index(
    SlangReflectionTypeLayout* type,
    unsigned index)
{
    return reinterpret_cast<SlangReflectionVariableLayout*>(
        reinterpret_cast<slang::TypeLayoutReflection*>(type)->getFieldByIndex(index));
}

SLANG_C_API size_t slang_reflection_type_layout_get_element_stride(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category)
{
    return reinterpret_cast<slang::TypeLayoutReflection*>(type)->getElementStride(category);
}

SLANG_C_API SlangReflectionTypeLayout* slang_reflection_type_layout_get_element_type_layout(
    SlangReflectionTypeLayout* type)
{
    return reinterpret_cast<SlangReflectionTypeLayout*>(
        reinterpret_cast<slang::TypeLayoutReflection*>(type)->getElementTypeLayout());
}

SLANG_C_API SlangReflectionVariableLayout* slang_reflection_type_layout_get_element_var_layout(
    SlangReflectionTypeLayout* type)
{
    return reinterpret_cast<SlangReflectionVariableLayout*>(
        reinterpret_cast<slang::TypeLayoutReflection*>(type)->getElementVarLayout());
}

SLANG_C_API SlangReflectionVariableLayout* slang_reflection_type_layout_get_container_var_layout(
    SlangReflectionTypeLayout* type)
{
    return reinterpret_cast<SlangReflectionVariableLayout*>(
        reinterpret_cast<slang::TypeLayoutReflection*>(type)->getContainerVarLayout());
}

SLANG_C_API SlangParameterCategory slang_reflection_type_layout_get_parameter_category(
    SlangReflectionTypeLayout* type)
{
    return static_cast<SlangParameterCategory>(
        reinterpret_cast<slang::TypeLayoutReflection*>(type)->getParameterCategory());
}

SLANG_C_API SlangMatrixLayoutMode slang_reflection_type_layout_get_matrix_layout_mode(
    SlangReflectionTypeLayout* type)
{
    return reinterpret_cast<slang::TypeLayoutReflection*>(type)->getMatrixLayoutMode();
}

SLANG_C_API const char* slang_reflection_variable_get_name(SlangReflectionVariable* variable)
{
    return reinterpret_cast<slang::VariableReflection*>(variable)->getName();
}

SLANG_C_API SlangReflectionType* slang_reflection_variable_get_type(
    SlangReflectionVariable* variable)
{
    return reinterpret_cast<SlangReflectionType*>(
        reinterpret_cast<slang::VariableReflection*>(variable)->getType());
}

SLANG_C_API SlangReflectionVariable* slang_reflection_variable_layout_get_variable(
    SlangReflectionVariableLayout* variable)
{
    return reinterpret_cast<SlangReflectionVariable*>(
        reinterpret_cast<slang::VariableLayoutReflection*>(variable)->getVariable());
}

SLANG_C_API SlangReflectionTypeLayout* slang_reflection_variable_layout_get_type_layout(
    SlangReflectionVariableLayout* variable)
{
    return reinterpret_cast<SlangReflectionTypeLayout*>(
        reinterpret_cast<slang::VariableLayoutReflection*>(variable)->getTypeLayout());
}

SLANG_C_API size_t slang_reflection_variable_layout_get_offset(
    SlangReflectionVariableLayout* variable,
    SlangParameterCategory category)
{
    return reinterpret_cast<slang::VariableLayoutReflection*>(variable)->getOffset(category);
}

SLANG_C_API size_t slang_reflection_variable_layout_get_space(
    SlangReflectionVariableLayout* variable,
    SlangParameterCategory category)
{
    return reinterpret_cast<slang::VariableLayoutReflection*>(variable)->getBindingSpace(category);
}

SLANG_C_API const char* slang_reflection_variable_layout_get_semantic_name(
    SlangReflectionVariableLayout* variable)
{
    return reinterpret_cast<slang::VariableLayoutReflection*>(variable)->getSemanticName();
}

SLANG_C_API size_t slang_reflection_variable_layout_get_semantic_index(
    SlangReflectionVariableLayout* variable)
{
    return reinterpret_cast<slang::VariableLayoutReflection*>(variable)->getSemanticIndex();
}

SLANG_C_API SlangStage slang_reflection_variable_layout_get_stage(
    SlangReflectionVariableLayout* variable)
{
    return reinterpret_cast<slang::VariableLayoutReflection*>(variable)->getStage();
}

SLANG_C_API unsigned slang_reflection_parameter_get_binding_index(
    SlangReflectionParameter* parameter)
{
    return reinterpret_cast<slang::VariableLayoutReflection*>(parameter)->getBindingIndex();
}

SLANG_C_API unsigned slang_reflection_parameter_get_binding_space(
    SlangReflectionParameter* parameter)
{
    return reinterpret_cast<slang::VariableLayoutReflection*>(parameter)->getBindingSpace();
}

SLANG_C_API void slang_blob_destroy(ISlangBlob* blob)
{
    if (!blob)
        return;
    try
    {
        blob->release();
    }
    catch (...)
    {
        // C++ exceptions must not cross the stable C ABI.
    }
}

SLANG_C_API const void* slang_blob_get_buffer_pointer(
    ISlangBlob* blob)
{
    return blob ? blob->getBufferPointer() : nullptr;
}

SLANG_C_API size_t slang_blob_get_buffer_size(ISlangBlob* blob)
{
    return blob ? blob->getBufferSize() : 0;
}

SLANG_C_API uint32_t slang_abi_version(void)
{
    return SLANG_C_API_ABI_VERSION;
}

}
