#pragma once

#include "Math/Vector2.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    inline constexpr std::uint32_t DDGIMaxProbeCount = 1024u;
    inline constexpr std::uint32_t DDGIProbeRayDirectionCount = 64u;

    /**
     * @brief 手動配置するDDGIプローブ格子の値設定
     *
     * OriginとProbeSpacingはワールド単位で、各spacingは正数とする。
     * 既定値は有限だが無効で、明示的に有効化するまでDDGIを使用しない。
     */
    struct DDGIVolumeParameters
    {
        bool bEnabled = false;
        Math::Vector3 Origin = Math::Vector3::Zero;
        Math::Vector3 ProbeSpacing = Math::Vector3::One;
        std::uint32_t ProbeCountX = 1u;
        std::uint32_t ProbeCountY = 1u;
        std::uint32_t ProbeCountZ = 1u;
    };

    DDGIVolumeParameters MakeDefaultDDGIVolumeParameters();

    /**
     * @brief volumeが有効で、全設定値と格子終端が有限範囲内か検証する
     */
    bool IsDDGIVolumeValid(const DDGIVolumeParameters& parameters);

    /**
     * @brief 無効な設定を無効状態の既定volumeへ戻す
     */
    DDGIVolumeParameters SanitizeDDGIVolumeParameters(
        const DDGIVolumeParameters& parameters);

    /**
     * @brief 必要なレイトレーシング機能がなければボリュームを無効化する
     */
    inline DDGIVolumeParameters SanitizeDDGIVolumeParametersForRHI(
        const DDGIVolumeParameters& parameters,
        bool bSupportsAccelerationStructure,
        bool bSupportsRayQuery)
    {
        if (!bSupportsAccelerationStructure || !bSupportsRayQuery)
        {
            return MakeDefaultDDGIVolumeParameters();
        }
        return SanitizeDDGIVolumeParameters(parameters);
    }

    /**
     * @brief 有効なvolumeの総probe数を返す。無効なvolumeでは0を返す
     */
    std::uint32_t GetDDGIProbeCount(const DDGIVolumeParameters& parameters);

    /**
     * @brief X最速のrow-major順で格子座標をprobe indexへ変換する
     *
     * 失敗時はoutIndexを0にしてfalseを返す。
     */
    bool TryGetDDGIProbeIndex(
        const DDGIVolumeParameters& parameters,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z,
        std::uint32_t& outIndex);

    /**
     * @brief X最速のrow-major順でprobe indexを格子座標へ変換する
     *
     * 失敗時は全座標を0にしてfalseを返す。
     */
    bool TryGetDDGIProbeCoordinates(
        const DDGIVolumeParameters& parameters,
        std::uint32_t index,
        std::uint32_t& outX,
        std::uint32_t& outY,
        std::uint32_t& outZ);

    /**
     * @brief 有限な非ゼロ方向をoctahedral UVへ写像する
     *
     * +ZはUV中心に対応する。入力が無効ならUV(0.5, 0.5)へ戻してfalseを返す。
     */
    bool EncodeDDGIOctahedralDirection(
        const Math::Vector3& direction,
        Math::Vector2& outUv);

    /**
     * @brief 0..1のoctahedral UVを正規化方向へ戻す
     *
     * 入力が無効なら+Zへ戻してfalseを返す。
     */
    bool DecodeDDGIOctahedralDirection(
        const Math::Vector2& uv,
        Math::Vector3& outDirection);

    /**
     * @brief 固定64本の球面Fibonacci ray列から指定方向を返す
     *
     * indexが範囲外なら+Zへ戻してfalseを返す。
     */
    bool TryGetDDGIProbeRayDirection(
        std::uint32_t index,
        Math::Vector3& outDirection);

} // namespace NorvesLib::Core::Rendering
