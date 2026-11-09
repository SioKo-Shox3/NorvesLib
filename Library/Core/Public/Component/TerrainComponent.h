// クック済み地形の高さ場と区画メッシュを所有し、フレームごとに区画を配置する。
// 地形は BeginPlay ではなくパス設定後の最初の Tick で読むため、作成してから値を設定できる。
// EndPlay でモデルと高さ場の登録を返し、配置途中でも保持した資源を残さない。
// 所有者の破棄では子 Entity を Inner の連鎖に任せ、所有者が残る場合だけ明示的に外す。
#pragma once

#include "Component.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/Terrain/TerrainSystem.h"

namespace NorvesLib::Core::Component
{
    class TerrainComponent : public Component
    {
        REFLECTION_CLASS(TerrainComponent, Component)

    public:
        TerrainComponent();
        explicit TerrainComponent(const FieldInitializer* initializer);
        explicit TerrainComponent(const IUnknown* sourceObject);
        ~TerrainComponent() override;

        void BeginPlay() override;
        void EndPlay() override;

        void SetTerrainPath(const Container::String& path) { getTerrainPath() = path; }
        Container::String GetTerrainPath() { return getTerrainPath(); }
        void SetTilePathPrefix(const Container::String& prefix) { getTilePathPrefix() = prefix; }
        Container::String GetTilePathPrefix() { return getTilePathPrefix(); }
        void SetBaseColor(float r, float g, float b)
        {
            getBaseColorR() = r;
            getBaseColorG() = g;
            getBaseColorB() = b;
        }
        void GetBaseColor(float& r, float& g, float& b)
        {
            r = getBaseColorR();
            g = getBaseColorG();
            b = getBaseColorB();
        }
        void SetRoughness(float roughness) { getRoughness() = roughness; }
        float GetRoughness() { return getRoughness(); }
        void SetCastShadow(bool bCast) { getbCastShadow() = bCast; }
        bool GetCastShadow() { return getbCastShadow(); }
        void SetTilesPerFrame(uint32_t count) { getTilesPerFrame() = count; }
        uint32_t GetTilesPerFrame() { return getTilesPerFrame(); }

        Container::TSharedPtr<const Rendering::Terrain::TerrainHeightField> GetHeightField() const { return m_HeightField; }
        uint32_t GetPlacedTileCount() const { return m_PlacedTileCount; }
        uint32_t GetTileCount() const { return static_cast<uint32_t>(m_Tiles.size()); }
        /// 欠けた区画を含め、全区画の配置の試行が終わったかを返す。読込み前と読込み失敗時は偽。
        bool IsFullyPlaced() const { return m_TerrainHandle.IsValid() && m_NextTile == GetTileCount(); }

        PROPERTY(Container::String, TerrainPath)
        PROPERTY(Container::String, TilePathPrefix)
        PROPERTY(float, BaseColorR)
        PROPERTY(float, BaseColorG)
        PROPERTY(float, BaseColorB)
        PROPERTY(float, Roughness)
        PROPERTY(bool, bCastShadow)
        PROPERTY(uint32_t, TilesPerFrame)

    private:
        struct TileCoordinate
        {
            uint32_t X = 0;
            uint32_t Z = 0;
        };

        void SetDefaults();
        bool LoadTerrain();
        void PlacePendingTiles();
        bool PlaceTile(const TileCoordinate& tile);

        // 保存するのは上の値だけ。以下の登録と所有物は BeginPlay / EndPlay の間だけ有効。
        Container::TSharedPtr<const Asset::AssetSystem> m_AssetSystem;
        Container::TSharedPtr<const Rendering::Terrain::TerrainHeightField> m_HeightField;
        Rendering::Terrain::TerrainHandle m_TerrainHandle;
        Rendering::Terrain::TerrainSystem* m_TerrainSystem = nullptr;
        Rendering::RenderResources* m_RenderResources = nullptr;
        World* m_World = nullptr;
        // Component を単独で外すと Outer が先に切れるため、所有者を World 内で引き直す。
        uint64_t m_OwnerEntityId = 0;
        bool m_bEndingPlay = false;
        bool m_bLoadFailed = false;
        Container::VariableArray<TileCoordinate> m_Tiles;
        // Tick 内の生成では ECS の行が未作成なので、即座に発行される ObjectId で子を記録する。
        Container::VariableArray<uint64_t> m_ChildEntityIds;
        Container::VariableArray<Rendering::ModelHandle> m_Models;
        uint32_t m_NextTile = 0;
        uint32_t m_PlacedTileCount = 0;
    };
} // namespace NorvesLib::Core::Component
