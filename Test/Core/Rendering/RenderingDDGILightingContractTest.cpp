#include "Rendering/ViewRenderContext.h"

#include "Container/Containers.h"
#include "RHI/RHITypes.h"

#define private public
#include "Rendering/DDGIProbePass.h"
#undef private

#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

using TestString = NorvesLib::Core::Container::TString<char>;

namespace
{
#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIRはRenderingDDGILightingContractTestに必要です。
#endif

    TestString ReadSource(const char* relativePath)
    {
        std::ifstream file(std::filesystem::path(NORVES_SOURCE_DIR) / relativePath,
                           std::ios::binary);
        assert(file.is_open());
        TestString result;
        char buffer[4096];
        while (file)
        {
            file.read(buffer, sizeof(buffer));
            const std::streamsize count = file.gcount();
            if (count > 0)
            {
                result.append(buffer, static_cast<TestString::size_type>(count));
            }
        }
        return result;
    }

    TestString RemoveWhitespace(const TestString& source)
    {
        TestString normalized;
        normalized.reserve(source.size());
        for (const char character : source)
        {
            if (!std::isspace(static_cast<unsigned char>(character)))
            {
                normalized.push_back(character);
            }
        }
        return normalized;
    }

    void AssertContains(const TestString& source, const char* expected)
    {
        const TestString normalizedSource = RemoveWhitespace(source);
        const TestString normalizedExpected = RemoveWhitespace(TestString(expected));
        const bool bContainsExpected =
            normalizedSource.find(normalizedExpected) != TestString::npos;
        if (!bContainsExpected)
        {
            std::cerr << "契約文字列がありません: " << expected << "\n";
        }
        assert(bContainsExpected);
    }

    TestString ExtractBlock(const TestString& source, const char* signature)
    {
        const std::size_t signaturePosition = source.find(signature);
        assert(signaturePosition != TestString::npos);
        const std::size_t openingPosition = source.find('{', signaturePosition);
        assert(openingPosition != TestString::npos);

        std::size_t depth = 0u;
        for (std::size_t position = openingPosition; position < source.size(); ++position)
        {
            if (source[position] == '{')
            {
                ++depth;
            }
            else if (source[position] == '}')
            {
                assert(depth > 0u);
                --depth;
                if (depth == 0u)
                {
                    return source.substr(signaturePosition,
                                         position - signaturePosition + 1u);
                }
            }
        }

        assert(false);
        return {};
    }

    void TestDDGIAtlasPublicationReset()
    {
        PhysicalLightingResources resources;
        resources.Begin(17u, 3u, 5u);
        assert(resources.bActive);
        assert(!resources.bDDGIAtlasPublished);
        assert(resources.DDGIProbeCount == 0u);
        assert(!resources.DDGIIrradianceAtlas);
        assert(!resources.DDGIDistanceAtlas);

        resources.PublishDDGIAtlas({}, {}, 4u, true);
        assert(!resources.bDDGIAtlasPublished);
        assert(resources.DDGIProbeCount == 0u);
        assert(!resources.DDGIIrradianceAtlas);
        assert(!resources.DDGIDistanceAtlas);

        resources.DDGIProbeCount = 4u;
        resources.bDDGIAtlasPublished = true;
        resources.Begin(18u, 3u, 5u);
        assert(!resources.bDDGIAtlasPublished);
        assert(resources.DDGIProbeCount == 0u);
        assert(!resources.DDGIIrradianceAtlas);
        assert(!resources.DDGIDistanceAtlas);
    }

