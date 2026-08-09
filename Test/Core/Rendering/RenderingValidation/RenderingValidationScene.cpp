#include "RenderingValidation/RenderingValidationScene.h"

#include "RenderingValidation/GpuTestEnvironment.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Math/MathTypes.h"
#include "Math/QuaternionUtils.h"
#include "Math/VectorUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Random/Random.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        using Core::Rendering::CameraProxy;
        using Core::Rendering::MaterialHandle;
        using Core::Rendering::MeshDataHandle;

        constexpr MeshDataHandle PlaneHandle{0x52300001u};
        constexpr MeshDataHandle SphereHandle{0x52300002u};

        bool EqualFloatArray(const float* left, const float* right, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
            {
                if (left[index] != right[index])
                {
                    return false;
                }
            }
            return true;
        }

        bool EqualObject(const SceneObjectSpec& left, const SceneObjectSpec& right)
        {
            return left.Primitive == right.Primitive &&
                   EqualFloatArray(left.Position, right.Position, 3) &&
                   EqualFloatArray(left.RotationEulerDegrees, right.RotationEulerDegrees, 3) &&
                   EqualFloatArray(left.Scale, right.Scale, 3) &&
                   EqualFloatArray(left.Color, right.Color, 4);
        }

        bool EqualLight(const SceneLightSpec& left, const SceneLightSpec& right)
        {
            return left.Kind == right.Kind &&
                   EqualFloatArray(left.PositionOrDirection, right.PositionOrDirection, 3) &&
                   EqualFloatArray(left.Color, right.Color, 3) && left.Intensity == right.Intensity &&
                   left.Range == right.Range && left.bCastShadows == right.bCastShadows;
        }

        bool EqualCamera(const CameraProxy& left, const CameraProxy& right)
        {
            return left.CameraId == right.CameraId && left.PositionX == right.PositionX &&
                   left.PositionY == right.PositionY && left.PositionZ == right.PositionZ &&
                   left.ForwardX == right.ForwardX && left.ForwardY == right.ForwardY &&
                   left.ForwardZ == right.ForwardZ && left.UpX == right.UpX && left.UpY == right.UpY &&
                   left.UpZ == right.UpZ && left.RightX == right.RightX && left.RightY == right.RightY &&
                   left.RightZ == right.RightZ && left.Projection == right.Projection &&
                   left.FieldOfView == right.FieldOfView && left.AspectRatio == right.AspectRatio &&
                   left.NearPlane == right.NearPlane && left.FarPlane == right.FarPlane &&
                   left.OrthoWidth == right.OrthoWidth && left.OrthoHeight == right.OrthoHeight &&
                   left.Viewport.X == right.Viewport.X && left.Viewport.Y == right.Viewport.Y &&
                   left.Viewport.Width == right.Viewport.Width && left.Viewport.Height == right.Viewport.Height &&
                   left.Viewport.MinDepth == right.Viewport.MinDepth &&
                   left.Viewport.MaxDepth == right.Viewport.MaxDepth && left.CullingMask == right.CullingMask &&
                   left.RenderOrder == right.RenderOrder;
        }

        SceneObjectSpec MakeObject(ScenePrimitiveKind primitive,
                                   float px, float py, float pz,
                                   float rx, float ry, float rz,
                                   float sx, float sy, float sz,
                                   float red, float green, float blue, float alpha)
        {
            SceneObjectSpec object;
            object.Primitive = primitive;
            object.Position[0] = px;
            object.Position[1] = py;
            object.Position[2] = pz;
            object.RotationEulerDegrees[0] = rx;
            object.RotationEulerDegrees[1] = ry;
            object.RotationEulerDegrees[2] = rz;
            object.Scale[0] = sx;
            object.Scale[1] = sy;
            object.Scale[2] = sz;
            object.Color[0] = red;
            object.Color[1] = green;
            object.Color[2] = blue;
            object.Color[3] = alpha;
            return object;
        }

        void BuildIndoorLayout(SceneLayout& layout)
        {
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Plane, 0.0f, -2.0f, 0.0f,
                                                0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                                0.60f, 0.60f, 0.60f, 1.0f));
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Plane, 0.0f, 3.0f, 0.0f,
                                                180.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                                0.70f, 0.70f, 0.70f, 1.0f));
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Plane, 0.0f, 0.5f, -5.0f,
                                                90.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                                0.70f, 0.70f, 0.70f, 1.0f));
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Plane, -5.0f, 0.5f, 0.0f,
                                                0.0f, 0.0f, -90.0f, 1.0f, 1.0f, 1.0f,
                                                0.65f, 0.15f, 0.15f, 1.0f));
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Plane, 5.0f, 0.5f, 0.0f,
                                                0.0f, 0.0f, 90.0f, 1.0f, 1.0f, 1.0f,
                                                0.15f, 0.65f, 0.15f, 1.0f));
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Sphere, -1.5f, -1.0f, 0.0f,
                                                0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                                0.80f, 0.20f, 0.15f, 1.0f));
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Sphere, 1.5f, -1.25f, -1.0f,
                                                0.0f, 0.0f, 0.0f, 0.75f, 0.75f, 0.75f,
                                                0.20f, 0.35f, 0.85f, 1.0f));

            SceneLightSpec light;
            light.Kind = SceneLightKind::Point;
            light.PositionOrDirection[0] = 0.0f;
            light.PositionOrDirection[1] = 2.0f;
            light.PositionOrDirection[2] = 1.0f;
            light.Color[0] = 1.0f;
            light.Color[1] = 0.90f;
            light.Color[2] = 0.75f;
            light.Intensity = 4.0f;
            light.Range = 12.0f;
            light.bCastShadows = true;
            layout.Lights.push_back(light);
            layout.Camera = BuildLookAtCamera(Math::Vector3(0.0f, 0.0f, 7.0f), Math::Vector3::Zero,
                                              ValidationWidth, ValidationHeight);
        }

        void BuildOutdoorLayout(Random::Generator& random, SceneLayout& layout)
        {
            layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Plane, 0.0f, -1.0f, 0.0f,
                                                0.0f, 0.0f, 0.0f, 2.0f, 1.0f, 2.0f,
                                                0.30f, 0.45f, 0.25f, 1.0f));
            constexpr float Colors[5][4] = {
                {0.85f, 0.30f, 0.20f, 1.0f},
                {0.20f, 0.55f, 0.90f, 1.0f},
                {0.80f, 0.70f, 0.20f, 1.0f},
                {0.45f, 0.80f, 0.35f, 1.0f},
                {0.70f, 0.35f, 0.80f, 1.0f}};
            for (size_t index = 0; index < 5; ++index)
            {
                const float x = random.GetFloat(-4.0f, 4.0f);
                const float z = random.GetFloat(-4.0f, 4.0f);
                const float radius = random.GetFloat(0.35f, 0.90f);
                layout.Objects.push_back(MakeObject(ScenePrimitiveKind::Sphere, x, -1.0f + radius, z,
                                                    0.0f, 0.0f, 0.0f, radius, radius, radius,
                                                    Colors[index][0], Colors[index][1], Colors[index][2],
                                                    Colors[index][3]));
            }

            SceneLightSpec light;
            light.Kind = SceneLightKind::Directional;
            const Math::Vector3 direction = Math::VectorUtils::Normalize(Math::Vector3(-0.4f, -1.0f, -0.25f));
            light.PositionOrDirection[0] = direction.x;
            light.PositionOrDirection[1] = direction.y;
            light.PositionOrDirection[2] = direction.z;
            light.Color[0] = 1.0f;
            light.Color[1] = 0.95f;
            light.Color[2] = 0.85f;
            light.Intensity = 2.0f;
            light.bCastShadows = true;
            layout.Lights.push_back(light);
            layout.Camera = BuildLookAtCamera(Math::Vector3(7.0f, 5.0f, 9.0f), Math::Vector3::Zero,
                                              ValidationWidth, ValidationHeight);
        }
    }

    bool SceneLayout::operator==(const SceneLayout& other) const
    {
        if (Objects.size() != other.Objects.size() || Lights.size() != other.Lights.size() ||
            !EqualCamera(Camera, other.Camera))
        {
            return false;
        }
        for (size_t index = 0; index < Objects.size(); ++index)
        {
            if (!EqualObject(Objects[index], other.Objects[index]))
            {
                return false;
            }
        }
        for (size_t index = 0; index < Lights.size(); ++index)
        {
            if (!EqualLight(Lights[index], other.Lights[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool BuildSceneLayout(SceneKind kind, uint32_t seed, SceneLayout& outLayout)
    {
        outLayout = {};
        Random::Generator random(seed);
        if (kind == SceneKind::Indoor)
        {
            BuildIndoorLayout(outLayout);
            return true;
        }
        if (kind == SceneKind::Outdoor)
        {
            BuildOutdoorLayout(random, outLayout);
            return true;
        }
        return false;
    }

    CameraProxy BuildLookAtCamera(const Math::Vector3& position,
                                  const Math::Vector3& target,
                                  uint32_t width,
                                  uint32_t height)
    {
        const Math::Vector3 forward = Math::VectorUtils::Normalize(target - position);
        const Math::Vector3 right = Math::VectorUtils::Normalize(Math::VectorUtils::Cross(forward, Math::Vector3::Up));
        const Math::Vector3 up = Math::VectorUtils::Normalize(Math::VectorUtils::Cross(right, forward));

        CameraProxy camera;
        camera.CameraId = 1;
        camera.PositionX = position.x;
        camera.PositionY = position.y;
        camera.PositionZ = position.z;
        camera.ForwardX = forward.x;
        camera.ForwardY = forward.y;
        camera.ForwardZ = forward.z;
        camera.RightX = right.x;
        camera.RightY = right.y;
        camera.RightZ = right.z;
        camera.UpX = up.x;
        camera.UpY = up.y;
        camera.UpZ = up.z;
        camera.Projection = Core::Rendering::ProjectionType::Perspective;
        camera.FieldOfView = 60.0f;
        camera.AspectRatio = static_cast<float>(width) / static_cast<float>(height);
        camera.NearPlane = 0.1f;
        camera.FarPlane = 100.0f;
        camera.Viewport = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        return camera;
    }

    void FixedStepSentinelComponent::FixedTick(float fixedDeltaTime)
    {
        if (fixedDeltaTime != 1.0f / 60.0f)
        {
            m_bFixedDeltaValid = false;
            return;
        }
        if (m_ObservedSteps < ValidationWarmupFixedSteps)
        {
            ++m_ObservedSteps;
        }
    }

    uint64_t FixedStepSentinelComponent::GetObservedSteps() const
    {
        return m_ObservedSteps;
    }

    bool FixedStepSentinelComponent::IsFixedDeltaValid() const
    {
        return m_bFixedDeltaValid;
    }

    void SceneFixtureResourceLease::Bind(ISceneFixtureResourceReleaser* releaser) noexcept
    {
        m_pReleaser = releaser;
    }

    void SceneFixtureResourceLease::Reserve(size_t meshCapacity, size_t materialCapacity)
    {
        m_Meshes.reserve(meshCapacity);
        m_Materials.reserve(materialCapacity);
    }

    void SceneFixtureResourceLease::TrackMesh(MeshDataHandle handle)
    {
        m_Meshes.push_back(handle);
    }

    void SceneFixtureResourceLease::TrackMaterial(MaterialHandle handle)
    {
        m_Materials.push_back(handle);
    }

    void SceneFixtureResourceLease::Release() noexcept
    {
        if (m_pReleaser != nullptr)
        {
            for (size_t index = m_Materials.size(); index > 0; --index)
            {
                m_pReleaser->ReleaseMaterial(m_Materials[index - 1]);
            }
            for (size_t index = m_Meshes.size(); index > 0; --index)
            {
                m_pReleaser->UnregisterMesh(m_Meshes[index - 1]);
            }
        }
        m_Materials.clear();
        m_Meshes.clear();
    }

    bool SceneFixtureResourceLease::IsEmpty() const noexcept
    {
        return m_Meshes.empty() && m_Materials.empty();
    }

    SceneFixtureInitializationGuard::SceneFixtureInitializationGuard(SceneFixtureResourceLease* lease) noexcept
        : m_pLease(lease)
    {
    }

    SceneFixtureInitializationGuard::~SceneFixtureInitializationGuard() noexcept
    {
        if (!m_bCommitted && m_pLease != nullptr)
        {
            m_pLease->Release();
        }
    }

    void SceneFixtureInitializationGuard::Commit() noexcept
    {
        m_bCommitted = true;
    }

    bool RenderingValidationSceneFixture::Initialize(Core::World& world,
                                                     Core::Rendering::RenderResources& resources,
                                                     SceneKind kind,
                                                     uint32_t seed)
    {
        Shutdown(resources);
        if (!BuildSceneLayout(kind, seed, m_Layout))
        {
            return false;
        }

        m_Lease.Reserve(2, 1);
        m_pResources = &resources;
        m_Lease.Bind(this);
        SceneFixtureInitializationGuard guard(&m_Lease);

        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> planeVertices;
        Core::Container::VariableArray<uint32_t> planeIndices;
        Core::Rendering::ProceduralMeshGenerator::GeneratePlane(
            10.0f, 10.0f, 1, 1, planeVertices, planeIndices);
        if (!resources.Meshes().Register(PlaneHandle, planeVertices.data(),
                                         planeVertices.size() * sizeof(Core::Rendering::Mesh3DVertex),
                                         planeIndices.data(), static_cast<uint32_t>(planeIndices.size())))
        {
            return false;
        }
        m_Lease.TrackMesh(PlaneHandle);

        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> sphereVertices;
        Core::Container::VariableArray<uint32_t> sphereIndices;
        Core::Rendering::ProceduralMeshGenerator::GenerateUVSphere(
            1.0f, 24, 12, sphereVertices, sphereIndices);
        if (!resources.Meshes().Register(SphereHandle, sphereVertices.data(),
                                         sphereVertices.size() * sizeof(Core::Rendering::Mesh3DVertex),
                                         sphereIndices.data(), static_cast<uint32_t>(sphereIndices.size())))
        {
            return false;
        }
        m_Lease.TrackMesh(SphereHandle);

        Core::Rendering::MaterialCreateData materialData;
        materialData.DebugName = TEXT("RenderingValidationNeutral");
        materialData.bTwoSided = true;
        const MaterialHandle material = resources.Materials().Create(materialData);
        if (!material.IsValid())
        {
            return false;
        }
        m_Lease.TrackMaterial(material);

        for (const SceneObjectSpec& object : m_Layout.Objects)
        {
            Core::Entity* entity = world.SpawnEntity();
            if (entity == nullptr)
            {
                return false;
            }
            entity->SetPosition(object.Position[0], object.Position[1], object.Position[2]);
            const float radians = Math::Constants::PI / 180.0f;
            entity->SetRotation(Math::QuaternionUtils::FromEulerAngles(Math::Vector3(
                object.RotationEulerDegrees[0] * radians,
                object.RotationEulerDegrees[1] * radians,
                object.RotationEulerDegrees[2] * radians)));
            entity->SetScale(object.Scale[0], object.Scale[1], object.Scale[2]);

            Core::Component::MeshComponent* mesh = world.CreateComponent<Core::Component::MeshComponent>(entity);
            if (mesh == nullptr)
            {
                return false;
            }
            mesh->SetMeshHandle(object.Primitive == ScenePrimitiveKind::Plane ? PlaneHandle : SphereHandle);
            mesh->SetMaterial(0, material);
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                mesh->SetCustomData(channel, object.Color[channel]);
            }
        }

        for (const SceneLightSpec& light : m_Layout.Lights)
        {
            Core::Entity* entity = world.SpawnEntity();
            if (entity == nullptr)
            {
                return false;
            }
            Core::Component::LightComponent* component = nullptr;
            if (light.Kind == SceneLightKind::Point)
            {
                entity->SetPosition(light.PositionOrDirection[0], light.PositionOrDirection[1],
                                    light.PositionOrDirection[2]);
                auto* point = world.CreateComponent<Core::Component::PointLightComponent>(entity);
                if (point != nullptr)
                {
                    point->SetRange(light.Range);
                }
                component = point;
            }
            else
            {
                auto* directional = world.CreateComponent<Core::Component::DirectionalLightComponent>(entity);
                if (directional != nullptr)
                {
                    directional->SetLightDirection(light.PositionOrDirection[0], light.PositionOrDirection[1],
                                                   light.PositionOrDirection[2]);
                }
                component = directional;
            }
            if (component == nullptr)
            {
                return false;
            }
            component->SetLightColor(light.Color[0], light.Color[1], light.Color[2]);
            component->SetIntensity(light.Intensity);
            component->SetCastShadows(light.bCastShadows);
        }

        Core::Entity* sentinelEntity = world.SpawnEntity();
        if (sentinelEntity == nullptr)
        {
            return false;
        }
        m_pSentinel = world.CreateComponent<FixedStepSentinelComponent>(sentinelEntity);
        if (m_pSentinel == nullptr)
        {
            return false;
        }

        guard.Commit();
        return true;
    }

    void RenderingValidationSceneFixture::Shutdown(Core::Rendering::RenderResources& resources)
    {
        (void)resources;
        m_Lease.Release();
        m_pSentinel = nullptr;
        m_pResources = nullptr;
        m_Layout = {};
    }

    void RenderingValidationSceneFixture::ApplyCamera(Core::Rendering::RenderWorld& renderWorld) const
    {
        renderWorld.SetMainCamera(m_Layout.Camera);
    }

    uint64_t RenderingValidationSceneFixture::GetObservedFixedStepCount() const
    {
        return m_pSentinel != nullptr ? m_pSentinel->GetObservedSteps() : 0;
    }

    bool RenderingValidationSceneFixture::IsCaptureStateStable() const
    {
        return m_pSentinel != nullptr && m_pSentinel->IsFixedDeltaValid() &&
               m_pSentinel->GetObservedSteps() == ValidationWarmupFixedSteps;
    }

    void RenderingValidationSceneFixture::UnregisterMesh(MeshDataHandle handle) noexcept
    {
        if (m_pResources != nullptr)
        {
            m_pResources->Meshes().Unregister(handle);
        }
    }

    void RenderingValidationSceneFixture::ReleaseMaterial(MaterialHandle handle) noexcept
    {
        if (m_pResources != nullptr)
        {
            m_pResources->Materials().Release(handle);
        }
    }
}
