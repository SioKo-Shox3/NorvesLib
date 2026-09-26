#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"

#if __has_include("Rendering/LightingPassLightPacking.h")
#include "Rendering/LightingPassLightPacking.h"
#else
#error Rendering/LightingPassLightPacking.h is required for LightingLightBufferTest.
#endif

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Rendering;
namespace CoreContainer = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    class LifecycleBuffer final : public RHI::IBuffer
    {
    public:
        uint64_t GetSize() const override { return 256; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)offset;
            (void)size;
            return nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            (void)data;
            (void)size;
            (void)offset;
        }
        RHI::ResourceUsage GetUsage() const override { return RHI::ResourceUsage::StorageBuffer; }
    };

    class LifecycleTexture final : public RHI::ITexture
    {
    public:
        explicit LifecycleTexture(uint32_t arraySize = 1)
            : m_ArraySize(arraySize)
        {
        }

        uint32_t GetWidth() const override { return 1; }
        uint32_t GetHeight() const override { return 1; }
        uint32_t GetDepth() const override { return 1; }
        uint32_t GetMipLevels() const override { return 1; }
        uint32_t GetArraySize() const override { return m_ArraySize; }
        RHI::Format GetFormat() const override { return RHI::Format::R8G8B8A8_UNORM; }
        RHI::ResourceUsage GetUsage() const override { return RHI::ResourceUsage::ShaderRead; }
        bool IsCubemap() const override { return false; }
        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

    private:
        uint32_t m_ArraySize = 1;
    };

    class LifecycleSampler final : public RHI::ISampler
    {
    public:
        RHI::FilterMode GetFilterMin() const override { return RHI::FilterMode::Linear; }
        RHI::FilterMode GetFilterMag() const override { return RHI::FilterMode::Linear; }
        RHI::FilterMode GetFilterMip() const override { return RHI::FilterMode::Linear; }
        RHI::TextureAddressMode GetAddressModeU() const override { return RHI::TextureAddressMode::Clamp; }
        RHI::TextureAddressMode GetAddressModeV() const override { return RHI::TextureAddressMode::Clamp; }
        RHI::TextureAddressMode GetAddressModeW() const override { return RHI::TextureAddressMode::Clamp; }
        uint32_t GetMaxAnisotropy() const override { return 1; }
        RHI::CompareFunc GetCompareFunc() const override { return RHI::CompareFunc::Never; }
    };

    void ConfigureAssertOutput()
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }

    bool NearlyEqual(float actual, float expected, float epsilon = 1.0e-5f)
    {
        return std::fabs(actual - expected) <= epsilon;
    }

    float ReadPackedLightFloat(const GPULightData& light, std::size_t byteOffset)
    {
        float value = 0.0f;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&light);
        std::memcpy(&value, bytes + byteOffset, sizeof(value));
        return value;
    }

    LightProxy MakePointLight(uint32_t index)
    {
        LightProxy proxy;
        proxy.LightId = index + 1;
        proxy.Type = LightType::Point;
        proxy.PositionX = static_cast<float>(index) + 0.25f;
        proxy.PositionY = static_cast<float>(index) + 0.5f;
        proxy.PositionZ = static_cast<float>(index) + 0.75f;
        proxy.DirectionX = -0.25f;
        proxy.DirectionY = -0.5f;
        proxy.DirectionZ = -0.75f;
        proxy.InnerConeAngle = 0.91f;
        proxy.OuterConeAngle = 0.73f;
        proxy.ColorR = 1.0f;
        proxy.ColorG = 1.0f;
        proxy.ColorB = 1.0f;
        proxy.Intensity = 1.0f + static_cast<float>(index);
        proxy.Range = 10.0f + static_cast<float>(index);
        proxy.bVisible = true;
        return proxy;
    }

    void AssertGPULightDataLayout()
    {
        assert(sizeof(GPULightData) == 64);
        assert(offsetof(GPULightData, position) == 0);
        assert(offsetof(GPULightData, direction) == 16);
        assert(offsetof(GPULightData, direction) + sizeof(float[4]) == 32);
        assert(offsetof(GPULightData, attenuation) == 48);
    }

    void TestPackTwentyValidPointLights()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        for (uint32_t i = 0; i < 20; ++i)
        {
            proxies.push_back(MakePointLight(i));
        }

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 20);
        assert(packedLights.size() == 20);

        const GPULightData& first = packedLights[0];
        assert(first.position[0] == 0.25f);
        assert(first.position[1] == 0.5f);
        assert(first.position[2] == 0.75f);
        assert(first.position[3] == static_cast<float>(static_cast<int>(LightType::Point)));
        assert(first.direction[3] == 0.91f);
        assert(ReadPackedLightFloat(first, 32) == 1.0f);
        assert(ReadPackedLightFloat(first, 36) == 1.0f);
        assert(ReadPackedLightFloat(first, 40) == 1.0f);
        assert(ReadPackedLightFloat(first, 44) == 1.0f);
        assert(ReadPackedLightFloat(first, 48) == 10.0f);
        assert(ReadPackedLightFloat(first, 52) == 0.73f);

        const GPULightData& last = packedLights[19];
        assert(last.position[0] == 19.25f);
        assert(last.position[1] == 19.5f);
        assert(last.position[2] == 19.75f);
        assert(last.position[3] == static_cast<float>(static_cast<int>(LightType::Point)));
        assert(ReadPackedLightFloat(last, 32) == 1.0f);
        assert(ReadPackedLightFloat(last, 36) == 1.0f);
        assert(ReadPackedLightFloat(last, 40) == 1.0f);
        assert(ReadPackedLightFloat(last, 44) == 20.0f);
        assert(ReadPackedLightFloat(last, 48) == 29.0f);
        assert(ReadPackedLightFloat(last, 52) == 0.73f);
    }

    void TestInvalidLightsAreSkipped()
    {
        CoreContainer::VariableArray<LightProxy> proxies;

        LightProxy invisible = MakePointLight(0);
        invisible.bVisible = false;
        proxies.push_back(invisible);

        LightProxy zeroIntensity = MakePointLight(1);
        zeroIntensity.Intensity = 0.0f;
        proxies.push_back(zeroIntensity);

        LightProxy valid = MakePointLight(2);
        proxies.push_back(valid);

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 1);
        assert(packedLights.size() == 1);
        assert(packedLights[0].position[0] == valid.PositionX);
        assert(ReadPackedLightFloat(packedLights[0], 44) == valid.Intensity);
    }

    void TestPackWhiteAndColoredLightsUsesYOneChromaticityAndCanonicalIntensity()
    {
        struct ChromaticityCase
        {
            float ColorR;
            float ColorG;
            float ColorB;
            float CanonicalIntensity;
            float ExpectedR;
            float ExpectedG;
            float ExpectedB;
            float ExpectedIntensity;
        };

        // Y=0.2126R+0.7152G+0.0722B. Expected values are hand-derived literals.
        const ChromaticityCase cases[] = {
            {1.0f, 1.0f, 1.0f, 100.0f, 1.0f, 1.0f, 1.0f, 100.0f},
            {1.0f, 0.25f, 0.1f, 50.0f, 2.5086549f, 0.6271637f, 0.2508655f, 50.0f},
        };

        CoreContainer::VariableArray<LightProxy> proxies;
        for (const ChromaticityCase& testCase : cases)
        {
            LightProxy proxy;
            proxy.LightId = static_cast<uint64_t>(proxies.size()) + 1;
            proxy.Type = LightType::Point;
            proxy.ColorR = testCase.ColorR;
            proxy.ColorG = testCase.ColorG;
            proxy.ColorB = testCase.ColorB;
            proxy.CanonicalIntensity = testCase.CanonicalIntensity;
            proxy.bVisible = true;
            proxies.push_back(proxy);
        }

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 2);
        assert(packedLights.size() == 2);
        for (uint32_t index = 0; index < 2; ++index)
        {
            const ChromaticityCase& testCase = cases[index];
            const GPULightData& light = packedLights[index];
            const float normalizedR = ReadPackedLightFloat(light, 32);
            const float normalizedG = ReadPackedLightFloat(light, 36);
            const float normalizedB = ReadPackedLightFloat(light, 40);
            assert(NearlyEqual(normalizedR, testCase.ExpectedR));
            assert(NearlyEqual(normalizedG, testCase.ExpectedG));
            assert(NearlyEqual(normalizedB, testCase.ExpectedB));
            assert(NearlyEqual(ReadPackedLightFloat(light, 44), testCase.ExpectedIntensity));
            assert(NearlyEqual(0.2126f * normalizedR +
                                   0.7152f * normalizedG +
                                   0.0722f * normalizedB,
                               1.0f));
        }
    }

    // CSMとRT影を掛ける灯だけがattenuation[2]=1で詰められ、既定では全灯0のまま。
    void TestShadowedLightIsMarkedOnlyForTheSelectedLight()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        for (uint32_t i = 0; i < 3; ++i)
        {
            LightProxy light = MakePointLight(i);
            light.Type = i < 2u ? LightType::Directional : LightType::Point;
            proxies.push_back(light);
        }
        const CoreContainer::Span<const LightProxy> span(proxies.data(), proxies.size());

        CoreContainer::VariableArray<GPULightData> packedLights;
        assert(PackLightingPassLights(span, packedLights) == 3u);
        for (const GPULightData& light : packedLights)
        {
            assert(ReadPackedLightFloat(light, 56) == 0.0f);
        }

        assert(PackLightingPassLights(span, packedLights, &proxies[1]) == 3u);
        assert(ReadPackedLightFloat(packedLights[0], 56) == 0.0f);
        assert(ReadPackedLightFloat(packedLights[1], 56) == 1.0f);
        assert(ReadPackedLightFloat(packedLights[2], 56) == 0.0f);
    }

    void TestEmptyInputProducesNoLights()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        CoreContainer::VariableArray<GPULightData> packedLights;

        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 0);
        assert(packedLights.empty());
    }

    void TestAllInvalidInputProducesNoLights()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        LightProxy invisible = MakePointLight(0);
        invisible.bVisible = false;
        proxies.push_back(invisible);

        LightProxy zeroIntensity = MakePointLight(1);
        zeroIntensity.Intensity = 0.0f;
        proxies.push_back(zeroIntensity);

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 0);
        assert(packedLights.empty());
    }

    void TestLocalAttenuationBoundaryLiteralTable()
    {
        struct AttenuationCase
        {
            double Distance;
            double ExpectedInverseSquare;
        };

        // The near clamp is 0.01 m; these are fixed, hand-derived contract rows.
        const AttenuationCase cases[] = {
            {0.005, 10000.0},
            {0.010, 10000.0},
            {0.020, 2500.0},
        };

        for (const AttenuationCase& testCase : cases)
        {
            const double actualInverseSquare = ComputeLocalInverseSquare(testCase.Distance);
            assert(std::fabs(actualInverseSquare - testCase.ExpectedInverseSquare) <= 1.0e-9);
        }

        struct RangeWindowCase
        {
            double Distance;
            double Range;
            double ExpectedWindow;
        };

        // The range window is a separate literal term from inverse-square attenuation.
        const RangeWindowCase rangeCases[] = {
            {2.0, 1000.0, 0.999999999968},
            {1000.0, 1000.0, 0.0},
            {1001.0, 1000.0, 0.0},
            {0.0001, 0.00005, 0.0},
        };

        for (const RangeWindowCase& testCase : rangeCases)
        {
            const double actualWindow = ComputeRangeWindow(testCase.Distance, testCase.Range);
            assert(std::fabs(actualWindow - testCase.ExpectedWindow) <= 1.0e-12);
        }
    }

    void TestPhysicalLightingResourceLifecycle()
    {
        static_assert(std::is_same_v<decltype(PhysicalLightingResources::FrameNumber), uint64_t>);
        static_assert(std::is_same_v<decltype(PhysicalLightingResources::ViewId), uint32_t>);
        static_assert(std::is_same_v<decltype(PhysicalLightingResources::ViewportId), uint32_t>);

        PhysicalLightingResources resources;
        resources.Begin(11, 2, 3);

        const RHI::BufferPtr lightBuffer = CoreContainer::MakeShared<LifecycleBuffer>();
        const RHI::TexturePtr shadowMap = CoreContainer::MakeShared<LifecycleTexture>(4);
        const RHI::SamplerPtr shadowSampler = CoreContainer::MakeShared<LifecycleSampler>();
        const RHI::TexturePtr environment = CoreContainer::MakeShared<LifecycleTexture>();
        const RHI::SamplerPtr environmentSampler = CoreContainer::MakeShared<LifecycleSampler>();
        const RHI::TexturePtr diffuse = CoreContainer::MakeShared<LifecycleTexture>();
        const RHI::SamplerPtr diffuseSampler = CoreContainer::MakeShared<LifecycleSampler>();
        const RHI::TexturePtr prefiltered = CoreContainer::MakeShared<LifecycleTexture>();
        const RHI::SamplerPtr prefilteredSampler = CoreContainer::MakeShared<LifecycleSampler>();
        const RHI::TexturePtr dfg = CoreContainer::MakeShared<LifecycleTexture>();
        const RHI::SamplerPtr dfgSampler = CoreContainer::MakeShared<LifecycleSampler>();
        const float view[16] = {2.0f, 0.0f, 0.0f, 3.0f,
                                0.0f, 2.0f, 0.0f, 4.0f,
                                0.0f, 0.0f, 2.0f, 5.0f,
                                0.0f, 0.0f, 0.0f, 1.0f};
        const float projection[16] = {6.0f, 0.0f, 0.0f, 7.0f,
                                      0.0f, 6.0f, 0.0f, 8.0f,
                                      0.0f, 0.0f, 6.0f, 9.0f,
                                      0.0f, 0.0f, 0.0f, 1.0f};

        float cascadedViews[4][16] = {};
        float cascadedProjections[4][16] = {};
        for (uint32_t cascadeIndex = 0; cascadeIndex < 4; ++cascadeIndex)
        {
            cascadedViews[cascadeIndex][0] = 1.0f;
            cascadedViews[cascadeIndex][5] = 1.0f;
            cascadedViews[cascadeIndex][10] = 1.0f;
            cascadedViews[cascadeIndex][15] = 1.0f;
            cascadedProjections[cascadeIndex][0] = 1.0f;
            cascadedProjections[cascadeIndex][5] = 1.0f;
            cascadedProjections[cascadeIndex][10] = 1.0f;
            cascadedProjections[cascadeIndex][15] = 1.0f;
        }
        const float splitDistances[5] = {0.1f, 10.0f, 25.0f, 60.0f, 150.0f};

        resources.PublishCascadedShadow(&cascadedViews[0][0],
                                        &cascadedProjections[0][0],
                                        splitDistances,
                                        4,
                                        77,
                                        true);
        resources.PublishDirectionalShadow(view, projection, 77, true, shadowMap, shadowSampler);
        resources.PublishLighting(lightBuffer,
                                  2,
                                  128,
                                  environment,
                                  environmentSampler,
                                  diffuse,
                                  diffuseSampler,
                                  prefiltered,
                                  prefilteredSampler,
                                  dfg,
                                  dfgSampler,
                                  4,
                                  3.5f,
                                  true);

        assert(resources.Matches(11, 2, 3));
        assert(resources.bShadowPublished);
        assert(resources.bLightingPublished);
        assert(resources.DirectionalShadow.LightId == 77);
        assert(resources.DirectionalShadow.bEnabled);
        assert(resources.DirectionalShadow.View[3] == 3.0f);
        assert(resources.DirectionalShadow.Projection[7] == 8.0f);
        assert(resources.CascadedShadow.CascadeCount == 4);
        assert(resources.CascadedShadow.SplitDistances[4] == 150.0f);
        assert(resources.CascadedShadow.View[3][0] == 1.0f);
        assert(resources.LightBuffer == lightBuffer);
        assert(resources.LogicalLightCount == 2);
        assert(resources.LightBufferSizeBytes == 128);
        assert(resources.EnvironmentRadianceTexture == environment);
        assert(resources.DiffuseIrradianceSampler == diffuseSampler);
        assert(resources.PrefilteredSpecularTexture == prefiltered);
        assert(resources.DfgLutSampler == dfgSampler);
        assert(resources.PrefilteredSpecularMipLevels == 4);
        assert(resources.IBLIntensity == 3.5f);
        assert(resources.bIBLEnabled);

        const PhysicalLightingResources copied = resources;
        assert(copied.Matches(11, 2, 3));
        assert(copied.DirectionalShadow.View[3] == 3.0f);
        assert(copied.DirectionalShadow.Projection[7] == 8.0f);
        assert(copied.CascadedShadow.CascadeCount == 4);
        assert(copied.CascadedShadow.SplitDistances[1] == 10.0f);
        assert(copied.LightBuffer == lightBuffer);
        assert(copied.EnvironmentRadianceTexture == environment);
        assert(copied.PrefilteredSpecularMipLevels == 4);
        assert(copied.IBLIntensity == 3.5f);

        resources.Begin(12, 4, 5);
        assert(resources.Matches(12, 4, 5));
        assert(!resources.Matches(11, 2, 3));
        assert(!resources.bShadowPublished);
        assert(!resources.bLightingPublished);
        assert(!resources.ShadowMapTexture);
        assert(!resources.ShadowSampler);
        assert(!resources.LightBuffer);
        assert(!resources.EnvironmentRadianceTexture);
        assert(!resources.EnvironmentRadianceSampler);
        assert(!resources.DiffuseIrradianceTexture);
        assert(!resources.DiffuseIrradianceSampler);
        assert(!resources.PrefilteredSpecularTexture);
        assert(!resources.PrefilteredSpecularSampler);
        assert(!resources.DfgLutTexture);
        assert(!resources.DfgLutSampler);
        assert(resources.LogicalLightCount == 0);
        assert(resources.LightBufferSizeBytes == 0);
        assert(resources.PrefilteredSpecularMipLevels == 0);
        assert(resources.IBLIntensity == 0.0f);
        assert(!resources.bIBLEnabled);
        assert(resources.DirectionalShadow.LightId == 0);
        assert(!resources.DirectionalShadow.bEnabled);
        assert(resources.DirectionalShadow.View[0] == 1.0f);
        assert(resources.DirectionalShadow.View[3] == 0.0f);
        assert(resources.DirectionalShadow.Projection[0] == 1.0f);
        assert(resources.DirectionalShadow.Projection[7] == 0.0f);

        resources.Invalidate();
        assert(!resources.bActive);
        assert(resources.FrameNumber == 0);
        assert(resources.ViewId == UINT32_MAX);
        assert(resources.ViewportId == UINT32_MAX);
        assert(!resources.bShadowPublished);
        assert(!resources.bLightingPublished);
        assert(!resources.ShadowMapTexture);
        assert(!resources.LightBuffer);
        assert(!resources.EnvironmentRadianceTexture);
        assert(!resources.DfgLutTexture);
        assert(resources.PrefilteredSpecularMipLevels == 0);
        assert(resources.IBLIntensity == 0.0f);
        assert(!resources.bIBLEnabled);
        assert(resources.DirectionalShadow.View[0] == 1.0f);
        assert(resources.DirectionalShadow.View[3] == 0.0f);
        assert(!resources.DirectionalShadow.bEnabled);
    }

    void TestSceneProxySkyLifecycle()
    {
        SceneProxy scene;
        scene.SkyAtmosphere.bEnabled = true;
        scene.SkyAtmosphere.SunAltitudeDegrees = 12.0f;
        scene.Clear();
        assert(!scene.SkyAtmosphere.bEnabled);
        assert(scene.SkyAtmosphere.SunAltitudeDegrees == 45.0f);
    }
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "LightingLightBufferTest start\n";

    AssertGPULightDataLayout();
    TestPackTwentyValidPointLights();
    TestInvalidLightsAreSkipped();
    TestEmptyInputProducesNoLights();
    TestShadowedLightIsMarkedOnlyForTheSelectedLight();
    TestAllInvalidInputProducesNoLights();
    TestPackWhiteAndColoredLightsUsesYOneChromaticityAndCanonicalIntensity();
    TestLocalAttenuationBoundaryLiteralTable();
    TestPhysicalLightingResourceLifecycle();
    TestSceneProxySkyLifecycle();

    std::cout << "LightingLightBufferTest passed\n";
    return 0;
}
