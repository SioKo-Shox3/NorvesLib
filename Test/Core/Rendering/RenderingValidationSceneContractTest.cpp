#include "RenderingValidation/RenderingValidationScene.h"

#include "RenderingValidation/GpuTestEnvironment.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"

#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core;
using namespace NorvesLib::Test::RenderingValidation;

namespace NorvesLib::Test::RenderingValidation
{
    struct RenderingValidationSceneContractTestAccess
    {
        static void Commit(RenderingValidationSceneFixture& fixture,
                           SceneFixtureInitializationState& state,
                           ISceneFixtureResourceReleaser* stableOwner) noexcept
        {
            SceneFixtureInitializationGuard guard(&state.Lease);
            guard.BindObjects(state.pWorld, &state.Objects);
            guard.BindPublication(&fixture, &state, stableOwner);
            guard.Commit();
        }

        static void Abort(RenderingValidationSceneFixture& fixture,
                          SceneFixtureInitializationState& state,
                          ISceneFixtureResourceReleaser* stableOwner) noexcept
        {
            SceneFixtureInitializationGuard guard(&state.Lease);
            guard.BindObjects(state.pWorld, &state.Objects);
            guard.BindPublication(&fixture, &state, stableOwner);
        }

        static void Shutdown(RenderingValidationSceneFixture& fixture,
                             ISceneFixtureResourceReleaser* stableOwner) noexcept
        {
            fixture.ShutdownPublishedInitialization(stableOwner);
        }

        static void ReleasePublishedLease(RenderingValidationSceneFixture& fixture) noexcept
        {
            fixture.m_Lease.Release();
        }

        static bool IsPublishedEmpty(const RenderingValidationSceneFixture& fixture) noexcept
        {
            return !fixture.m_bPublished && fixture.m_pWorld == nullptr &&
                   fixture.m_pResources == nullptr && fixture.m_pSentinel == nullptr &&
                   fixture.m_pP4PlaneMesh == nullptr && fixture.m_pP4SphereMesh == nullptr &&
                   fixture.m_pEmissiveMesh == nullptr && fixture.m_pTransparentMesh == nullptr &&
                   fixture.m_pP4LightEntity == nullptr && fixture.m_Lease.IsEmpty() &&
                   fixture.m_Objects.empty();
        }

        static bool MatchesPublishedState(const RenderingValidationSceneFixture& fixture,
                              NorvesLib::Core::World* world,
                              NorvesLib::Core::Rendering::RenderResources* resources,
                                          FixedStepSentinelComponent* sentinel,
                                          ISceneFixtureResourceReleaser* stableOwner,
                                          size_t meshCount,
                                          size_t textureCount,
                                          size_t materialCount) noexcept
        {
            return fixture.m_bPublished && fixture.m_pWorld == world &&
                   fixture.m_pResources == resources && fixture.m_pSentinel == sentinel &&
                   fixture.m_Lease.m_pReleaser == stableOwner &&
                   fixture.m_Lease.TrackedMeshCount() == meshCount &&
                   fixture.m_Lease.TrackedTextureCount() == textureCount &&
                   fixture.m_Lease.TrackedMaterialCount() == materialCount;
        }

