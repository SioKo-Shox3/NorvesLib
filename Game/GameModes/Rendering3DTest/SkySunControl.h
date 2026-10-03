#pragma once

// 起動画面（Rendering3DTest）の空の太陽を、方向ライト操作（LightController）の角度で動かすための変換。
//
// LightController は光の進行方向を Yaw/Pitch（度）で持つ（進行方向 = (cosP sinY, sinP, cosP cosY)）。
// 空の太陽は観測点から太陽へ向かう方向を仰角・方位（度）で持つ
// （太陽方向 = (cosA cosZ, sinA, cosA sinZ)、SkyAtmosphere.h）。太陽方向は進行方向の逆向き。
// あわせて、時刻に合わせる手動露出（EV100）とシャッター速度の換算も置く。

#include <algorithm>
#include <cmath>

namespace Game::GameModes
{

    inline constexpr float kSkySunDegreesToRadians = 3.14159265358979323846f / 180.0f;

    /**
     * @brief 空の太陽の仰角・方位を、方向ライト操作の Yaw/Pitch へ変換する
     */
    inline void ConvertSkySunToLightControllerAngles(float altitudeDegrees, float azimuthDegrees,
                                                     float& outYawDegrees, float& outPitchDegrees)
    {
        const float azimuth = azimuthDegrees * kSkySunDegreesToRadians;
        outPitchDegrees = -altitudeDegrees;
        outYawDegrees = std::atan2(-std::cos(azimuth), -std::sin(azimuth)) / kSkySunDegreesToRadians;
        if (outYawDegrees < 0.0f)
        {
            outYawDegrees += 360.0f;
        }
    }

    /**
     * @brief 方向ライト操作の Yaw/Pitch を、空の太陽の仰角（0〜90）・方位（-180〜180）へ変換する
     *
     * 光が上向きに進む（太陽が地平線の下になる）角度は仰角0へ丸める。
     */
    inline void ConvertLightControllerAnglesToSkySun(float yawDegrees, float pitchDegrees,
                                                     float& outAltitudeDegrees, float& outAzimuthDegrees)
    {
        const float yaw = yawDegrees * kSkySunDegreesToRadians;
        outAltitudeDegrees = std::clamp(-pitchDegrees, 0.0f, 90.0f);
        outAzimuthDegrees = std::atan2(-std::cos(yaw), -std::sin(yaw)) / kSkySunDegreesToRadians;
    }

    /**
     * @brief 絞り値・ISO を保ったまま、手動露出 EV100 になるシャッター速度（秒）を求める
     *
     * EV100 = log2(N^2 / t * 100 / ISO)（CameraComponent と同じ定義）を t について解く。
     */
    inline float ComputeShutterSpeedForEV100(float aperture, float iso, float ev100)
    {
        return aperture * aperture * (100.0f / iso) / std::exp2(ev100);
    }

    /**
     * @brief 絞り値・シャッター速度（秒）・ISO から EV100 を求める
     */
    inline float ComputeEV100(float aperture, float shutterSpeed, float iso)
    {
        return std::log2(aperture * aperture / shutterSpeed * (100.0f / iso));
    }

} // namespace Game::GameModes
