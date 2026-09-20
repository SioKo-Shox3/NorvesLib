#include "RenderingValidation/RenderingValidationScene.h"

#include "RenderingValidation/GpuTestEnvironment.h"
#include "Component/CameraComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Math/MathTypes.h"
#include "Math/MatrixUtils.h"
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
        constexpr MeshDataHandle R1ScreenPlaneHandle{0x52300003u};
        constexpr double R1PerspectiveHalfAngleTangent = 0.577350269189625764509148780501957456;
        constexpr double R1OccluderCenter[3] = {1.3333333, 0.0, 1.0};
        constexpr double R1ProjectionTolerancePixels = 1.0e-4;
        constexpr double R1IndoorTargetCenter[3] = {
            -0.034985351520794,
            -0.000170898437704,
            0.499998291730881};
        constexpr double R1IndoorBackgroundCenter[3] = {
            -0.027941894497941,
            -0.000183105468968,
            0.249998635649681};
        constexpr double R1OutdoorTargetCenter[3] = {
            5.234618808803869,
            4.196780671097502,
            8.052662865211994};
        constexpr double R1OutdoorBackgroundCenter[3] = {
            5.158142839319194,
            4.096378254984689,
            7.822092750485282};
        constexpr float R1Albedo[4] = {0.5f, 0.25f, 0.125f, 1.0f};
        constexpr float R1BlackAlbedo[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        constexpr float R1ScalarZero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        constexpr float R1ScalarHalf[4] = {0.5f, 0.5f, 0.5f, 1.0f};
        constexpr float R1ScalarOne[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        enum class R1TextureIndex : uint32_t
        {
            TargetAlbedo,
            BackgroundAlbedo,
            MetallicZero,
            MetallicHalf,
            Roughness,
            AmbientOcclusionOne,
            AmbientOcclusionZero,
            Height,
            FlatNormal,
            Count
        };
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

        void BuildR1PlaneMesh(
            SceneKind kind,
            Core::Container::VariableArray<Core::Rendering::Mesh3DVertex>& outVertices,
            Core::Container::VariableArray<uint32_t>& outIndices)
        {
            Core::Rendering::ProceduralMeshGenerator::GeneratePlane(
                10.0f, 10.0f, 1, 1, outVertices, outIndices);

            // Indoor uses the existing wall-facing representation. Outdoor keeps
            // the generator's native ground-facing winding and normal.
            if (kind != SceneKind::Indoor)
            {
                return;
            }

            for (Core::Rendering::Mesh3DVertex& vertex : outVertices)
            {
                vertex.Normal[0] = 0.0f;
                vertex.Normal[1] = 0.0f;
                vertex.Normal[2] = 1.0f;
            }
            for (size_t index = 0; index + 2 < outIndices.size(); index += 3)
            {
                const uint32_t second = outIndices[index + 1];
                outIndices[index + 1] = outIndices[index + 2];
                outIndices[index + 2] = second;
            }
        }

        bool ValidateR1PlaneMeshContract(
            SceneKind kind,
            const Core::Container::VariableArray<Core::Rendering::Mesh3DVertex>& vertices,
            const Core::Container::VariableArray<uint32_t>& indices)
        {
            if (vertices.size() != 4u || indices.size() != 6u)
            {
                return false;
            }

            constexpr float expectedPositions[4][3] = {
                {-5.0f, 0.0f, -5.0f},
                {5.0f, 0.0f, -5.0f},
                {-5.0f, 0.0f, 5.0f},
                {5.0f, 0.0f, 5.0f}};
            constexpr uint32_t outdoorIndices[6] = {0u, 1u, 2u, 1u, 3u, 2u};
            constexpr uint32_t indoorIndices[6] = {0u, 2u, 1u, 1u, 2u, 3u};
            const float expectedNormalX = 0.0f;
            const float expectedNormalY = kind == SceneKind::Indoor ? 0.0f : 1.0f;
            const float expectedNormalZ = kind == SceneKind::Indoor ? 1.0f : 0.0f;
            const uint32_t* expectedIndices =
                kind == SceneKind::Indoor ? indoorIndices : outdoorIndices;

            for (size_t vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex)
            {
                const Core::Rendering::Mesh3DVertex& vertex = vertices[vertexIndex];
                for (uint32_t component = 0u; component < 3u; ++component)
                {
                    if (vertex.Position[component] != expectedPositions[vertexIndex][component])
                    {
                        return false;
                    }
                }
                if (vertex.Normal[0] != expectedNormalX ||
                    vertex.Normal[1] != expectedNormalY ||
                    vertex.Normal[2] != expectedNormalZ)
                {
                    return false;
                }
            }

            for (size_t index = 0u; index < indices.size(); ++index)
            {
                if (indices[index] != expectedIndices[index])
                {
                    return false;
                }
            }

            for (size_t triangle = 0u; triangle < indices.size(); triangle += 3u)
            {
                const Core::Rendering::Mesh3DVertex& first = vertices[indices[triangle]];
                const Core::Rendering::Mesh3DVertex& second = vertices[indices[triangle + 1u]];
                const Core::Rendering::Mesh3DVertex& third = vertices[indices[triangle + 2u]];
                const double edgeAX = static_cast<double>(second.Position[0]) - first.Position[0];
                const double edgeAZ = static_cast<double>(second.Position[2]) - first.Position[2];
                const double edgeBX = static_cast<double>(third.Position[0]) - first.Position[0];
                const double edgeBZ = static_cast<double>(third.Position[2]) - first.Position[2];
                const double signedY = edgeAZ * edgeBX - edgeAX * edgeBZ;
                if (kind == SceneKind::Indoor ? signedY <= 0.0 : signedY >= 0.0)
                {
                    return false;
                }
            }
            return true;
        }

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

        TextureHandle CreateR1FloatTexture(RenderResources& resources,
                                            const float (&pixels)[4],
                                            const TCHAR* debugName)
        {
            Core::Rendering::TextureCreateInfo textureInfo;
            textureInfo.Width = 1u;
            textureInfo.Height = 1u;
            textureInfo.PixelFormat = Core::Rendering::TextureCreateInfo::Format::RGBA32_FLOAT;
            textureInfo.DebugName = debugName;
            return resources.Textures().CreateTexture(textureInfo, pixels, sizeof(pixels));
        }

        TextureHandle CreateR1FlatNormalTexture(RenderResources& resources)
        {
            Core::Rendering::TextureCreateInfo textureInfo;
            textureInfo.Width = 1u;
            textureInfo.Height = 1u;
            textureInfo.PixelFormat = Core::Rendering::TextureCreateInfo::Format::RGBA16_FLOAT;
            textureInfo.DebugName = TEXT("RenderingValidationR1FlatNormal");
            constexpr uint16_t pixels[] = {0x3800u, 0x3800u, 0x3C00u, 0x3C00u};
            return resources.Textures().CreateTexture(textureInfo, pixels, sizeof(pixels));
        }

        bool ValidateR1ProjectedEdges(const Core::Entity& entity,
                                      const CameraProxy& camera,
                                      double expectedMinX,
                                      double expectedMaxX,
                                      double expectedMinY,
                                      double expectedMaxY)
        {
            const Math::Transform& transform = entity.GetWorldTransform();
            const Math::Matrix4x4 world = Math::MatrixUtils::CreateWorldRowVector(
                transform.position,
                transform.rotation,
                transform.scale);
            const Math::Vector3 cameraPosition(camera.PositionX, camera.PositionY, camera.PositionZ);
            const Math::Vector3 cameraForward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
            const Math::Vector3 cameraRight(camera.RightX, camera.RightY, camera.RightZ);
            const Math::Vector3 cameraUp(camera.UpX, camera.UpY, camera.UpZ);
            const Math::Vector3 localCorners[4] = {
                Math::Vector3(-5.0f, -5.0f, 0.0f),
                Math::Vector3(5.0f, -5.0f, 0.0f),
                Math::Vector3(-5.0f, 5.0f, 0.0f),
                Math::Vector3(5.0f, 5.0f, 0.0f)};
            double minX = 1.0e30;
            double maxX = -1.0e30;
            double minY = 1.0e30;
            double maxY = -1.0e30;
            for (const Math::Vector3& localCorner : localCorners)
            {
                const double localX = static_cast<double>(localCorner.x);
                const double localY = static_cast<double>(localCorner.y);
                const double localZ = static_cast<double>(localCorner.z);
                const double worldX =
                    localX * static_cast<double>(world.m[0][0]) +
                    localY * static_cast<double>(world.m[1][0]) +
                    localZ * static_cast<double>(world.m[2][0]) +
                    static_cast<double>(world.m[3][0]);
                const double worldY =
                    localX * static_cast<double>(world.m[0][1]) +
                    localY * static_cast<double>(world.m[1][1]) +
                    localZ * static_cast<double>(world.m[2][1]) +
                    static_cast<double>(world.m[3][1]);
                const double worldZ =
                    localX * static_cast<double>(world.m[0][2]) +
                    localY * static_cast<double>(world.m[1][2]) +
                    localZ * static_cast<double>(world.m[2][2]) +
                    static_cast<double>(world.m[3][2]);
                const double relativeX = worldX - static_cast<double>(camera.PositionX);
                const double relativeY = worldY - static_cast<double>(camera.PositionY);
                const double relativeZ = worldZ - static_cast<double>(camera.PositionZ);
                const double depth =
                    relativeX * static_cast<double>(cameraForward.x) +
                    relativeY * static_cast<double>(cameraForward.y) +
                    relativeZ * static_cast<double>(cameraForward.z);
                if (!std::isfinite(depth) || depth <= 0.0)
                {
                    return false;
                }
                const double right =
                    relativeX * static_cast<double>(cameraRight.x) +
                    relativeY * static_cast<double>(cameraRight.y) +
                    relativeZ * static_cast<double>(cameraRight.z);
                const double up =
                    relativeX * static_cast<double>(cameraUp.x) +
                    relativeY * static_cast<double>(cameraUp.y) +
                    relativeZ * static_cast<double>(cameraUp.z);
                double pixelX = 0.0;
                double pixelY = 0.0;
                if (camera.Projection == Core::Rendering::ProjectionType::Perspective)
                {
                    pixelX = (right / (depth * R1PerspectiveHalfAngleTangent) + 1.0) * 128.0;
                    pixelY = (1.0 - up / (depth * R1PerspectiveHalfAngleTangent)) * 128.0;
                }
                else if (camera.Projection == Core::Rendering::ProjectionType::Orthographic &&
                         camera.OrthoWidth > 0.0f && camera.OrthoHeight > 0.0f)
                {
                    pixelX = (right / (static_cast<double>(camera.OrthoWidth) * 0.5) + 1.0) * 128.0;
                    pixelY = (1.0 - up / (static_cast<double>(camera.OrthoHeight) * 0.5)) * 128.0;
                }
                else
                {
                    return false;
                }
                minX = std::min(minX, pixelX);
                maxX = std::max(maxX, pixelX);
                minY = std::min(minY, pixelY);
                maxY = std::max(maxY, pixelY);
            }
            return std::isfinite(minX) && std::isfinite(maxX) &&
                   std::isfinite(minY) && std::isfinite(maxY) &&
                   std::abs(minX - expectedMinX) <= R1ProjectionTolerancePixels &&
                   std::abs(maxX - expectedMaxX) <= R1ProjectionTolerancePixels &&
                   std::abs(minY - expectedMinY) <= R1ProjectionTolerancePixels &&
                   std::abs(maxY - expectedMaxY) <= R1ProjectionTolerancePixels;
        }

        bool ValidateR1ShadowOccluder(const Core::Entity& entity, const CameraProxy& camera)
        {
            const Math::Transform& transform = entity.GetWorldTransform();
            const Math::Matrix4x4 world = Math::MatrixUtils::CreateWorldRowVector(
                transform.position,
                transform.rotation,
                transform.scale);
            const Math::Vector3 tangentX(world.m[0][0], world.m[0][1], world.m[0][2]);
            const Math::Vector3 tangentY(world.m[1][0], world.m[1][1], world.m[1][2]);
            const float tangentLengthX = Math::VectorUtils::Length(tangentX);
            const float tangentLengthY = Math::VectorUtils::Length(tangentY);
            const Math::Vector3 normal = Math::VectorUtils::Normalize(
                Math::VectorUtils::Cross(tangentX, tangentY));
            const Math::Vector3 expectedNormal(-0.8f, 0.0f, -0.6f);
            if (!std::isfinite(tangentLengthX) || !std::isfinite(tangentLengthY) ||
                std::abs(tangentLengthX - 0.075f) > 1.0e-5f ||
                std::abs(tangentLengthY - 0.075f) > 1.0e-5f ||
                Math::VectorUtils::LengthSquared(normal) < 0.99f ||
                Math::VectorUtils::Dot(normal, expectedNormal) < 1.0f - 1.0e-5f)
            {
                return false;
            }

            const Math::Vector3 cameraPosition(camera.PositionX, camera.PositionY, camera.PositionZ);
            const Math::Vector3 cameraForward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
            const Math::Vector3 cameraRight(camera.RightX, camera.RightY, camera.RightZ);
            const Math::Vector3 cameraUp(camera.UpX, camera.UpY, camera.UpZ);
            const Math::Vector3 localCorners[4] = {
                Math::Vector3(-5.0f, -5.0f, 0.0f),
                Math::Vector3(5.0f, -5.0f, 0.0f),
                Math::Vector3(-5.0f, 5.0f, 0.0f),
                Math::Vector3(5.0f, 5.0f, 0.0f)};
            double minX = 1.0e30;
            double maxX = -1.0e30;
            double minY = 1.0e30;
            double maxY = -1.0e30;
            for (const Math::Vector3& localCorner : localCorners)
            {
                const double worldX = static_cast<double>(localCorner.x) * world.m[0][0] +
                                      static_cast<double>(localCorner.y) * world.m[1][0] +
                                      static_cast<double>(world.m[3][0]);
                const double worldY = static_cast<double>(localCorner.x) * world.m[0][1] +
                                      static_cast<double>(localCorner.y) * world.m[1][1] +
                                      static_cast<double>(world.m[3][1]);
                const double worldZ = static_cast<double>(localCorner.x) * world.m[0][2] +
                                      static_cast<double>(localCorner.y) * world.m[1][2] +
                                      static_cast<double>(world.m[3][2]);
                const Math::Vector3 relative(static_cast<float>(worldX - cameraPosition.x),
                                             static_cast<float>(worldY - cameraPosition.y),
                                             static_cast<float>(worldZ - cameraPosition.z));
                const double depth = Math::VectorUtils::Dot(relative, cameraForward);
                const double right = Math::VectorUtils::Dot(relative, cameraRight);
                const double up = Math::VectorUtils::Dot(relative, cameraUp);
                if (!std::isfinite(depth) || depth <= 0.0)
                {
                    return false;
                }
                const double pixelX = (right / (depth * R1PerspectiveHalfAngleTangent) + 1.0) * 128.0;
                const double pixelY = (1.0 - up / (depth * R1PerspectiveHalfAngleTangent)) * 128.0;
                if (!std::isfinite(pixelX) || !std::isfinite(pixelY))
                {
                    return false;
                }
                minX = std::min(minX, pixelX);
                maxX = std::max(maxX, pixelX);
                minY = std::min(minY, pixelY);
                maxY = std::max(maxY, pixelY);
            }
            const bool bOverlapsMainRoi = maxX >= static_cast<double>(R1RoiMinX) &&
                                           minX <= static_cast<double>(R1RoiMaxX) &&
                                           maxY >= static_cast<double>(R1RoiMinY) &&
                                           minY <= static_cast<double>(R1RoiMaxY);
            return !bOverlapsMainRoi;
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
                   left.RenderOrder == right.RenderOrder && left.Aperture == right.Aperture &&
                   left.ShutterSpeed == right.ShutterSpeed && left.ISO == right.ISO &&
                   left.ExposureCompensation == right.ExposureCompensation &&
                   left.EV100 == right.EV100 && left.Exposure == right.Exposure &&
                   left.PreExposure == right.PreExposure && left.InvPreExposure == right.InvPreExposure;
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
            if (!Core::Component::CameraComponent::TryBuildExposureSnapshot(
                    4.0f, 1.0f / 60.0f, 100.0f, 4.0f, layout.Camera))
            {
                return;
            }
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
            light.Intensity = 10000.0f;
            light.bCastShadows = true;
            layout.Lights.push_back(light);
            layout.Camera = BuildLookAtCamera(Math::Vector3(7.0f, 5.0f, 9.0f), Math::Vector3::Zero,
                                              ValidationWidth, ValidationHeight);
            if (!Core::Component::CameraComponent::TryBuildExposureSnapshot(
                    4.0f, 1.0f / 60.0f, 100.0f, 0.0f, layout.Camera))
            {
                return;
            }
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

    bool ValidateR1PlaneMeshContractForTesting()
    {
        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> indoorVertices;
        Core::Container::VariableArray<uint32_t> indoorIndices;
        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> outdoorVertices;
        Core::Container::VariableArray<uint32_t> outdoorIndices;
        BuildR1PlaneMesh(SceneKind::Indoor, indoorVertices, indoorIndices);
        BuildR1PlaneMesh(SceneKind::Outdoor, outdoorVertices, outdoorIndices);
        return ValidateR1PlaneMeshContract(SceneKind::Indoor, indoorVertices, indoorIndices) &&
               ValidateR1PlaneMeshContract(SceneKind::Outdoor, outdoorVertices, outdoorIndices);
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
        m_P4DfgTileObjects.clear();
        m_NeutralMaterial = state.NeutralMaterial;
        m_P4Materials = std::move(state.P4Materials);
        m_bR1PhysicalFixturePrepared = false;
        m_bR1PhysicalFixtureFailed = false;
        m_bR1ObjectPresence = false;
        m_R1PhysicalCamera = {};
        m_pR1TargetEntity = m_Objects.size() > 2u ? m_Objects[2] : nullptr;
        m_pR1BackgroundEntity = nullptr;
        m_pR1OccluderEntity = nullptr;
        m_pR1TargetMesh = m_Objects.size() > 2u
                               ? m_Objects[2]->GetComponent<Core::Component::MeshComponent>()
                               : nullptr;
        m_pR1BackgroundMesh = nullptr;
        m_pR1OccluderMesh = nullptr;
        m_pR1PointLightEntity = nullptr;
        m_pR1DirectionalLightEntity = nullptr;
        m_R1ScreenPlaneHandle = MeshDataHandle::Invalid();
        m_R1Textures.fill(TextureHandle::Invalid());
        m_R1TargetMaterials.fill(MaterialHandle::Invalid());
        m_R1BackgroundMaterial = MaterialHandle::Invalid();
        m_R1OccluderMaterial = MaterialHandle::Invalid();
        m_bR3ShadowedShaftsPrepared = false;
        m_bR3ShadowedShaftsFailed = false;
        m_R3ShadowedShaftsCamera = {};
        m_pR3ShadowBackgroundEntity = nullptr;
        m_pR3ShadowOccluderEntity = nullptr;
        m_pR3ShadowBackgroundMesh = nullptr;
        m_pR3ShadowOccluderMesh = nullptr;
        m_R3ShadowPlaneHandle = MeshDataHandle::Invalid();
        m_R3ShadowAlbedoTexture = TextureHandle::Invalid();
        m_R3ShadowMaterial = MaterialHandle::Invalid();

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
        state.NeutralMaterial = MaterialHandle::Invalid();
        state.Layout = {};
        state.P4Materials.fill(MaterialHandle::Invalid());
        m_bPublished = true;
    }

    void RenderingValidationSceneFixture::ShutdownPublishedInitialization(
        ISceneFixtureResourceReleaser* releaser) noexcept
    {
        if (m_pWorld != nullptr)
        {
            for (size_t index = m_P4DfgTileObjects.size(); index > 0; --index)
            {
                m_pWorld->RemoveEntity(m_P4DfgTileObjects[index - 1]);
            }
        }
        m_P4DfgTileObjects.clear();
        Core::World* pWorld = m_pWorld;
        m_pWorld = nullptr;
        m_pResources = nullptr;
        m_pSentinel = nullptr;
        m_pP4PlaneMesh = nullptr;
        m_pP4SphereMesh = nullptr;
        m_pEmissiveMesh = nullptr;
        m_pTransparentMesh = nullptr;
        m_pP4LightEntity = nullptr;
        m_bR1PhysicalFixturePrepared = false;
        m_bR1PhysicalFixtureFailed = false;
        m_bR1ObjectPresence = false;
        m_R1PhysicalCamera = {};
        m_pR1TargetEntity = nullptr;
        m_pR1BackgroundEntity = nullptr;
        m_pR1OccluderEntity = nullptr;
        m_pR1TargetMesh = nullptr;
        m_pR1BackgroundMesh = nullptr;
        m_pR1OccluderMesh = nullptr;
        m_pR1PointLightEntity = nullptr;
        m_pR1DirectionalLightEntity = nullptr;
        m_R1ScreenPlaneHandle = MeshDataHandle::Invalid();
        m_R1Textures.fill(TextureHandle::Invalid());
        m_R1TargetMaterials.fill(MaterialHandle::Invalid());
        m_R1BackgroundMaterial = MaterialHandle::Invalid();
        m_R1OccluderMaterial = MaterialHandle::Invalid();
        m_bR3ShadowedShaftsPrepared = false;
        m_bR3ShadowedShaftsFailed = false;
        m_R3ShadowedShaftsCamera = {};
        m_pR3ShadowBackgroundEntity = nullptr;
        m_pR3ShadowOccluderEntity = nullptr;
        m_pR3ShadowBackgroundMesh = nullptr;
        m_pR3ShadowOccluderMesh = nullptr;
        m_R3ShadowPlaneHandle = MeshDataHandle::Invalid();
        m_R3ShadowAlbedoTexture = TextureHandle::Invalid();
        m_R3ShadowMaterial = MaterialHandle::Invalid();
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
        m_NeutralMaterial = MaterialHandle::Invalid();
        m_Layout = {};
    }

    bool RenderingValidationSceneFixture::Initialize(Core::World& world,
                                                     Core::Rendering::RenderResources& resources,
                                                     SceneKind kind,
                                                     uint32_t seed)
    {
        Shutdown(resources);
        m_SceneKind = kind;
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
        BuildR1PlaneMesh(kind, planeVertices, planeIndices);
        if (!ValidateR1PlaneMeshContract(kind, planeVertices, planeIndices))
        {
            return false;
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
        staging.NeutralMaterial = materials[0];

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
        renderWorld.SetMainCamera(GetCamera());
    }

    bool RenderingValidationSceneFixture::ApplyBaseValidationFixture() const
    {
        if (m_pP4PlaneMesh == nullptr || m_pP4SphereMesh == nullptr ||
            !m_NeutralMaterial.IsValid() || !ClearP4DfgTileFixture())
        {
            return false;
        }
        m_pP4PlaneMesh->SetMaterial(0u, m_NeutralMaterial);
        m_pP4PlaneMesh->SetCustomData(0u, 0.5f);
        m_pP4PlaneMesh->SetCustomData(1u, 0.5f);
        m_pP4PlaneMesh->SetCustomData(2u, 0.5f);
        m_pP4PlaneMesh->SetCustomData(3u, 1.0f);
        m_pP4PlaneMesh->SetVisible(true);
        m_pP4SphereMesh->SetVisible(false);
        if (m_pEmissiveMesh != nullptr)
        {
            m_pEmissiveMesh->SetVisible(true);
        }
        if (m_pTransparentMesh != nullptr)
        {
            m_pTransparentMesh->SetVisible(true);
        }
        if (m_pP4LightEntity != nullptr)
        {
            m_pP4LightEntity->SetActive(true);
            auto* light = m_pP4LightEntity->GetComponent<Core::Component::LightComponent>();
            if (light == nullptr)
            {
                return false;
            }
            light->SetLightVisible(false);
            light->SetLightVisible(true);
        }
        return true;
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

    bool RenderingValidationSceneFixture::ApplyP4DfgTileFixture() const
    {
        if (m_pWorld == nullptr || m_pP4PlaneMesh == nullptr || m_pP4SphereMesh == nullptr ||
            m_P4Materials[0].IsValid() == false ||
            !m_P4DfgTileObjects.empty())
        {
            return false;
        }

        const float radians = Math::Constants::PI / 180.0f;
        const Math::Quaternion rotation = Math::QuaternionUtils::FromEulerAngles(
            Math::Vector3(90.0f * radians, 0.0f, 0.0f));
        for (uint32_t row = 0u; row < R1P4DfgTileGridSize; ++row)
        {
            for (uint32_t column = 0u; column < R1P4DfgTileGridSize; ++column)
            {
                const uint32_t materialIndex =
                    row * P4MetallicQueryTextureCount + P4DfgQueryIndices[column];
                if (materialIndex >= P4MaterialCount || !m_P4Materials[materialIndex].IsValid())
                {
                    ClearP4DfgTileFixture();
                    return false;
                }
                Core::Entity* entity = m_pWorld->SpawnEntity();
                if (entity == nullptr)
                {
                    ClearP4DfgTileFixture();
                    return false;
                }
                m_P4DfgTileObjects.push_back(entity);
                entity->SetPosition(
                    (static_cast<float>(column) - 2.0f) * 0.005f,
                    (2.0f - static_cast<float>(row)) * 0.005f,
                    0.0f);
                entity->SetRotation(rotation);
                entity->SetScale(R1P4DfgTileScale, R1P4DfgTileScale, R1P4DfgTileScale);
                Core::Component::MeshComponent* mesh =
                    m_pWorld->CreateComponent<Core::Component::MeshComponent>(entity);
                if (mesh == nullptr)
                {
                    ClearP4DfgTileFixture();
                    return false;
                }
                mesh->SetMeshHandle(PlaneHandle);
                mesh->SetMaterial(0u, m_P4Materials[materialIndex]);
                mesh->SetCustomData(0u, 0.5f);
                mesh->SetCustomData(1u, 0.5f);
                mesh->SetCustomData(2u, 0.5f);
                mesh->SetCustomData(3u, 1.0f);
            }
        }

        m_pP4PlaneMesh->SetVisible(false);
        m_pP4SphereMesh->SetVisible(false);
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
            m_pP4LightEntity->SetActive(false);
        }
        return true;
    }

    bool RenderingValidationSceneFixture::ClearP4DfgTileFixture() const
    {
        if (m_pWorld != nullptr)
        {
            for (size_t index = m_P4DfgTileObjects.size(); index > 0; --index)
            {
                m_pWorld->RemoveEntity(m_P4DfgTileObjects[index - 1]);
            }
        }
        m_P4DfgTileObjects.clear();
        if (m_pP4PlaneMesh != nullptr)
        {
            m_pP4PlaneMesh->SetVisible(true);
        }
        if (m_pP4SphereMesh != nullptr)
        {
            m_pP4SphereMesh->SetVisible(false);
        }
        if (m_pP4PlaneMesh != nullptr)
        {
            m_pP4PlaneMesh->SetVisible(false);
        }
        return true;
    }

    bool RenderingValidationSceneFixture::EnsureR1PhysicalFixture(bool bObjectPresence) const
    {
        if (m_bR1PhysicalFixturePrepared)
        {
            return m_bR1ObjectPresence == bObjectPresence;
        }
        if (m_bR1PhysicalFixtureFailed || m_pWorld == nullptr || m_pResources == nullptr ||
            m_pR1TargetEntity == nullptr || m_pR1TargetMesh == nullptr)
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }

        const bool bIndoor = m_SceneKind == SceneKind::Indoor;
        if (bObjectPresence || !bIndoor)
        {
            m_R1PhysicalCamera = m_Layout.Camera;
        }
        else
        {
            m_R1PhysicalCamera = BuildLookAtCamera(Math::Vector3(0.0f, 0.0f, 4.0f),
                                                   Math::Vector3::Zero,
                                                   ValidationWidth,
                                                   ValidationHeight);
        }
        const float exposureCompensation = bObjectPresence ? (bIndoor ? 4.0f : 0.0f) : 4.0f;
        if (!Core::Component::CameraComponent::TryBuildExposureSnapshot(
                4.0f,
                1.0f / 60.0f,
                100.0f,
                exposureCompensation,
                m_R1PhysicalCamera))
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }

        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> vertices;
        Core::Container::VariableArray<uint32_t> indices;
        const float localPositions[4][3] = {
            {-5.0f, -5.0f, 0.0f},
            {5.0f, -5.0f, 0.0f},
            {-5.0f, 5.0f, 0.0f},
            {5.0f, 5.0f, 0.0f}};
        const float localTexCoords[4][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
            {1.0f, 1.0f}};
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            Core::Rendering::Mesh3DVertex vertex{};
            vertex.Position[0] = localPositions[index][0];
            vertex.Position[1] = localPositions[index][1];
            vertex.Position[2] = localPositions[index][2];
            vertex.Normal[0] = 0.0f;
            vertex.Normal[1] = 0.0f;
            vertex.Normal[2] = 1.0f;
            vertex.TexCoord[0] = localTexCoords[index][0];
            vertex.TexCoord[1] = localTexCoords[index][1];
            vertices.push_back(vertex);
        }
        indices.push_back(0u);
        indices.push_back(1u);
        indices.push_back(2u);
        indices.push_back(1u);
        indices.push_back(3u);
        indices.push_back(2u);
        if (!m_pResources->Meshes().Register(R1ScreenPlaneHandle,
                                              vertices.data(),
                                              vertices.size() * sizeof(Core::Rendering::Mesh3DVertex),
                                              indices.data(),
                                              static_cast<uint32_t>(indices.size())))
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        m_R1ScreenPlaneHandle = R1ScreenPlaneHandle;
        m_Lease.TrackMesh(R1ScreenPlaneHandle);

        auto createTexture = [this](R1TextureIndex index,
                                     const float (&pixels)[4],
                                     const TCHAR* debugName) -> bool
        {
            const TextureHandle handle = CreateR1FloatTexture(*m_pResources, pixels, debugName);
            if (!handle.IsValid())
            {
                return false;
            }
            m_R1Textures[static_cast<size_t>(index)] = handle;
            m_Lease.TrackTexture(handle);
            return true;
        };
        if (!createTexture(R1TextureIndex::TargetAlbedo,
                           R1Albedo,
                           TEXT("RenderingValidationR1TargetAlbedo")) ||
            !createTexture(R1TextureIndex::BackgroundAlbedo,
                           R1BlackAlbedo,
                           TEXT("RenderingValidationR1BackgroundAlbedo")) ||
            !createTexture(R1TextureIndex::MetallicZero,
                           R1ScalarZero,
                           TEXT("RenderingValidationR1MetallicZero")) ||
            !createTexture(R1TextureIndex::MetallicHalf,
                           R1ScalarHalf,
                           TEXT("RenderingValidationR1MetallicHalf")) ||
            !createTexture(R1TextureIndex::Roughness,
                           R1ScalarHalf,
                           TEXT("RenderingValidationR1Roughness")) ||
            !createTexture(R1TextureIndex::AmbientOcclusionOne,
                           R1ScalarOne,
                           TEXT("RenderingValidationR1AmbientOcclusionOne")) ||
            !createTexture(R1TextureIndex::AmbientOcclusionZero,
                           R1ScalarZero,
                           TEXT("RenderingValidationR1AmbientOcclusionZero")) ||
            !createTexture(R1TextureIndex::Height,
                           R1ScalarHalf,
                           TEXT("RenderingValidationR1Height")))
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        const TextureHandle flatNormal = CreateR1FlatNormalTexture(*m_pResources);
        if (!flatNormal.IsValid())
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        m_R1Textures[static_cast<size_t>(R1TextureIndex::FlatNormal)] = flatNormal;
        m_Lease.TrackTexture(flatNormal);

        auto createTargetMaterial = [this](TextureHandle metallicTexture,
                                            const TCHAR* debugName) -> MaterialHandle
        {
            Core::Rendering::MaterialCreateData materialData;
            materialData.AlbedoTexture = m_R1Textures[static_cast<size_t>(R1TextureIndex::TargetAlbedo)];
            materialData.NormalTexture = m_R1Textures[static_cast<size_t>(R1TextureIndex::FlatNormal)];
            materialData.MetallicTexture = metallicTexture;
            materialData.RoughnessTexture = m_R1Textures[static_cast<size_t>(R1TextureIndex::Roughness)];
            materialData.AOTexture = m_R1Textures[static_cast<size_t>(R1TextureIndex::AmbientOcclusionOne)];
            materialData.HeightTexture = m_R1Textures[static_cast<size_t>(R1TextureIndex::Height)];
            materialData.HeightScale = 0.0f;
            materialData.Blend = Core::Rendering::BlendMode::Translucent;
            materialData.Shading = Core::Rendering::ShadingModel::DefaultLit;
            materialData.bTwoSided = true;
            materialData.bCastShadows = false;
            materialData.DebugName = debugName;
            return m_pResources->Materials().Create(materialData);
        };
        m_R1TargetMaterials[0] = createTargetMaterial(
            m_R1Textures[static_cast<size_t>(R1TextureIndex::MetallicZero)],
            TEXT("RenderingValidationR1TargetM0"));
        m_R1TargetMaterials[1] = createTargetMaterial(
            m_R1Textures[static_cast<size_t>(R1TextureIndex::MetallicHalf)],
            TEXT("RenderingValidationR1TargetM05"));
        if (!m_R1TargetMaterials[0].IsValid() || !m_R1TargetMaterials[1].IsValid())
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        m_Lease.TrackMaterial(m_R1TargetMaterials[0]);
        m_Lease.TrackMaterial(m_R1TargetMaterials[1]);

        Core::Rendering::MaterialCreateData backgroundMaterialData;
        backgroundMaterialData.AlbedoTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::BackgroundAlbedo)];
        backgroundMaterialData.NormalTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::FlatNormal)];
        backgroundMaterialData.MetallicTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::MetallicZero)];
        backgroundMaterialData.RoughnessTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::Roughness)];
        backgroundMaterialData.AOTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::AmbientOcclusionZero)];
        backgroundMaterialData.HeightTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::Height)];
        backgroundMaterialData.HeightScale = 0.0f;
        backgroundMaterialData.EmissiveColor[0] = 1.0f;
        backgroundMaterialData.EmissiveColor[1] = 1.0f;
        backgroundMaterialData.EmissiveColor[2] = 1.0f;
        backgroundMaterialData.EmissiveLuminanceNits = 7.2f;
        backgroundMaterialData.Blend = Core::Rendering::BlendMode::Translucent;
        backgroundMaterialData.Shading = Core::Rendering::ShadingModel::DefaultLit;
        backgroundMaterialData.bTwoSided = true;
        backgroundMaterialData.bCastShadows = false;
        backgroundMaterialData.DebugName = TEXT("RenderingValidationR1Background");
        m_R1BackgroundMaterial = m_pResources->Materials().Create(backgroundMaterialData);
        if (!m_R1BackgroundMaterial.IsValid())
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        m_Lease.TrackMaterial(m_R1BackgroundMaterial);

        Core::Rendering::MaterialCreateData occluderMaterialData = backgroundMaterialData;
        occluderMaterialData.AlbedoTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::TargetAlbedo)];
        occluderMaterialData.AOTexture =
            m_R1Textures[static_cast<size_t>(R1TextureIndex::AmbientOcclusionOne)];
        occluderMaterialData.EmissiveColor[0] = 0.0f;
        occluderMaterialData.EmissiveColor[1] = 0.0f;
        occluderMaterialData.EmissiveColor[2] = 0.0f;
        occluderMaterialData.EmissiveLuminanceNits = 0.0f;
        occluderMaterialData.Blend = Core::Rendering::BlendMode::Opaque;
        occluderMaterialData.bCastShadows = true;
        occluderMaterialData.DebugName = TEXT("RenderingValidationR1ShadowOccluder");
        m_R1OccluderMaterial = m_pResources->Materials().Create(occluderMaterialData);
        if (!m_R1OccluderMaterial.IsValid())
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        m_Lease.TrackMaterial(m_R1OccluderMaterial);

        auto spawnMesh = [this](Core::Entity*& outEntity,
                                Core::Component::MeshComponent*& outMesh) -> bool
        {
            outEntity = m_pWorld->SpawnEntity();
            if (outEntity == nullptr)
            {
                return false;
            }
            m_Objects.push_back(outEntity);
            outMesh = m_pWorld->CreateComponent<Core::Component::MeshComponent>(outEntity);
            return outMesh != nullptr;
        };
        if (!spawnMesh(m_pR1BackgroundEntity, m_pR1BackgroundMesh) ||
            !spawnMesh(m_pR1OccluderEntity, m_pR1OccluderMesh))
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }

        auto spawnLight = [this](bool bDirectional, Core::Entity*& outEntity) -> bool
        {
            outEntity = m_pWorld->SpawnEntity();
            if (outEntity == nullptr)
            {
                return false;
            }
            m_Objects.push_back(outEntity);
            Core::Component::LightComponent* component = nullptr;
            if (bDirectional)
            {
                component = m_pWorld->CreateComponent<Core::Component::DirectionalLightComponent>(outEntity);
            }
            else
            {
                component = m_pWorld->CreateComponent<Core::Component::PointLightComponent>(outEntity);
            }
            return component != nullptr;
        };
        if (bIndoor)
        {
            m_pR1PointLightEntity = m_pP4LightEntity;
            if (m_pR1PointLightEntity == nullptr &&
                !spawnLight(false, m_pR1PointLightEntity))
            {
                m_bR1PhysicalFixtureFailed = true;
                return false;
            }
        }
        else
        {
            m_pR1DirectionalLightEntity = m_pP4LightEntity;
            if (m_pR1DirectionalLightEntity == nullptr &&
                !spawnLight(true, m_pR1DirectionalLightEntity))
            {
                m_bR1PhysicalFixtureFailed = true;
                return false;
            }
        }
        if (m_pR1PointLightEntity == nullptr && !spawnLight(false, m_pR1PointLightEntity))
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        if (m_pR1DirectionalLightEntity == nullptr &&
            !spawnLight(true, m_pR1DirectionalLightEntity))
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }

        auto configurePoint = [this]() -> bool
        {
            if (m_pR1PointLightEntity == nullptr)
            {
                return false;
            }
            m_pR1PointLightEntity->SetPosition(0.0f, 0.0f, 2.0f);
            auto* point = m_pR1PointLightEntity->GetComponent<Core::Component::PointLightComponent>();
            if (point == nullptr ||
                !point->SetIntensityUnit(Core::Component::LightIntensityUnit::Candela))
            {
                return false;
            }
            point->SetLightColor(1.0f, 1.0f, 1.0f);
            point->SetIntensity(100.0f);
            point->SetRange(1000.0f);
            point->SetAttenuationConstant(1.0f);
            point->SetAttenuationLinear(0.0f);
            point->SetAttenuationQuadratic(0.0f);
            point->SetCastShadows(false);
            point->SetLightVisible(true);
            return true;
        };
        auto configureDirectional = [this, bObjectPresence]() -> bool
        {
            if (m_pR1DirectionalLightEntity == nullptr)
            {
                return false;
            }
            auto* directional =
                m_pR1DirectionalLightEntity->GetComponent<Core::Component::DirectionalLightComponent>();
            if (directional == nullptr ||
                !directional->SetIntensityUnit(Core::Component::LightIntensityUnit::Lux))
            {
                return false;
            }
            if (bObjectPresence)
            {
                if (m_Layout.Lights.empty())
                {
                    return false;
                }
                const SceneLightSpec& mainLight = m_Layout.Lights[0];
                directional->SetLightDirection(mainLight.PositionOrDirection[0],
                                               mainLight.PositionOrDirection[1],
                                               mainLight.PositionOrDirection[2]);
                directional->SetLightColor(mainLight.Color[0], mainLight.Color[1], mainLight.Color[2]);
                directional->SetIntensity(10000.0f);
                directional->SetCastShadows(true);
            }
            else
            {
                directional->SetLightDirection(-0.8f, 0.0f, -0.6f);
                directional->SetLightColor(1.0f, 1.0f, 1.0f);
                directional->SetIntensity(100.0f);
                directional->SetCastShadows(false);
            }
            directional->SetLightVisible(true);
            return true;
        };
        if (!configurePoint() || !configureDirectional())
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }

        const Math::Vector3 cameraForward(
            m_R1PhysicalCamera.ForwardX,
            m_R1PhysicalCamera.ForwardY,
            m_R1PhysicalCamera.ForwardZ);
        const Math::Vector3 cameraRight(
            m_R1PhysicalCamera.RightX,
            m_R1PhysicalCamera.RightY,
            m_R1PhysicalCamera.RightZ);
        const Math::Vector3 cameraUp(
            m_R1PhysicalCamera.UpX,
            m_R1PhysicalCamera.UpY,
            m_R1PhysicalCamera.UpZ);
        const Math::Quaternion screenRotation =
            Math::QuaternionUtils::LookRotation(cameraForward * -1.0f);
        Math::Quaternion targetRotation = screenRotation;
        Math::Quaternion backgroundRotation = bObjectPresence
                                                   ? screenRotation
                                                   : Math::QuaternionUtils::LookRotation(cameraForward);
        auto setScreenTransform = [](Core::Entity& entity,
                                     const Math::Quaternion& rotation,
                                     const double center[3],
                                     float scaleX,
                                     float scaleY,
                                     float scaleZ)
        {
            entity.SetPosition(static_cast<float>(center[0]),
                               static_cast<float>(center[1]),
                               static_cast<float>(center[2]));
            entity.SetRotation(rotation);
            entity.SetScale(scaleX, scaleY, scaleZ);
        };

        double targetCenter[3] = {};
        double backgroundCenter[3] = {};
        float targetScaleX = 0.0f;
        float targetScaleY = 0.0f;
        float backgroundScaleX = 0.0f;
        float backgroundScaleY = 0.0f;
        float targetScaleZ = 1.0f;
        float backgroundScaleZ = 1.0f;
        if (bObjectPresence)
        {
            const double* target = bIndoor ? R1IndoorTargetCenter : R1OutdoorTargetCenter;
            const double* background = bIndoor ? R1IndoorBackgroundCenter : R1OutdoorBackgroundCenter;
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                targetCenter[axis] = target[axis];
                backgroundCenter[axis] = background[axis];
            }
            if (bIndoor)
            {
                targetScaleX = 0.0015625f;
                targetScaleY = 0.0015625f;
                backgroundScaleX = 0.00296875f;
                backgroundScaleY = 0.0015625f;
            }
            else
            {
                targetScaleX = 0.036084391824352f;
                targetScaleY = 0.036084391824352f;
                backgroundScaleX = 0.077130387524552f;
                backgroundScaleY = 0.040594940802396f;
            }
        }
        else
        {
            const double cameraPosition[3] = {
                static_cast<double>(m_R1PhysicalCamera.PositionX),
                static_cast<double>(m_R1PhysicalCamera.PositionY),
                static_cast<double>(m_R1PhysicalCamera.PositionZ)};
            const double forward[3] = {
                static_cast<double>(m_R1PhysicalCamera.ForwardX),
                static_cast<double>(m_R1PhysicalCamera.ForwardY),
                static_cast<double>(m_R1PhysicalCamera.ForwardZ)};
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                targetCenter[axis] = cameraPosition[axis] + forward[axis] * 4.0;
                backgroundCenter[axis] = cameraPosition[axis] + forward[axis] * 4.25;
            }
            targetScaleX = 0.17320508075688773f;
            targetScaleY = 0.17320508075688773f;
            backgroundScaleX = 0.4907477288111819f;
            backgroundScaleY = 0.4907477288111819f;
        }
        if (bObjectPresence)
        {
            auto buildCompensatedTransform = [&](float scaleX,
                                                 float scaleY,
                                                 Math::Quaternion& outRotation,
                                                 float& outScaleY) -> bool
            {
                const double sx = static_cast<double>(scaleX);
                const double height = static_cast<double>(scaleY);
                const double uy = static_cast<double>(cameraUp.y);
                const double denominator = 1.0 / (height * height) -
                                           (1.0 - uy * uy) / (sx * sx);
                if (!std::isfinite(denominator) || denominator <= 0.0)
                {
                    return false;
                }
                const double sy = uy / std::sqrt(denominator);
                if (!std::isfinite(sy) || sy <= 0.0)
                {
                    return false;
                }
                Math::Vector3 basisX(
                    cameraRight.x,
                    cameraRight.y * static_cast<float>(sx / sy),
                    cameraRight.z);
                Math::Vector3 basisY(
                    cameraUp.x * static_cast<float>(height / sx),
                    cameraUp.y * static_cast<float>(height / sy),
                    cameraUp.z * static_cast<float>(height / sx));
                basisX = Math::VectorUtils::Normalize(basisX);
                basisY = Math::VectorUtils::Normalize(basisY);
                const Math::Vector3 basisZ =
                    Math::VectorUtils::Normalize(Math::VectorUtils::Cross(basisX, basisY));
                const Math::Matrix4x4 desiredRotation(
                    basisX.x, basisX.y, basisX.z, 0.0f,
                    basisY.x, basisY.y, basisY.z, 0.0f,
                    basisZ.x, basisZ.y, basisZ.z, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
                outRotation = Math::QuaternionUtils::FromRotationMatrix(desiredRotation);
                outScaleY = static_cast<float>(sy);
                return true;
            };
            if (!buildCompensatedTransform(targetScaleX, targetScaleY, targetRotation, targetScaleY) ||
                !buildCompensatedTransform(backgroundScaleX,
                                            backgroundScaleY,
                                            backgroundRotation,
                                            backgroundScaleY))
            {
                m_bR1PhysicalFixtureFailed = true;
                return false;
            }
            targetScaleZ = targetScaleX;
            backgroundScaleZ = backgroundScaleX;
        }
        setScreenTransform(*m_pR1TargetEntity,
                           targetRotation,
                           targetCenter,
                           targetScaleX,
                           targetScaleY,
                           targetScaleZ);
        setScreenTransform(*m_pR1BackgroundEntity,
                           backgroundRotation,
                           backgroundCenter,
                           backgroundScaleX,
                           backgroundScaleY,
                           backgroundScaleZ);
        m_pR1TargetMesh->SetMeshHandle(R1ScreenPlaneHandle);
        m_pR1TargetMesh->SetMaterial(0u, m_R1TargetMaterials[0]);
        m_pR1TargetMesh->SetCustomData(0u, 1.0f);
        m_pR1TargetMesh->SetCustomData(1u, 1.0f);
        m_pR1TargetMesh->SetCustomData(2u, 1.0f);
        m_pR1TargetMesh->SetCustomData(3u, 0.5f);
        m_pR1TargetMesh->SetCastShadow(false);
        m_pR1TargetMesh->SetReceiveShadow(true);
        m_pR1TargetMesh->SetVisible(true);

        m_pR1BackgroundMesh->SetMeshHandle(R1ScreenPlaneHandle);
        m_pR1BackgroundMesh->SetMaterial(0u, m_R1BackgroundMaterial);
        m_pR1BackgroundMesh->SetCustomData(0u, 1.0f);
        m_pR1BackgroundMesh->SetCustomData(1u, 1.0f);
        m_pR1BackgroundMesh->SetCustomData(2u, 1.0f);
        m_pR1BackgroundMesh->SetCustomData(3u, 1.0f);
        m_pR1BackgroundMesh->SetCastShadow(false);
        m_pR1BackgroundMesh->SetReceiveShadow(false);
        m_pR1BackgroundMesh->SetVisible(true);

        setScreenTransform(*m_pR1OccluderEntity,
                           screenRotation,
                           R1OccluderCenter,
                           0.075f,
                           0.075f,
                           0.075f);
        const Math::Quaternion occluderLookRotation =
            Math::QuaternionUtils::LookRotation(Math::Vector3(-0.8f, 0.0f, -0.6f));
        m_pR1OccluderEntity->SetRotation(Math::Quaternion(-occluderLookRotation.x,
                                                            -occluderLookRotation.y,
                                                            -occluderLookRotation.z,
                                                            occluderLookRotation.w));
        m_pR1OccluderMesh->SetMeshHandle(R1ScreenPlaneHandle);
        m_pR1OccluderMesh->SetMaterial(0u, m_R1OccluderMaterial);
        m_pR1OccluderMesh->SetCastShadow(true);
        m_pR1OccluderMesh->SetReceiveShadow(false);
        m_pR1OccluderMesh->SetVisible(false);

        if (!bObjectPresence)
        {
            for (Core::Entity* entity : m_Objects)
            {
                if (entity == nullptr || entity == m_pR1TargetEntity ||
                    entity == m_pR1BackgroundEntity || entity == m_pR1OccluderEntity ||
                    entity == m_pR1PointLightEntity || entity == m_pR1DirectionalLightEntity)
                {
                    continue;
                }
                if (auto* mesh = entity->GetComponent<Core::Component::MeshComponent>())
                {
                    mesh->SetVisible(false);
                }
            }
        }
        m_pR1PointLightEntity->SetActive(bObjectPresence && bIndoor);
        m_pR1DirectionalLightEntity->SetActive(bObjectPresence && !bIndoor);
        if (!ValidateR1PhysicalFixture(bObjectPresence))
        {
            m_bR1PhysicalFixtureFailed = true;
            return false;
        }
        m_bR1ObjectPresence = bObjectPresence;
        m_bR1PhysicalFixturePrepared = true;
        return true;
    }

    bool RenderingValidationSceneFixture::ValidateR1PhysicalFixture(bool bObjectPresence) const
    {
        if (m_R1ScreenPlaneHandle != R1ScreenPlaneHandle ||
            m_pR1TargetEntity == nullptr || m_pR1BackgroundEntity == nullptr ||
            m_pR1OccluderEntity == nullptr || m_pR1TargetMesh == nullptr ||
            m_pR1BackgroundMesh == nullptr || m_pR1OccluderMesh == nullptr ||
            !m_R1TargetMaterials[0].IsValid() || !m_R1TargetMaterials[1].IsValid() ||
            !m_R1BackgroundMaterial.IsValid() || !m_R1OccluderMaterial.IsValid() ||
            m_R1TargetMaterials[0] == m_R1TargetMaterials[1] ||
            m_R1TargetMaterials[0] == m_R1BackgroundMaterial ||
            m_R1TargetMaterials[1] == m_R1BackgroundMaterial)
        {
            return false;
        }
        const double expectedMinX = bObjectPresence ? 18.0 : 80.0;
        const double expectedMaxX = bObjectPresence ? 58.0 : 176.0;
        const double expectedBackgroundMinX = bObjectPresence ? 18.0 : 0.0;
        const double expectedBackgroundMaxX = bObjectPresence ? 94.0 : 256.0;
        if (!ValidateR1ProjectedEdges(*m_pR1TargetEntity,
                                      m_R1PhysicalCamera,
                                      expectedMinX,
                                      expectedMaxX,
                                      bObjectPresence ? 108.0 : 80.0,
                                      bObjectPresence ? 148.0 : 176.0) ||
            !ValidateR1ProjectedEdges(*m_pR1BackgroundEntity,
                                       m_R1PhysicalCamera,
                                       expectedBackgroundMinX,
                                       expectedBackgroundMaxX,
                                       bObjectPresence ? 108.0 : 0.0,
                                       bObjectPresence ? 148.0 : 256.0))
        {
            return false;
        }
        if (!bObjectPresence && !ValidateR1ShadowOccluder(*m_pR1OccluderEntity,
                                                           m_R1PhysicalCamera))
        {
            return false;
        }
        return true;
    }

    bool RenderingValidationSceneFixture::ApplyTransparentPhysicalLightingRow(uint32_t rowIndex) const
    {
        if (rowIndex >= TransparentPhysicalLightingRowCount ||
            !EnsureR1PhysicalFixture(false))
        {
            return false;
        }
        if (m_pP4SphereMesh != nullptr)
        {
            m_pP4SphereMesh->SetVisible(false);
        }
        const bool bMetallicHalf = rowIndex == 2u || rowIndex == 3u ||
                                   rowIndex == 8u || rowIndex == 9u;
        m_pR1TargetMesh->SetMaterial(0u, m_R1TargetMaterials[bMetallicHalf ? 1u : 0u]);
        m_pR1TargetMesh->SetVisible(true);
        m_pR1BackgroundMesh->SetVisible(true);
        m_pR1OccluderMesh->SetVisible(rowIndex == 5u);
        m_pR1PointLightEntity->SetActive(rowIndex == 1u || rowIndex == 3u);
        m_pR1DirectionalLightEntity->SetActive(rowIndex == 4u || rowIndex == 5u);
        if (auto* directional =
                m_pR1DirectionalLightEntity->GetComponent<Core::Component::DirectionalLightComponent>())
        {
            directional->SetCastShadows(rowIndex == 5u);
        }
        return true;
    }

    bool RenderingValidationSceneFixture::ApplyTransparentPhysicalLightingObjectPresence() const
    {
        if (!EnsureR1PhysicalFixture(true))
        {
            return false;
        }
        m_pR1TargetMesh->SetMaterial(0u, m_R1TargetMaterials[0]);
        m_pR1TargetMesh->SetVisible(true);
        m_pR1BackgroundMesh->SetVisible(true);
        m_pR1OccluderMesh->SetVisible(false);
        const bool bIndoor = m_SceneKind == SceneKind::Indoor;
        m_pR1PointLightEntity->SetActive(bIndoor);
        m_pR1DirectionalLightEntity->SetActive(!bIndoor);
        return true;
    }

    bool RenderingValidationSceneFixture::EnsureR3ShadowedShaftsFixture() const
    {
        if (m_bR3ShadowedShaftsPrepared)
        {
            return true;
        }
        if (m_bR3ShadowedShaftsFailed || m_SceneKind != SceneKind::Outdoor ||
            m_pWorld == nullptr || m_pResources == nullptr || m_pP4LightEntity == nullptr)
        {
            m_bR3ShadowedShaftsFailed = true;
            return false;
        }

        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> vertices;
        Core::Container::VariableArray<uint32_t> indices;
        constexpr float localPositions[4][3] = {
            {-1.0f, -1.0f, 0.0f},
            {1.0f, -1.0f, 0.0f},
            {-1.0f, 1.0f, 0.0f},
            {1.0f, 1.0f, 0.0f}};
        constexpr float localTexCoords[4][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
            {1.0f, 1.0f}};
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            Core::Rendering::Mesh3DVertex vertex{};
            vertex.Position[0] = localPositions[index][0];
            vertex.Position[1] = localPositions[index][1];
            vertex.Position[2] = localPositions[index][2];
            vertex.Normal[0] = 0.0f;
            vertex.Normal[1] = 0.0f;
            vertex.Normal[2] = 1.0f;
            vertex.TexCoord[0] = localTexCoords[index][0];
            vertex.TexCoord[1] = localTexCoords[index][1];
            vertices.push_back(vertex);
        }
        indices.push_back(0u);
        indices.push_back(1u);
        indices.push_back(2u);
        indices.push_back(1u);
        indices.push_back(3u);
        indices.push_back(2u);

        constexpr MeshDataHandle r3ShadowPlaneHandle{0x52300004u};
        if (!m_pResources->Meshes().Register(r3ShadowPlaneHandle,
                                              vertices.data(),
                                              vertices.size() * sizeof(Core::Rendering::Mesh3DVertex),
                                              indices.data(),
                                              static_cast<uint32_t>(indices.size())))
        {
            m_bR3ShadowedShaftsFailed = true;
            return false;
        }
        m_R3ShadowPlaneHandle = r3ShadowPlaneHandle;
        m_Lease.TrackMesh(r3ShadowPlaneHandle);

        constexpr float blackAlbedo[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        m_R3ShadowAlbedoTexture = CreateR1FloatTexture(
            *m_pResources,
            blackAlbedo,
            TEXT("RenderingValidationR3ShadowedShaftsAlbedo"));
        if (!m_R3ShadowAlbedoTexture.IsValid())
        {
            m_bR3ShadowedShaftsFailed = true;
            return false;
        }
        m_Lease.TrackTexture(m_R3ShadowAlbedoTexture);

        Core::Rendering::MaterialCreateData materialData;
        materialData.AlbedoTexture = m_R3ShadowAlbedoTexture;
        materialData.Blend = Core::Rendering::BlendMode::Opaque;
        materialData.bTwoSided = true;
        materialData.bCastShadows = true;
        materialData.DebugName = TEXT("RenderingValidationR3ShadowedShafts");
        m_R3ShadowMaterial = m_pResources->Materials().Create(materialData);
        if (!m_R3ShadowMaterial.IsValid())
        {
            m_bR3ShadowedShaftsFailed = true;
            return false;
        }
        m_Lease.TrackMaterial(m_R3ShadowMaterial);

        auto spawnShadowMesh = [this](Core::Entity*& outEntity,
                                      Core::Component::MeshComponent*& outMesh) -> bool
        {
            outEntity = m_pWorld->SpawnEntity();
            if (outEntity == nullptr)
            {
                return false;
            }
            m_Objects.push_back(outEntity);
            outMesh = m_pWorld->CreateComponent<Core::Component::MeshComponent>(outEntity);
            if (outMesh == nullptr)
            {
                return false;
            }
            outMesh->SetMeshHandle(m_R3ShadowPlaneHandle);
            outMesh->SetMaterial(0u, m_R3ShadowMaterial);
            outMesh->SetCastShadow(false);
            outMesh->SetReceiveShadow(false);
            outMesh->SetVisible(true);
            return true;
        };
        if (!spawnShadowMesh(m_pR3ShadowBackgroundEntity, m_pR3ShadowBackgroundMesh) ||
            !spawnShadowMesh(m_pR3ShadowOccluderEntity, m_pR3ShadowOccluderMesh))
        {
            m_bR3ShadowedShaftsFailed = true;
            return false;
        }

        m_pR3ShadowBackgroundEntity->SetPosition(0.0f, 0.0f, 20.0f);
        m_pR3ShadowBackgroundEntity->SetScale(14.0f, 14.0f, 1.0f);
        m_pR3ShadowOccluderEntity->SetPosition(0.0f, 0.0f, 8.0f);
        m_pR3ShadowOccluderEntity->SetScale(1.5f, 1.5f, 1.0f);

        m_R3ShadowedShaftsCamera = BuildLookAtCamera(Math::Vector3::Zero,
                                                     Math::Vector3(0.0f, 0.0f, 1.0f),
                                                     ValidationWidth,
                                                     ValidationHeight);
        m_R3ShadowedShaftsCamera.FarPlane = 30.0f;
        if (!Core::Component::CameraComponent::TryBuildExposureSnapshot(
                4.0f,
                1.0f / 60.0f,
                100.0f,
                0.0f,
                m_R3ShadowedShaftsCamera))
        {
            m_bR3ShadowedShaftsFailed = true;
            return false;
        }

        m_bR3ShadowedShaftsPrepared = true;
        return true;
    }

    bool RenderingValidationSceneFixture::ApplyR3ShadowedShaftsFixture(
        bool bOccluderCastsShadow,
        bool bDirectionalLightEnabled) const
    {
        if (!EnsureR3ShadowedShaftsFixture())
        {
            return false;
        }

        for (Core::Entity* entity : m_Objects)
        {
            if (entity == nullptr)
            {
                continue;
            }
            if (Core::Component::MeshComponent* mesh =
                    entity->GetComponent<Core::Component::MeshComponent>())
            {
                mesh->SetVisible(entity == m_pR3ShadowBackgroundEntity ||
                                 entity == m_pR3ShadowOccluderEntity);
            }
            if (Core::Component::LightComponent* light =
                    entity->GetComponent<Core::Component::LightComponent>())
            {
                const bool bMainDirectional = bDirectionalLightEnabled &&
                                              entity == m_pP4LightEntity;
                entity->SetActive(bMainDirectional);
                light->SetLightVisible(bMainDirectional);
            }
        }

        auto* directional =
            m_pP4LightEntity->GetComponent<Core::Component::DirectionalLightComponent>();
        if (directional == nullptr ||
            !directional->SetIntensityUnit(Core::Component::LightIntensityUnit::Lux))
        {
            return false;
        }
        directional->SetLightDirection(0.0f, 0.0f, -1.0f);
        directional->SetLightColor(1.0f, 1.0f, 1.0f);
        directional->SetIntensity(bDirectionalLightEnabled ? 10000.0f : 0.0f);
        directional->SetCastShadows(bDirectionalLightEnabled);
        directional->SetLightVisible(bDirectionalLightEnabled);
        m_pP4LightEntity->SetActive(bDirectionalLightEnabled);

        m_pR3ShadowBackgroundMesh->SetCastShadow(false);
        m_pR3ShadowOccluderMesh->SetCastShadow(bOccluderCastsShadow);
        return true;
    }

    const Core::Rendering::CameraProxy& RenderingValidationSceneFixture::GetCamera() const
    {
        return m_bR1PhysicalFixturePrepared ? m_R1PhysicalCamera : m_Layout.Camera;
    }

    const Core::Rendering::CameraProxy&
    RenderingValidationSceneFixture::GetR3ShadowedShaftsCamera() const
    {
        return m_R3ShadowedShaftsCamera;
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
