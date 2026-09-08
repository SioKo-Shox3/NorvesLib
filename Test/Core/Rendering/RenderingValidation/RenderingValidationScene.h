#pragma once

#include "RenderingValidation/GpuTestEnvironment.h"
#include "Component/Component.h"
#include "Container/Containers.h"
#include "Math/Vector3.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneProxy.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
    class World;

    namespace Component
    {
        class MeshComponent;
    }
}

namespace NorvesLib::Core::Rendering
{
    class RenderResources;
    class RenderWorld;
}

namespace NorvesLib::Test::RenderingValidation
{
    struct RenderingValidationSceneContractTestAccess;

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

    enum class MaterialKind : uint8_t
    {
        NeutralOpaque,
        EmissiveOpaque,
        LegacyTransparent
    };

    enum class SceneLightKind : uint8_t
    {
        Point,
        Directional
    };

    struct SceneObjectSpec
    {
        ScenePrimitiveKind Primitive = ScenePrimitiveKind::Plane;
        MaterialKind Material = MaterialKind::NeutralOpaque;
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

    enum class P4Scenario : uint8_t
    {
        Raw250TextureRepresentation,
        Raw251DfgLut,
        Raw252RoughnessSweep,
        Raw252TargetNotOne,
        Raw252WhiteFurnace,
        Raw254DirectConductorEndpoint
    };

    struct P4ScenarioRow
    {
        P4Scenario Scenario = P4Scenario::Raw250TextureRepresentation;
        uint32_t RowIndex = 0u;
    };

    inline constexpr uint32_t R1RoiMinX = 112u;
    inline constexpr uint32_t R1RoiMaxX = 143u;
    inline constexpr uint32_t R1RoiMinY = 112u;
    inline constexpr uint32_t R1RoiMaxY = 143u;
    inline constexpr uint32_t R1AnchorX = 127u;
    inline constexpr uint32_t R1AnchorY = 127u;
    inline constexpr float R1PlaneScale = 0.0025f;
    inline constexpr double R1CameraTargetX = 0.000195312500232831;
    inline constexpr double R1CameraTargetY = -0.000195312500698492;

    struct RenderingValidationRunConfig
    {
        SceneKind Scene = SceneKind::Indoor;
        uint32_t Seed = ValidationSeed;
        Core::Rendering::FrameCaptureSourceKind CaptureSource = Core::Rendering::FrameCaptureSourceKind::PresentationColor;
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
        virtual void ReleaseTexture(Core::Rendering::TextureHandle handle) noexcept = 0;
        virtual void ReleaseMaterial(Core::Rendering::MaterialHandle handle) noexcept = 0;
    };

    class SceneFixtureResourceLease
    {
    public:
        void Bind(ISceneFixtureResourceReleaser* releaser) noexcept;
        void TrackMesh(Core::Rendering::MeshDataHandle handle);
        void TrackTexture(Core::Rendering::TextureHandle handle);
        void TrackMaterial(Core::Rendering::MaterialHandle handle);
        void Release() noexcept;
        bool IsEmpty() const noexcept;
        size_t TrackedMeshCount() const noexcept;
        size_t TrackedTextureCount() const noexcept;
        size_t TrackedMaterialCount() const noexcept;

    private:
        friend struct RenderingValidationSceneContractTestAccess;
        void Reserve(size_t meshCapacity, size_t textureCapacity, size_t materialCapacity);

        friend class RenderingValidationSceneFixture;

        ISceneFixtureResourceReleaser* m_pReleaser = nullptr;
        Core::Container::VariableArray<Core::Rendering::MeshDataHandle> m_Meshes;
        Core::Container::VariableArray<Core::Rendering::TextureHandle> m_Textures;
        Core::Container::VariableArray<Core::Rendering::MaterialHandle> m_Materials;
    };

    class RenderingValidationSceneFixture;
    struct SceneFixtureInitializationState;

    class SceneFixtureInitializationGuard
    {
    public:
        explicit SceneFixtureInitializationGuard(SceneFixtureResourceLease* lease) noexcept;
        ~SceneFixtureInitializationGuard() noexcept;
        void BindObjects(Core::World* world,
                         Core::Container::VariableArray<Core::Entity*>* objects) noexcept;
        void BindPublication(RenderingValidationSceneFixture* fixture,
                             SceneFixtureInitializationState* state,
                             ISceneFixtureResourceReleaser* stableOwner) noexcept;
        void TrackObject(Core::Entity* object);
        void Commit() noexcept;

    private:
        SceneFixtureResourceLease* m_pLease = nullptr;
        Core::World* m_pWorld = nullptr;
        Core::Container::VariableArray<Core::Entity*>* m_pObjects = nullptr;
        RenderingValidationSceneFixture* m_pFixture = nullptr;
        SceneFixtureInitializationState* m_pState = nullptr;
        ISceneFixtureResourceReleaser* m_pStableOwner = nullptr;
        bool m_bCommitted = false;
    };

