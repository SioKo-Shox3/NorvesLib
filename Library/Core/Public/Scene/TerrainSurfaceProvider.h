// 地形の層と草の密度から、足元の表面と濡れを照会する。
#pragma once

#include "Container/FixedArray.h"
#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Scene/SceneQuery.h"

namespace NorvesLib::Core::Environment
{
    class EnvironmentSystem;
}

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainHeightField;
    struct TerrainMaterialDesc;
    class TerrainSystem;
}

namespace NorvesLib::Core::Vegetation
{
    class GrassField;
    class Vegetation;
}

namespace NorvesLib::Core::Scene
{
    class SurfaceRegistry;

    /// 足元の濡れ（0..1）。環境が有効なら環境の濡れ、無効なら地形の濡れを使う。
    /// 各 provider はこの関数を通し、草の水気の偏り（標本が無ければ中立の 0.5）を ComputeEffectiveWetness で反映する。
    float ComputeGroundWetness01(const Environment::EnvironmentSystem& environment,
        const Rendering::Terrain::TerrainSystem& terrain, const Vegetation::GrassField* grass, float x, float z);

    /// 地形の層を双線形補間して表面を返す。TerrainComponent が表を読めた高さ場は表の表面名を使い、
    /// それ以外は高さ場の層名から組み立てる仮の材質記述の名前を使う。各システムと登録簿の寿命は持ち主が保証する。
    class TerrainSurfaceProvider final : public ISurfaceProvider
    {
    public:
        TerrainSurfaceProvider(const Rendering::Terrain::TerrainSystem& terrain, const SurfaceRegistry& registry,
            const Environment::EnvironmentSystem& environment, const Vegetation::Vegetation& vegetation);
        EPhysicsSceneQueryResult SampleSurface(const SurfaceQuery& query, SurfaceContribution& out) const override;

        /// TerrainComponent の LayerTablePath から読み取った層ごとの表面名を高さ場に結ぶ。
        /// 結んでいない高さ場は、高さ場の層名から組み立てる仮の材質記述を使う。GameThread 専用。
        bool SetLayerSurfaceNames(const Container::TSharedPtr<const Rendering::Terrain::TerrainHeightField>& field,
            const Rendering::Terrain::TerrainMaterialDesc& desc);

    private:
        static constexpr size_t kMaxLayers = 8;
        using LayerIds = Container::FixedArray<SurfaceId, kMaxLayers>;
        struct CachedLayers
        {
            Container::TWeakPtr<const Rendering::Terrain::TerrainHeightField> Field;
            const Rendering::Terrain::TerrainHeightField* Raw = nullptr;
            uint64_t RegistryRevision = 0;
            uint64_t BindingRevision = 0;
            LayerIds Ids;
        };

        struct LayerSurfaceBinding
        {
            Container::TWeakPtr<const Rendering::Terrain::TerrainHeightField> Field;
            const Rendering::Terrain::TerrainHeightField* Raw = nullptr;
            uint64_t FirstBoundRevision = 0;
            size_t LayerCount = 0;
            Container::FixedArray<Container::AnsiString, kMaxLayers> Names;
        };

        const LayerIds& GetLayerIds(const Container::TSharedPtr<const Rendering::Terrain::TerrainHeightField>& field) const;

        const Rendering::Terrain::TerrainSystem& m_Terrain;
        const SurfaceRegistry& m_Registry;
        const Environment::EnvironmentSystem& m_Environment;
        const Vegetation::Vegetation& m_Vegetation;
        // 照会は GameThread 専用。高さ場の寿命を延ばさず、弱参照の期限で同じアドレスの別の高さ場を区別する。
        mutable Container::FixedArray<CachedLayers, 8> m_Cache;
        // 登録解除後の高さ場は FindTerrain が返さない。Engine の破棄時に EndPlay からここに触れずに済むよう、弱参照だけを持つ。
        Container::FixedArray<LayerSurfaceBinding, 16> m_Bindings;
        uint64_t m_BindingRevision = 0;
    };

    /// 表面の種別はコライダー、地形の層、草の密度、既定の順に決まる。
    /// 草は地形の層が種別を決めた場所でも密度を返す。参照で借りる各システムと登録簿の寿命は持ち主が保証する。
    class VegetationSurfaceProvider final : public ISurfaceProvider
    {
    public:
        VegetationSurfaceProvider(const Vegetation::Vegetation& vegetation, const SurfaceRegistry& registry,
            const Rendering::Terrain::TerrainSystem& terrain, const Environment::EnvironmentSystem& environment);
        EPhysicsSceneQueryResult SampleSurface(const SurfaceQuery& query, SurfaceContribution& out) const override;

    private:
        const Vegetation::Vegetation& m_Vegetation;
        const SurfaceRegistry& m_Registry;
        const Rendering::Terrain::TerrainSystem& m_Terrain;
        const Environment::EnvironmentSystem& m_Environment;
        mutable uint64_t m_CachedRevision = UINT64_MAX;
        mutable SurfaceId m_GrassId;
        mutable bool m_bFoundGrass = false;
    };
}
