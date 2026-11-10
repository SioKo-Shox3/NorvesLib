#pragma once

#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 世界で共通に使う水平な風の設定。 */
    struct WindParameters
    {
        /// 共通の風を使うか。false では草・低木は従来の風を使う。
        bool bEnabled = false;
        /// 風の吹いていく向き[度]。Y 軸周りで 0 は +X、90 は +Z。正規化後は [0, 360)。
        float DirectionYawDegrees = 0.0f;
        /// 平均風速[m/s]。範囲 [0, 100]。
        float BaseSpeed = 3.0f;
        /// 平均風速に対する突風の振れ幅の比。範囲 [0, 1]。
        float GustStrength = 0.4f;
        /// 突風の時間周波数[Hz]。入力は (0, 10]、正規化後は 1/4096 Hz の整数倍。
        float GustFrequencyHz = 0.125f;
        /// 突風の塊の大きさ[m]。範囲 [0.1, 10000]。
        float GustScaleMeters = 20.0f;
        /// 平均風速に対する風向と直交する乱流の振れ幅の比。範囲 [0, 1]。
        float Turbulence = 0.15f;
    };

    /// 共通の風が無効な既定の設定を返す。
    WindParameters MakeDefaultWindParameters();

    /// 非有限値と範囲外の値を項目ごとの既定値に戻し、風向の折り返しと周波数の量子化を行う。
    WindParameters SanitizeWindParameters(const WindParameters& in);

    /// ゲーム開始からの時刻[s]を 4096 秒で折り返した風の時刻[s]へ変換する。
    float WindClockSeconds(double gameTotalTime);

    /// 整えた設定から風の吹いていく水平単位方向 XZ（各成分 [-1, 1]）を返す。
    void WindDirectionXZ(const WindParameters& p, float& outX, float& outZ);

    /// 32 ビットの入力を 32 ビットのハッシュへ変換する。
    uint32_t WindHash(uint32_t x);

    /// 有限な無次元座標 x の値ノイズを [0, 1] で返す。floor(x) は int32 の範囲内とする。
    float WindValueNoise1D(float x, uint32_t seed);

    /**
     * @brief 整えた設定、水平単位方向、水平位置[m]、風の時刻[s]から突風の値 [0, 1] を返す。
     * 突風の無次元座標は有限で、floor が int32 の範囲内とする。
     */
    float WindGustNoise(const WindParameters& p, float dirX, float dirZ, float posX, float posZ, float time);

    /**
     * @brief 整えた設定とワールド位置[m]、風の時刻[s]から風速[m/s]を返す。無効ならゼロベクトル。
     * 突風の無次元座標とその 3.1 倍は有限で、それぞれの floor が int32 の範囲内とする。
     */
    Math::Vector3 EvaluateWind(const WindParameters& p, const Math::Vector3& position, float time);

    /// 風の時計の折り返し周期[s]。
    inline constexpr double WindClockWrapSeconds = 4096.0;
    /// 乱流の値ノイズに用いる 32 ビットの種。
    inline constexpr uint32_t WindTurbulenceSeed = 0x68bc21ebu;
} // namespace NorvesLib::Core::Rendering
