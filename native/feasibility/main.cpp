#include <slang-com-ptr.h>
#include <slang.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Slang::ComPtr;

struct Target
{
    const char* name;
    const char* profile;
    const char* extension;
    SlangCompileTarget format;
};

constexpr std::array<const char*, 3> kEntryPointNames = {
    "vertex_main",
    "fragment_main",
    "compute_main",
};

#if defined(__ANDROID__)
constexpr std::array<Target, 1> kTargets = {
    Target{"spirv", "spirv_1_3", ".spv", SLANG_SPIRV},
};
#else
constexpr std::array<Target, 3> kTargets = {
    Target{"hlsl", "sm_6_0", ".hlsl", SLANG_HLSL},
    Target{"spirv", "spirv_1_3", ".spv", SLANG_SPIRV},
    Target{"metal", "metallib_2_3", ".metal", SLANG_METAL},
};
#endif

void printDiagnostics(const char* operation, slang::IBlob* diagnostics)
{
    if (!diagnostics || diagnostics->getBufferSize() == 0)
        return;

    std::cerr << operation << ":\n"
              << static_cast<const char*>(diagnostics->getBufferPointer()) << '\n';
}

bool succeeded(const char* operation, SlangResult result, slang::IBlob* diagnostics = nullptr)
{
    printDiagnostics(operation, diagnostics);
    if (SLANG_SUCCEEDED(result))
        return true;

    std::cerr << operation << " failed with Slang result " << result << '\n';
    return false;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

bool writeBlob(const std::filesystem::path& path, slang::IBlob* blob, bool text)
{
    if (!blob)
        return false;

    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        return false;

    auto size = blob->getBufferSize();
    const auto* data = static_cast<const char*>(blob->getBufferPointer());
    if (text && size != 0 && data[size - 1] == '\0')
        --size;
    stream.write(data, static_cast<std::streamsize>(size));
    return stream.good();
}

bool validateReflection(slang::IBlob* reflection)
{
    if (!reflection)
        return false;

    const std::string_view json(
        static_cast<const char*>(reflection->getBufferPointer()),
        reflection->getBufferSize());
    for (const char* entryPointName : kEntryPointNames)
    {
        if (json.find(entryPointName) == std::string_view::npos)
            return false;
    }
    return true;
}

bool validateCode(const Target& target, slang::IBlob* code)
{
    if (!code || code->getBufferSize() == 0)
        return false;
    if (target.format != SLANG_SPIRV)
        return true;
    if (code->getBufferSize() < 5 * sizeof(std::uint32_t))
        return false;

    std::uint32_t header[2] = {};
    std::memcpy(header, code->getBufferPointer(), sizeof(header));
    return header[0] == 0x07230203 && header[1] == 0x00010300;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: slang-slim-feasibility <input.hlsl> <output-directory>\n";
        return 2;
    }

    const std::filesystem::path inputPath = argv[1];
    const std::filesystem::path outputRoot = argv[2];
    const std::string source = readTextFile(inputPath);
    if (source.empty())
    {
        std::cerr << "failed to read non-empty input: " << inputPath << '\n';
        return 2;
    }

    ComPtr<slang::IGlobalSession> globalSession;
    if (!succeeded(
            "create global session",
            slang::createGlobalSession(globalSession.writeRef())))
        return 1;

    std::array<slang::TargetDesc, kTargets.size()> targetDescs = {};
    for (std::size_t index = 0; index < kTargets.size(); ++index)
    {
        targetDescs[index].format = kTargets[index].format;
        targetDescs[index].profile = globalSession->findProfile(kTargets[index].profile);
        if (targetDescs[index].profile == SLANG_PROFILE_UNKNOWN)
        {
            std::cerr << "unknown profile: " << kTargets[index].profile << '\n';
            return 1;
        }
    }

    slang::SessionDesc sessionDesc = {};
    sessionDesc.targets = targetDescs.data();
    sessionDesc.targetCount = static_cast<SlangInt>(targetDescs.size());
    sessionDesc.allowGLSLSyntax = false;

    ComPtr<slang::ISession> session;
    if (!succeeded(
            "create compile session",
            globalSession->createSession(sessionDesc, session.writeRef())))
        return 1;

    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(
        "slang_slim_feasibility",
        inputPath.string().c_str(),
        source.c_str(),
        diagnostics.writeRef());
    printDiagnostics("load HLSL module", diagnostics);
    if (!module)
        return 1;

    std::array<ComPtr<slang::IEntryPoint>, kEntryPointNames.size()> entryPoints;
    std::vector<slang::IComponentType*> components;
    components.reserve(1 + entryPoints.size());
    components.push_back(module);
    for (std::size_t index = 0; index < entryPoints.size(); ++index)
    {
        if (!succeeded(
                "find entry point",
                module->findEntryPointByName(
                    kEntryPointNames[index], entryPoints[index].writeRef())))
            return 1;
        components.push_back(entryPoints[index]);
    }

    ComPtr<slang::IComponentType> program;
    diagnostics.setNull();
    if (!succeeded(
            "compose program",
            session->createCompositeComponentType(
                components.data(),
                static_cast<SlangInt>(components.size()),
                program.writeRef(),
                diagnostics.writeRef()),
            diagnostics))
        return 1;

    ComPtr<slang::IComponentType> linkedProgram;
    diagnostics.setNull();
    if (!succeeded(
            "link program",
            program->link(linkedProgram.writeRef(), diagnostics.writeRef()),
            diagnostics))
        return 1;

    for (std::size_t targetIndex = 0; targetIndex < kTargets.size(); ++targetIndex)
    {
        const auto& target = kTargets[targetIndex];
        const auto targetDirectory = outputRoot / target.name;

        diagnostics.setNull();
        slang::ProgramLayout* layout = linkedProgram->getLayout(
            static_cast<SlangInt>(targetIndex), diagnostics.writeRef());
        printDiagnostics("get target layout", diagnostics);
        if (!layout)
            return 1;

        ComPtr<slang::IBlob> reflection;
        if (!succeeded("serialize reflection", layout->toJson(reflection.writeRef())) ||
            !validateReflection(reflection) ||
            !writeBlob(targetDirectory / "reflection.json", reflection, true))
        {
            std::cerr << "failed to write reflection for " << target.name << '\n';
            return 1;
        }

        for (std::size_t entryIndex = 0; entryIndex < entryPoints.size(); ++entryIndex)
        {
            ComPtr<slang::IBlob> code;
            diagnostics.setNull();
            if (!succeeded(
                    "generate entry-point code",
                    linkedProgram->getEntryPointCode(
                        static_cast<SlangInt>(entryIndex),
                        static_cast<SlangInt>(targetIndex),
                        code.writeRef(),
                        diagnostics.writeRef()),
                    diagnostics))
                return 1;

            const auto path = targetDirectory /
                (std::string(kEntryPointNames[entryIndex]) + target.extension);
            if (!validateCode(target, code) ||
                !writeBlob(path, code, target.format != SLANG_SPIRV))
            {
                std::cerr << "failed to write output: " << path << '\n';
                return 1;
            }
        }
    }

    std::cout << "generated " << (kTargets.size() * kEntryPointNames.size())
              << " entry-point artifacts and " << kTargets.size()
              << " reflection documents\n";
    return 0;
}
