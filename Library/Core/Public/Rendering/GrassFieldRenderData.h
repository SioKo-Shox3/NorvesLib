#pragma once

#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 種別の GPU 向けデータ（16 バイト境界の並びで std430 の配列に使用） */
    struct GrassSpeciesGpuData
    {
        /** @brief 根元の色（線形 RGB、A は未使用の 0） */
        float RootColor[4];
        /** @brief 草先の色（線形 RGB、A は未使用の 0） */
        float TipColor[4];
        /** @brief 湿ったときの色（線形 RGB、A は未使用の 0） */
        float WetColor[4];
        /** @brief 草丈の最小値（m） */
        float HeightMinMeters;
        /** @brief 草丈の最大値（m） */
        float HeightMaxMeters;
        /** @brief 葉の幅（m） */
        float BladeWidthMeters;
        /** @brief 曲がり具合 */
        float Bend;
        /** @brief 硬さ */
        float Stiffness;
        /** @brief 通常時の粗さ */
        float Roughness;
        /** @brief 湿ったときの粗さ */
        float WetRoughness;
        /** @brief 湿ったときの垂れ具合 */
        float WetDroop;
        /** @brief 風への応答 */
        float WindResponse;
        /** @brief 踏みつけへの応答 */
        float TrampleResponse;
        /** @brief 不透明度 */
        float Opacity;
        /** @brief 1 平方メートルあたりの葉の枚数 */
        float BladesPerSquareMeter;
        /** @brief LOD の距離（m） */
        float LodDistanceMeters[4];
        /** @brief LOD ごとに残す割合 */
        float LodKeepFraction[4];
    };
    static_assert(sizeof(GrassSpeciesGpuData) % 16 == 0);

    /** @brief 草の場の 1 チャンクの描画データ */
    struct GrassFieldChunkRenderData
    {
        /** @brief チャンクの X 座標 */
        int32_t ChunkX = 0;
        /** @brief チャンクの Z 座標 */
        int32_t ChunkZ = 0;
        /** @brief LayerAPixels 内の先頭バイト位置（RGBA8、行は +X、列は +Z） */
        uint32_t LayerAByteOffset = 0;
        /** @brief LayerBPixels 内の先頭バイト位置 */
        uint32_t LayerBByteOffset = 0;
    };

    /** @brief 作成後に書き換えず、TSharedPtr<const> で共有する草の場の描画データ */
    struct GrassFieldRenderData
    {
        /** @brief チャンクの大きさ（m） */
        float ChunkSizeMeters = 32.0f;
        /** @brief LayerA の解像度 */
        uint32_t LayerAResolution = 64;
        /** @brief LayerB の解像度 */
        uint32_t LayerBResolution = 32;
        /** @brief 内容が変わるたびに増える番号（描画側のアップロード判断用） */
        uint64_t ContentRevision = 0;
        /** @brief チャンクの一覧（ChunkZ、ChunkX の昇順） */
        Container::VariableArray<GrassFieldChunkRenderData> Chunks;
        /** @brief LayerA の RGBA8 画素（R: 密度、G: 高さ係数、B: 湿りの偏り［128 が中立］、A: 種別 ID 0..63） */
        Container::VariableArray<uint8_t> LayerAPixels;
        /** @brief LayerB の RGBA8 画素（RGB: 色味［128 が中立］、A: 予約の 0） */
        Container::VariableArray<uint8_t> LayerBPixels;
        /** @brief 種別の一覧（添字が種別 ID） */
        Container::VariableArray<GrassSpeciesGpuData> Species;
    };
} // namespace NorvesLib::Core::Rendering
