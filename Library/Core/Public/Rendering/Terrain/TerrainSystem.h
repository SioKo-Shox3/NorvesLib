// ---------------------------------------------------------------------------
// 地形の高さ場の登録と検索を、エンジンが所有するための仕組み。
// 地形 Component が高さ場を登録・解除し、Game とエンジンの部品が座標から引く。
// この仕組みの操作は GameThread で行い、並列に引くときは返された高さ場を共有する。
// ---------------------------------------------------------------------------
#pragma once

#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/Vector2.h"
#include "Rendering/Terrain/TerrainHeightField.h"

#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Core::Rendering::Terrain
{
    /// 地形の登録を識別する世代付きハンドル。
    struct TerrainHandle
    {
        static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0u;

        constexpr bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0u;
        }

        bool operator==(const TerrainHandle& other) const = default;
    };

    /// 地面の標本。
    struct GroundSample
    {
        bool bValid = false;
        float Height = 0.0f;
        /// 最細段（LOD0）の三角形の面法線。
        Math::Vector3 Normal = Math::Vector3(0.0f, 1.0f, 0.0f);
        /// 面法線と上方向のなす角（acos(Normal.y)）。
        float SlopeRadians = 0.0f;
        /// スプラットの重みが最大の層の番号。
        uint8_t SurfaceId = kInvalidTerrainSurfaceId;
        uint32_t TileId = kInvalidTerrainTileId;
    };

    /// 地面のレイの当たり。
    struct GroundHit
    {
        bool bValid = false;
        float Distance = 0.0f;
        Math::Vector3 Point;
        /// 原点が地表の下なら零ベクトル。
        Math::Vector3 Normal;
        uint8_t SurfaceId = kInvalidTerrainSurfaceId;
        uint32_t TileId = kInvalidTerrainTileId;
        TerrainHandle Terrain;
    };

    /// 地形 Component の登録を保持し、Game などに高さ場の共有参照を返す。
    /// 登録・解除・検索・資産の設定と取得・Clear は GameThread だけで行う。
    /// 返された高さ場は、登録を解除した後も共有参照を持つ間は並列に引ける。
    class TerrainSystem
    {
    public:
        /// 無効な高さ場、または引ける範囲がない格子は登録せず、無効なハンドルを返す。
        TerrainHandle RegisterTerrain(Container::TSharedPtr<const TerrainHeightField> field);

        /// 無効または解除済みのハンドルは何もしない。
        void UnregisterTerrain(TerrainHandle handle);

        /// GameThread で検索し、範囲が重なる場合は先に登録された高さ場を返す。
        Container::TSharedPtr<const TerrainHeightField> FindTerrain(float x, float z) const;

        /// 有効な登録の共有参照を返す。無効または解除済みなら空を返す。
        Container::TSharedPtr<const TerrainHeightField> GetTerrain(TerrainHandle handle) const;

        uint32_t GetTerrainCount() const;

        /// GameThread で地面を引く。範囲が重なる場合は先に登録された高さ場を選ぶ。
        /// 地形がない、または高さが NaN なら outSample を既定値に戻して false を返す。
        bool SampleGround(float x, float z, GroundSample& outSample) const;

        /// GameThread で全登録の最近の当たりを引く。同距離なら先に登録された高さ場を選ぶ。
        /// 当たりがなければ outHit を既定値に戻して false を返す。
        bool RaycastGround(const Math::Ray& ray, float maxDistance, GroundHit& outHit) const;

        /// GameThread で各位置の地面を SampleGround と同じ計算で引く。位置の y 成分を z として使う。
        /// 短い方の長さまで処理し、outSamples の残りは既定値に戻す。
        void SampleGroundBatch(Container::Span<const Math::Vector2> positions,
                               Container::Span<GroundSample> outSamples) const;

        /// 地形 Component がクック済みの資産を読むための資産システムを保持する。
        void SetAssetSystem(Container::TSharedPtr<const Asset::AssetSystem> assetSystem);
        Container::TSharedPtr<const Asset::AssetSystem> GetAssetSystem() const;

        /// すべての登録を解除する。資産システムの設定は保持する。
        void Clear();

    private:
        struct TerrainSlot
        {
            Container::TSharedPtr<const TerrainHeightField> Field;
            uint32_t Generation = 1u;
            uint32_t Previous = TerrainHandle::InvalidIndex;
            uint32_t Next = TerrainHandle::InvalidIndex;
        };

        /// GameThread で登録順に範囲を検索し、共有参照を複製せずに高さ場を借りる。
        const TerrainHeightField* FindTerrainRaw(float x, float z) const;
        bool IsLiveHandle(TerrainHandle handle) const;
        void ReleaseSlot(uint32_t index);

        Container::VariableArray<TerrainSlot> m_Slots;
        uint32_t m_FirstTerrain = TerrainHandle::InvalidIndex;
        uint32_t m_LastTerrain = TerrainHandle::InvalidIndex;
        uint32_t m_FirstFree = TerrainHandle::InvalidIndex;
        uint32_t m_TerrainCount = 0u;
        Container::TSharedPtr<const Asset::AssetSystem> m_AssetSystem;
    };
} // namespace NorvesLib::Core::Rendering::Terrain