    void TestStaleAtlasInvalidation()
    {
        DDGIProbePass probePass;
        DDGIProbePass::FrameResources previousFrame;
        previousFrame.FrameIndex = 1u;
        previousFrame.ViewId = 3u;
        previousFrame.ViewportId = 5u;
        previousFrame.FrameNumber = 16u;
        previousFrame.AtlasProbeCount = 4u;
        previousFrame.bAtlasValid = true;
        probePass.m_FrameResources.push_back(previousFrame);

        ViewRenderContext failedContext;
        failedContext.FrameIndex = 1u;
        failedContext.FrameNumber = 18u;
        failedContext.PhysicalLighting.ViewId = 3u;
        failedContext.PhysicalLighting.ViewportId = 5u;
        assert(!probePass.Execute(failedContext));
        assert(!probePass.GetIrradianceAtlas(1u, 3u, 5u));
        assert(!probePass.GetDistanceAtlas(1u, 3u, 5u));
        assert(probePass.GetAtlasProbeCount(1u, 3u, 5u) == 0u);

        const TestString ddgiPass =
            ReadSource("Library/Core/Private/Rendering/DDGIProbePass.cpp");
        const TestString execute = ExtractBlock(ddgiPass, "bool DDGIProbePass::Execute");
        const std::size_t invalidationPosition = execute.find("resources.bAtlasValid = false;");
        const std::size_t earlyReturnGuard = execute.find("if (context.CommandList == nullptr");
        assert(invalidationPosition != TestString::npos);
        assert(earlyReturnGuard != TestString::npos);
        assert(invalidationPosition < earlyReturnGuard);
        AssertContains(execute, "resources.AtlasProbeCount = 0u;");

        const TestString getProbeCount =
            ExtractBlock(ddgiPass, "uint32_t DDGIProbePass::GetAtlasProbeCount");
        AssertContains(getProbeCount, "resources.bAtlasValid");
    }

    void TestLightingFallbackAndPublication()
    {
        const TestString lightingPass =
            ReadSource("Library/Core/Private/Rendering/LightingPass.cpp");
        const TestString execute =
            ExtractBlock(lightingPass, "void LightingPass::ExecuteWithInputs");
        const std::size_t updatePosition = execute.find("m_DDGIProbePass.Execute(context)");
        const std::size_t publicationPosition =
            execute.find("IsCompleteDDGILightingPublication(", updatePosition);
        const std::size_t parameterUpdatePosition =
            execute.find("m_LightDataBuffer->Update(&ddgiParameters", publicationPosition);
        assert(updatePosition != TestString::npos);
        assert(publicationPosition != TestString::npos);
        assert(parameterUpdatePosition != TestString::npos);
        assert(updatePosition < publicationPosition);
        assert(publicationPosition < parameterUpdatePosition);
        AssertContains(execute, "context.PhysicalLighting.PublishDDGIAtlas");
        AssertContains(execute, "ddgiParameters.info[0] = 1u");
        AssertContains(execute, "m_DefaultDDGIIrradianceAtlas");
        AssertContains(execute, "m_DefaultDDGIDistanceAtlas");
        AssertContains(execute, "BindTexture(17,");
        AssertContains(execute, "BindSampler(17,");
        AssertContains(execute, "BindTexture(18,");
        AssertContains(execute, "BindSampler(18,");
        AssertContains(execute, "offsetof(GPULightingParams, ddgi)");

        const TestString completePublication =
            ExtractBlock(lightingPass, "static bool IsCompleteDDGILightingPublication");
        AssertContains(completePublication, "IsDDGIVolumeValid(volume)");
        AssertContains(completePublication, "SupportsDDGILighting(context)");
        AssertContains(completePublication, "atlasProbeCount == expectedProbeCount");
        AssertContains(completePublication, "lighting.FrameNumber == context.FrameNumber");

        const TestString atlasCheck =
            ExtractBlock(lightingPass, "static bool AreDDGIAtlasResourcesComplete");
        AssertContains(atlasCheck, "GetFormat() == RHI::Format::R16G16B16A16_FLOAT");
        AssertContains(atlasCheck, "GetFormat() == RHI::Format::R16G16_FLOAT");
        AssertContains(atlasCheck, "RHI::ResourceUsage::ShaderRead");

        const TestString supportCheck =
            ExtractBlock(lightingPass, "static bool SupportsDDGILighting");
        AssertContains(supportCheck, "bAccelerationStructure");
        AssertContains(supportCheck, "bRayQuery");
        AssertContains(supportCheck, "bBufferDeviceAddress");
        AssertContains(supportCheck, "bShaderInt64");
    }

