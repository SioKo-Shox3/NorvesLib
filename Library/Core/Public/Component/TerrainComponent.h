// クック済み地形の高さ場と区画メッシュを所有し、層の表からスプラット材質を作って区画を配置する。
// 地形は BeginPlay ではなくパス設定後の最初の Tick で読むため、作成してから値を設定できる。
// 流し込みを有効にすると、区画を 1 つずつ出し入れできる。
// EndPlay でモデルと高さ場の登録を返し、配置途中でも保持した資源を残さない。
// 所有者の破棄では子 Entity を Inner の連鎖に任せ、所有者が残る場合だけ明示的に外す。
#pragma once

#include "Component.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/Terrain/TerrainSystem.h"
#include "Streaming/ITerrainTileSite.h"

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
        void SetLayerTablePath(const Container::String& path) { getLayerTablePath() = path; }
        Container::String GetLayerTablePath() { return getLayerTablePath(); }
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

        /// 地形を読む最初の Tick までに設定する。有効なら区画を自動で置かず、PlaceTileAt / RemoveTileAt で 1 つずつ出し入れする。
        void SetTileStreaming(bool bEnabled) { m_bTileStreaming = bEnabled; }
        bool IsTileStreaming() const { return m_bTileStreaming; }

        /// 以下の区画の操作・照会はすべて GameThread で行う。読込み前・範囲外の操作は何もしない。
        /// 既に置いてあれば真。常駐の印は呼ぶ側が設定する。
        bool PlaceTileAt(uint32_t x, uint32_t z);
        void RemoveTileAt(uint32_t x, uint32_t z);
        bool IsTilePlacedAt(uint32_t x, uint32_t z) const;
        /// 行優先の番号。読込み前・範囲外は kInvalidTerrainTileId。
        uint32_t GetTileIdAt(uint32_t x, uint32_t z) const;
        /// 区画が実際に覆う升目の中心を境界と照合し、Z→X の順で返す。読込み前は空。
        void CollectTilesInBounds(const Streaming::StreamingBounds& bounds,
                                  Container::VariableArray<Streaming::TerrainTileCoordinate>& out) const;
        Streaming::ITerrainTileSite& GetTileSite() { return m_TileSite; }

        Container::TSharedPtr<const Rendering::Terrain::TerrainHeightField> GetHeightField() const { return m_HeightField; }
        uint32_t GetPlacedTileCount() const { return m_PlacedTileCount; }
        uint32_t GetTileCount() const { return static_cast<uint32_t>(m_Tiles.size()); }
        /// 欠けた区画を含め、全区画の配置の試行が終わったかを返す。読込み前と読込み失敗時は偽。
        bool IsFullyPlaced() const { return m_TerrainHandle.IsValid() && m_NextTile == GetTileCount(); }

        PROPERTY(Container::String, TerrainPath)
        /// アセットの根 Assets/ からの層の表の道。
        PROPERTY(Container::String, LayerTablePath)
        PROPERTY(Container::String, TilePathPrefix)
        PROPERTY(float, BaseColorR)
        PROPERTY(float, BaseColorG)
        PROPERTY(float, BaseColorB)
        PROPERTY(float, Roughness)
        PROPERTY(bool, bCastShadow)
        PROPERTY(uint32_t, TilesPerFrame)

    private:
        class TileSite final : public Streaming::ITerrainTileSite
        {
        public:
            explicit TileSite(TerrainComponent* owner) : m_Owner(owner) {}
            Streaming::ETerrainTileSiteState GetSiteState() const override;
            void CollectTilesInBounds(const Streaming::StreamingBounds& bounds,
                                     Container::VariableArray<Streaming::TerrainTileCoordinate>& outTiles) const override;
            uint32_t GetTileId(const Streaming::TerrainTileCoordinate& tile) const override;
            bool PlaceTile(const Streaming::TerrainTileCoordinate& tile) override;
            void RemoveTile(const Streaming::TerrainTileCoordinate& tile) override;
            bool IsTilePlaced(const Streaming::TerrainTileCoordinate& tile) const override;

        private:
            TerrainComponent* m_Owner; // 非所有。所有者のメンバとして同じ寿命を持つ。
        };

        struct TileCoordinate
        {
            uint32_t X = 0;
            uint32_t Z = 0;
        };

        struct PlacedTile
        {
            uint64_t ChildEntityId = 0;
            Rendering::ModelHandle Model;
            bool bPlaced = false;
        };

        void SetDefaults();
        bool LoadTerrain();
        void LoadSplatMaterial(const Rendering::Terrain::TerrainHeightField& heightField);
        void ReleaseSplatTextures();
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
        bool m_bTileStreaming = false;
        TileSite m_TileSite{this};
        Container::VariableArray<TileCoordinate> m_Tiles;
        // Tick 内の生成では ECS の行が未作成なので、即座に発行される ObjectId で子を記録する。
        Container::VariableArray<PlacedTile> m_PlacedTiles;
        uint32_t m_TileCountX = 0;
        uint32_t m_TileCountZ = 0;
        uint32_t m_TileCells = 0;
        Rendering::MegaGeometry::MegaTerrainSplat m_Splat;
        uint32_t m_NextTile = 0;
        uint32_t m_PlacedTileCount = 0;
    };
} // namespace NorvesLib::Core::Component
