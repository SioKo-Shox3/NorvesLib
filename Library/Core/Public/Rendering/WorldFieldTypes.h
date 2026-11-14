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
    inline constexpr uint32_t WorldFieldDrynessMaxSources = 64u;
    inline constexpr uint32_t WorldFieldDrynessResolution = 256u;
    inline constexpr float WorldFieldDrynessDefaultNearTexelMeters = 0.25f;
    inline constexpr float WorldFieldDrynessDefaultFarTexelMeters = 2.0f;
    inline constexpr float WorldFieldDrynessDefaultNearBlendMeters = 4.0f;

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

    /** @brief std140 の UBO の配列へそのまま写す並び。式は Vegetation::DrynessMap::Sample と同じ。 */
    struct alignas(16) WorldFieldDrynessSource
    {
        float CenterXZ[2] = {};
        float RadiusA = 0.0f;
        float RadiusB = 0.0f;
        float CosYaw = 1.0f;
        float SinYaw = 0.0f;
        float Falloff = 0.0f;
        float Level = 0.0f;
        float Reserved0[4] = {};
        float Reserved1[4] = {};
    };
    static_assert(alignof(WorldFieldDrynessSource) == 16);
    static_assert(sizeof(WorldFieldDrynessSource) == 64);
    static_assert(offsetof(WorldFieldDrynessSource, CenterXZ) == 0);
    static_assert(offsetof(WorldFieldDrynessSource, RadiusA) == 8);
    static_assert(offsetof(WorldFieldDrynessSource, RadiusB) == 12);
    static_assert(offsetof(WorldFieldDrynessSource, CosYaw) == 16);
    static_assert(offsetof(WorldFieldDrynessSource, SinYaw) == 20);
    static_assert(offsetof(WorldFieldDrynessSource, Falloff) == 24);
    static_assert(offsetof(WorldFieldDrynessSource, Level) == 28);
    static_assert(offsetof(WorldFieldDrynessSource, Reserved0) == 32);
    static_assert(offsetof(WorldFieldDrynessSource, Reserved1) == 48);

    /** @brief 画像を記録した世界座標。uv = (worldXZ - OriginXZ) / ExtentMeters。 */
    struct WorldFieldState
    {
        double OriginXZ[2] = {};
        float ExtentMeters = 0.0f;
        float TexelSizeMeters = 0.0f;
        bool bValid = false;
    };

    /** @brief このフレームで公開する今と前の画像に、それぞれ対応する座標。 */
    struct WorldFieldFrameState
    {
        WorldFieldState Current;
        WorldFieldState Previous;
        WorldFieldState DrynessNear;
        WorldFieldState DrynessFar;
        float DrynessNearBlendMeters = 0.0f;
    };

    inline constexpr float WorldFieldDefaultBendScale = 0.7f;
    inline constexpr float WorldFieldDefaultFlattenScale = 0.7f;

    /** @brief 消費側の std140 UBO の末尾へ埋める定数（GLSL の同名構造体と同じ並び）。
     * 今・前とも有効で、WorldFieldCurrent / WorldFieldPrevious の両方を TryReadTexture できたときだけ使う。
     * 前の位置は、前の画像を PreviousOriginXZ と PreviousInvExtent で引き、同じ変形式で再評価する。
     */
    struct alignas(16) WorldFieldSampleConstants
    {
        float CurrentOriginXZ[2] = {};
        float CurrentInvExtent = 0.0f;
        uint32_t bEnabled = 0u;
        float PreviousOriginXZ[2] = {};
        float PreviousInvExtent = 0.0f;
        float Padding0 = 0.0f;
        float BendScale = 0.0f;
        float FlattenScale = 0.0f;
        float Padding[2] = {};
    };
    static_assert(alignof(WorldFieldSampleConstants) == 16);
    static_assert(sizeof(WorldFieldSampleConstants) == 48);
    static_assert(offsetof(WorldFieldSampleConstants, CurrentOriginXZ) == 0);
    static_assert(offsetof(WorldFieldSampleConstants, CurrentInvExtent) == 8);
    static_assert(offsetof(WorldFieldSampleConstants, bEnabled) == 12);
    static_assert(offsetof(WorldFieldSampleConstants, PreviousOriginXZ) == 16);
    static_assert(offsetof(WorldFieldSampleConstants, PreviousInvExtent) == 24);
    static_assert(offsetof(WorldFieldSampleConstants, Padding0) == 28);
    static_assert(offsetof(WorldFieldSampleConstants, BendScale) == 32);
    static_assert(offsetof(WorldFieldSampleConstants, FlattenScale) == 36);
    static_assert(offsetof(WorldFieldSampleConstants, Padding) == 40);

    /** @brief 両方の座標が有効なら係数を設定し、無効なら詰め物も含めてすべて 0 を返す。 */
    WorldFieldSampleConstants BuildWorldFieldSampleConstants(const WorldFieldFrameState& state);

    /** @brief 近傍・遠景の枯れ度を参照する std140 UBO の定数。 */
    struct alignas(16) WorldFieldDrynessSampleConstants
    {
        float NearOriginXZ[2] = {};
        float NearInvExtent = 0.0f;
        uint32_t bEnabled = 0u;
        float FarOriginXZ[2] = {};
        float FarInvExtent = 0.0f;
        float NearBlendMeters = 0.0f;
    };
    static_assert(alignof(WorldFieldDrynessSampleConstants) == 16);
    static_assert(sizeof(WorldFieldDrynessSampleConstants) == 32);
    static_assert(offsetof(WorldFieldDrynessSampleConstants, NearOriginXZ) == 0);
    static_assert(offsetof(WorldFieldDrynessSampleConstants, NearInvExtent) == 8);
    static_assert(offsetof(WorldFieldDrynessSampleConstants, bEnabled) == 12);
    static_assert(offsetof(WorldFieldDrynessSampleConstants, FarOriginXZ) == 16);
    static_assert(offsetof(WorldFieldDrynessSampleConstants, FarInvExtent) == 24);
    static_assert(offsetof(WorldFieldDrynessSampleConstants, NearBlendMeters) == 28);

    /** @brief 両方の座標が有効なら係数を設定し、無効ならすべて 0 を返す。 */
    WorldFieldDrynessSampleConstants BuildWorldFieldDrynessSampleConstants(const WorldFieldFrameState& state);

    /** @brief 根元で引いた RGBA から世界空間のずれを返す。GLSL の WorldFieldDisplacement と同じ式。
     * h=max(height,0)、s=clamp(t,0,1)、水平のずれ=RG*h*bendScale*s*s、
     * 高さのずれ=-h*s*flattenScale*clamp(B,0,1)。A は変形に使わない。
     * 既定値では最大曲げで先端が丈の 0.7 倍横へ動き、最大押し倒しで高さが元の 0.3 倍になる。
     * 根元の位置はサンプル座標にだけ使い、返したずれを変形前の頂点位置へ加える。
     */
    void WorldFieldDisplacement(const float field[4], float height, float t, float displacement[3],
        float bendScale = WorldFieldDefaultBendScale, float flattenScale = WorldFieldDefaultFlattenScale);

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
        bool bDrynessEnabled = true;
        float DrynessNearTexelMeters = WorldFieldDrynessDefaultNearTexelMeters;
        float DrynessFarTexelMeters = WorldFieldDrynessDefaultFarTexelMeters;
        float DrynessNearBlendMeters = WorldFieldDrynessDefaultNearBlendMeters;
    };

    float GetWorldFieldExtentMeters(WorldFieldRange range);
    float GetWorldFieldTexelSizeMeters(WorldFieldRange range);
    WorldFieldSettings SanitizeWorldFieldSettings(const WorldFieldSettings& settings);

    /** @brief 先頭32入力だけを検査して出力を置換する。半径は0..50 m、強さは0..1、未知のチャンネルは除く。
     * 非有限値、両半径0、強さ0は除く。入力が出力自身を参照していても使用できる。
     */
    uint32_t SanitizeWorldFieldInteractors(Container::Span<const WorldFieldInteractor> input,
        Container::VariableArray<WorldFieldInteractor>& output);

    /** @brief 先頭 WorldFieldDrynessMaxSources 件だけを検査して出力を置換し、残した数を返す。
     * 入力が出力自身を参照していても使用できる。
     */
    uint32_t SanitizeWorldFieldDrynessSources(Container::Span<const WorldFieldDrynessSource> input,
        Container::VariableArray<WorldFieldDrynessSource>& output);

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
