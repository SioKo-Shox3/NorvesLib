#include "Rendering/FramePacket.h"
#include "Rendering/VolumetricFog.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace NorvesLib::Core::Rendering;

static_assert(std::is_trivially_copyable_v<VolumetricFogParameters>);
static_assert(std::is_standard_layout_v<VolumetricFogParameters>);

namespace
{
    bool NearlyEqual(float lhs, float rhs, float epsilon)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    void TestFiniteDefaultsAndSanitization()
    {
        const VolumetricFogParameters defaults = MakeDefaultVolumetricFogParameters();
        assert(!defaults.bEnabled);
        assert(std::isfinite(defaults.DensityAtBaseHeight));
        assert(std::isfinite(defaults.BaseHeight));
        assert(std::isfinite(defaults.HeightFalloffPerUnit));

        VolumetricFogParameters invalid = defaults;
        invalid.bEnabled = true;
        invalid.DensityAtBaseHeight = std::numeric_limits<float>::quiet_NaN();
        invalid.BaseHeight = std::numeric_limits<float>::infinity();
        invalid.HeightFalloffPerUnit = -1.0f;
        const VolumetricFogParameters sanitized =
            SanitizeVolumetricFogParameters(invalid);
        assert(sanitized.bEnabled);
        assert(sanitized.DensityAtBaseHeight == defaults.DensityAtBaseHeight);
        assert(sanitized.BaseHeight == defaults.BaseHeight);
        assert(sanitized.HeightFalloffPerUnit == defaults.HeightFalloffPerUnit);

        SceneProxy sceneProxy;
        sceneProxy.SetVolumetricFogParameters(invalid);
        assert(sceneProxy.VolumetricFog.bEnabled);
        assert(sceneProxy.VolumetricFog.DensityAtBaseHeight == defaults.DensityAtBaseHeight);
        assert(sceneProxy.VolumetricFog.BaseHeight == defaults.BaseHeight);
        assert(sceneProxy.VolumetricFog.HeightFalloffPerUnit ==
               defaults.HeightFalloffPerUnit);

        VolumetricFogParameters outOfRange = defaults;
        outOfRange.DensityAtBaseHeight = 20.0f;
        outOfRange.BaseHeight = 2.0e6f;
        outOfRange.HeightFalloffPerUnit = 2.0f;
        const VolumetricFogParameters clamped =
            SanitizeVolumetricFogParameters(outOfRange);
        assert(clamped.DensityAtBaseHeight == 10.0f);
        assert(clamped.BaseHeight == 1.0e6f);
        assert(clamped.HeightFalloffPerUnit == 1.0f);
    }

    void TestAnalyticTransmittanceForHorizontalAndVerticalSlopes()
    {
        VolumetricFogParameters parameters = MakeDefaultVolumetricFogParameters();
        parameters.bEnabled = true;
        parameters.DensityAtBaseHeight = 0.1f;
        parameters.HeightFalloffPerUnit = 0.2f;

        const float horizontal = ComputeHeightFogTransmittance(parameters, 0.0f, 0.0f, 10.0f);
        const float ascending = ComputeHeightFogTransmittance(parameters, 0.0f, 0.5f, 10.0f);
        const float descending = ComputeHeightFogTransmittance(parameters, 0.0f, -0.5f, 10.0f);

        assert(NearlyEqual(horizontal, 0.36787945f, 1.0e-6f));
        assert(NearlyEqual(ascending, 0.53146362f, 1.0e-6f));
        assert(NearlyEqual(descending, 0.17937408f, 1.0e-6f));
        assert(descending < horizontal && horizontal < ascending);
    }

    void TestInvalidRayInputsAndDisabledFogAreIdentity()
    {
        VolumetricFogParameters parameters = MakeDefaultVolumetricFogParameters();
        assert(ComputeHeightFogTransmittance(parameters, 0.0f, 0.0f, 10.0f) == 1.0f);

        parameters.bEnabled = true;
        assert(ComputeHeightFogTransmittance(
                   parameters, std::numeric_limits<float>::quiet_NaN(), 0.0f, 10.0f) == 1.0f);
        assert(ComputeHeightFogTransmittance(
                   parameters, 0.0f, std::numeric_limits<float>::infinity(), 10.0f) == 1.0f);
        assert(ComputeHeightFogTransmittance(parameters, 0.0f, 0.0f, -1.0f) == 1.0f);
    }

    void TestFramePacketSnapshotLifecycle()
    {
        FramePacketManager manager;
        manager.Initialize();

        FramePacket* writePacket = manager.AcquireForWrite();
        assert(writePacket != nullptr);
        VolumetricFogParameters parameters = MakeDefaultVolumetricFogParameters();
        parameters.bEnabled = true;
        parameters.DensityAtBaseHeight = 0.25f;
        parameters.BaseHeight = 12.0f;
        parameters.HeightFalloffPerUnit = 0.05f;
        writePacket->Scene.SetVolumetricFogParameters(parameters);
        manager.FinishWrite(writePacket);

        FramePacket* readPacket = manager.AcquireForRead();
        assert(readPacket == writePacket);
        assert(readPacket->Scene.VolumetricFog.bEnabled);
        assert(readPacket->Scene.VolumetricFog.DensityAtBaseHeight == 0.25f);
        assert(readPacket->Scene.VolumetricFog.BaseHeight == 12.0f);
        assert(readPacket->Scene.VolumetricFog.HeightFalloffPerUnit == 0.05f);
        manager.FinishRead(readPacket);

        const VolumetricFogParameters cleared = MakeDefaultVolumetricFogParameters();
        assert(!readPacket->Scene.VolumetricFog.bEnabled);
        assert(readPacket->Scene.VolumetricFog.DensityAtBaseHeight ==
               cleared.DensityAtBaseHeight);
        assert(readPacket->Scene.VolumetricFog.BaseHeight == cleared.BaseHeight);
        assert(readPacket->Scene.VolumetricFog.HeightFalloffPerUnit ==
               cleared.HeightFalloffPerUnit);
        assert(manager.IsEmpty());
    }
} // namespace

int main()
{
    std::cout << "VolumetricFogModelTest start\n";
    TestFiniteDefaultsAndSanitization();
    TestAnalyticTransmittanceForHorizontalAndVerticalSlopes();
    TestInvalidRayInputsAndDisabledFogAreIdentity();
    TestFramePacketSnapshotLifecycle();
    std::cout << "VolumetricFogModelTest passed\n";
    return 0;
}
