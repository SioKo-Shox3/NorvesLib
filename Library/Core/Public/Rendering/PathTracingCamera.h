// FramePacketの前後値だけで薄レンズとシャッターの試料を構築する。
#pragma once

#include "Rendering/SceneProxy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 24 mmの撮像面高を使うPTカメラの決定論的な1試料。 */
    struct PathTracingCameraSample
    {
        CameraProxy Camera;
        float LensOffset[3] = {};
        float ShutterTime = 1.0f;
        bool bThinLens = false;
    };

    namespace PathTracingCameraDetail
    {
        constexpr float SensorHeight = 0.024f;
        constexpr float Pi = 3.14159265358979323846f;

        inline float Halton(uint32_t index, uint32_t base)
        {
            float result = 0.0f;
            float scale = 1.0f / static_cast<float>(base);
            while (index > 0u)
            {
                result += scale * static_cast<float>(index % base);
                index /= base;
                scale /= static_cast<float>(base);
            }
            return result;
        }

        inline bool Normalize(float vector[3])
        {
            const float lengthSquared = vector[0] * vector[0] +
                                        vector[1] * vector[1] +
                                        vector[2] * vector[2];
            if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-12f)
            {
                return false;
            }
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                vector[axis] *= inverseLength;
            }
            return true;
        }

        inline float FocalLength(float fieldOfViewDegrees)
        {
            if (!std::isfinite(fieldOfViewDegrees) ||
                fieldOfViewDegrees <= 0.0f || fieldOfViewDegrees >= 179.0f)
            {
                return 0.0f;
            }
            return SensorHeight /
                   (2.0f * std::tan(fieldOfViewDegrees * Pi / 360.0f));
        }
    }

    inline PathTracingCameraSample SamplePathTracingCamera(
        const CameraProxy& current, const CameraProxy* previous,
        float frameDeltaTime, uint32_t sampleIndex)
    {
        PathTracingCameraSample result;
        result.Camera = current;
        const uint32_t sequenceIndex = sampleIndex + 1u;
        if (std::isfinite(frameDeltaTime) && frameDeltaTime > 0.0f &&
            std::isfinite(current.ShutterSpeed) && current.ShutterSpeed > 0.0f)
        {
            const float shutterFraction =
                std::clamp(current.ShutterSpeed / frameDeltaTime, 0.0f, 1.0f);
            result.ShutterTime = 1.0f - shutterFraction *
                (1.0f - PathTracingCameraDetail::Halton(sequenceIndex, 5u));
        }
        if (previous && previous->CameraId == current.CameraId &&
            previous->Projection == current.Projection)
        {
            const float previousPosition[3] = {
                previous->PositionX, previous->PositionY, previous->PositionZ};
            float* position[3] = {
                &result.Camera.PositionX, &result.Camera.PositionY,
                &result.Camera.PositionZ};
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                *position[axis] = std::lerp(previousPosition[axis],
                                            *position[axis], result.ShutterTime);
            }
            float forward[3] = {
                std::lerp(previous->ForwardX, current.ForwardX, result.ShutterTime),
                std::lerp(previous->ForwardY, current.ForwardY, result.ShutterTime),
                std::lerp(previous->ForwardZ, current.ForwardZ, result.ShutterTime)};
            float up[3] = {
                std::lerp(previous->UpX, current.UpX, result.ShutterTime),
                std::lerp(previous->UpY, current.UpY, result.ShutterTime),
                std::lerp(previous->UpZ, current.UpZ, result.ShutterTime)};
            if (PathTracingCameraDetail::Normalize(forward) &&
                PathTracingCameraDetail::Normalize(up))
            {
                result.Camera.ForwardX = forward[0];
                result.Camera.ForwardY = forward[1];
                result.Camera.ForwardZ = forward[2];
                result.Camera.UpX = up[0];
                result.Camera.UpY = up[1];
                result.Camera.UpZ = up[2];
            }
            result.Camera.FieldOfView = std::lerp(previous->FieldOfView,
                                                   current.FieldOfView,
                                                   result.ShutterTime);
        }

        const float focalLength = PathTracingCameraDetail::FocalLength(
            result.Camera.FieldOfView);
        if (result.Camera.Projection != ProjectionType::Perspective ||
            !std::isfinite(current.FocusDistance) ||
            current.FocusDistance <= focalLength || focalLength <= 0.0f ||
            !std::isfinite(current.FarPlane) || current.FarPlane <= 0.0f ||
            !std::isfinite(current.Aperture) || current.Aperture <= 0.0f)
        {
            return result;
        }
        float forward[3] = {result.Camera.ForwardX, result.Camera.ForwardY,
                            result.Camera.ForwardZ};
        float up[3] = {result.Camera.UpX, result.Camera.UpY,
                       result.Camera.UpZ};
        if (!PathTracingCameraDetail::Normalize(forward) ||
            !PathTracingCameraDetail::Normalize(up))
        {
            return result;
        }
        float right[3] = {
            forward[1] * up[2] - forward[2] * up[1],
            forward[2] * up[0] - forward[0] * up[2],
            forward[0] * up[1] - forward[1] * up[0]};
        if (!PathTracingCameraDetail::Normalize(right))
        {
            return result;
        }
        up[0] = right[1] * forward[2] - right[2] * forward[1];
        up[1] = right[2] * forward[0] - right[0] * forward[2];
        up[2] = right[0] * forward[1] - right[1] * forward[0];
        const float lensRadius = focalLength / (2.0f * current.Aperture);
        const float radius = lensRadius * std::sqrt(
            PathTracingCameraDetail::Halton(sequenceIndex, 2u));
        const float phase = 2.0f * PathTracingCameraDetail::Pi *
            PathTracingCameraDetail::Halton(sequenceIndex, 3u);
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            result.LensOffset[axis] = radius *
                (std::cos(phase) * right[axis] + std::sin(phase) * up[axis]);
        }
        result.bThinLens = true;
        return result;
    }

    inline float ComputePathTracingCocPixels(const CameraProxy& camera,
                                              float objectDistance,
                                              uint32_t imageHeight)
    {
        const float focalLength = PathTracingCameraDetail::FocalLength(
            camera.FieldOfView);
        if (imageHeight == 0u || focalLength <= 0.0f ||
            !std::isfinite(camera.FocusDistance) ||
            camera.FocusDistance <= focalLength ||
            !std::isfinite(objectDistance) || objectDistance <= focalLength ||
            !std::isfinite(camera.Aperture) || camera.Aperture <= 0.0f)
        {
            return 0.0f;
        }
        const float diameter = focalLength / camera.Aperture;
        return diameter * focalLength *
               std::abs(objectDistance - camera.FocusDistance) /
               (objectDistance * (camera.FocusDistance - focalLength)) *
               static_cast<float>(imageHeight) /
               PathTracingCameraDetail::SensorHeight;
    }

    /** @brief 線形部分が一致する並進だけを補間し、回転・scale・shear変化を拒否する。 */
    inline bool InterpolatePathTracingTransform(const float previous[12],
                                                 const float current[12],
                                                 float shutterTime,
                                                 float output[12])
    {
        if (!previous || !current || !output ||
            !std::isfinite(shutterTime) || shutterTime < 0.0f ||
            shutterTime > 1.0f)
        {
            return false;
        }
        for (uint32_t index = 0u; index < 12u; ++index)
        {
            if (!std::isfinite(previous[index]) ||
                !std::isfinite(current[index]))
            {
                return false;
            }
            if (index % 4u != 3u &&
                std::abs(previous[index] - current[index]) > 1.0e-5f)
            {
                return false;
            }
            output[index] = index % 4u == 3u
                                ? std::lerp(previous[index], current[index],
                                            shutterTime)
                                : current[index];
        }
        return true;
    }
}