    struct SceneFixtureInitializationState
    {
        Core::World* pWorld = nullptr;
        Core::Rendering::RenderResources* pResources = nullptr;
        FixedStepSentinelComponent* pSentinel = nullptr;
        Core::Component::MeshComponent* pP4PlaneMesh = nullptr;
        Core::Component::MeshComponent* pP4SphereMesh = nullptr;
        Core::Component::MeshComponent* pEmissiveMesh = nullptr;
        Core::Component::MeshComponent* pTransparentMesh = nullptr;
        Core::Entity* pP4LightEntity = nullptr;
        SceneFixtureResourceLease Lease;
        SceneLayout Layout;
        Core::Container::VariableArray<Core::Entity*> Objects;
        Core::Container::FixedArray<Core::Rendering::MaterialHandle, 30> P4Materials;
    };

    enum class TransparentPhysicalLightingRow : uint8_t
    {
        DirectM0Off,
        DirectM0On,
        DirectM05Off,
        DirectM05On,
        ShadowUnshadowed,
        Shadowed,
        IblM0Off,
        IblM0On,
        IblM05Off,
        IblM05On,
        Complete
    };

    inline constexpr uint32_t TransparentPhysicalLightingRowCount = 10u;

    class RenderingValidationSceneFixture final : private ISceneFixtureResourceReleaser
    {
    public:
        bool Initialize(Core::World& world,
                        Core::Rendering::RenderResources& resources,
                        SceneKind kind,
                        uint32_t seed);
        void Shutdown(Core::Rendering::RenderResources& resources);
        void ApplyCamera(Core::Rendering::RenderWorld& renderWorld) const;
        bool ApplyP4ScenarioRow(const P4ScenarioRow& row) const;
        bool ApplyTransparentPhysicalLightingRow(uint32_t rowIndex) const;
        bool ApplyTransparentPhysicalLightingObjectPresence() const;
        const Core::Rendering::CameraProxy& GetCamera() const;
        uint64_t GetObservedFixedStepCount() const;
        bool IsCaptureStateStable() const;
        size_t TrackedMeshCount() const noexcept;
        size_t TrackedTextureCount() const noexcept;
        size_t TrackedMaterialCount() const noexcept;

    private:
        friend class SceneFixtureInitializationGuard;
        friend struct RenderingValidationSceneContractTestAccess;

        void PublishInitializationState(SceneFixtureInitializationState& state,
                                        ISceneFixtureResourceReleaser* stableOwner) noexcept;
        void ShutdownPublishedInitialization(ISceneFixtureResourceReleaser* releaser) noexcept;
        bool EnsureR1PhysicalFixture(bool bObjectPresence) const;
        bool ValidateR1PhysicalFixture(bool bObjectPresence) const;

        void UnregisterMesh(Core::Rendering::MeshDataHandle handle) noexcept override;
        void ReleaseTexture(Core::Rendering::TextureHandle handle) noexcept override;
        void ReleaseMaterial(Core::Rendering::MaterialHandle handle) noexcept override;

        Core::World* m_pWorld = nullptr;
        Core::Rendering::RenderResources* m_pResources = nullptr;
        FixedStepSentinelComponent* m_pSentinel = nullptr;
        Core::Component::MeshComponent* m_pP4PlaneMesh = nullptr;
        Core::Component::MeshComponent* m_pP4SphereMesh = nullptr;
        Core::Component::MeshComponent* m_pEmissiveMesh = nullptr;
        Core::Component::MeshComponent* m_pTransparentMesh = nullptr;
        Core::Entity* m_pP4LightEntity = nullptr;
        mutable SceneFixtureResourceLease m_Lease;
        SceneLayout m_Layout;
        mutable Core::Container::VariableArray<Core::Entity*> m_Objects;
        Core::Container::FixedArray<Core::Rendering::MaterialHandle, 30> m_P4Materials;
        SceneKind m_SceneKind = SceneKind::Indoor;
        mutable bool m_bR1PhysicalFixturePrepared = false;
        mutable bool m_bR1PhysicalFixtureFailed = false;
        mutable bool m_bR1ObjectPresence = false;
        mutable Core::Rendering::CameraProxy m_R1PhysicalCamera;
        mutable Core::Entity* m_pR1TargetEntity = nullptr;
        mutable Core::Entity* m_pR1BackgroundEntity = nullptr;
        mutable Core::Entity* m_pR1OccluderEntity = nullptr;
        mutable Core::Component::MeshComponent* m_pR1TargetMesh = nullptr;
        mutable Core::Component::MeshComponent* m_pR1BackgroundMesh = nullptr;
        mutable Core::Component::MeshComponent* m_pR1OccluderMesh = nullptr;
        mutable Core::Entity* m_pR1PointLightEntity = nullptr;
        mutable Core::Entity* m_pR1DirectionalLightEntity = nullptr;
        mutable Core::Rendering::MeshDataHandle m_R1ScreenPlaneHandle =
            Core::Rendering::MeshDataHandle::Invalid();
        mutable Core::Container::FixedArray<Core::Rendering::TextureHandle, 9> m_R1Textures;
        mutable Core::Container::FixedArray<Core::Rendering::MaterialHandle, 2> m_R1TargetMaterials;
        mutable Core::Rendering::MaterialHandle m_R1BackgroundMaterial =
            Core::Rendering::MaterialHandle::Invalid();
        mutable Core::Rendering::MaterialHandle m_R1OccluderMaterial =
            Core::Rendering::MaterialHandle::Invalid();
        bool m_bPublished = false;
    };
}
