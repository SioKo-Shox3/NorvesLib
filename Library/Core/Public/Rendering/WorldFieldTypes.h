#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    // RG は曲げの向き、B は押し倒し、A は痕跡（予約）に割り当てる。
    inline constexpr uint32_t WorldFieldChannelBend = 1u << 0;
    inline constexpr uint32_t WorldFieldChannelFlatten = 1u << 1;
    inline constexpr uint32_t WorldFieldChannelTrail = 1u << 2;
    inline constexpr uint32_t WorldFieldChannelAll = 0x7u;
    inline constexpr uint32_t WorldFieldMaxInteractors = 32u;
    inline constexpr uint32_t WorldFieldResolution = 256u;

    /** @brief 世界座標 m のカプセル。std430 の配列へそのまま転送できる並び。 */
    struct alignas(16) WorldFieldInteractor
    {
        float P0[3] = {};
        float Radius = 0.0f;
        float P1[3] = {};
        float Strength = 1.0f;
        float PrevP0[3] = {};
        float PrevRadius = 0.0f;
        float PrevP1[3] = {};
        uint32_t ChannelMask = WorldFieldChannelAll;
    };
    static_assert(sizeof(WorldFieldInteractor) == 64);
    static_assert(offsetof(WorldFieldInteractor, P0) == 0);
    static_assert(offsetof(WorldFieldInteractor, Radius) == 12);
    static_assert(offsetof(WorldFieldInteractor, P1) == 16);
    static_assert(offsetof(WorldFieldInteractor, Strength) == 28);
    static_assert(offsetof(WorldFieldInteractor, PrevP0) == 32);
    static_assert(offsetof(WorldFieldInteractor, PrevRadius) == 44);
    static_assert(offsetof(WorldFieldInteractor, PrevP1) == 48);
    static_assert(offsetof(WorldFieldInteractor, ChannelMask) == 60);

    enum class WorldFieldRange : uint8_t
    {
        Range64m,
        Range128m
    };

    enum class WorldFieldCenterMode : uint8_t
    {
        Camera,
        // カメラ位置に視線方向 XZ × ForwardOffsetMeters を加える。
        CameraForward
    };

    struct WorldFieldSettings
    {
        bool bEnabled = true;
        WorldFieldRange Range = WorldFieldRange::Range64m;
        WorldFieldCenterMode CenterMode = WorldFieldCenterMode::Camera;
        float ForwardOffsetMeters = 10.0f;
        float DecayTimeConstantSeconds = 1.5f;
    };

    float GetWorldFieldExtentMeters(WorldFieldRange range);
    float GetWorldFieldTexelSizeMeters(WorldFieldRange range);
    WorldFieldSettings SanitizeWorldFieldSettings(const WorldFieldSettings& settings);

    /** @brief 先頭32入力だけを検査して出力を置換する。半径は0..50 m、強さは0..1、未知のチャンネルは除く。
     * 非有限値、両半径0、強さ0は除く。入力が出力自身を参照していても使用できる。
     */
    uint32_t SanitizeWorldFieldInteractors(Container::Span<const WorldFieldInteractor> input,
        Container::VariableArray<WorldFieldInteractor>& output);

    /** @brief フィールド左下の原点を表す整数画素座標（XZ）。初回は無効にする。 */
    struct WorldFieldOrigin
    {
        int64_t X = 0;
        int64_t Z = 0;
        bool bValid = false;
    };

    struct WorldFieldScrollResult
    {
        WorldFieldOrigin Origin;
        int64_t ShiftX = 0;
        int64_t ShiftZ = 0;
        double OriginXMeters = 0.0;
        double OriginZMeters = 0.0;
        bool bReset = true;
    };

    /** @brief 中心画素=floor(center/texel)、原点=中心画素-resolution/2（整数除算）。
     * 負の中心も負方向へ丸め、境界ちょうどはその境界から始まる画素に属する。
     * 中心画素は±2^60に丸め、その際はreset。無効入力は無効原点とresetを返す。
     * shiftは新原点−前原点（差の桁あふれ時はint64_tの端へ丸める）。
     * 消費側は uv = (worldXZ - originXZ) / extent で参照する。
     */
    WorldFieldScrollResult CalculateWorldFieldScroll(const WorldFieldOrigin& previousOrigin,
        double centerXMeters, double centerZMeters, double texelSizeMeters,
        uint32_t resolution = WorldFieldResolution);
} // 名前空間 NorvesLib::Core::Rendering
