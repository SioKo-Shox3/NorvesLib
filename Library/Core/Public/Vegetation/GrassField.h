#pragma once

#include "Vegetation/GrassSpecies.h"
#include "Vegetation/GroundHeightSource.h"
#include "Rendering/GrassFieldRenderData.h"
#include "Rendering/SkyOcclusion.h"

namespace NorvesLib::Core::Vegetation
{
    class DrynessMap;

    /** @brief 草の場のチャンク寸法と画素数。寸法は有限の正数、解像度は正数を指定する。 */
    struct GrassFieldDesc
    {
        float ChunkSizeMeters = 32.0f;
        uint32_t LayerAResolution = 64;
        uint32_t LayerBResolution = 32;
    };

    struct GrassChunkCoord
    {
        int32_t X = 0;
        int32_t Z = 0;
    };

    struct GrassSample
    {
        bool bValid = false;
        float Density = 0.0f;
        float HeightMeters = 0.0f;
        float Moisture = 0.0f;
        uint32_t SpeciesId = 0;
        Math::Vector3 Tint{1.0f, 1.0f, 1.0f};
    };

    struct GrassSurface
    {
        bool bValid = false;
        uint32_t SpeciesId = 0;
        float MoistureBias = 0.0f;
    };

    /**
     * @brief 全体の濡れ度と LayerA.B / 255（0.5 付近が中立）から有効な濡れ度を求める。
     * 計算式はこの関数の実装に集約する。
     */
    float ComputeEffectiveWetness(float globalWetness, float moistureBias);

    /**
     * @brief GameThread 専用の草の場。チャンクの LayerA / LayerB を正本として保持する。
     * const クエリは内部状態を書き換えず再入可能。更新とクエリを並行して実行しないこと。
     * 踏み倒し（GR44）と枯れ度（GR45）は別系統で扱い、この本体を書き換えない。
     * 遮蔽による「見えない」のしきい値は GR24 が決める。
     */
    class GrassField
    {
    public:
        explicit GrassField(const GrassFieldDesc& desc = {});
        const GrassFieldDesc& GetDesc() const;

        /**
         * @brief LayerAResolution^2 * 4、LayerBResolution^2 * 4 バイトの画素をコピーする。
         * 大きさや設定が不正、または連結画素配列が 32bit のバイト位置を超えるなら false。
         * 密度 R がすべて 0 なら既存チャンクを除去する。
         * 行は +Z、行内は +X。画素 (i,j) の中心は原点 + (i+0.5,j+0.5) * 画素幅。
         */
        bool SetChunk(GrassChunkCoord coord, Container::Span<const uint8_t> layerA,
                      Container::Span<const uint8_t> layerB);
        bool RemoveChunk(GrassChunkCoord coord);
        bool HasChunk(GrassChunkCoord coord) const;
        uint32_t GetChunkCount() const;
        void SetSpeciesTable(const GrassSpeciesTable& table);
        const GrassSpeciesTable& GetSpeciesTable() const;

        /** @brief 地面を借用する。nullptr は既定の高さ 0 の定数面に戻す。借用先は場より長く生存すること。 */
        void SetGroundHeightSource(const IGroundHeightSource* pSource);
        /** @brief 成功した SetChunk、各 RemoveChunk、SetSpeciesTable の呼び出しごとに増える。 */
        uint64_t GetRevision() const;

        GrassSample Sample(float x, float z) const;
        float QueryTopHeight(float x, float z) const;
        /** @brief 区間を 0.5m 以下の刻みで積分した透過率（0..1）を返す。 */
        float QueryOcclusion(const Math::Vector3& a, const Math::Vector3& b) const;
        /**
         * @brief 区間を stepMeters 以下の刻みで積分した透過率（0..1）を返す。積分回数には上限がある。
         * stepMeters が有限の正数でなければ 0.5 m を使う。0.5 m かつ pDryness が nullptr なら 2 引数版と一致する。
         * pDryness を渡すと、DrynessMap::Sample と LayerB の A による基礎の枯れ度の大きい方を描画と同様に使う。
         * 種別の枯れ度応答で密度と草丈を補正し、枯れ度 0 の点では補正なしの値とビット一致する。
         * 踏み倒し（GR44）は GPU 上の場にのみ記録され、CPU から読めないため含めない。
         */
        float QueryOcclusion(const Math::Vector3& a, const Math::Vector3& b, float stepMeters,
                             const DrynessMap* pDryness = nullptr) const;
        /**
         * @brief 区間の草の光学的厚さ τ（0 以上）を返す。QueryOcclusion は同じ刻み・枯れ度・積分回数の上限で exp(-τ) を返す。
         * pOutMaxTopHeight には草のある積分点での、地面の高さと枯れ度を反映した草丈の和の最大値（m）を書く。
         * 草のある積分点がない場合や区間が不正な場合は負の無限大を書く。
         */
        float QueryOpticalDepth(const Math::Vector3& a, const Math::Vector3& b, float stepMeters,
                                const DrynessMap* pDryness = nullptr, float* pOutMaxTopHeight = nullptr) const;
        GrassSurface QuerySurface(float x, float z) const;
        float GetCameraClearance(const Math::Vector3& position) const;
        Container::TSharedPtr<const Rendering::GrassFieldRenderData> BuildRenderData() const;
        /**
         * @brief 保持する全チャンクを覆う下草の層を、テクセル中心の草丈と密度から作る。
         * 不正な幅・解像度、チャンクなし、配置を表せない場合は false を返し、出力を変更しない。
         * 解像度の上限は配置の制約に合わせて最大 8192 とする。
         */
        bool BuildSkyOcclusionUndergrowth(Rendering::SkyOcclusionFieldLayout& outLayout,
            Container::VariableArray<Rendering::SkyOcclusionUndergrowthTexel>& outTexels,
            float texelMeters = 1.0f, uint32_t maxResolution = 1024u) const;

    private:
        struct Chunk
        {
            GrassChunkCoord Coord;
            Container::VariableArray<uint8_t> LayerA;
            Container::VariableArray<uint8_t> LayerB;
        };

        static uint64_t MakeChunkKey(GrassChunkCoord coord);
        bool TryGetChunkCoord(float x, float z, GrassChunkCoord& outCoord) const;
        const Chunk* FindChunk(GrassChunkCoord coord) const;
        const uint8_t* ReadPixel(const Chunk& chunk, int32_t pixelX, int32_t pixelZ,
                                 bool bLayerA) const;
        Math::Vector3 SampleLayer(const Chunk& chunk, float localX, float localZ, bool bLayerA,
                                  float* pOutAlpha = nullptr) const;
        GrassSample SampleInternal(float x, float z, float* pOutBaseDryness) const;
        float GetGroundHeight(float x, float z) const;

        GrassFieldDesc m_Desc;
        Container::UnorderedMap<uint64_t, Chunk> m_Chunks;
        GrassSpeciesTable m_SpeciesTable;
        ConstantGroundHeightSource m_DefaultGround;
        const IGroundHeightSource* m_pGroundHeightSource = nullptr;
        uint64_t m_Revision = 0;
    };
} // namespace NorvesLib::Core::Vegetation
