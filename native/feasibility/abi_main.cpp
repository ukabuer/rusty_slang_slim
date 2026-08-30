#include "slang_c_api.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

namespace
{
constexpr char kSharedSource[] =
    "float4 shared_tint() { return float4(1.0, 0.75, 0.5, 1.0); }\n";

constexpr char kSource[] = R"(
#include "shared.hlsl"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(vertexId == 0 ? -1.0 : 1.0, 0.0, 0.0, 1.0);
    output.uv = float2(0.0, 0.0);
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0
{
    return shared_tint() + float4(input.uv, 0.0, 0.0);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void compute_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
}
)";

struct EntryPoint
{
    const char* name;
    SlangStage stage;
};

constexpr std::array<EntryPoint, 3> kEntries = {{
    {"vertex_main", SLANG_STAGE_VERTEX},
    {"fragment_main", SLANG_STAGE_FRAGMENT},
    {"compute_main", SLANG_STAGE_COMPUTE},
}};

#if defined(__ANDROID__)
constexpr std::array<SlangCompileTarget, 1> kTargetFormats = {{SLANG_SPIRV}};
#else
constexpr std::array<SlangCompileTarget, 3> kTargetFormats = {{
    SLANG_HLSL,
    SLANG_SPIRV,
    SLANG_METAL,
}};
#endif

void printBlob(const char* label, ISlangBlob* blob)
{
    if (!blob)
        return;
    const auto* data = static_cast<const char*>(slang_blob_get_buffer_pointer(blob));
    const auto size = slang_blob_get_buffer_size(blob);
    if (data && size != 0)
        std::cerr << label << ":\n" << std::string_view(data, size) << '\n';
}

bool contains(ISlangBlob* blob, std::string_view needle)
{
    if (!blob)
        return false;
    const auto* data = static_cast<const char*>(slang_blob_get_buffer_pointer(blob));
    const auto size = slang_blob_get_buffer_size(blob);
    if (!data || size < needle.size())
        return false;
    return std::string_view(data, size).find(needle) != std::string_view::npos;
}

extern "C" SlangResult loadVirtualFile(void*, const char* path, ISlangBlob** outBlob)
{
    if (!path || !outBlob)
        return SLANG_E_INVALID_ARG;
    if (std::string_view(path) != "abi/shared.hlsl")
        return SLANG_E_NOT_FOUND;
    return slang_create_blob(kSharedSource, sizeof(kSharedSource) - 1, outBlob);
}

void releaseBlob(ISlangBlob*& blob)
{
    if (blob)
        slang_blob_destroy(blob);
    blob = nullptr;
}
} // namespace

