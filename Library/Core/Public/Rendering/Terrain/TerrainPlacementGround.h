#pragma once

#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainHeightField;

    /// 接地用の疎な高さ格子。整数・浮動小数点はすべて little-endian で、構造体の詰め物は書かない。
    /// NVPG / uint32 版 / float32 塊幅・間隔 / uint32 一辺の点数 /
    /// float64 原点 X・Z・最大 X・Z / uint32 塊数・参照数 の順。
    /// 参照は uint32 名前のバイト数・UTF-8 論理名・uint32 パッケージのバイト数・UTF-8 パス・uint64 cooked_hash。
    /// その後に int32 bx・bz、float32 高さ 33×33、補間可のビット 128 バイトを塊ごとに並べる。
    /// 高さと升目は Z 行優先。升目 (x,z) のビット番号は z×32+x、バイト内では下位ビットから数える。
    /// 塊の原点は (原点 X + 8×bx, 原点 Z + 8×bz)。DEM の範囲を越える末尾の標本は NaN。
    namespace PlacementGroundFormat
    {
        inline constexpr uint32_t kMagic = 0x4750564eu;
        inline constexpr uint32_t kVersion = 2u;
        inline constexpr float kBlockMeters = 8.0f;
        inline constexpr float kSpacing = 0.25f;
        inline constexpr uint32_t kSide = 33u;
        inline constexpr uint32_t kSamples = kSide * kSide;
        inline constexpr uint32_t kHeaderBytes = 60u;
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

    /// 焼いた面の接地用の高さ。Load の後は読み取りだけで、複数のスレッドから同時に引ける。
    class TerrainPlacementGround
    {
    public:
        /// path はアセットの根からの相対パス、または絶対パス。壊れた見出し・大きさ・塊を拒否する。
        static Container::TSharedPtr<const TerrainPlacementGround> Load(Container::AnsiStringView path,
                                                                        Container::AnsiString &outError);

        /// 可の升目だけ双線形補間する。不可では outY を変えず、塊の欠落・範囲の外は NoBlock。
        PlacementGroundSample Sample(float x, float z, float &outY) const;
        /// 不可の升目は false。可なら焼いた面、塊が無ければ DEM を outY へ書いて true。
        bool SampleGround(const TerrainHeightField &field, float x, float z, float &outY) const;
        /// 順番に依存せず論理名と cooked_hash の組を照合する。CookedPackage は照合しない。
        bool IsCurrent(Container::Span<const TerrainPlacementGroundSource> sources) const;
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
        double m_FirstX = 0.0;
        double m_FirstZ = 0.0;
        double m_MaxX = 0.0;
        double m_MaxZ = 0.0;
        Container::VariableArray<TerrainPlacementGroundSource> m_Sources;
        Container::VariableArray<Block> m_Blocks;
    };
} // namespace NorvesLib::Core::Rendering::Terrain
