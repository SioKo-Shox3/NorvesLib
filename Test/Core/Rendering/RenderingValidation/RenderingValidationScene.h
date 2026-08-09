#pragma once

#include "RenderingValidation/GpuTestEnvironment.h"
#include "Component/Component.h"
#include "Container/Containers.h"
#include "Math/Vector3.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneProxy.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Core::Rendering
{
    class RenderResources;
    class RenderWorld;
}

namespace NorvesLib::Test::RenderingValidation
{
    enum class SceneKind : uint8_t
    {
        Indoor,
        Outdoor
    };

    enum class ScenePrimitiveKind : uint8_t
    {
        Plane,
        Sphere
    };

    enum class SceneLightKind : uint8_t
    {
        Point,
        Directional
    };

    struct SceneObjectSpec
    {
        ScenePrimitiveKind Primitive = ScenePrimitiveKind::Plane;
        float Position[3] = {};
        float RotationEulerDegrees[3] = {};
        float Scale[3] = {1.0f, 1.0f, 1.0f};
        float Color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct SceneLightSpec
    {
        SceneLightKind Kind = SceneLightKind::Point;
        float PositionOrDirection[3] = {};
        float Color[3] = {1.0f, 1.0f, 1.0f};
        float Intensity = 1.0f;
        float Range = 10.0f;
        bool bCastShadows = true;
    };

    struct SceneLayout
    {
        Core::Container::VariableArray<SceneObjectSpec> Objects;
        Core::Container::VariableArray<SceneLightSpec> Lights;
        Core::Rendering::CameraProxy Camera;

        bool operator==(const SceneLayout& other) const;
    };

    struct RenderingValidationRunConfig
    {
        SceneKind Scene = SceneKind::Indoor;
        uint32_t Seed = ValidationSeed;
    };

    bool BuildSceneLayout(SceneKind kind, uint32_t seed, SceneLayout& outLayout);
    Core::Rendering::CameraProxy BuildLookAtCamera(
        const Math::Vector3& position,
        const Math::Vector3& target,
        uint32_t width,
        uint32_t height);

    class FixedStepSentinelComponent final : public Core::Component::Component
    {
    public:
        void FixedTick(float fixedDeltaTime) override;
        uint64_t GetObservedSteps() const;
        bool IsFixedDeltaValid() const;

    private:
        uint64_t m_ObservedSteps = 0;
        bool m_bFixedDeltaValid = true;
    };

    class ISceneFixtureResourceReleaser
    {
    public:
        virtual ~ISceneFixtureResourceReleaser() = default;
        virtual void UnregisterMesh(Core::Rendering::MeshDataHandle handle) noexcept = 0;
        virtual void ReleaseMaterial(Core::Rendering::MaterialHandle handle) noexcept = 0;
    };

    class SceneFixtureResourceLease
    {
    public:
        void Bind(ISceneFixtureResourceReleaser* releaser) noexcept;
        void TrackMesh(Core::Rendering::MeshDataHandle handle);
        void TrackMaterial(Core::Rendering::MaterialHandle handle);
        void Release() noexcept;
        bool IsEmpty() const noexcept;

    private:
        void Reserve(size_t meshCapacity, size_t materialCapacity);

        friend class RenderingValidationSceneFixture;

        ISceneFixtureResourceReleaser* m_pReleaser = nullptr;
        Core::Container::VariableArray<Core::Rendering::MeshDataHandle> m_Meshes;
        Core::Container::VariableArray<Core::Rendering::MaterialHandle> m_Materials;
    };

    class SceneFixtureInitializationGuard
    {
    public:
        explicit SceneFixtureInitializationGuard(SceneFixtureResourceLease* lease) noexcept;
        ~SceneFixtureInitializationGuard() noexcept;
        void Commit() noexcept;

    private:
        SceneFixtureResourceLease* m_pLease = nullptr;
        bool m_bCommitted = false;
    };

    class RenderingValidationSceneFixture final : private ISceneFixtureResourceReleaser
    {
    public:
        bool Initialize(Core::World& world,
                        Core::Rendering::RenderResources& resources,
                        SceneKind kind,
                        uint32_t seed);
        void Shutdown(Core::Rendering::RenderResources& resources);
        void ApplyCamera(Core::Rendering::RenderWorld& renderWorld) const;
        uint64_t GetObservedFixedStepCount() const;
        bool IsCaptureStateStable() const;

    private:
        void UnregisterMesh(Core::Rendering::MeshDataHandle handle) noexcept override;
        void ReleaseMaterial(Core::Rendering::MaterialHandle handle) noexcept override;

        Core::Rendering::RenderResources* m_pResources = nullptr;
        FixedStepSentinelComponent* m_pSentinel = nullptr;
        SceneFixtureResourceLease m_Lease;
        SceneLayout m_Layout;
    };
}
