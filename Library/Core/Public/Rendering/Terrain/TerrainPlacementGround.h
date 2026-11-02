#pragma once

#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Asset
{
    class AssetFileReader;
}

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainHeightField;

    /// 接地用の疎な高さ格子の版 3。整数・浮動小数点はすべて little-endian で、詰め物はない。
    /// 見出しは uint32 magic (kMagic)・版 (3)・一辺の点数 (33)・層の数・参照の数の順で、計 20 バイト。
    /// 参照ごとに uint32 名前のバイト数・UTF-8 論理名・uint32 パッケージのバイト数・UTF-8 パス・uint64 cooked_hash。
    /// 層ごとに uint32 flags・float32 塊幅・間隔・float64 原点 X・Z・最大 X・Z・uint32 塊の数を並べる（計 48 バイト）。
    /// flags の bit 0 (kLayerFallbackToHeightField = 1u) が立つ層は、塊の無い所を高さ場へ戻す。
    /// 続けて各塊の int32 bx・bz、
    /// float32 高さ 33×33、補間可のビット 128 バイトを並べる。
    /// 高さと升目は Z 行優先。升目 (x,z) のビット番号は z×32+x、バイト内では下位ビットから数える。
    /// 塊の原点は (層の原点 X + 塊幅×bx, 層の原点 Z + 塊幅×bz)。
    namespace PlacementGroundFormat
    {
        inline constexpr uint32_t kMagic = 0x4750564eu;
        inline constexpr uint32_t kVersion = 3u;
        inline constexpr float kBlockMeters = 8.0f;
        inline constexpr float kSpacing = 0.25f;
        inline constexpr float kCastleBlockMeters = 32.0f;
        inline constexpr float kCastleSpacing = 1.0f;
        inline constexpr uint32_t kLayerFallbackToHeightField = 1u;
        inline constexpr uint32_t kSide = 33u;
        inline constexpr uint32_t kSamples = kSide * kSide;
        inline constexpr uint32_t kHeaderBytes = 20u;
        inline constexpr uint32_t kLayerHeaderBytes = 48u;
        inline constexpr uint32_t kCellSide = kSide - 1u;
        inline constexpr uint32_t kCells = kCellSide * kCellSide;
        inline constexpr uint32_t kMaskBytes = kCells / 8u;
        inline constexpr uint32_t kBlockBytes = 8u + 4u * kSamples + kMaskBytes;
    }

    /// 古さの判定に使う、問い合わせた区画（継ぎ目の隣の区画も含む）のマニフェスト上の参照。
    struct TerrainPlacementGroundSource
    {
        Container::AnsiString LogicalPath;
        Container::AnsiString CookedPackage;
        uint64_t CookedHash = 0;
    };

    /// 焼いた面の補間可否。不可の升目では配置を見送るため DEM へ戻さない。
    enum class PlacementGroundSample : uint8_t
    {
        Baked,
        Unreliable,
        NoBlock
    };

    /// 焼いた面の接地用の高さ。近景の層は塊の無い所を高さ場へ戻す。
    /// 遠景の castle の段の焼いた面は別の層で、塊の無い所は不可。Load の後は複数のスレッドから同時に引ける。
    class TerrainPlacementGround
    {
    public:
        /// path はアセットの根からの相対パス、または絶対パス。壊れた見出し・大きさ・塊を拒否する。
        static Container::TSharedPtr<const TerrainPlacementGround> Load(Container::AnsiStringView path,
                                                                        Container::AnsiString &outError);
        /// クック済み資産の根を持つ読み込み器から読む。
        static Container::TSharedPtr<const TerrainPlacementGround> Load(Container::AnsiStringView path,
                                                                        const Asset::AssetFileReader &reader,
                                                                        Container::AnsiString &outError);

        /// 可の升目だけ双線形補間する。不可では outY を変えず、範囲の外は NoBlock。
        /// 塊の欠落は、層の設定に応じて NoBlock または Unreliable を返す。
        PlacementGroundSample Sample(float x, float z, float &outY) const;
        /// 不可の升目や高さ場へ戻さない層の塊の欠落は false。それ以外は焼いた面か高さ場の高さを返す。
        bool SampleGround(const TerrainHeightField &field, float x, float z, float &outY) const;
        /// 点を含む層の間隔。範囲の外では近景の間隔を返す。
        float GetSpacing(float x, float z) const;
        size_t GetLayerCount() const
        {
            return m_Layers.size();
        }
        /// 順番に依存せず論理名と cooked_hash の組を照合する。CookedPackage は照合しない。
        bool IsCurrent(Container::Span<const TerrainPlacementGroundSource> sources) const;
        size_t GetBlockCount() const
        {
            size_t count = 0;
            for (const auto &layer : m_Layers)
            {
                count += layer.Blocks.size();
            }
            return count;
        }
        Container::Span<const TerrainPlacementGroundSource> GetSources() const
        {
            return {m_Sources.data(), m_Sources.size()};
        }

    private:
        TerrainPlacementGround() = default;
        struct Block
        {
            int32_t X = 0;
            int32_t Z = 0;
            float Heights[PlacementGroundFormat::kSamples] = {};
            uint8_t ReliableCells[PlacementGroundFormat::kMaskBytes] = {};
        };
        struct Layer
        {
            uint32_t Flags = 0;
            float BlockMeters = 0.0f;
            float Spacing = 0.0f;
            double FirstX = 0.0;
            double FirstZ = 0.0;
            double MaxX = 0.0;
            double MaxZ = 0.0;
            Container::VariableArray<Block> Blocks;
        };
        const Layer *FindLayer(float x, float z) const;
        Container::VariableArray<TerrainPlacementGroundSource> m_Sources;
        Container::VariableArray<Layer> m_Layers;
    };
} // namespace NorvesLib::Core::Rendering::Terrain