int main()
{
    if (slang_abi_version() != SLANG_C_API_ABI_VERSION)
    {
        std::cerr << "unexpected ABI version\n";
        return 1;
    }

    SlangGlobalSessionDesc globalDesc = {};
    globalDesc.structureSize = sizeof(globalDesc);
    globalDesc.apiVersion = SLANG_API_VERSION;
    globalDesc.minLanguageVersion = SLANG_LANGUAGE_VERSION_2025;

    IGlobalSession* global = nullptr;
    SlangResult status = slang_create_global_session(&globalDesc, &global);
    if (status != SLANG_OK || !global)
    {
        std::cerr << "failed to create global session: " << status << '\n';
        return 1;
    }

    std::array<SlangTargetDesc, kTargetFormats.size()> targets = {};
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        targets[index].structureSize = sizeof(SlangTargetDesc);
        targets[index].format = kTargetFormats[index];
        const char* profile = kTargetFormats[index] == SLANG_HLSL
            ? "sm_6_0"
            : (kTargetFormats[index] == SLANG_SPIRV ? "spirv_1_3" : "metallib_2_3");
        targets[index].profile = slang_global_session_find_profile(global, profile);
        if (targets[index].profile == SLANG_PROFILE_UNKNOWN)
        {
            std::cerr << "target profile unavailable: " << profile << '\n';
            slang_global_session_destroy(global);
            return 1;
        }
    }

    SlangFileSystemDesc fileSystemDesc = {};
    fileSystemDesc.structureSize = sizeof(fileSystemDesc);
    fileSystemDesc.loadFile = loadVirtualFile;
    ISlangFileSystem* fileSystem = nullptr;
    status = slang_file_system_create(&fileSystemDesc, &fileSystem);
    if (status != SLANG_OK || !fileSystem)
    {
        std::cerr << "failed to create file system: " << status << '\n';
        slang_global_session_destroy(global);
        return 1;
    }

    SlangSessionDesc sessionDesc = {};
    sessionDesc.structureSize = sizeof(sessionDesc);
    sessionDesc.targets = targets.data();
    sessionDesc.targetCount = static_cast<SlangInt>(targets.size());
    sessionDesc.fileSystem = fileSystem;

    ISession* session = nullptr;
    status = slang_global_session_create_session(global, &sessionDesc, &session);
    if (status != SLANG_OK || !session)
    {
        std::cerr << "failed to create session: " << status << '\n';
        slang_file_system_destroy(fileSystem);
        slang_global_session_destroy(global);
        return 1;
    }

    ISlangBlob* source = nullptr;
    status = slang_create_blob(kSource, sizeof(kSource) - 1, &source);
    IModule* module = nullptr;
    ISlangBlob* diagnostics = nullptr;
    if (status == SLANG_OK)
        status = slang_session_load_module_from_source(
            session,
            "slang_c_api_abi_test",
            "abi/main.hlsl",
            source,
            &diagnostics,
            &module);
    releaseBlob(source);
    if (status != SLANG_OK || !module)
    {
        printBlob("module diagnostics", diagnostics);
        releaseBlob(diagnostics);
        slang_session_destroy(session);
        slang_file_system_destroy(fileSystem);
        slang_global_session_destroy(global);
        return 1;
    }
    releaseBlob(diagnostics);

    std::array<IEntryPoint*, kEntries.size()> entryPoints = {};
    for (std::size_t index = 0; index < entryPoints.size(); ++index)
    {
        status = slang_module_find_and_check_entry_point(
            module,
            kEntries[index].name,
            kEntries[index].stage,
            &entryPoints[index],
            &diagnostics);
        if (status != SLANG_OK || !entryPoints[index])
        {
            printBlob("entry point diagnostics", diagnostics);
            releaseBlob(diagnostics);
            for (auto*& entryPoint : entryPoints)
                if (entryPoint)
                    slang_component_type_destroy(entryPoint);
            slang_component_type_destroy(module);
            slang_session_destroy(session);
            slang_file_system_destroy(fileSystem);
            slang_global_session_destroy(global);
            return 1;
        }
        releaseBlob(diagnostics);
    }

    std::array<IComponentType*, kEntries.size() + 1> components = {};
    components[0] = module;
    for (std::size_t index = 0; index < entryPoints.size(); ++index)
        components[index + 1] = entryPoints[index];

    IComponentType* program = nullptr;
    status = slang_session_create_composite_component_type(
        session,
        components.data(),
        static_cast<SlangInt>(components.size()),
        &program,
        &diagnostics);
    releaseBlob(diagnostics);
    if (status != SLANG_OK || !program)
    {
        slang_component_type_destroy(module);
        for (auto*& entryPoint : entryPoints)
            slang_component_type_destroy(entryPoint);
        slang_session_destroy(session);
        slang_file_system_destroy(fileSystem);
        slang_global_session_destroy(global);
        return 1;
    }

    IComponentType* linked = nullptr;
    status = slang_component_type_link(program, &linked, &diagnostics);
    if (status != SLANG_OK || !linked)
    {
        printBlob("link diagnostics", diagnostics);
        releaseBlob(diagnostics);
        slang_component_type_destroy(program);
        slang_component_type_destroy(module);
        for (auto*& entryPoint : entryPoints)
            slang_component_type_destroy(entryPoint);
        slang_session_destroy(session);
        slang_file_system_destroy(fileSystem);
        slang_global_session_destroy(global);
        return 1;
    }
    releaseBlob(diagnostics);

    for (std::size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex)
    {
        ProgramLayout* layout = nullptr;
        status = slang_component_type_get_layout(linked, targetIndex, &layout, &diagnostics);
        if (status != SLANG_OK || !layout)
        {
            printBlob("layout diagnostics", diagnostics);
            releaseBlob(diagnostics);
            slang_component_type_destroy(linked);
            slang_component_type_destroy(program);
            slang_component_type_destroy(module);
            for (auto*& entryPoint : entryPoints)
                slang_component_type_destroy(entryPoint);
            slang_session_destroy(session);
            slang_file_system_destroy(fileSystem);
            slang_global_session_destroy(global);
            return 1;
        }
        releaseBlob(diagnostics);

        ISlangBlob* reflection = nullptr;
        status = slang_program_layout_to_json(layout, &reflection);
        if (status != SLANG_OK || !contains(reflection, "vertex_main") ||
            !contains(reflection, "fragment_main") || !contains(reflection, "compute_main"))
        {
            printBlob("reflection", reflection);
            releaseBlob(reflection);
            slang_program_layout_destroy(layout);
            slang_component_type_destroy(linked);
            slang_component_type_destroy(program);
            slang_component_type_destroy(module);
            for (auto*& entryPoint : entryPoints)
                slang_component_type_destroy(entryPoint);
            slang_session_destroy(session);
            slang_file_system_destroy(fileSystem);
            slang_global_session_destroy(global);
            return 1;
        }
        releaseBlob(reflection);

        for (std::size_t entryIndex = 0; entryIndex < entryPoints.size(); ++entryIndex)
        {
            ISlangBlob* code = nullptr;
            status = slang_component_type_get_entry_point_code(
                linked,
                static_cast<SlangInt>(entryIndex),
                static_cast<SlangInt>(targetIndex),
                &code,
                &diagnostics);
            if (status != SLANG_OK || !code ||
                slang_blob_get_buffer_size(code) == 0)
            {
                printBlob("code diagnostics", diagnostics);
                releaseBlob(diagnostics);
                releaseBlob(code);
                slang_program_layout_destroy(layout);
                slang_component_type_destroy(linked);
                slang_component_type_destroy(program);
                slang_component_type_destroy(module);
                for (auto*& entryPoint : entryPoints)
                    slang_component_type_destroy(entryPoint);
                slang_session_destroy(session);
                slang_file_system_destroy(fileSystem);
                slang_global_session_destroy(global);
                return 1;
            }
            releaseBlob(diagnostics);

            if (kTargetFormats[targetIndex] == SLANG_SPIRV)
            {
                if (slang_blob_get_buffer_size(code) < 2 * sizeof(uint32_t))
                {
                    releaseBlob(code);
                    slang_program_layout_destroy(layout);
                    slang_component_type_destroy(linked);
                    slang_component_type_destroy(program);
                    slang_component_type_destroy(module);
                    for (auto*& entryPoint : entryPoints)
                        slang_component_type_destroy(entryPoint);
                    slang_session_destroy(session);
                    slang_file_system_destroy(fileSystem);
                    slang_global_session_destroy(global);
                    return 1;
                }
                uint32_t header[2] = {};
                std::memcpy(
                    header,
                    slang_blob_get_buffer_pointer(code),
                    sizeof(header));
                if (header[0] != 0x07230203 || header[1] != 0x00010300)
                {
                    std::cerr << "unexpected SPIR-V header\n";
                    releaseBlob(code);
                    slang_program_layout_destroy(layout);
                    slang_component_type_destroy(linked);
                    slang_component_type_destroy(program);
                    slang_component_type_destroy(module);
                    for (auto*& entryPoint : entryPoints)
                        slang_component_type_destroy(entryPoint);
                    slang_session_destroy(session);
                    slang_file_system_destroy(fileSystem);
                    slang_global_session_destroy(global);
                    return 1;
                }
            }
            releaseBlob(code);
        }

        slang_program_layout_destroy(layout);
    }

    std::cout << "ABI compile passed for " << kEntries.size() << " entry points and "
              << kTargetFormats.size() << " target(s)\n";
    slang_component_type_destroy(linked);
    slang_component_type_destroy(program);
    slang_component_type_destroy(module);
    for (auto*& entryPoint : entryPoints)
        slang_component_type_destroy(entryPoint);
    slang_session_destroy(session);
    slang_file_system_destroy(fileSystem);
    slang_global_session_destroy(global);
    return 0;
}
