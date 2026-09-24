#include "Rendering/CascadedShadowLightMatrices.h"

#include "Math/MathTypes.h"
#include "Math/MatrixUtils.h"
#include "Math/VectorUtils.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float MinimumDepth = 0.01f;
        constexpr float MinimumRadius = 0.001f;
        constexpr float MinimumRadiusStep = 0.001f;
        constexpr float MinimumTexelSize = 0.0001f;
        constexpr float LightDistancePadding = 0.1f;

        struct CameraBasis
        {
            Math::Vector3 Position = Math::Vector3::Zero;
            Math::Vector3 Forward = Math::Vector3(0.0f, 0.0f, -1.0f);
            Math::Vector3 Right = Math::Vector3::UnitX;
            Math::Vector3 Up = Math::Vector3::UnitY;
        };

        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool IsFiniteMatrix(const Math::Matrix4x4& value)
        {
            for (uint32_t index = 0; index < 16u; ++index)
            {
                if (!std::isfinite(value.values[index]))
                {
                    return false;
                }
            }

            return true;
        }

        bool IsFiniteBounds(const BoundingSphere& bounds)
        {
            return std::isfinite(bounds.CenterX) &&
                std::isfinite(bounds.CenterY) &&
                std::isfinite(bounds.CenterZ) &&
                std::isfinite(bounds.Radius) &&
                bounds.Radius > 0.0f;
        }

        Math::Vector3 MakeBoundsCenter(const BoundingSphere& bounds)
        {
            return Math::Vector3(bounds.CenterX, bounds.CenterY, bounds.CenterZ);
        }

        float ResolveCameraAspect(const CameraProxy& camera)
        {
            if (std::isfinite(camera.AspectRatio) && camera.AspectRatio > Math::Constants::EPSILON)
            {
                return camera.AspectRatio;
            }

            if (std::isfinite(camera.Viewport.Width) &&
                std::isfinite(camera.Viewport.Height) &&
                camera.Viewport.Width > Math::Constants::EPSILON &&
                camera.Viewport.Height > Math::Constants::EPSILON)
            {
                const float viewportAspect = camera.Viewport.Width / camera.Viewport.Height;
                if (std::isfinite(viewportAspect) && viewportAspect > Math::Constants::EPSILON)
                {
                    return viewportAspect;
                }
            }

            return 1.0f;
        }

        bool BuildCameraBasis(const CameraProxy& camera, CameraBasis& outBasis)
        {
            const Math::Vector3 position(camera.PositionX, camera.PositionY, camera.PositionZ);
            const Math::Vector3 forwardInput(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
            const Math::Vector3 upInput(camera.UpX, camera.UpY, camera.UpZ);
            if (!IsFiniteVector(position) || !IsFiniteVector(forwardInput) || !IsFiniteVector(upInput))
            {
                return false;
            }

            if (Math::VectorUtils::Length(forwardInput) <= Math::Constants::EPSILON ||
                Math::VectorUtils::Length(upInput) <= Math::Constants::EPSILON)
            {
                return false;
            }

            const Math::Vector3 forward = Math::VectorUtils::Normalize(forwardInput);
            const Math::Vector3 viewZ = forward * -1.0f;
            const Math::Vector3 right = Math::VectorUtils::Normalize(
                Math::VectorUtils::Cross(upInput, viewZ));
            if (Math::VectorUtils::Length(right) <= Math::Constants::EPSILON)
            {
                return false;
            }

            const Math::Vector3 up = Math::VectorUtils::Normalize(
                Math::VectorUtils::Cross(viewZ, right));
            if (!IsFiniteVector(forward) || !IsFiniteVector(right) || !IsFiniteVector(up))
            {
                return false;
            }

            outBasis.Position = position;
            outBasis.Forward = forward;
            outBasis.Right = right;
            outBasis.Up = up;
            return true;
        }

        bool ValidateCameraProjection(const CameraProxy& camera)
        {
            if (!std::isfinite(camera.NearPlane) ||
                !std::isfinite(camera.FarPlane) ||
                camera.NearPlane <= 0.0f ||
                camera.FarPlane <= camera.NearPlane)
            {
                return false;
            }

            if (camera.Projection == ProjectionType::Orthographic)
            {
                return std::isfinite(camera.OrthoWidth) &&
                    std::isfinite(camera.OrthoHeight) &&
                    camera.OrthoWidth > 0.0f &&
                    camera.OrthoHeight > 0.0f;
            }

            return std::isfinite(camera.FieldOfView) &&
                camera.FieldOfView > 0.0f &&
                camera.FieldOfView < 179.0f;
        }

        bool PrepareSettings(const CascadedShadowMatrixSettings& settings,
                             float& outLambda,
                             float& outDepthPadding,
                             float& outLightDistance)
        {
            if (settings.CascadeCount != CSM_CASCADE_COUNT ||
                settings.ShadowMapResolution == 0u ||
                !std::isfinite(settings.SplitLambda) ||
                !std::isfinite(settings.DepthPadding) ||
                !std::isfinite(settings.Directional.LightDistance) ||
                settings.DepthPadding < 0.0f ||
                settings.Directional.LightDistance <= 0.0f)
            {
                return false;
            }

            outLambda = std::clamp(settings.SplitLambda, 0.0f, 1.0f);
            outDepthPadding = settings.DepthPadding;
            outLightDistance = settings.Directional.LightDistance;
            return true;
        }

        bool BuildSplitDistances(float nearPlane,
                                 float farPlane,
                                 float splitLambda,
                                 float* outSplitDistances)
        {
            if (outSplitDistances == nullptr ||
                !std::isfinite(nearPlane) ||
                !std::isfinite(farPlane) ||
                !std::isfinite(splitLambda) ||
                nearPlane <= 0.0f ||
                farPlane <= nearPlane)
            {
                return false;
            }

            const double logNear = std::log(static_cast<double>(nearPlane));
            const double logFar = std::log(static_cast<double>(farPlane));
            const double linearWeight = 1.0 - static_cast<double>(splitLambda);
            outSplitDistances[0] = nearPlane;

            for (uint32_t index = 1u; index < CSM_CASCADE_COUNT; ++index)
            {
                const double fraction = static_cast<double>(index) /
                    static_cast<double>(CSM_CASCADE_COUNT);
                const double logarithmicDistance = std::exp(
                    logNear + (logFar - logNear) * fraction);
                const double linearDistance =
                    static_cast<double>(nearPlane) +
                    (static_cast<double>(farPlane) - static_cast<double>(nearPlane)) * fraction;
                const double splitDistance =
                    logarithmicDistance * static_cast<double>(splitLambda) +
                    linearDistance * linearWeight;
                if (!std::isfinite(splitDistance) ||
                    splitDistance <= static_cast<double>(outSplitDistances[index - 1u]))
                {
                    return false;
                }

                outSplitDistances[index] = static_cast<float>(splitDistance);
                if (!std::isfinite(outSplitDistances[index]))
                {
                    return false;
                }
            }

            outSplitDistances[CSM_CASCADE_COUNT] = farPlane;
            return true;
        }

        Math::Vector3 SelectStableUpVector(const Math::Vector3& normalizedDirection)
        {
            const Math::Vector3 defaultUp = Math::Vector3::UnitY;
            if (std::abs(Math::VectorUtils::Dot(normalizedDirection, defaultUp)) <= 0.98f)
            {
                return defaultUp;
            }

            return Math::Vector3::UnitX;
        }

        bool BuildLightBasis(const Math::Vector3& normalizedDirection,
                             Math::Vector3& outRight,
                             Math::Vector3& outUp,
                             Math::Vector3& outViewZ)
        {
            outViewZ = normalizedDirection * -1.0f;
            const Math::Vector3 stableUp = SelectStableUpVector(normalizedDirection);
            outRight = Math::VectorUtils::Normalize(
                Math::VectorUtils::Cross(stableUp, outViewZ));
            if (Math::VectorUtils::Length(outRight) <= Math::Constants::EPSILON)
            {
                return false;
            }

            outUp = Math::VectorUtils::Normalize(
                Math::VectorUtils::Cross(outViewZ, outRight));
            return IsFiniteVector(outRight) && IsFiniteVector(outUp) && IsFiniteVector(outViewZ);
        }

        bool RoundToGrid(float coordinate, float gridSize, float& outCoordinate)
        {
            if (!std::isfinite(coordinate) ||
                !std::isfinite(gridSize) ||
                gridSize <= 0.0f)
            {
                return false;
            }

            const double gridCoordinate = static_cast<double>(coordinate) /
                static_cast<double>(gridSize);
            const double roundedCoordinate = std::floor(gridCoordinate + 0.5);
            const double snappedCoordinate = roundedCoordinate * static_cast<double>(gridSize);
            if (!std::isfinite(snappedCoordinate))
            {
                return false;
            }

            outCoordinate = static_cast<float>(snappedCoordinate);
            return std::isfinite(outCoordinate);
        }

        bool SnapCenterToTexel(const Math::Vector3& center,
                               const Math::Vector3& lightRight,
                               const Math::Vector3& lightUp,
                               float texelSize,
                               Math::Vector3& outSnappedCenter)
        {
            const float centerRight = Math::VectorUtils::Dot(center, lightRight);
            const float centerUp = Math::VectorUtils::Dot(center, lightUp);
            float snappedRight = 0.0f;
            float snappedUp = 0.0f;
            if (!RoundToGrid(centerRight, texelSize, snappedRight) ||
                !RoundToGrid(centerUp, texelSize, snappedUp))
            {
                return false;
            }

            outSnappedCenter = center +
                lightRight * (snappedRight - centerRight) +
                lightUp * (snappedUp - centerUp);
            return IsFiniteVector(outSnappedCenter);
        }

        float CalculateMaximumCornerDistance(const Math::Vector3& center,
                                             const Math::Vector3* corners)
        {
            float maximumDistance = 0.0f;
            for (uint32_t index = 0u; index < 8u; ++index)
            {
                maximumDistance = std::max(
                    maximumDistance,
                    Math::VectorUtils::Length(corners[index] - center));
            }

            return maximumDistance;
        }

        bool BuildFrustumSlice(const CameraProxy& camera,
                               const CameraBasis& basis,
                               float nearDistance,
                               float farDistance,
                               Math::Vector3* outCorners)
        {
            if (outCorners == nullptr ||
                !std::isfinite(nearDistance) ||
                !std::isfinite(farDistance) ||
                nearDistance <= 0.0f ||
                farDistance <= nearDistance)
            {
                return false;
            }

            float nearHalfWidth = 0.0f;
            float nearHalfHeight = 0.0f;
            float farHalfWidth = 0.0f;
            float farHalfHeight = 0.0f;
            if (camera.Projection == ProjectionType::Orthographic)
            {
                nearHalfWidth = camera.OrthoWidth * 0.5f;
                nearHalfHeight = camera.OrthoHeight * 0.5f;
                farHalfWidth = nearHalfWidth;
                farHalfHeight = nearHalfHeight;
            }
            else
            {
                const float fieldOfViewRadians = camera.FieldOfView *
                    (Math::Constants::PI / 180.0f);
                const float tangent = std::tan(fieldOfViewRadians * 0.5f);
                const float aspect = ResolveCameraAspect(camera);
                if (!std::isfinite(tangent) || tangent <= 0.0f || !std::isfinite(aspect))
                {
                    return false;
                }

                nearHalfHeight = tangent * nearDistance;
                nearHalfWidth = nearHalfHeight * aspect;
                farHalfHeight = tangent * farDistance;
                farHalfWidth = farHalfHeight * aspect;
            }

            if (!std::isfinite(nearHalfWidth) ||
                !std::isfinite(nearHalfHeight) ||
                !std::isfinite(farHalfWidth) ||
                !std::isfinite(farHalfHeight))
            {
                return false;
            }

            const Math::Vector3 nearCenter = basis.Position + basis.Forward * nearDistance;
            const Math::Vector3 farCenter = basis.Position + basis.Forward * farDistance;
            if (!IsFiniteVector(nearCenter) || !IsFiniteVector(farCenter))
            {
                return false;
            }

            outCorners[0] = nearCenter - basis.Right * nearHalfWidth - basis.Up * nearHalfHeight;
            outCorners[1] = nearCenter + basis.Right * nearHalfWidth - basis.Up * nearHalfHeight;
            outCorners[2] = nearCenter + basis.Right * nearHalfWidth + basis.Up * nearHalfHeight;
            outCorners[3] = nearCenter - basis.Right * nearHalfWidth + basis.Up * nearHalfHeight;
            outCorners[4] = farCenter - basis.Right * farHalfWidth - basis.Up * farHalfHeight;
            outCorners[5] = farCenter + basis.Right * farHalfWidth - basis.Up * farHalfHeight;
            outCorners[6] = farCenter + basis.Right * farHalfWidth + basis.Up * farHalfHeight;
            outCorners[7] = farCenter - basis.Right * farHalfWidth + basis.Up * farHalfHeight;

            for (uint32_t index = 0u; index < 8u; ++index)
            {
                if (!IsFiniteVector(outCorners[index]))
                {
                    return false;
                }
            }

            return true;
        }

        void IncludeRelativeDepth(float relativeDepth,
                                  float radius,
                                  float& inOutMinimum,
                                  float& inOutMaximum,
                                  bool& bHasDepth)
        {
            const float minimum = relativeDepth - radius;
            const float maximum = relativeDepth + radius;
            if (!std::isfinite(minimum) || !std::isfinite(maximum))
            {
                return;
            }

            if (!bHasDepth)
            {
                inOutMinimum = minimum;
                inOutMaximum = maximum;
                bHasDepth = true;
                return;
            }

            inOutMinimum = std::min(inOutMinimum, minimum);
            inOutMaximum = std::max(inOutMaximum, maximum);
        }

        bool BuildCascade(const CameraProxy& camera,
                          const CameraBasis& cameraBasis,
                          const Math::Vector3& normalizedDirection,
                          float nearDistance,
                          float farDistance,
                          float depthPadding,
                          float baseLightDistance,
                          uint32_t shadowMapResolution,
                          const Container::VariableArray<BoundingSphere>* casterBounds,
                          CascadedShadowCascade& outCascade)
        {
            Math::Vector3 corners[8];
            if (!BuildFrustumSlice(camera, cameraBasis, nearDistance, farDistance, corners))
            {
                return false;
            }

            Math::Vector3 center = Math::Vector3::Zero;
            for (uint32_t index = 0u; index < 8u; ++index)
            {
                center += corners[index];
            }
            center *= 1.0f / 8.0f;
            if (!IsFiniteVector(center))
            {
                return false;
            }

            float radius = CalculateMaximumCornerDistance(center, corners);
            if (!std::isfinite(radius))
            {
                return false;
            }
            radius = std::max(radius, MinimumRadius);

            Math::Vector3 lightRight;
            Math::Vector3 lightUp;
            Math::Vector3 lightViewZ;
            if (!BuildLightBasis(normalizedDirection, lightRight, lightUp, lightViewZ))
            {
                return false;
            }

            float texelSize = (2.0f * radius) /
                static_cast<float>(shadowMapResolution);
            if (!std::isfinite(texelSize) || texelSize <= 0.0f)
            {
                return false;
            }

            const float radiusStep = std::max(texelSize, MinimumRadiusStep);
            radius = std::ceil(radius / radiusStep) * radiusStep + radiusStep;
            if (!std::isfinite(radius) || radius <= 0.0f)
            {
                return false;
            }

            Math::Vector3 snappedCenter = center;
            for (uint32_t iteration = 0u; iteration < 3u; ++iteration)
            {
                texelSize = std::max(
                    (2.0f * radius) / static_cast<float>(shadowMapResolution),
                    MinimumTexelSize);
                if (!std::isfinite(texelSize) ||
                    !SnapCenterToTexel(center, lightRight, lightUp, texelSize, snappedCenter))
                {
                    return false;
                }

                const float snappedExtent = CalculateMaximumCornerDistance(snappedCenter, corners);
                if (!std::isfinite(snappedExtent))
                {
                    return false;
                }
                if (snappedExtent <= radius)
                {
                    break;
                }

                radius = snappedExtent;
            }

            const float finalExtent = CalculateMaximumCornerDistance(snappedCenter, corners);
            radius = std::max(radius, finalExtent);
            texelSize = std::max(
                (2.0f * radius) / static_cast<float>(shadowMapResolution),
                MinimumTexelSize);
            if (!std::isfinite(radius) ||
                !std::isfinite(texelSize) ||
                radius <= 0.0f ||
                !SnapCenterToTexel(center, lightRight, lightUp, texelSize, snappedCenter))
            {
                return false;
            }

            float minimumRelativeDepth = 0.0f;
            float maximumRelativeDepth = 0.0f;
            bool bHasDepth = false;
            for (uint32_t index = 0u; index < 8u; ++index)
            {
                const float relativeDepth = Math::VectorUtils::Dot(
                    corners[index] - snappedCenter, lightViewZ);
                IncludeRelativeDepth(relativeDepth, 0.0f,
                                     minimumRelativeDepth, maximumRelativeDepth, bHasDepth);
            }

            if (casterBounds != nullptr)
            {
                for (const BoundingSphere& bounds : *casterBounds)
                {
                    if (!IsFiniteBounds(bounds))
                    {
                        continue;
                    }

                    const float relativeDepth = Math::VectorUtils::Dot(
                        MakeBoundsCenter(bounds) - snappedCenter, lightViewZ);
                    IncludeRelativeDepth(relativeDepth, bounds.Radius,
                                         minimumRelativeDepth, maximumRelativeDepth, bHasDepth);
                }
            }

            if (!bHasDepth ||
                !std::isfinite(minimumRelativeDepth) ||
                !std::isfinite(maximumRelativeDepth))
            {
                return false;
            }

            const float requiredLightDistance =
                MinimumDepth + LightDistancePadding + depthPadding - minimumRelativeDepth;
            const float lightDistance = std::max(baseLightDistance, requiredLightDistance);
            if (!std::isfinite(lightDistance) || lightDistance <= 0.0f)
            {
                return false;
            }

            const Math::Vector3 lightPosition =
                snappedCenter - normalizedDirection * lightDistance;
            if (!IsFiniteVector(lightPosition))
            {
                return false;
            }

            const float nearDepth = std::max(
                MinimumDepth,
                lightDistance + minimumRelativeDepth - depthPadding);
            const float farDepth = lightDistance + maximumRelativeDepth + depthPadding;
            if (!std::isfinite(nearDepth) ||
                !std::isfinite(farDepth) ||
                farDepth <= nearDepth)
            {
                return false;
            }

            const Math::Matrix4x4 view = Math::MatrixUtils::CreateLookAt(
                lightPosition,
                snappedCenter,
                SelectStableUpVector(normalizedDirection));
            const Math::Matrix4x4 projection = Math::MatrixUtils::CreateOrthographic(
                radius * 2.0f,
                radius * 2.0f,
                nearDepth,
                farDepth);
            if (!IsFiniteMatrix(view) || !IsFiniteMatrix(projection))
            {
                return false;
            }

            outCascade.bEnabled = true;
            outCascade.NearDistance = nearDistance;
            outCascade.FarDistance = farDistance;
            outCascade.Radius = radius;
            outCascade.NearDepth = nearDepth;
            outCascade.FarDepth = farDepth;
            outCascade.TexelSize = texelSize;
            outCascade.OrthoSize = radius * 2.0f;
            outCascade.Center = center;
            outCascade.SnappedCenter = snappedCenter;
            outCascade.LightPosition = lightPosition;
            outCascade.Direction = normalizedDirection;
            outCascade.View = view;
            outCascade.Projection = projection;
            return true;
        }
    } // namespace

    CascadedShadowMatrixSettings MakeDefaultCascadedShadowMatrixSettings()
    {
        CascadedShadowMatrixSettings result;
        result.Directional = MakeDefaultDirectionalShadowMatrixSettings();
        return result;
    }

    CascadedShadowMatrixResult BuildCascadedShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const CameraProxy& camera,
        const CascadedShadowMatrixSettings& settings,
        const Container::VariableArray<BoundingSphere>* casterBounds)
    {
        CascadedShadowMatrixResult result;
        const uint32_t shaderVisibleDirectionalCount =
            CountShaderVisibleDirectionalLights(lightProxies);
        result.bHasMultipleDirectionalLights = shaderVisibleDirectionalCount > 1u;

        const LightProxy* selectedLight = SelectShadowedDirectionalLight(lightProxies);
        if (selectedLight == nullptr || !ValidateCameraProjection(camera))
        {
            return result;
        }

        float splitLambda = 0.0f;
        float depthPadding = 0.0f;
        float baseLightDistance = 0.0f;
        if (!PrepareSettings(settings, splitLambda, depthPadding, baseLightDistance))
        {
            return result;
        }

        CameraBasis cameraBasis;
        if (!BuildCameraBasis(camera, cameraBasis))
        {
            return result;
        }

        const Math::Vector3 lightDirection(
            selectedLight->DirectionX,
            selectedLight->DirectionY,
            selectedLight->DirectionZ);
        const Math::Vector3 normalizedDirection = Math::VectorUtils::Normalize(lightDirection);
        if (!IsFiniteVector(normalizedDirection) ||
            Math::VectorUtils::Length(normalizedDirection) <= Math::Constants::EPSILON)
        {
            return result;
        }

        CascadedShadowMatrixResult builtResult;
        builtResult.bHasMultipleDirectionalLights = result.bHasMultipleDirectionalLights;
        if (!BuildSplitDistances(camera.NearPlane,
                                 camera.FarPlane,
                                 splitLambda,
                                 builtResult.SplitDistances))
        {
            return result;
        }

        for (uint32_t index = 0u; index < CSM_CASCADE_COUNT; ++index)
        {
            if (!BuildCascade(camera,
                              cameraBasis,
                              normalizedDirection,
                              builtResult.SplitDistances[index],
                              builtResult.SplitDistances[index + 1u],
                              depthPadding,
                              baseLightDistance,
                              settings.ShadowMapResolution,
                              casterBounds,
                              builtResult.Cascades[index]))
            {
                return result;
            }
        }

        builtResult.bEnabled = true;
        builtResult.CascadeCount = CSM_CASCADE_COUNT;
        builtResult.LightId = selectedLight->LightId;
        builtResult.Direction = normalizedDirection;
        return builtResult;
    }

    CascadedShadowMatrixResult BuildCascadedShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const CameraProxy* camera,
        const CascadedShadowMatrixSettings& settings,
        const Container::VariableArray<BoundingSphere>* casterBounds)
    {
        CascadedShadowMatrixResult result;
        result.bHasMultipleDirectionalLights =
            CountShaderVisibleDirectionalLights(lightProxies) > 1u;
        if (camera == nullptr)
        {
            return result;
        }

        return BuildCascadedShadowLightMatrices(
            lightProxies, *camera, settings, casterBounds);
    }
} // namespace NorvesLib::Core::Rendering