    void TestVisibilityWeightedVolumeSampling()
    {
        const TestString shader = ReadSource("Assets/Shaders/lighting.frag");
        AssertContains(shader,
                       "layout(set = 0, binding = 17) uniform sampler2DArray ddgiIrradianceAtlas");
        AssertContains(shader,
                       "layout(set = 0, binding = 18) uniform sampler2DArray ddgiDistanceAtlas");

        const TestString sample =
            ExtractBlock(shader, "bool TrySampleDDGIIrradiance");
        AssertContains(sample, "params.ddgiInfo.x == 0u");
        AssertContains(sample, "any(lessThan(gridPosition, vec3(0.0)))");
        AssertContains(sample, "any(greaterThan(gridPosition, gridMaximum))");
        AssertContains(sample, "ivec3 baseProbe = ivec3(floor(gridPosition));");
        AssertContains(sample, "corner < 8u");
        AssertContains(sample, "mix(vec3(1.0) - alpha, alpha, vec3(offset))");
        AssertContains(sample, "SampleDDGIVisibility");
        assert(sample.find("vec3 gridPosition = clamp") == TestString::npos);

        const TestString visibility =
            ExtractBlock(shader, "float SampleDDGIVisibility");
        AssertContains(visibility, "ddgiDistanceAtlas");
        AssertContains(visibility, "moments.y - meanDistance * meanDistance");
        AssertContains(visibility, "return max(0.05, chebyshev);");

        const TestString iblEndpoint =
            ExtractBlock(shader, "vec3 EvaluateIblEndpoint");
        AssertContains(iblEndpoint, "vec3 irradiance = bUseDDGI");
        AssertContains(iblEndpoint, "? ddgiIrradiance");
        AssertContains(iblEndpoint,
                       "textureLod(diffuseIrradiance, EquirectangularUV(N), 0.0).rgb");
        AssertContains(iblEndpoint,
                       "return diffuseIBL * ao + specularIBL * specularAO * iblIntensity;");

        const TestString mainFunction = ExtractBlock(shader, "void main()");
        const TestString iblBranchSignature = "if (params.bIBLEnabled != 0u)";
        const std::size_t firstIblBranch = mainFunction.find(iblBranchSignature);
        assert(firstIblBranch != TestString::npos);
        const std::size_t lightingIblBranch =
            mainFunction.find(iblBranchSignature,
                              firstIblBranch + iblBranchSignature.size());
        assert(lightingIblBranch != TestString::npos);
        assert(mainFunction.find(iblBranchSignature,
                                 lightingIblBranch + iblBranchSignature.size()) ==
               TestString::npos);
        const TestString iblBranch = ExtractBlock(mainFunction.substr(lightingIblBranch),
                                                  iblBranchSignature.c_str());
        AssertContains(iblBranch, "bDDGIAvailable");
        AssertContains(iblBranch, "EvaluateIblEndpoint");
        AssertContains(iblBranch, "ambient = EvaluateIblEndpoint");
        const std::size_t endpointPosition =
            iblBranch.find("EvaluateIblEndpoint(");
        assert(endpointPosition != TestString::npos);
        assert(iblBranch.find("EvaluateIblEndpoint(", endpointPosition + 1u) ==
               TestString::npos);
        assert(iblBranch.find("diffuseIrradiance") == TestString::npos);
        assert(iblBranch.find("ambient += EvaluateDiffuseEndpoint") == TestString::npos);
        AssertContains(mainFunction, "TrySampleDDGIIrradiance(worldPos, N, ddgiIrradiance)");
        AssertContains(mainFunction, "if (bDDGIAvailable)");
        AssertContains(mainFunction, "ambient += EvaluateDiffuseEndpoint(ddgiIrradiance");
        AssertContains(mainFunction, "params.debugViewMode >= 246u");
    }
}

int main()
{
    TestDDGIAtlasPublicationReset();
    TestStaleAtlasInvalidation();
    TestLightingFallbackAndPublication();
    TestVisibilityWeightedVolumeSampling();
    std::cout << "DDGI照明契約テスト: 合格\n";
    return 0;
}
