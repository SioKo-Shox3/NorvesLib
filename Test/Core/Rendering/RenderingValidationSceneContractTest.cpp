#include "RenderingValidation/RenderingValidationScene.h"

#include "RenderingValidation/GpuTestEnvironment.h"

#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core;
using namespace NorvesLib::Test::RenderingValidation;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "RenderingValidationSceneContractTest RED: " << message << "\n";
            std::abort();
        }
    }

    class FakeReleaser final : public ISceneFixtureResourceReleaser
    {
    public:
        void UnregisterMesh(Rendering::MeshDataHandle handle) noexcept override
        {
            Events.push_back(handle.Id);
        }

        void ReleaseTexture(Rendering::TextureHandle handle) noexcept override
        {
            Events.push_back(TextureEventMask | handle.Id);
        }

        void ReleaseMaterial(Rendering::MaterialHandle handle) noexcept override
        {
            Events.push_back(MaterialEventMask | handle.Id);
        }

        static constexpr uint64_t MaterialEventMask = uint64_t{1} << 63;
        static constexpr uint64_t TextureEventMask = uint64_t{1} << 62;
        Container::VariableArray<uint64_t> Events;
    };

    void RequireEvent(const FakeReleaser& releaser, size_t index, uint64_t expected)
    {
        Require(index < releaser.Events.size(), "resource lease event is missing");
        Require(releaser.Events[index] == expected, "resource lease release order is invalid");
    }

    Container::String ReadSourceFile(const char* path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return {};
        }

        Container::String source;
        char buffer[4096]{};
        while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
        {
            source.append(buffer, static_cast<size_t>(input.gcount()));
        }
        return source;
    }

    void VerifyKnownCdOracleContract()
    {
        Container::String source =
            ReadSourceFile("Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp");
        if (source.empty())
        {
            source = ReadSourceFile("../Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp");
        }
        if (source.empty())
        {
            source = ReadSourceFile("../../../../Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp");
        }
        Require(!source.empty(), "known-cd oracle source could not be opened");
        Require(source.find("const double rangeWindow = std::pow") != Container::String::npos,
                "known-cd oracle must calculate the independent range window");
        Require(source.find("double channelMeanRelativeError[3] = {}") != Container::String::npos,
                "known-cd oracle must aggregate mean relative error for RGB channels");
        Require(source.find("known-cd anchor RGB oracle") != Container::String::npos,
                "known-cd oracle must compare the anchor RGB sample");
        Require(source.find("known-cd previous image is unavailable") != Container::String::npos,
                "known-cd A/B oracle must fail when the previous image is absent");
    }

    void VerifyGuardRollback(size_t meshCount, bool bTrackMaterial)
    {
        FakeReleaser releaser;
        SceneFixtureResourceLease lease;
        lease.Bind(&releaser);
        {
            SceneFixtureInitializationGuard guard(&lease);
            for (size_t index = 0; index < meshCount; ++index)
            {
                lease.TrackMesh(Rendering::MeshDataHandle{index + 1});
            }
            if (bTrackMaterial)
            {
                lease.TrackTexture(Rendering::TextureHandle{7});
                lease.TrackMaterial(Rendering::MaterialHandle{9});
            }
        }

        size_t eventIndex = 0;
        if (bTrackMaterial)
        {
            RequireEvent(releaser, eventIndex++, FakeReleaser::MaterialEventMask | 9);
            RequireEvent(releaser, eventIndex++, FakeReleaser::TextureEventMask | 7);
        }
        for (size_t index = meshCount; index > 0; --index)
        {
            RequireEvent(releaser, eventIndex++, index);
        }
        Require(releaser.Events.size() == eventIndex, "resource lease rollback has an unexpected event count");
        Require(lease.IsEmpty(), "resource lease rollback did not clear tracked handles");
    }
}

