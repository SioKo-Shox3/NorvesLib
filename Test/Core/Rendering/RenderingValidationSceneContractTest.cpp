#include "RenderingValidation/RenderingValidationScene.h"

#include "RenderingValidation/GpuTestEnvironment.h"

#include <cstdlib>

using namespace NorvesLib::Core;
using namespace NorvesLib::Test::RenderingValidation;

namespace
{
    void Require(bool condition)
    {
        if (!condition)
        {
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

        void ReleaseMaterial(Rendering::MaterialHandle handle) noexcept override
        {
            Events.push_back(MaterialEventMask | handle.Id);
        }

        static constexpr uint64_t MaterialEventMask = uint64_t{1} << 63;
        Container::VariableArray<uint64_t> Events;
    };

    void RequireEvent(const FakeReleaser& releaser, size_t index, uint64_t expected)
    {
        Require(index < releaser.Events.size());
        Require(releaser.Events[index] == expected);
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
                lease.TrackMaterial(Rendering::MaterialHandle{9});
            }
        }

        size_t eventIndex = 0;
        if (bTrackMaterial)
        {
            RequireEvent(releaser, eventIndex++, FakeReleaser::MaterialEventMask | 9);
        }
        for (size_t index = meshCount; index > 0; --index)
        {
            RequireEvent(releaser, eventIndex++, index);
        }
        Require(releaser.Events.size() == eventIndex);
        Require(lease.IsEmpty());
    }
}

int main()
{
    SceneLayout first;
    SceneLayout second;
    SceneLayout outdoor;

    Require(BuildSceneLayout(SceneKind::Indoor, ValidationSeed, first));
    Require(BuildSceneLayout(SceneKind::Indoor, ValidationSeed, second));
    Require(first == second);
    Require(first.Objects.size() == 7u);
    Require(first.Lights.size() == 1u);
    Require(first.Camera.Viewport.Width == 256.0f);
    Require(BuildSceneLayout(SceneKind::Outdoor, ValidationSeed, outdoor));
    Require(outdoor.Objects.size() == 6u);
    Require(outdoor.Lights.size() == 1u);

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
        lease.TrackMaterial(Rendering::MaterialHandle{9});
        guard.Commit();
    }
    Require(releaser.Events.empty());
    Require(!lease.IsEmpty());
    lease.Release();
    Require(releaser.Events.size() == 3u);
    RequireEvent(releaser, 0, FakeReleaser::MaterialEventMask | 9);
    RequireEvent(releaser, 1, 2);
    RequireEvent(releaser, 2, 1);
    lease.Release();
    Require(releaser.Events.size() == 3u);
    return 0;
}
