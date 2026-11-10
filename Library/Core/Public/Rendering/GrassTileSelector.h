#pragma once

#include "Rendering/GrassGenerateReference.h"

namespace NorvesLib::Core::Rendering
{
    /** @brief 草のタイルを選別するカメラとチャンク窓の入力。 */
    struct GrassTileSelectionInput
    {
        Math::Vector3 CameraPosition;
        // MatrixUtils::ExtractClipSpaceFrustumPlanes の正規化済み平面。内側が正。
        float FrustumPlanes[6][4] = {};
        uint32_t WindowSize = 16;
    };

    /** @brief チャンクの密度をキャッシュし、草の生成タイルを決定的に選別する。 */
    class GrassTileSelector
    {
    public:
        /**
         * @brief outTiles を空にして TileZ、TileX の昇順で選別結果を格納し、数を返す。
         * 設定は GrassGenerateSettings の契約に従い、WindowSize は正数とする。
         * カメラと距離範囲のタイル座標は int32_t に収まること。
         */
        uint32_t Select(const GrassFieldRenderData& field, const GrassGenerateSettings& settings,
                        const GrassTileSelectionInput& input, Container::VariableArray<GrassTileInfo>& outTiles);

    private:
        void UpdateDensityCache(const GrassFieldRenderData& field);

        Container::VariableArray<uint8_t> m_ChunkMaxDensities;
        const GrassFieldRenderData* m_CachedField = nullptr;
        const GrassFieldChunkRenderData* m_CachedChunks = nullptr;
        const uint8_t* m_CachedLayerAPixels = nullptr;
        uint64_t m_CachedContentRevision = 0;
        size_t m_CachedLayerAByteCount = 0;
        uint32_t m_CachedLayerAResolution = 0;
    };
} // namespace NorvesLib::Core::Rendering
