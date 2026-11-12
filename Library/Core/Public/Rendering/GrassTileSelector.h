#pragma once

#include "Rendering/GrassGenerateReference.h"

#include <limits>

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

    /** @brief 草のタイルの選択を行のチャンクに分けて並列に回す設定。 */
    struct GrassTileSelectionParallelSettings
    {
        // 候補のタイル数がこの数以上のときだけ並列に回す（既定は並列にしない）。
        uint64_t MinCandidateTileCount = std::numeric_limits<uint64_t>::max();
        // 1 チャンクの行（TileZ）の数（0 は 1 として扱う）。
        uint32_t RowsPerChunk = 8;
    };

    /** @brief チャンクの密度をキャッシュし、草の生成タイルを決定的に選別する。 */
    class GrassTileSelector
    {
    public:
        /** @brief タイル選別の並列設定を変更する。 */
        void SetParallelSettings(const GrassTileSelectionParallelSettings& settings)
        {
            m_ParallelSettings = settings;
        }

        /** @brief タイル選別の並列設定を取得する。 */
        const GrassTileSelectionParallelSettings& GetParallelSettings() const
        {
            return m_ParallelSettings;
        }

        /**
         * @brief outTiles を空にして TileZ、TileX の昇順で選別結果を格納し、数を返す。
         * 設定は GrassGenerateSettings の契約に従い、WindowSize は正数とする。
         * カメラと距離範囲のタイル座標は int32_t に収まること。
         */
        uint32_t Select(const GrassFieldRenderData& field, const GrassGenerateSettings& settings,
                        const GrassTileSelectionInput& input, Container::VariableArray<GrassTileInfo>& outTiles);

    private:
        void UpdateDensityCache(const GrassFieldRenderData& field);

        GrassTileSelectionParallelSettings m_ParallelSettings;
        Container::VariableArray<Container::VariableArray<GrassTileInfo>> m_ChunkTiles;
        Container::VariableArray<uint8_t> m_ChunkMaxDensities;
        const GrassFieldRenderData* m_CachedField = nullptr;
        const GrassFieldChunkRenderData* m_CachedChunks = nullptr;
        const uint8_t* m_CachedLayerAPixels = nullptr;
        uint64_t m_CachedContentRevision = 0;
        size_t m_CachedLayerAByteCount = 0;
        uint32_t m_CachedLayerAResolution = 0;
    };
} // namespace NorvesLib::Core::Rendering
