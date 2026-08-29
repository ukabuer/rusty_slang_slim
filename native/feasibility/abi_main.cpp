#include "slang_slim.h"

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

constexpr std::array<slang_slim_entry_point_desc, 3> kEntries = {{
    {sizeof(slang_slim_entry_point_desc), "vertex_main", SLANG_SLIM_STAGE_VERTEX},
    {sizeof(slang_slim_entry_point_desc), "fragment_main", SLANG_SLIM_STAGE_FRAGMENT},
    {sizeof(slang_slim_entry_point_desc), "compute_main", SLANG_SLIM_STAGE_COMPUTE},
}};

#if defined(__ANDROID__)
constexpr std::array<slang_slim_target_desc, 1> kTargets = {{
    {sizeof(slang_slim_target_desc), SLANG_SLIM_TARGET_SPIRV},
}};
#else
constexpr std::array<slang_slim_target_desc, 3> kTargets = {{
    {sizeof(slang_slim_target_desc), SLANG_SLIM_TARGET_HLSL},
    {sizeof(slang_slim_target_desc), SLANG_SLIM_TARGET_SPIRV},
    {sizeof(slang_slim_target_desc), SLANG_SLIM_TARGET_METAL},
}};
#endif

void printBlob(const char* label, slang_slim_blob blob)
{
    if (blob.data && blob.size != 0)
        std::cerr << label << ":\n"
                  << std::string_view(reinterpret_cast<const char*>(blob.data), blob.size)
                  << '\n';
}

bool contains(slang_slim_blob blob, std::string_view needle)
{
    if (!blob.data || blob.size < needle.size())
        return false;
    const std::string_view text(reinterpret_cast<const char*>(blob.data), blob.size);
    return text.find(needle) != std::string_view::npos;
}
} // namespace

int main()
{
    slang_slim_compiler* compiler = nullptr;
    auto status = slang_slim_compiler_create(&compiler);
    if (status != SLANG_SLIM_STATUS_OK || !compiler)
    {
        std::cerr << "failed to create compiler: " << status << '\n';
        return 1;
    }

    if (slang_slim_abi_version() != SLANG_SLIM_ABI_VERSION)
    {
        std::cerr << "unexpected ABI version\n";
        slang_slim_compiler_destroy(compiler);
        return 1;
    }

    slang_slim_virtual_file virtualFile = {
        sizeof(slang_slim_virtual_file),
        "abi/shared.hlsl",
        reinterpret_cast<const uint8_t*>(kSharedSource),
        sizeof(kSharedSource) - 1,
    };
    slang_slim_compile_desc desc = {};
    desc.struct_size = sizeof(desc);
    desc.module_name = "slang_slim_abi_test";
    desc.source_path = "abi/main.hlsl";
    desc.source = reinterpret_cast<const uint8_t*>(kSource);
    desc.source_size = sizeof(kSource) - 1;
    desc.entry_points = kEntries.data();
    desc.entry_point_count = kEntries.size();
    desc.targets = kTargets.data();
    desc.target_count = kTargets.size();
    desc.virtual_files = &virtualFile;
    desc.virtual_file_count = 1;

    slang_slim_compilation* compilation = nullptr;
    status = slang_slim_compile(compiler, &desc, &compilation);
    slang_slim_blob diagnostics = {};
    slang_slim_compilation_get_diagnostics(compilation, &diagnostics);
    if (status != SLANG_SLIM_STATUS_OK)
    {
        printBlob("compile diagnostics", diagnostics);
        slang_slim_compilation_destroy(compilation);
        slang_slim_compiler_destroy(compiler);
        return 1;
    }

    if (slang_slim_compilation_target_count(compilation) != kTargets.size() ||
        slang_slim_compilation_entry_point_count(compilation) != kEntries.size())
    {
        std::cerr << "unexpected result dimensions\n";
        slang_slim_compilation_destroy(compilation);
        slang_slim_compiler_destroy(compiler);
        return 1;
    }

    for (std::size_t targetIndex = 0; targetIndex < kTargets.size(); ++targetIndex)
    {
        slang_slim_blob reflection = {};
        if (slang_slim_compilation_get_reflection_json(compilation, targetIndex, &reflection) !=
                SLANG_SLIM_STATUS_OK ||
            !contains(reflection, "vertex_main") || !contains(reflection, "fragment_main") ||
            !contains(reflection, "compute_main"))
        {
            std::cerr << "reflection validation failed for target " << targetIndex << '\n';
            printBlob("reflection", reflection);
            slang_slim_compilation_destroy(compilation);
            slang_slim_compiler_destroy(compiler);
            return 1;
        }

        for (std::size_t entryIndex = 0; entryIndex < kEntries.size(); ++entryIndex)
        {
            slang_slim_blob code = {};
            if (slang_slim_compilation_get_code(compilation, targetIndex, entryIndex, &code) !=
                    SLANG_SLIM_STATUS_OK ||
                !code.data || code.size == 0)
            {
                std::cerr << "code validation failed for target " << targetIndex << ", entry "
                          << entryIndex << '\n';
                slang_slim_compilation_destroy(compilation);
                slang_slim_compiler_destroy(compiler);
                return 1;
            }

            if (kTargets[targetIndex].target == SLANG_SLIM_TARGET_SPIRV)
            {
                if (code.size < 2 * sizeof(uint32_t))
                {
                    std::cerr << "SPIR-V output is too small\n";
                    slang_slim_compilation_destroy(compilation);
                    slang_slim_compiler_destroy(compiler);
                    return 1;
                }
                uint32_t header[2] = {};
                std::memcpy(header, code.data, sizeof(header));
                if (header[0] != 0x07230203 || header[1] != 0x00010300)
                {
                    std::cerr << "unexpected SPIR-V header\n";
                    slang_slim_compilation_destroy(compilation);
                    slang_slim_compiler_destroy(compiler);
                    return 1;
                }
            }
        }
    }

    std::cout << "ABI compile passed for " << kEntries.size() << " entry points and "
              << kTargets.size() << " target(s)\n";
    slang_slim_compilation_destroy(compilation);
    slang_slim_compiler_destroy(compiler);
    return 0;
}
