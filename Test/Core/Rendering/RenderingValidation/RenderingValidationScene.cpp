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

#include <cmath>
#include <cstdint>
#include <utility>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        using Core::Rendering::CameraProxy;
        using Core::Rendering::MaterialHandle;
        using Core::Rendering::MeshDataHandle;
        using Core::Rendering::RenderResources;
        using Core::Rendering::TextureHandle;

        constexpr MeshDataHandle PlaneHandle{0x52300001u};
        constexpr MeshDataHandle SphereHandle{0x52300002u};
        constexpr uint32_t P4RoughnessCount = 5u;
        constexpr uint32_t P4MetallicQueryTextureCount = 6u;
        constexpr uint32_t P4MaterialCount = P4RoughnessCount * P4MetallicQueryTextureCount;
        constexpr uint32_t P4FurnaceMetallicCount = 3u;
        constexpr uint32_t P4TargetMetallicIndex = 2u;
        constexpr uint32_t P4DirectRoughnessIndex = 4u;
        constexpr uint32_t P4DirectMaterialIndex = 26u;
        constexpr float P4RoughnessValues[P4RoughnessCount] = {
            0.05f,
            0.25f,
            0.50f,
            0.75f,
            1.00f};
        constexpr float P4MetallicQueryValues[P4MetallicQueryTextureCount] = {
            0.0f,
            0.5f,
            1.0f,
            0.1f,
            0.25f,
            0.75f};
        constexpr uint32_t P4DfgQueryIndices[5] = {3u, 4u, 1u, 5u, 2u};
        constexpr bool AreP4QueryValuesDistinct()
        {
            for (uint32_t first = 0u; first < P4MetallicQueryTextureCount; ++first)
            {
                for (uint32_t second = first + 1u;
                     second < P4MetallicQueryTextureCount;
                     ++second)
                {
                    if (P4MetallicQueryValues[first] == P4MetallicQueryValues[second])
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        static_assert(P4DfgQueryIndices[0] == 3u && P4DfgQueryIndices[1] == 4u &&
                          P4DfgQueryIndices[2] == 1u && P4DfgQueryIndices[3] == 5u &&
                          P4DfgQueryIndices[4] == 2u,
                      "P4 DFG query axis must map to the five distinct query textures");
        static_assert(AreP4QueryValuesDistinct(),
                      "P4 query texture values must remain distinct");
        static_assert(P4DirectMaterialIndex ==
                          P4DirectRoughnessIndex * P4MetallicQueryTextureCount + P4TargetMetallicIndex,
                      "P4 direct conductor material index must remain 26");

        class RenderingValidationResourceReleaser final : public ISceneFixtureResourceReleaser
        {
        public:
            explicit RenderingValidationResourceReleaser(RenderResources* resources) noexcept
                : m_pResources(resources)
            {
            }

            void UnregisterMesh(MeshDataHandle handle) noexcept override
            {
                if (m_pResources != nullptr)
                {
                    m_pResources->Meshes().Unregister(handle);
                }
            }

            void ReleaseTexture(TextureHandle handle) noexcept override
            {
                if (m_pResources != nullptr)
                {
                    m_pResources->Textures().ReleaseTexture(handle);
                }
            }

            void ReleaseMaterial(MaterialHandle handle) noexcept override
            {
                if (m_pResources != nullptr)
                {
                    m_pResources->Materials().Release(handle);
                }
            }

        private:
            RenderResources* m_pResources = nullptr;
        };

        TextureHandle CreateScalarTexture(RenderResources& resources,
                                           float value,
                                           const TCHAR* debugName)
        {
            Core::Rendering::TextureCreateInfo textureInfo;
            textureInfo.Width = 1u;
            textureInfo.Height = 1u;
            textureInfo.PixelFormat = Core::Rendering::TextureCreateInfo::Format::RGBA32_FLOAT;
            textureInfo.DebugName = debugName;
            const double doubleValue = static_cast<double>(value);
            const double clampedValue =
                doubleValue < 0.0 ? 0.0 : doubleValue > 1.0 ? 1.0 : doubleValue;
            const double quantizedValue =
                std::floor(clampedValue * 255.0 + 0.5) / 255.0;
            const float canonicalValue = static_cast<float>(quantizedValue);
            const float pixels[4] = {canonicalValue, canonicalValue, canonicalValue, 1.0f};
            return resources.Textures().CreateTexture(textureInfo, pixels, sizeof(pixels));
        }

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
            return left.Primitive == right.Primitive && left.Material == right.Material &&
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
            SceneObjectSpec neutral = MakeObject(ScenePrimitiveKind::Plane, 0.0f, 0.0f, 0.0f,
                                                  90.0f, 0.0f, 0.0f,
                                                  R1PlaneScale, R1PlaneScale, R1PlaneScale,
                                                  0.50f, 0.50f, 0.50f, 1.0f);
            neutral.Material = MaterialKind::NeutralOpaque;
            layout.Objects.push_back(neutral);

            SceneObjectSpec emissive = MakeObject(ScenePrimitiveKind::Plane, 0.035f, 0.0f, 0.0f,
                                                  90.0f, 0.0f, 0.0f, 0.0005f, 0.0005f, 0.0005f,
                                                  1.0f, 1.0f, 1.0f, 1.0f);
            emissive.Material = MaterialKind::EmissiveOpaque;
            layout.Objects.push_back(emissive);

            SceneObjectSpec transparent = MakeObject(ScenePrimitiveKind::Plane, -0.035f, 0.0f, 0.0f,
                                                      90.0f, 0.0f, 0.0f, 0.0005f, 0.0005f, 0.0005f,
                                                      1.0f, 1.0f, 1.0f, 0.5f);
            transparent.Material = MaterialKind::LegacyTransparent;
            layout.Objects.push_back(transparent);

            SceneLightSpec light;
            light.Kind = SceneLightKind::Point;
            light.PositionOrDirection[0] = 0.0f;
            light.PositionOrDirection[1] = 0.0f;
            light.PositionOrDirection[2] = 2.0f;
            light.Color[0] = 1.0f;
            light.Color[1] = 1.0f;
            light.Color[2] = 1.0f;
            light.Intensity = 100.0f;
            light.Range = 1000.0f;
            light.bCastShadows = false;
            layout.Lights.push_back(light);
            layout.Camera = BuildLookAtCamera(
                Math::Vector3(0.0f, 0.0f, 4.0f),
                Math::Vector3(static_cast<float>(R1CameraTargetX),
                              static_cast<float>(R1CameraTargetY),
                              0.0f),
                ValidationWidth, ValidationHeight);
            layout.Camera.Projection = Core::Rendering::ProjectionType::Orthographic;
            layout.Camera.OrthoWidth = 0.1f;
            layout.Camera.OrthoHeight = 0.1f;
            layout.Camera.NearPlane = 0.1f;
            layout.Camera.FarPlane = 10.0f;
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

    void SceneFixtureResourceLease::Reserve(size_t meshCapacity,
                                            size_t textureCapacity,
                                            size_t materialCapacity)
    {
        m_Meshes.reserve(meshCapacity);
        m_Textures.reserve(textureCapacity);
        m_Materials.reserve(materialCapacity);
    }

    void SceneFixtureResourceLease::TrackMesh(MeshDataHandle handle)
    {
        m_Meshes.push_back(handle);
    }

    void SceneFixtureResourceLease::TrackTexture(Core::Rendering::TextureHandle handle)
    {
        m_Textures.push_back(handle);
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
            for (size_t index = m_Textures.size(); index > 0; --index)
            {
                m_pReleaser->ReleaseTexture(m_Textures[index - 1]);
            }
            for (size_t index = m_Meshes.size(); index > 0; --index)
            {
                m_pReleaser->UnregisterMesh(m_Meshes[index - 1]);
            }
        }
        m_Materials.clear();
        m_Textures.clear();
        m_Meshes.clear();
    }

    bool SceneFixtureResourceLease::IsEmpty() const noexcept
    {
        return m_Meshes.empty() && m_Textures.empty() && m_Materials.empty();
    }

    size_t SceneFixtureResourceLease::TrackedMeshCount() const noexcept
    {
        return m_Meshes.size();
    }

    size_t SceneFixtureResourceLease::TrackedTextureCount() const noexcept
    {
        return m_Textures.size();
    }

    size_t SceneFixtureResourceLease::TrackedMaterialCount() const noexcept
    {
        return m_Materials.size();
    }

    SceneFixtureInitializationGuard::SceneFixtureInitializationGuard(SceneFixtureResourceLease* lease) noexcept
        : m_pLease(lease)
    {
    }

    void SceneFixtureInitializationGuard::BindObjects(
        Core::World* world,
        Core::Container::VariableArray<Core::Entity*>* objects) noexcept
    {
        m_pWorld = world;
        m_pObjects = objects;
    }

    void SceneFixtureInitializationGuard::BindPublication(
        RenderingValidationSceneFixture* fixture,
        SceneFixtureInitializationState* state,
        ISceneFixtureResourceReleaser* stableOwner) noexcept
    {
        m_pFixture = fixture;
        m_pState = state;
        m_pStableOwner = stableOwner;
    }

    void SceneFixtureInitializationGuard::TrackObject(Core::Entity* object)
    {
        if (m_pObjects != nullptr && object != nullptr)
        {
            m_pObjects->push_back(object);
        }
    }

    SceneFixtureInitializationGuard::~SceneFixtureInitializationGuard() noexcept
    {
        if (!m_bCommitted && m_pLease != nullptr)
        {
            if (m_pWorld != nullptr && m_pObjects != nullptr)
            {
                for (size_t index = m_pObjects->size(); index > 0; --index)
                {
                    m_pWorld->RemoveEntity((*m_pObjects)[index - 1]);
                }
                m_pObjects->clear();
            }
            m_pLease->Release();
        }
    }

    void SceneFixtureInitializationGuard::Commit() noexcept
    {
        if (m_pFixture != nullptr && m_pState != nullptr)
        {
            m_pFixture->PublishInitializationState(*m_pState, m_pStableOwner);
        }
        m_bCommitted = true;
    }

    void RenderingValidationSceneFixture::PublishInitializationState(
        SceneFixtureInitializationState& state,
        ISceneFixtureResourceReleaser* stableOwner) noexcept
    {
        m_pWorld = state.pWorld;
        m_pResources = state.pResources;
        m_pSentinel = state.pSentinel;
        m_pP4PlaneMesh = state.pP4PlaneMesh;
        m_pP4SphereMesh = state.pP4SphereMesh;
        m_pEmissiveMesh = state.pEmissiveMesh;
        m_pTransparentMesh = state.pTransparentMesh;
        m_pP4LightEntity = state.pP4LightEntity;
        m_Lease = std::move(state.Lease);
        state.Lease.Bind(nullptr);
        m_Lease.Bind(stableOwner);
        m_Layout = std::move(state.Layout);
        m_Objects = std::move(state.Objects);
        m_P4Materials = std::move(state.P4Materials);

        state.pWorld = nullptr;
        state.pResources = nullptr;
        state.pSentinel = nullptr;
        state.pP4PlaneMesh = nullptr;
        state.pP4SphereMesh = nullptr;
        state.pEmissiveMesh = nullptr;
        state.pTransparentMesh = nullptr;
        state.pP4LightEntity = nullptr;
        state.Lease.Bind(nullptr);
        state.Objects.clear();
        state.Layout = {};
        state.P4Materials.fill(MaterialHandle::Invalid());
        m_bPublished = true;
    }

    void RenderingValidationSceneFixture::ShutdownPublishedInitialization(
        ISceneFixtureResourceReleaser* releaser) noexcept
    {
        Core::World* pWorld = m_pWorld;
        m_pWorld = nullptr;
        m_pResources = nullptr;
        m_pSentinel = nullptr;
        m_pP4PlaneMesh = nullptr;
        m_pP4SphereMesh = nullptr;
        m_pEmissiveMesh = nullptr;
        m_pTransparentMesh = nullptr;
        m_pP4LightEntity = nullptr;
        m_bPublished = false;

        if (pWorld != nullptr)
        {
            for (size_t index = m_Objects.size(); index > 0; --index)
            {
                pWorld->RemoveEntity(m_Objects[index - 1]);
            }
        }
        m_Objects.clear();

        m_Lease.Bind(releaser);
        m_Lease.Release();
        m_Lease.Bind(nullptr);
        m_P4Materials.fill(MaterialHandle::Invalid());
        m_Layout = {};
    }

    bool RenderingValidationSceneFixture::Initialize(Core::World& world,
                                                     Core::Rendering::RenderResources& resources,
                                                     SceneKind kind,
                                                     uint32_t seed)
    {
        Shutdown(resources);
        SceneFixtureInitializationState staging;
        staging.pWorld = &world;
        staging.pResources = &resources;
        SceneLayout& layout = staging.Layout;
        if (!BuildSceneLayout(kind, seed, layout))
        {
            return false;
        }

        SceneFixtureResourceLease& lease = staging.Lease;
        lease.Reserve(2, 13, 33);
        RenderingValidationResourceReleaser releaser(&resources);
        lease.Bind(&releaser);
        Core::Container::VariableArray<Core::Entity*>& objects = staging.Objects;
        objects.reserve(layout.Objects.size() + layout.Lights.size() + 2u);
        SceneFixtureInitializationGuard guard(&lease);
        guard.BindObjects(&world, &objects);
        guard.BindPublication(this, &staging, this);

        FixedStepSentinelComponent*& pSentinel = staging.pSentinel;
        Core::Component::MeshComponent*& pP4PlaneMesh = staging.pP4PlaneMesh;
        Core::Component::MeshComponent*& pP4SphereMesh = staging.pP4SphereMesh;
        Core::Component::MeshComponent*& pEmissiveMesh = staging.pEmissiveMesh;
        Core::Component::MeshComponent*& pTransparentMesh = staging.pTransparentMesh;
        Core::Entity*& pP4LightEntity = staging.pP4LightEntity;
        Core::Container::FixedArray<MaterialHandle, 30>& p4Materials = staging.P4Materials;

        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> planeVertices;
        Core::Container::VariableArray<uint32_t> planeIndices;
        Core::Rendering::ProceduralMeshGenerator::GeneratePlane(
            10.0f, 10.0f, 1, 1, planeVertices, planeIndices);
        for (Core::Rendering::Mesh3DVertex& vertex : planeVertices)
        {
            vertex.Normal[0] = 0.0f;
            vertex.Normal[1] = 0.0f;
            vertex.Normal[2] = 1.0f;
        }
        for (size_t index = 0; index + 2 < planeIndices.size(); index += 3)
        {
            const uint32_t second = planeIndices[index + 1];
            planeIndices[index + 1] = planeIndices[index + 2];
            planeIndices[index + 2] = second;
        }
        if (!resources.Meshes().Register(PlaneHandle, planeVertices.data(),
                                         planeVertices.size() * sizeof(Core::Rendering::Mesh3DVertex),
                                         planeIndices.data(), static_cast<uint32_t>(planeIndices.size())))
        {
            return false;
        }
        lease.TrackMesh(PlaneHandle);

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
        lease.TrackMesh(SphereHandle);

        Core::Rendering::TextureCreateInfo normalTextureInfo;
        normalTextureInfo.Width = 1;
        normalTextureInfo.Height = 1;
        normalTextureInfo.PixelFormat = Core::Rendering::TextureCreateInfo::Format::RGBA16_FLOAT;
        normalTextureInfo.DebugName = TEXT("RenderingValidationExactFlatNormal");
        constexpr uint16_t normalPixels[] = {0x3800u, 0x3800u, 0x3C00u, 0x3C00u};
        const Core::Rendering::TextureHandle normalTexture = resources.Textures().CreateTexture(
            normalTextureInfo, normalPixels, sizeof(normalPixels));
        if (!normalTexture.IsValid())
        {
            return false;
        }
        lease.TrackTexture(normalTexture);

        Core::Container::FixedArray<MaterialHandle, 3> materials;
        for (uint32_t index = 0; index < 3u; ++index)
        {
            Core::Rendering::MaterialCreateData materialData;
            materialData.NormalTexture = normalTexture;
            materialData.DebugName = index == 0u
                                         ? TEXT("RenderingValidationNeutral")
                                         : index == 1u ? TEXT("RenderingValidationEmissive")
                                                       : TEXT("RenderingValidationLegacyTransparent");
            materialData.bTwoSided = true;
            materialData.bCastShadows = false;
            if (index == 1u)
            {
                materialData.EmissiveColor[0] = 1.0f;
                materialData.EmissiveColor[1] = 1.0f;
                materialData.EmissiveColor[2] = 1.0f;
                materialData.EmissiveLuminanceNits = 100.0f;
            }
            else if (index == 2u)
            {
                materialData.Blend = Core::Rendering::BlendMode::Translucent;
            }
            materials[index] = resources.Materials().Create(materialData);
            if (!materials[index].IsValid())
            {
                return false;
            }
            lease.TrackMaterial(materials[index]);
        }

        Core::Container::FixedArray<TextureHandle, P4RoughnessCount> roughnessTextures;
        for (uint32_t index = 0; index < P4RoughnessCount; ++index)
        {
            roughnessTextures[index] = CreateScalarTexture(
                resources, P4RoughnessValues[index], TEXT("RenderingValidationP4Roughness"));
            if (!roughnessTextures[index].IsValid())
            {
                return false;
            }
            lease.TrackTexture(roughnessTextures[index]);
        }

        Core::Container::FixedArray<TextureHandle, P4MetallicQueryTextureCount> metallicQueryTextures;
        for (uint32_t index = 0; index < P4MetallicQueryTextureCount; ++index)
        {
            metallicQueryTextures[index] = CreateScalarTexture(
                resources, P4MetallicQueryValues[index], TEXT("RenderingValidationP4MetallicQuery"));
            if (!metallicQueryTextures[index].IsValid())
            {
                return false;
            }
            lease.TrackTexture(metallicQueryTextures[index]);
        }

        const TextureHandle aoTexture = CreateScalarTexture(
            resources, 1.0f, TEXT("RenderingValidationP4AmbientOcclusion"));
        if (!aoTexture.IsValid())
        {
            return false;
        }
        lease.TrackTexture(aoTexture);

        for (uint32_t roughnessIndex = 0; roughnessIndex < P4RoughnessCount; ++roughnessIndex)
        {
            for (uint32_t metallicQueryIndex = 0;
                 metallicQueryIndex < P4MetallicQueryTextureCount;
                 ++metallicQueryIndex)
            {
                const uint32_t materialIndex =
                    roughnessIndex * P4MetallicQueryTextureCount + metallicQueryIndex;
                Core::Rendering::MaterialCreateData materialData;
                materialData.NormalTexture = normalTexture;
                materialData.RoughnessTexture = roughnessTextures[roughnessIndex];
                materialData.MetallicTexture = metallicQueryTextures[metallicQueryIndex];
                materialData.AOTexture = aoTexture;
                materialData.bTwoSided = true;
                materialData.bCastShadows = false;
                materialData.DebugName = TEXT("RenderingValidationP4Material");
                p4Materials[materialIndex] = resources.Materials().Create(materialData);
                if (!p4Materials[materialIndex].IsValid())
                {
                    return false;
                }
                lease.TrackMaterial(p4Materials[materialIndex]);
            }
        }

        for (const SceneObjectSpec& object : layout.Objects)
        {
            Core::Entity* entity = world.SpawnEntity();
            if (entity == nullptr)
            {
                return false;
            }
            guard.TrackObject(entity);
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
            const uint32_t materialIndex = static_cast<uint32_t>(object.Material);
            mesh->SetMaterial(0, materials[materialIndex]);
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                mesh->SetCustomData(channel, object.Color[channel]);
            }

            if (pP4PlaneMesh == nullptr && object.Primitive == ScenePrimitiveKind::Plane &&
                object.Material == MaterialKind::NeutralOpaque)
            {
                pP4PlaneMesh = mesh;
            }
            if (object.Material == MaterialKind::EmissiveOpaque)
            {
                pEmissiveMesh = mesh;
            }
            if (object.Material == MaterialKind::LegacyTransparent)
            {
                pTransparentMesh = mesh;
            }
        }

        Core::Entity* p4SphereEntity = world.SpawnEntity();
        if (p4SphereEntity == nullptr)
        {
            return false;
        }
        guard.TrackObject(p4SphereEntity);
        p4SphereEntity->SetPosition(0.0f, 0.0f, 0.0f);
        p4SphereEntity->SetScale(0.025f, 0.025f, 0.025f);
        pP4SphereMesh = world.CreateComponent<Core::Component::MeshComponent>(p4SphereEntity);
        if (pP4SphereMesh == nullptr)
        {
            return false;
        }
        pP4SphereMesh->SetMeshHandle(SphereHandle);
        pP4SphereMesh->SetMaterial(0, p4Materials[0]);
        pP4SphereMesh->SetCustomData(0, 1.0f);
        pP4SphereMesh->SetCustomData(1, 1.0f);
        pP4SphereMesh->SetCustomData(2, 1.0f);
        pP4SphereMesh->SetCustomData(3, 1.0f);
        pP4SphereMesh->SetVisible(false);

        for (const SceneLightSpec& light : layout.Lights)
        {
            Core::Entity* entity = world.SpawnEntity();
            if (entity == nullptr)
            {
                return false;
            }
            guard.TrackObject(entity);
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
            pP4LightEntity = entity;
        }

        Core::Entity* sentinelEntity = world.SpawnEntity();
        if (sentinelEntity == nullptr)
        {
            return false;
        }
        guard.TrackObject(sentinelEntity);
        pSentinel = world.CreateComponent<FixedStepSentinelComponent>(sentinelEntity);
        if (pSentinel == nullptr)
        {
            return false;
        }

        guard.Commit();
        return true;
    }

    void RenderingValidationSceneFixture::Shutdown(Core::Rendering::RenderResources&)
    {
        RenderingValidationResourceReleaser releaser(m_pResources);
        ShutdownPublishedInitialization(&releaser);
    }

    void RenderingValidationSceneFixture::ApplyCamera(Core::Rendering::RenderWorld& renderWorld) const
    {
        renderWorld.SetMainCamera(m_Layout.Camera);
    }

    bool RenderingValidationSceneFixture::ApplyP4ScenarioRow(const P4ScenarioRow& row) const
    {
        if (m_pP4PlaneMesh == nullptr || m_pP4SphereMesh == nullptr ||
            !m_P4Materials[0].IsValid())
        {
            return false;
        }

        uint32_t roughnessIndex = 0u;
        uint32_t metallicQueryIndex = 0u;
        bool bUseSphere = false;
        float albedo = 0.5f;
        switch (row.Scenario)
        {
        case P4Scenario::Raw250TextureRepresentation:
            if (row.RowIndex != 0u)
            {
                return false;
            }
            break;
        case P4Scenario::Raw251DfgLut:
            if (row.RowIndex >= P4RoughnessCount * 5u)
            {
                return false;
            }
            roughnessIndex = row.RowIndex / 5u;
            metallicQueryIndex = P4DfgQueryIndices[row.RowIndex % 5u];
            break;
        case P4Scenario::Raw252RoughnessSweep:
            if (row.RowIndex > P4RoughnessCount)
            {
                return false;
            }
            if (row.RowIndex == P4RoughnessCount)
            {
                roughnessIndex = 1u;
                metallicQueryIndex = P4TargetMetallicIndex;
            }
            else
            {
                roughnessIndex = row.RowIndex;
            }
            break;
        case P4Scenario::Raw252TargetNotOne:
            if (row.RowIndex != 0u)
            {
                return false;
            }
            roughnessIndex = 1u;
            metallicQueryIndex = P4TargetMetallicIndex;
            break;
        case P4Scenario::Raw252WhiteFurnace:
            if (row.RowIndex >= P4RoughnessCount * P4FurnaceMetallicCount)
            {
                return false;
            }
            roughnessIndex = row.RowIndex / P4FurnaceMetallicCount;
            metallicQueryIndex = row.RowIndex % P4FurnaceMetallicCount;
            bUseSphere = true;
            albedo = 1.0f;
            break;
        case P4Scenario::Raw254DirectConductorEndpoint:
            if (row.RowIndex != 0u || m_pP4LightEntity == nullptr)
            {
                return false;
            }
            roughnessIndex = P4DirectRoughnessIndex;
            metallicQueryIndex = P4TargetMetallicIndex;
            albedo = 1.0f;
            break;
        default:
            return false;
        }

        const uint32_t materialIndex =
            roughnessIndex * P4MetallicQueryTextureCount + metallicQueryIndex;
        if (materialIndex >= P4MaterialCount || !m_P4Materials[materialIndex].IsValid())
        {
            return false;
        }

        m_pP4PlaneMesh->SetMaterial(0u, m_P4Materials[materialIndex]);
        m_pP4SphereMesh->SetMaterial(0u, m_P4Materials[materialIndex]);
        for (uint32_t channel = 0u; channel < 4u; ++channel)
        {
            const float value = channel == 3u ? 1.0f : albedo;
            m_pP4PlaneMesh->SetCustomData(channel, value);
            m_pP4SphereMesh->SetCustomData(channel, value);
        }
        m_pP4PlaneMesh->SetVisible(!bUseSphere);
        m_pP4SphereMesh->SetVisible(bUseSphere);
        if (m_pEmissiveMesh != nullptr)
        {
            m_pEmissiveMesh->SetVisible(false);
        }
        if (m_pTransparentMesh != nullptr)
        {
            m_pTransparentMesh->SetVisible(false);
        }
        if (m_pP4LightEntity != nullptr)
        {
            m_pP4LightEntity->SetActive(row.Scenario == P4Scenario::Raw254DirectConductorEndpoint);
        }
        return true;
    }

    const Core::Rendering::CameraProxy& RenderingValidationSceneFixture::GetCamera() const
    {
        return m_Layout.Camera;
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

    size_t RenderingValidationSceneFixture::TrackedMeshCount() const noexcept
    {
        return m_Lease.TrackedMeshCount();
    }

    size_t RenderingValidationSceneFixture::TrackedTextureCount() const noexcept
    {
        return m_Lease.TrackedTextureCount();
    }

    size_t RenderingValidationSceneFixture::TrackedMaterialCount() const noexcept
    {
        return m_Lease.TrackedMaterialCount();
    }

    void RenderingValidationSceneFixture::UnregisterMesh(MeshDataHandle handle) noexcept
    {
        if (m_pResources != nullptr)
        {
            m_pResources->Meshes().Unregister(handle);
        }
    }

    void RenderingValidationSceneFixture::ReleaseTexture(Core::Rendering::TextureHandle handle) noexcept
    {
        if (m_pResources != nullptr)
        {
            m_pResources->Textures().ReleaseTexture(handle);
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