        static bool IsStagingCleared(const SceneFixtureInitializationState& state) noexcept
        {
            return state.pWorld == nullptr && state.pResources == nullptr &&
                   state.pSentinel == nullptr && state.pP4PlaneMesh == nullptr &&
                   state.pP4SphereMesh == nullptr && state.pEmissiveMesh == nullptr &&
                   state.pTransparentMesh == nullptr && state.pP4LightEntity == nullptr &&
                   state.Lease.IsEmpty() && state.Lease.m_pReleaser == nullptr &&
                   state.Objects.empty();
        }
    };
}

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
        Rendering::MeshDataHandle CreateMesh(uint64_t id) noexcept
        {
            ++GeneratedMeshCount;
            ++ActiveMeshCount;
            return Rendering::MeshDataHandle{id};
        }

        Rendering::TextureHandle CreateTexture(uint64_t id) noexcept
        {
            ++GeneratedTextureCount;
            ++ActiveTextureCount;
            return Rendering::TextureHandle{id};
        }

        Rendering::MaterialHandle CreateMaterial(uint64_t id) noexcept
        {
            ++GeneratedMaterialCount;
            ++ActiveMaterialCount;
            return Rendering::MaterialHandle{id};
        }

        void ResetObservations() noexcept
        {
            Events.clear();
            GeneratedMeshCount = 0;
            GeneratedTextureCount = 0;
            GeneratedMaterialCount = 0;
            ActiveMeshCount = 0;
            ActiveTextureCount = 0;
            ActiveMaterialCount = 0;
            bInvalidRelease = false;
        }

        void UnregisterMesh(Rendering::MeshDataHandle handle) noexcept override
        {
            Events.push_back(handle.Id);
            if (ActiveMeshCount > 0)
            {
                --ActiveMeshCount;
            }
            else
            {
                bInvalidRelease = true;
            }
        }

        void ReleaseTexture(Rendering::TextureHandle handle) noexcept override
        {
            Events.push_back(TextureEventMask | handle.Id);
            if (ActiveTextureCount > 0)
            {
                --ActiveTextureCount;
            }
            else
            {
                bInvalidRelease = true;
            }
        }

        void ReleaseMaterial(Rendering::MaterialHandle handle) noexcept override
        {
            Events.push_back(MaterialEventMask | handle.Id);
            if (ActiveMaterialCount > 0)
            {
                --ActiveMaterialCount;
            }
            else
            {
                bInvalidRelease = true;
            }
        }

        static constexpr uint64_t MaterialEventMask = uint64_t{1} << 63;
        static constexpr uint64_t TextureEventMask = uint64_t{1} << 62;
        Container::VariableArray<uint64_t> Events;
        size_t GeneratedMeshCount = 0;
        size_t GeneratedTextureCount = 0;
        size_t GeneratedMaterialCount = 0;
        size_t ActiveMeshCount = 0;
        size_t ActiveTextureCount = 0;
        size_t ActiveMaterialCount = 0;
        bool bInvalidRelease = false;
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

    void RequireGenerationCounts(const FakeReleaser& releaser,
                                 size_t meshCount,
                                 size_t textureCount,
                                 size_t materialCount)
    {
        Require(releaser.GeneratedMeshCount == meshCount,
                "fake releaser must generate the requested mesh count");
        Require(releaser.GeneratedTextureCount == textureCount,
                "fake releaser must generate the requested texture count");
        Require(releaser.GeneratedMaterialCount == materialCount,
                "fake releaser must generate the requested material count");
    }

    void RequireTrackedCounts(const SceneFixtureResourceLease& lease,
                              size_t meshCount,
                              size_t textureCount,
                              size_t materialCount)
    {
        Require(lease.TrackedMeshCount() == meshCount,
                "resource lease mesh accessor returned an unexpected count");
        Require(lease.TrackedTextureCount() == textureCount,
                "resource lease texture accessor returned an unexpected count");
        Require(lease.TrackedMaterialCount() == materialCount,
                "resource lease material accessor returned an unexpected count");
    }

    void TrackGeneratedResources(FakeReleaser& releaser,
                                 SceneFixtureResourceLease& lease,
                                 size_t meshCount,
                                 size_t textureCount,
                                 size_t materialCount)
    {
        for (size_t index = 0; index < meshCount; ++index)
        {
            lease.TrackMesh(releaser.CreateMesh(index + 1));
        }
        for (size_t index = 0; index < textureCount; ++index)
        {
            lease.TrackTexture(releaser.CreateTexture(index + 1));
        }
        for (size_t index = 0; index < materialCount; ++index)
        {
            lease.TrackMaterial(releaser.CreateMaterial(index + 1));
        }
    }

    void RequireReleaseOrder(const FakeReleaser& releaser,
                             size_t meshCount,
                             size_t textureCount,
                             size_t materialCount)
    {
        size_t eventIndex = 0;
        for (size_t index = materialCount; index > 0; --index)
        {
            RequireEvent(releaser, eventIndex++, FakeReleaser::MaterialEventMask | index);
        }
        for (size_t index = textureCount; index > 0; --index)
        {
            RequireEvent(releaser, eventIndex++, FakeReleaser::TextureEventMask | index);
        }
        for (size_t index = meshCount; index > 0; --index)
        {
            RequireEvent(releaser, eventIndex++, index);
        }
        Require(releaser.Events.size() == eventIndex, "resource lease rollback has an unexpected event count");
        Require(releaser.ActiveMeshCount == 0 && releaser.ActiveTextureCount == 0 &&
                    releaser.ActiveMaterialCount == 0,
                "fake releaser still has active resources after release");
        Require(!releaser.bInvalidRelease, "fake releaser observed an invalid or duplicate release");
    }

    void PrepareStagingState(SceneFixtureInitializationState& state,
                             FakeReleaser& releaser,
                             NorvesLib::Core::World* world,
                             NorvesLib::Core::Rendering::RenderResources* resources,
                             FixedStepSentinelComponent* sentinel,
                             size_t meshCount,
                             size_t textureCount,
                             size_t materialCount)
    {
        state.pWorld = world;
        state.pResources = resources;
        state.pSentinel = sentinel;
        state.P4Materials.fill(Rendering::MaterialHandle::Invalid());
        state.Lease.Bind(&releaser);
        TrackGeneratedResources(releaser,
                                state.Lease,
                                meshCount,
                                textureCount,
                                materialCount);
    }

    void VerifyFixtureTransaction()
    {
        constexpr size_t meshCount = 2;
        constexpr size_t textureCount = 13;
        constexpr size_t materialCount = 33;
        NorvesLib::Core::World world;
        NorvesLib::Core::Rendering::RenderResources resources;
        FixedStepSentinelComponent sentinel;
        RenderingValidationSceneFixture fixture;

        FakeReleaser rollbackReleaser;
        SceneFixtureInitializationState rollbackState;
        PrepareStagingState(rollbackState,
                            rollbackReleaser,
                            &world,
                            &resources,
                            &sentinel,
                            meshCount,
                            textureCount,
                            materialCount);
        Require(!rollbackState.Lease.IsEmpty(),
                "full rollback staging lease did not receive generated resources");
        Require(RenderingValidationSceneContractTestAccess::IsPublishedEmpty(fixture),
                "fixture published members changed before commit");
        RenderingValidationSceneContractTestAccess::Abort(fixture,
                                                           rollbackState,
                                                           &rollbackReleaser);
        Require(rollbackState.Lease.IsEmpty(),
                "full rollback did not clear staging lease");
        Require(RenderingValidationSceneContractTestAccess::IsPublishedEmpty(fixture),
                "full rollback published partial fixture state");
        RequireReleaseOrder(rollbackReleaser, meshCount, textureCount, materialCount);

        FakeReleaser partialReleaser;
        SceneFixtureInitializationState partialState;
        PrepareStagingState(partialState,
                            partialReleaser,
                            &world,
                            &resources,
                            &sentinel,
                            1,
                            7,
                            9);
        RenderingValidationSceneContractTestAccess::Abort(fixture,
                                                           partialState,
                                                           &partialReleaser);
        Require(partialState.Lease.IsEmpty(),
                "partial failure did not clear staging lease");
        Require(RenderingValidationSceneContractTestAccess::IsPublishedEmpty(fixture),
                "partial failure published fixture state");
        RequireReleaseOrder(partialReleaser, 1, 7, 9);

        FakeReleaser firstReleaser;
        SceneFixtureInitializationState firstState;
        PrepareStagingState(firstState,
                            firstReleaser,
                            &world,
                            &resources,
                            &sentinel,
                            meshCount,
                            textureCount,
                            materialCount);
        Require(RenderingValidationSceneContractTestAccess::IsPublishedEmpty(fixture),
                "fixture must remain unpublished before successful commit");
        RenderingValidationSceneContractTestAccess::Commit(fixture,
                                                            firstState,
                                                            &firstReleaser);
        Require(RenderingValidationSceneContractTestAccess::MatchesPublishedState(
                    fixture,
                    &world,
                    &resources,
                    &sentinel,
                    &firstReleaser,
                    meshCount,
                    textureCount,
                    materialCount),
                "commit did not publish the complete fixture state");
        Require(RenderingValidationSceneContractTestAccess::IsStagingCleared(firstState),
                "commit did not invalidate staging borrow and ownership state");
        Require(firstReleaser.Events.empty(),
                "commit released resources before shutdown");

        RenderingValidationSceneContractTestAccess::Shutdown(fixture, &firstReleaser);
        Require(RenderingValidationSceneContractTestAccess::IsPublishedEmpty(fixture),
                "shutdown did not clear published fixture members");
        RequireReleaseOrder(firstReleaser, meshCount, textureCount, materialCount);
        const size_t eventCountAfterShutdown = firstReleaser.Events.size();
        RenderingValidationSceneContractTestAccess::Shutdown(fixture, &firstReleaser);
        Require(firstReleaser.Events.size() == eventCountAfterShutdown,
                "double shutdown released a resource twice");

        FakeReleaser secondReleaser;
        SceneFixtureInitializationState secondState;
        PrepareStagingState(secondState,
                            secondReleaser,
                            &world,
                            &resources,
                            &sentinel,
                            meshCount,
                            textureCount,
                            materialCount);
        RenderingValidationSceneContractTestAccess::Commit(fixture,
                                                            secondState,
                                                            &secondReleaser);
        Require(RenderingValidationSceneContractTestAccess::MatchesPublishedState(
                    fixture,
                    &world,
                    &resources,
                    &sentinel,
                    &secondReleaser,
                    meshCount,
                    textureCount,
                    materialCount),
                "rebind did not publish the new stable owner");
        Require(RenderingValidationSceneContractTestAccess::IsStagingCleared(secondState),
                "rebind did not invalidate the second staging state");
        RenderingValidationSceneContractTestAccess::ReleasePublishedLease(fixture);
        RequireReleaseOrder(secondReleaser, meshCount, textureCount, materialCount);
        const size_t eventCountAfterRelease = secondReleaser.Events.size();
        RenderingValidationSceneContractTestAccess::ReleasePublishedLease(fixture);
        Require(secondReleaser.Events.size() == eventCountAfterRelease,
                "explicit lease release was not idempotent");
        RenderingValidationSceneContractTestAccess::Shutdown(fixture, &secondReleaser);
        Require(RenderingValidationSceneContractTestAccess::IsPublishedEmpty(fixture),
                "shutdown after explicit release did not clear fixture state");
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
    Require(static_cast<uint8_t>(P4Scenario::Raw254DirectConductorEndpoint) == 5u,
            "direct-conductor-endpoint must be the raw254 fixture scenario");
    Require(BuildSceneLayout(SceneKind::Outdoor, ValidationSeed, outdoor), "outdoor fixture layout is unavailable");

    VerifyKnownCdOracleContract();

    VerifyFixtureTransaction();
    std::cout << "RenderingValidationSceneContractTest PASS: fake_resources=2/13/33 rollback=PASS commit=PASS shutdown=PASS release_idempotent=PASS rebind=PASS\n";
    return 0;
}