int main()
{
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    SceneLayout first;
    SceneLayout second;
    SceneLayout outdoor;

    Require(BuildSceneLayout(SceneKind::Indoor, ValidationSeed, first), "indoor fixture layout is unavailable");
    Require(BuildSceneLayout(SceneKind::Indoor, ValidationSeed, second), "indoor fixture layout is not repeatable");
    Require(first == second, "indoor fixture layout is not deterministic");
    Require(first.Camera.Viewport.Width == 256.0f, "fixture width must remain 256 pixels");
    Require(first.Camera.Viewport.Height == 256.0f, "fixture height must remain 256 pixels");
    Require(first.Camera.Projection == Rendering::ProjectionType::Orthographic,
            "known-cd fixture must use orthographic projection");
    Require(first.Camera.PositionX == 0.0f && first.Camera.PositionY == 0.0f &&
                first.Camera.PositionZ == 4.0f,
            "known-cd fixture camera must be at (0,0,4)");
    Require(first.Camera.OrthoWidth == 0.1f && first.Camera.OrthoHeight == 0.1f,
            "known-cd fixture orthographic extent must be 0.1m by 0.1m");
    Require(R1RoiMinX == 112u && R1RoiMaxX == 143u &&
                R1RoiMinY == 112u && R1RoiMaxY == 143u &&
                R1AnchorX == 127u && R1AnchorY == 127u,
            "known-cd ROI and anchor coordinates are not fixed");
    Require(static_cast<float>(R1CameraTargetX) == 0.0001953125f &&
                static_cast<float>(R1CameraTargetY) == -0.0001953125f,
            "known-cd camera target float projection values are not fixed");
    const double projectedAnchorX =
        ((-R1CameraTargetX) / 0.1 + 0.5) * 256.0;
    const double projectedAnchorY =
        ((R1CameraTargetY) / 0.1 + 0.5) * 256.0;
    Require(std::abs(projectedAnchorX - 127.5) <= 1.0e-4 &&
                std::abs(projectedAnchorY - 127.5) <= 1.0e-4,
            "known-cd CPU projection does not put the anchor at pixel center");
    const double viewLength = std::sqrt(4.0 * 4.0);
    const double lightLength = std::sqrt(2.0 * 2.0);
    const double normalDotView = 1.0 * 4.0 / viewLength;
    const double normalDotLight = 1.0 * 2.0 / lightLength;
    Require(std::abs(normalDotView - 1.0) <= 1.0e-6 &&
                std::abs(normalDotLight - 1.0) <= 1.0e-6,
            "known-cd anchor N dot V/L is not the fixed ideal geometry");
    Require(first.Objects.size() >= 3u, "known-cd fixture must include opaque, emissive, and transparent rows");
    Require(first.Objects[0].Material == MaterialKind::NeutralOpaque &&
                first.Objects[1].Material == MaterialKind::EmissiveOpaque &&
                first.Objects[2].Material == MaterialKind::LegacyTransparent,
            "known-cd fixture material kinds are not neutral/emissive/legacy-transparent");
    Require(first.Objects[0].Position[0] == 0.0f && first.Objects[0].Position[1] == 0.0f &&
                first.Objects[0].Position[2] == 0.0f,
            "known-cd plane origin must be exact");
    Require(first.Objects[0].RotationEulerDegrees[0] == 90.0f,
            "known-cd plane must be rotated +90 degrees around X");
    Require(first.Objects[0].Scale[0] == 0.0025f && first.Objects[0].Scale[1] == 0.0025f &&
                first.Objects[0].Scale[2] == 0.0025f,
            "known-cd plane scale must be 0.0025");
    Require(first.Objects[0].Color[0] == 0.5f && first.Objects[0].Color[1] == 0.5f &&
                first.Objects[0].Color[2] == 0.5f && first.Objects[0].Color[3] == 1.0f,
            "known-cd neutral opaque albedo must be 0.5 with opaque alpha");
    Require(first.Lights.size() == 1u, "known-cd fixture must contain one point light");
    Require(first.Lights[0].PositionOrDirection[0] == 0.0f &&
                first.Lights[0].PositionOrDirection[1] == 0.0f &&
                first.Lights[0].PositionOrDirection[2] == 2.0f,
            "known-cd point light position must be (0,0,2)");
    Require(first.Lights[0].Intensity == 100.0f && first.Lights[0].Range == 1000.0f &&
                !first.Lights[0].bCastShadows,
            "known-cd point light must use 100cd, range 1000m, and shadows off");
    Require(BuildSceneLayout(SceneKind::Outdoor, ValidationSeed, outdoor), "outdoor fixture layout is unavailable");

    VerifyKnownCdOracleContract();

    VerifyGuardRollback(0, false);
    VerifyGuardRollback(1, false);
    VerifyGuardRollback(2, false);
    VerifyGuardRollback(2, true);

    FakeReleaser releaser;
    SceneFixtureResourceLease lease;
    lease.Bind(&releaser);
    {
        SceneFixtureInitializationGuard guard(&lease);
        lease.TrackMesh(Rendering::MeshDataHandle{1});
        lease.TrackMesh(Rendering::MeshDataHandle{2});
        lease.TrackTexture(Rendering::TextureHandle{7});
        lease.TrackMaterial(Rendering::MaterialHandle{9});
        guard.Commit();
    }
    Require(releaser.Events.empty(), "committed fixture lease released before explicit shutdown");
    Require(!lease.IsEmpty(), "committed fixture lease lost tracked handles");
    lease.Release();
    Require(releaser.Events.size() == 4u, "committed fixture lease must release mesh/texture/material resources");
    RequireEvent(releaser, 0, FakeReleaser::MaterialEventMask | 9);
    RequireEvent(releaser, 1, FakeReleaser::TextureEventMask | 7);
    RequireEvent(releaser, 2, 2);
    RequireEvent(releaser, 3, 1);
    lease.Release();
    Require(releaser.Events.size() == 4u, "resource lease release must be idempotent");
    return 0;
}
