#pragma once

#include "Rendering/GrassFieldRenderData.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct MegaHiZContract;

    inline constexpr uint32_t kGrassLodCount = 3;
    inline constexpr uint32_t kGrassHashSaltJitterX = 0;
    inline constexpr uint32_t kGrassHashSaltJitterZ = 1;
    inline constexpr uint32_t kGrassHashSaltKeep = 2;
    inline constexpr uint32_t kGrassHashSaltHeight = 3;
    inline constexpr uint32_t kGrassHashSaltFacing = 4;
    inline constexpr uint32_t kGrassGenerateFlagHiZ = 1u;
    // 1/255 の binary32。GLSL は uintBitsToFloat(0x3b808081u) として同じ値を使う。
    static constexpr float kGrassInv255 = 0x1.010102p-8f;

    /**
     * @brief 草の生成で CPU と GPU が共有する設定。
     * 寸法・間隔・量子化の最大値は有限の正数、距離の節は昇順とする。
     * TileSizeMeters は各間隔で割り切れ、チャンク寸法はタイル寸法の整数倍とする。
     * 帯はタイルの最も近い点までの水平距離が BandMaxDistanceMeters 以下の最小の添字。
     * 最後の帯より遠いタイルは選別側で除外する。
     */
    struct GrassGenerateSettings
    {
        float TileSizeMeters = 8.0f;
        float BandMaxDistanceMeters[3] = {25.0f, 60.0f, 120.0f};
        float BandSpacingMeters[3] = {0.125f, 0.25f, 0.5f};
        // 定数構築時に距離を二乗し、その二乗距離の間で KeepFractions を線形補間する。
        float KeepDistancesMeters[4] = {0.0f, 25.0f, 60.0f, 120.0f};
        float KeepFractions[4] = {1.0f, 0.25f, 0.0625f, 0.015625f};
        float FadeStartFraction = 0.9f;
        float FacingCullThreshold = 0.1f;
        uint32_t LodCapacity[3] = {1u << 19, 1u << 18, 1u << 17};
        uint32_t LodSegments[3] = {7, 3, 1};
        float HeightQuantMaxMeters = 2.0f;
        float WidthQuantMaxMeters = 0.25f;
        // 選別側が GrassTileInfo::GroundY に入れる地面の高さ。
        float GroundHeightMeters = 0.0f;
    };

    /** @brief std430 のタイル。原点は TileX/Z * TileSizeMeters、1 タイルは 1 チャンク内。 */
    struct GrassTileInfo
    {
        float OriginX = 0.0f;
        float OriginZ = 0.0f;
        float GroundY = 0.0f;
        uint32_t Band = 0;
        // GrassFloorDiv(TileX/Z, TilesPerChunk) の座標を GrassWindowLayerIndex に渡した層。
        uint32_t LayerIndex = 0;
        uint32_t Flags = 0; // 予約。現時点では 0。
        int32_t TileX = 0;
        int32_t TileZ = 0;
    };
    static_assert(sizeof(GrassTileInfo) == 32);
    static_assert(offsetof(GrassTileInfo, OriginX) == 0);
    static_assert(offsetof(GrassTileInfo, OriginZ) == 4);
    static_assert(offsetof(GrassTileInfo, GroundY) == 8);
    static_assert(offsetof(GrassTileInfo, Band) == 12);
    static_assert(offsetof(GrassTileInfo, LayerIndex) == 16);
    static_assert(offsetof(GrassTileInfo, Flags) == 20);
    static_assert(offsetof(GrassTileInfo, TileX) == 24);
    static_assert(offsetof(GrassTileInfo, TileZ) == 28);

    /** @brief 正の divisor（INT32_MAX 以下）で整数を床除算する。GLSL も同じ整数演算を使う。 */
    inline int32_t GrassFloorDiv(int32_t value, uint32_t divisor)
    {
        const int32_t size = static_cast<int32_t>(divisor);
        const int32_t quotient = value / size;
        const int32_t remainder = value % size;
        return quotient - (remainder < 0 ? 1 : 0);
    }

    /** @brief 負座標も正の剰余に直し、N * N 層の窓の層番号を返す。n は正数。 */
    inline uint32_t GrassWindowLayerIndex(int32_t cx, int32_t cz, uint32_t n)
    {
        const int64_t size = static_cast<int64_t>(n);
        const uint32_t x = static_cast<uint32_t>((static_cast<int64_t>(cx) % size + size) % size);
        const uint32_t z = static_cast<uint32_t>((static_cast<int64_t>(cz) % size + size) % size);
        return z * n + x;
    }

    /** @brief std430 のブレード。Packed は下から高さ 10、幅 8、種別 6、fade 8 ビット。 */
    struct GrassBladeRecord
    {
        float PositionX = 0.0f;
        float PositionY = 0.0f;
        float PositionZ = 0.0f;
        uint32_t Packed = 0;
    };
    static_assert(sizeof(GrassBladeRecord) == 16);
    static_assert(offsetof(GrassBladeRecord, PositionX) == 0);
    static_assert(offsetof(GrassBladeRecord, PositionY) == 4);
    static_assert(offsetof(GrassBladeRecord, PositionZ) == 8);
    static_assert(offsetof(GrassBladeRecord, Packed) == 12);

    inline float GrassSaturate(float value)
    {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    /** @brief 量子化の最大値は生成定数と同じ有限の正数を渡す。シェーダーも演算順を保つ。 */
    inline uint32_t PackGrassBlade(float heightMeters, float widthMeters, uint32_t speciesId, float fade,
                                   float heightQuantMaxMeters = 2.0f, float widthQuantMaxMeters = 0.25f)
    {
        const uint32_t height10 = static_cast<uint32_t>(GrassSaturate(heightMeters / heightQuantMaxMeters) * 1023.0f + 0.5f);
        const uint32_t width8 = static_cast<uint32_t>(GrassSaturate(widthMeters / widthQuantMaxMeters) * 255.0f + 0.5f);
        const uint32_t fade8 = static_cast<uint32_t>(GrassSaturate(fade) * 255.0f + 0.5f);
        return height10 | (width8 << 10) | ((speciesId & 63u) << 18) | (fade8 << 24);
    }

    struct GrassBladeParameters
    {
        float HeightMeters = 0.0f;
        float WidthMeters = 0.0f;
        uint32_t SpeciesId = 0;
        float Fade = 0.0f;
    };

    inline GrassBladeParameters UnpackGrassBlade(uint32_t packed, float heightQuantMaxMeters = 2.0f,
                                                 float widthQuantMaxMeters = 0.25f)
    {
        GrassBladeParameters result;
        result.HeightMeters = (static_cast<float>(packed & 1023u) / 1023.0f) * heightQuantMaxMeters;
        result.WidthMeters = (static_cast<float>((packed >> 10) & 255u) / 255.0f) * widthQuantMaxMeters;
        result.SpeciesId = (packed >> 18) & 63u;
        result.Fade = static_cast<float>((packed >> 24) & 255u) / 255.0f;
        return result;
    }

    inline constexpr uint32_t kGrassArgsOffset = 0;
    inline constexpr uint32_t kGrassArgsStride = 20;
    inline constexpr uint32_t kGrassCounterOffset = 64;
    inline constexpr uint32_t kGrassArgsBufferSize = 80;

    /** @brief 間接引数と、容量を超えた分も数えるカウンタの共有バッファ。予約語は 0。 */
    struct alignas(16) GrassGenerateArgsBuffer
    {
        MegaGeometry::DrawIndexedIndirectCommand Args[3] = {};
        uint32_t Reserved0 = 0;
        uint32_t Counter[3] = {};
        uint32_t Reserved1 = 0;
    };
    static_assert(sizeof(MegaGeometry::DrawIndexedIndirectCommand) == kGrassArgsStride);
    static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, IndexCount) == 0);
    static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, InstanceCount) == 4);
    static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, FirstIndex) == 8);
    static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, VertexOffset) == 12);
    static_assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, FirstInstance) == 16);
    static_assert(offsetof(GrassGenerateArgsBuffer, Args) == kGrassArgsOffset);
    static_assert(offsetof(GrassGenerateArgsBuffer, Reserved0) == 60);
    static_assert(offsetof(GrassGenerateArgsBuffer, Counter) == kGrassCounterOffset);
    static_assert(offsetof(GrassGenerateArgsBuffer, Reserved1) == 76);
    static_assert(sizeof(GrassGenerateArgsBuffer) == kGrassArgsBufferSize);

    /**
     * @brief std140 の生成定数。各行は GLSL の vec4 / uvec4、行列だけ mat4 に対応する。
     * 3 帯の配列の w と、用途を記していない成分は予約の 0。
     * 判定の経路は加減乗算・比較・floor と整数演算だけ。浮動小数の除算・sqrt・三角関数を使わない。
     * GLSL は precise を付け、CPU の写しと同じ括弧・演算順を使う。dot / mix へ置き換えない。
     * 逆数・二乗・セル数は BuildGrassGenerateConstants で計算した値を CPU と GPU で共有する。
     * 向きは u = 2 * unit(hash(gx, gz, band * 8 + 4)) - 1 とし、方向は (1 - u*u, 2*u)、
     * 長さは 1 + u*u。両面なので半円で十分で、描画側も同じハッシュと式から再導出する。
     * (tx, tz) = (camera.x - position.x, camera.z - position.z) とする。
     * 向きの判定は a = (1 - u*u)*tx + (2*u)*tz に対し、
     * a*a < FacingThresholdSquared * ((1 + u*u)*(1 + u*u)) * (tx*tx + tz*tz)。
     * 水平距離の二乗が 1e-8 未満なら向きの判定をしない。幅・fade・パック値は判定へ戻さない。
     */
    struct alignas(16) GrassGenerateConstants
    {
        float CameraPosition[4] = {};                  // xyz: 世界位置
        float FrustumPlanes[6][4] = {};                // xyz: 法線、w: 距離。内側が正。
        float BandSpacingMeters[4] = {};
        float BandMaxDistanceMeters[4] = {};
        float BandMaxDistanceSquared[4] = {};
        uint32_t BandCellsPerAxis[4] = {};             // round(TileSize / BandSpacing)
        float BandKeepScale[4] = {};                  // (BandSpacing / BandSpacing[0]) の二乗
        float KeepDistanceSquared[4] = {};
        float KeepInvSpanSquared[4] = {};             // xyz: 隣り合う二乗距離の差の逆数
        float KeepFractions[4] = {};
        uint32_t LodCapacity[4] = {};
        uint32_t LodBase[4] = {};                      // ブレード単位の累積位置
        uint32_t LodIndexCount[4] = {};                // LodSegments * 6
        uint32_t LodFirstIndex[4] = {};                // インデックス単位の累積位置
        float TileChunkFadeFacing[4] = {};             // TileSize、ChunkSize、FadeStartFraction、FacingCullThreshold
        uint32_t TilesPerChunk[4] = {};                // x: round(ChunkSize / TileSize)、INT32_MAX 以下
        uint32_t LayerResolutions[4] = {};             // x: LayerA、y: LayerB
        float SamplingAndFacing[4] = {};               // InvPixelWidthA、s0*s0、FacingThresholdSquared、fade 区間の逆数
        float BladeQuantMaxMeters[4] = {};            // x: 高さ、y: 幅
        uint32_t Flags[4] = {};                        // x の bit0: HZB を使う
        float HiZViewProjection[4][4] = {};            // GLSL mat4 の列を順に格納（CPU の VP を転置）
        uint32_t HiZDimensions[4] = {};                // 元の深度の幅、高さ、HZB の mip 数、予約
    };
    static_assert(alignof(GrassGenerateConstants) == 16);
    static_assert(sizeof(GrassGenerateConstants) == 480);
    static_assert(sizeof(GrassGenerateConstants) % 16 == 0);
    static_assert(offsetof(GrassGenerateConstants, CameraPosition) == 0);
    static_assert(offsetof(GrassGenerateConstants, FrustumPlanes) == 16);
    static_assert(offsetof(GrassGenerateConstants, BandSpacingMeters) == 112);
    static_assert(offsetof(GrassGenerateConstants, BandMaxDistanceMeters) == 128);
    static_assert(offsetof(GrassGenerateConstants, BandMaxDistanceSquared) == 144);
    static_assert(offsetof(GrassGenerateConstants, BandCellsPerAxis) == 160);
    static_assert(offsetof(GrassGenerateConstants, BandKeepScale) == 176);
    static_assert(offsetof(GrassGenerateConstants, KeepDistanceSquared) == 192);
    static_assert(offsetof(GrassGenerateConstants, KeepInvSpanSquared) == 208);
    static_assert(offsetof(GrassGenerateConstants, KeepFractions) == 224);
    static_assert(offsetof(GrassGenerateConstants, LodCapacity) == 240);
    static_assert(offsetof(GrassGenerateConstants, LodBase) == 256);
    static_assert(offsetof(GrassGenerateConstants, LodIndexCount) == 272);
    static_assert(offsetof(GrassGenerateConstants, LodFirstIndex) == 288);
    static_assert(offsetof(GrassGenerateConstants, TileChunkFadeFacing) == 304);
    static_assert(offsetof(GrassGenerateConstants, TilesPerChunk) == 320);
    static_assert(offsetof(GrassGenerateConstants, LayerResolutions) == 336);
    static_assert(offsetof(GrassGenerateConstants, SamplingAndFacing) == 352);
    static_assert(offsetof(GrassGenerateConstants, BladeQuantMaxMeters) == 368);
    static_assert(offsetof(GrassGenerateConstants, Flags) == 384);
    static_assert(offsetof(GrassGenerateConstants, HiZViewProjection) == 400);
    static_assert(offsetof(GrassGenerateConstants, HiZDimensions) == 464);

    /**
     * @brief 平面は MatrixUtils::ExtractClipSpaceFrustumPlanes の Planes を渡す。
     * 解像度と chunkSize は field と一致させる。有効な HZB だけ bit0 を立てる。
     * HZB は既存の HiZPyramid（深度の半分の mip0 と全 mip）を使う。
     */
    GrassGenerateConstants BuildGrassGenerateConstants(const GrassGenerateSettings& settings,
        const Math::Vector3& cameraPos, const Math::Vector4 frustumPlanes[6], uint32_t layerARes,
        uint32_t layerBRes, float chunkSize, const MegaHiZContract* hizOrNull = nullptr);

    /** @brief 整数の桁あふれは uint32_t の剰余。負の世界セル座標も uint32_t に写して渡す。 */
    inline uint32_t GrassHash(uint32_t x, uint32_t y, uint32_t z)
    {
        uint32_t h = (x * 0x8da6b343u) ^ (y * 0xd8163841u) ^ (z * 0xcb1ab31fu);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }

    inline float GrassHashToUnit(uint32_t h)
    {
        return static_cast<float>(h >> 8) * 0x1p-24f;
    }

    struct GrassGenerateCounts
    {
        uint64_t Candidates = 0; // 有効な帯の全セル数。チャンク欠損時の候補も含む。
        uint32_t PerLod[3] = {};  // 容量を超えた分も含めた、判定を通った本数。
    };

    /**
     * @brief HZB の bit0 が 0 の定数を使う CPU の写し。LayerIndex は参照しない。
     * outRecords は消去せず、容量内の記録をタイル順、セルの j、i 順に末尾へ追加する。
     * 容量の判定は呼び出しごとに 0 から始める。GPU とは順序に依存しない数と集合で比べる。
     * タイルの座標・原点・帯、設定値は有効で、解像度・チャンク寸法は field と一致すること。
     */
    GrassGenerateCounts RunGrassGenerateReference(const GrassFieldRenderData& field,
        const GrassGenerateConstants& constants, const GrassTileInfo* tiles, uint32_t tileCount,
        Container::VariableArray<GrassBladeRecord>* outRecords = nullptr);
} // namespace NorvesLib::Core::Rendering
