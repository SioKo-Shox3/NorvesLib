// GBuffer の Normal.w にシェーディングモデル ID と受け手の印を整数として詰める。
// Material.a は材質値 P0、Emissive.a は材質値 P1 を保持する。
// シェーディングモデル ID の 4〜15 は予約する。
// Assets/Shaders/Common/GBufferEncoding.glsl と同じ定数表を保ち、ShadingModelContractTest が突き合わせる。
// 半透明（forward_transparent.frag）とパストレーサーは ID を読まず DefaultLit として扱う。
#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering::GBufferContract
{
    // 標準のライティングを使うシェーディングモデル。
    inline constexpr uint32_t ShadingModelDefaultLit = 0u;
    // 毛皮のシェーディングモデル。
    inline constexpr uint32_t ShadingModelFur = 1u;
    // 目のシェーディングモデル。
    inline constexpr uint32_t ShadingModelEye = 2u;
    // 淡い平坦な面のシェーディングモデル。
    inline constexpr uint32_t ShadingModelPaleFlat = 3u;
    // シェーディングモデル ID に割り当てるビット数。
    inline constexpr uint32_t ShadingModelIdBits = 4u;
    // シェーディングモデル ID のビットマスク。
    inline constexpr uint32_t ShadingModelIdMask = 15u;
    // 地形の受け手を示す印。
    inline constexpr uint32_t ReceiverFlagTerrain = 1u;
    // 地形の影を起伏に詰める印。地形の印と併用する。
    inline constexpr uint32_t ReceiverFlagTightTerrainShadow = 2u;
    // 草や低木の株の受け手を示す印。
    inline constexpr uint32_t ReceiverFlagFoliageClump = 4u;
    // 鏡面反射のない材質を示す印。
    inline constexpr uint32_t ReceiverFlagDiffuseOnly = 8u;
    // 受け手の印のビットマスク。
    inline constexpr uint32_t ReceiverFlagMask = 15u;

    // ID と受け手の印を Normal.w に格納する整数にまとめる。
    constexpr uint32_t PackNormalW(uint32_t shadingModelId, uint32_t receiverFlags)
    {
        return ((receiverFlags & ReceiverFlagMask) << ShadingModelIdBits) |
               (shadingModelId & ShadingModelIdMask);
    }
} // namespace NorvesLib::Core::Rendering::GBufferContract
