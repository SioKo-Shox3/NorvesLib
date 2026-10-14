#include "GameModes/Wasteland/WastelandMode.h"

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/RHI/ITexture.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshPageSource.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <cstdint>
#include <cstdio>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;
namespace TerrainSpec = NorvesLib::Core::Rendering::Terrain::WastelandTerrain;

namespace Game::GameModes
{

    /// 地形の区画 1 つの読み込み
    struct WastelandTerrainTile
    {
        String LogicalPath;
        TSharedPtr<Asset::CookedMeshData> Mesh;
        /// ページ（NVMESH v1.1）の読み込み元。ページを 2 つ以上持つメッシュだけが持つ
        TSharedPtr<IGeometryPageSource> PageSource;
    };

    /// 地形の区画と、材質のテクスチャの読み込み
    struct WastelandAssembly
    {
        VariableArray<WastelandTerrainTile> Tiles;
        /// 1 層目（砂）の材質
        MaterialCreateData Material;
        /// 2 層目（乾いた土）・3 層目（岩肌）と混ぜ方
        MegaTerrainBlend TerrainBlend;
        uint32_t PendingTextureCount = 0;
        bool bCancelled = false;
        bool bPlaced = false;
    };

    namespace
    {
        // 昼の太陽。盆地を見下ろす視点（+Z 向き）に対して横から差し、起伏の陰影が出る向き
        constexpr float kSunAltitudeDegrees = 32.0f;
        constexpr float kSunAzimuthDegrees = -60.0f;
        // 晴天の昼の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）
        constexpr float kExposureEV100 = 15.0f;
        // 高さフォグ: 盆地の床（高さ 0）での密度[1/m]と、上へ向かって薄くなる割合[1/m]。
        // 1 km 先の稜線が霞みすぎず、遠近が分かる程度（地面すれすれの水平の 1 km で透過率 約 0.74）
        constexpr float kFogDensity = 0.0003f;
        constexpr float kFogFalloff = 0.012f;
        // カメラの遠方面[m]。2 km 四方の対角（約 2.8 km）の先まで描く
        constexpr float kFarPlane = 3200.0f;
        // 地形を置いてから SceneReady を出すまでの Tick の数（ページの読み込みが追いつく余裕）
        constexpr uint32_t kReadyTicksAfterTerrain = 90;
        // 地形の材質は 3 層の混ぜ合わせ。1 層目の砂は Poly Haven の sand_01（Assets/AssetSets/Rendering3DTestStartupMaterials.json が焼く）
        constexpr const char *kSandAlbedoPath = "Assets/Textures/PolyHaven/sand_01/sand_01_diff_4k.jpg";
        constexpr const char *kSandNormalPath = "Assets/Textures/PolyHaven/sand_01/sand_01_nor_dx_4k.jpg";
        constexpr const char *kSandOrmPath = "Assets/Textures/PolyHaven/sand_01/sand_01_orm_4k";
        // 2 層目の乾いた土は dry_ground_rocks（Assets/AssetSets/Rendering3DTestStressTextures.json が焼く）
        constexpr const char *kDirtAlbedoPath = "Assets/Textures/PolyHaven/dry_ground_rocks/dry_ground_rocks_diff_4k.jpg";
        constexpr const char *kDirtNormalPath = "Assets/Textures/PolyHaven/dry_ground_rocks/dry_ground_rocks_nor_dx_4k.jpg";
        constexpr const char *kDirtOrmPath = "Assets/Textures/PolyHaven/dry_ground_rocks/dry_ground_rocks_orm_4k";
        // 3 層目の岩肌は rock_face（Assets/AssetSets/WastelandTerrain.json が焼く）
        constexpr const char *kRockAlbedoPath = "Assets/Textures/PolyHaven/rock_face/rock_face_diff_4k.jpg";
        constexpr const char *kRockNormalPath = "Assets/Textures/PolyHaven/rock_face/rock_face_nor_dx_4k.jpg";
        constexpr const char *kRockOrmPath = "Assets/Textures/PolyHaven/rock_face/rock_face_orm_4k";
        // 材質の UV（ワールドの XZ ÷ 4 m）に掛ける倍率。岩肌は崖の大きな割れ目に合わせて 8 m で繰り返す
        constexpr float kDirtUVScale = 1.0f;
        constexpr float kRockUVScale = 0.5f;
        // 混ぜ方: 砂丘の風下の面（最も急な所で 20° 台）は砂のまま、メサの裾の崖錐は乾いた土、崖と外周の急な稜線は岩肌にする
        constexpr float kDirtSlopeDegrees[2] = {24.0f, 33.0f};
        constexpr float kRockSlopeDegrees[2] = {37.0f, 48.0f};
        // 盆地より高い所（崖錐・稜線の麓）ほど土へ寄せる
        constexpr float kDirtHeightRange[2] = {12.0f, 50.0f};
        constexpr float kDirtHeightBiasDegrees = 10.0f;
        constexpr float kBlendDepth = 0.3f;
        // 1 層目・2 層目・3 層目の 3 枚ずつ
        constexpr uint32_t kTerrainTextureCount = 9;

        /// 物理空・太陽・高さフォグ
        void ApplyEnvironment(GameModeContext &ctx)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = kSunAltitudeDegrees;
            sky.SunAzimuthDegrees = kSunAzimuthDegrees;
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(sky);

            VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
            fog.bEnabled = true;
            fog.DensityAtBaseHeight = kFogDensity;
            fog.BaseHeight = 0.0f;
            fog.HeightFalloffPerUnit = kFogFalloff;
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(fog);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        /// 空・霧の設定を、ほかのモードへ持ち越さないよう既定へ戻す
        void ResetEnvironment(GameModeContext &ctx)
        {
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(MakeDefaultSkyAtmosphereParameters());
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        /// 区画 (tileX, tileZ) のクック済みメッシュの論理パス
        String MakeTileLogicalPath(uint32_t tileX, uint32_t tileZ)
        {
            char suffix[32] = {};
            std::snprintf(suffix, sizeof(suffix), "%u_%u.generated", static_cast<unsigned int>(tileX),
                          static_cast<unsigned int>(tileZ));
            return String(TerrainSpec::kCookedMeshLogicalPathPrefix) + suffix;
        }

        /// 地形の全区画のクック済みメッシュを解決・解析する。1 つでも無い・壊れているときは理由を出して false
        bool LoadTerrainTiles(const TSharedPtr<const Asset::AssetSystem> &assetSystem, WastelandAssembly &assembly)
        {
            for (uint32_t tileZ = 0; tileZ < TerrainSpec::kTilesPerSide; ++tileZ)
            {
                for (uint32_t tileX = 0; tileX < TerrainSpec::kTilesPerSide; ++tileX)
                {
                    WastelandTerrainTile tile;
                    tile.LogicalPath = MakeTileLogicalPath(tileX, tileZ);
                    const Asset::AssetResolveResult resolved =
                        assetSystem->ResolveAsset(tile.LogicalPath, Asset::AssetKind::Model);
                    if (!resolved.UsedCooked())
                    {
                        LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s 地形のクック済みメッシュがありません。"
                                      "Scripts/CookAssets.ps1 で焼くと描けます",
                                      tile.LogicalPath.c_str());
                        return false;
                    }
                    Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
                    if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty() || parsed.Mesh.Vertices.empty())
                    {
                        LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s status=%u "
                                      "地形のクック済みメッシュを解析できません",
                                      tile.LogicalPath.c_str(), static_cast<unsigned int>(parsed.Status));
                        return false;
                    }
                    Asset::AssetCookedRange pageRange;
                    if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
                    {
                        tile.PageSource = MakeCookedMeshPageSource(parsed.Mesh, assetSystem->GetCookedFileReader(),
                                                                   pageRange.Request, pageRange.BaseOffset);
                    }
                    tile.Mesh = MakeShared<Asset::CookedMeshData>(std::move(parsed.Mesh));
                    assembly.Tiles.push_back(std::move(tile));
                }
            }
            return true;
        }

        /// 材質のテクスチャを 1 枚、非同期で読む。結果は主スレッドで target（assembly の中の枠）へ入れる。
        /// pNormalTwoChannel は法線のときだけ渡し、クック済みの法線が BC5 かを入れる
        void LoadMaterialTexture(TextureResources &textures, const TSharedPtr<WastelandAssembly> &assembly,
                                 const char *path, TextureHandle *target, bool *pNormalTwoChannel)
        {
            TextureResources *pTextures = &textures;
            const String pathText(path);
            textures.LoadTextureAsync(
                pathText,
                [pTextures, assembly, pathText, target, pNormalTwoChannel](TextureHandle handle)
                {
                    if (assembly->bCancelled)
                    {
                        // シーンを出た後に届いた結果。パスで読んだテクスチャはキャッシュが所有するので、何もしない
                        return;
                    }
                    if (!handle.IsValid())
                    {
                        LOG_WARNING_F("COOKED_MODEL_TEXTURES_MISSING scene=wasteland path=%s "
                                      "地形の材質のテクスチャを読めませんでした",
                                      pathText.c_str());
                    }
                    // target は assembly の中の枠（ラムダが assembly を持つので、ここでは生きている）
                    *target = handle;
                    if (pNormalTwoChannel != nullptr)
                    {
                        // クック済みの法線が BC5（2 チャンネル）なら印を付ける。無圧縮の RGBA8 なら付けない
                        const NorvesLib::RHI::ITexture *rhiTexture =
                            handle.IsValid() ? pTextures->GetRHITexture(handle) : nullptr;
                        *pNormalTwoChannel =
                            rhiTexture != nullptr && rhiTexture->GetFormat() == NorvesLib::RHI::Format::BC5_UNORM;
                    }
                    --assembly->PendingTextureCount;
                });
        }

        /// 材質がそろった地形の区画を MegaMesh にして置く。置けた区画の数を返す
        uint32_t PlaceTerrain(GameModeContext &ctx, WastelandAssembly &assembly)
        {
            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            uint32_t placed = 0;
            uint64_t clusters = 0;
            uint64_t vertices = 0;
            for (WastelandTerrainTile &tile : assembly.Tiles)
            {
                MegaMeshCreateInfo createInfo;
                if (!BuildMegaMeshCreateInfoFromCookedMesh(*tile.Mesh, createInfo))
                {
                    LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s MegaMesh の入力を作れませんでした",
                                  tile.LogicalPath.c_str());
                    continue;
                }
                createInfo.PageSource = tile.PageSource;
                createInfo.Material.AlbedoTexture = assembly.Material.AlbedoTexture;
                createInfo.Material.NormalTexture = assembly.Material.NormalTexture;
                createInfo.Material.ORMTexture = assembly.Material.ORMTexture;
                createInfo.Material.bNormalTwoChannel = assembly.Material.bNormalTwoChannel;
                createInfo.Material.TerrainBlend = assembly.TerrainBlend;
                createInfo.DebugName = tile.LogicalPath.c_str();
                const MegaMeshHandle megaMesh = megaGeometry.CreateMegaMesh(createInfo);
                if (!megaMesh.IsValid())
                {
                    LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s MegaMesh を作れませんでした", tile.LogicalPath.c_str());
                    continue;
                }
                const ModelHandle model = megaGeometry.RegisterModel(megaMesh, tile.LogicalPath.c_str(), tile.LogicalPath);
                if (!model.IsValid())
                {
                    megaGeometry.ReleaseMegaMesh(megaMesh);
                    LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s モデルとして登録できませんでした",
                                  tile.LogicalPath.c_str());
                    continue;
                }
                // モデルはシーンを出るときにスコープが返す
                ctx.ScopeRef.TrackModel(model);

                // 区画の頂点はワールドの座標で焼いてあるので、原点に置く
                Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
                auto *component =
                    object != nullptr ? ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(object) : nullptr;
                if (component == nullptr)
                {
                    LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s Entity を置けませんでした", tile.LogicalPath.c_str());
                    continue;
                }
                object->SetPosition(0.0f, 0.0f, 0.0f);
                component->SetMegaMeshHandle(megaMesh);
                component->SetCastShadow(true);
                clusters += tile.Mesh->Clusters.size();
                vertices += tile.Mesh->Vertices.size();
                ++placed;
            }
            LOG_INFO_F("WASTELAND_TERRAIN tiles=%u placed=%u clusters=%llu vertices=%llu",
                       static_cast<unsigned int>(assembly.Tiles.size()), placed,
                       static_cast<unsigned long long>(clusters), static_cast<unsigned long long>(vertices));
            // 解析したメッシュは MegaMesh へ渡したので手放す（ページはページの読み込み元が読む）
            for (WastelandTerrainTile &tile : assembly.Tiles)
            {
                tile.Mesh.reset();
            }
            return placed;
        }
    } // namespace

    GameModeEnterResult WastelandRoutine::Enter(GameModeContext &ctx, WastelandData &data)
    {
        LOG_INFO("wasteland を開始します");
        data.m_TickCount = 0;
        data.m_TerrainSettledTick = 0;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kWastelandSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("wasteland: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        // 視点の高さは地形からの高さ。Game もクッカーと同じ高さ場の関数で地形の高さを引く
        const Terrain::TerrainHeightField field(TerrainSpec::MakeParams());
        const float groundHeight = field.SampleHeight(pose.Position[0], pose.Position[2]);
        pose.Position[1] += groundHeight;
        LOG_INFO_F("WASTELAND_VIEW view=%s x=%.1f z=%.1f ground=%.2f eye=%.2f",
                   data.m_ViewName.empty() ? "default" : data.m_ViewName.c_str(), static_cast<double>(pose.Position[0]),
                   static_cast<double>(pose.Position[2]), static_cast<double>(groundHeight),
                   static_cast<double>(pose.Position[1]));

        ApplyEnvironment(ctx);

        // 地形の区画を読み、材質のテクスチャの読み込みを始める。無いときは空と太陽だけで組み立てを終える
        TSharedPtr<const Asset::AssetSystem> assetSystem;
        if (data.m_GetAssetSystem.IsBound())
        {
            assetSystem = data.m_GetAssetSystem.Invoke();
        }
        if (!assetSystem)
        {
            LOG_WARNING("COOKED_MODEL_MISSING scene=wasteland AssetSystem が無いため地形を置きません");
        }
        else
        {
            auto assembly = MakeShared<WastelandAssembly>();
            if (LoadTerrainTiles(assetSystem, *assembly))
            {
                assembly->Material.DebugName = "WastelandTerrain";
                MegaTerrainBlend &blend = assembly->TerrainBlend;
                blend.bEnabled = true;
                blend.Dirt.UVScale = kDirtUVScale;
                blend.Rock.UVScale = kRockUVScale;
                blend.DirtSlopeDegrees[0] = kDirtSlopeDegrees[0];
                blend.DirtSlopeDegrees[1] = kDirtSlopeDegrees[1];
                blend.RockSlopeDegrees[0] = kRockSlopeDegrees[0];
                blend.RockSlopeDegrees[1] = kRockSlopeDegrees[1];
                blend.DirtHeightRange[0] = kDirtHeightRange[0];
                blend.DirtHeightRange[1] = kDirtHeightRange[1];
                blend.DirtHeightBiasDegrees = kDirtHeightBiasDegrees;
                blend.BlendDepth = kBlendDepth;
                assembly->PendingTextureCount = kTerrainTextureCount;
                auto &textures = ctx.RenderResourcesRef.Textures();
                MaterialCreateData &sand = assembly->Material;
                LoadMaterialTexture(textures, assembly, kSandAlbedoPath, &sand.AlbedoTexture, nullptr);
                LoadMaterialTexture(textures, assembly, kSandNormalPath, &sand.NormalTexture, &sand.bNormalTwoChannel);
                LoadMaterialTexture(textures, assembly, kSandOrmPath, &sand.ORMTexture, nullptr);
                LoadMaterialTexture(textures, assembly, kDirtAlbedoPath, &blend.Dirt.AlbedoTexture, nullptr);
                LoadMaterialTexture(textures, assembly, kDirtNormalPath, &blend.Dirt.NormalTexture,
                                    &blend.Dirt.bNormalTwoChannel);
                LoadMaterialTexture(textures, assembly, kDirtOrmPath, &blend.Dirt.ORMTexture, nullptr);
                LoadMaterialTexture(textures, assembly, kRockAlbedoPath, &blend.Rock.AlbedoTexture, nullptr);
                LoadMaterialTexture(textures, assembly, kRockNormalPath, &blend.Rock.NormalTexture,
                                    &blend.Rock.bNormalTwoChannel);
                LoadMaterialTexture(textures, assembly, kRockOrmPath, &blend.Rock.ORMTexture, nullptr);
                data.m_pAssembly = assembly;
            }
        }

        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("wasteland: カメラを作れませんでした");
            if (data.m_pAssembly)
            {
                data.m_pAssembly->bCancelled = true;
                data.m_pAssembly.reset();
            }
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        Component::CameraComponent *camera = data.m_Rig.GetCamera();
        camera->SetFarPlane(kFarPlane);
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), kExposureEV100));
        camera->SetExposureCompensation(0.0f);
        camera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);
        return GameModeEnterResult::Succeeded;
    }

    void WastelandRoutine::Tick(GameModeContext &ctx, WastelandData &data, float deltaTime)
    {
        ++data.m_TickCount;
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);

        if (data.m_TerrainSettledTick == 0)
        {
            if (!data.m_pAssembly)
            {
                // 地形を置けない（クック済みが無い）。空と太陽だけのシーンとして組み立てを終える
                data.m_TerrainSettledTick = data.m_TickCount;
            }
            else if (data.m_pAssembly->PendingTextureCount == 0 && !data.m_pAssembly->bPlaced)
            {
                PlaceTerrain(ctx, *data.m_pAssembly);
                data.m_pAssembly->bPlaced = true;
                data.m_TerrainSettledTick = data.m_TickCount;
            }
        }

        // 地形を置いてから、ページの読み込みが追いつくまで待ち、台本を流し終えるまで決定的な撮影を待たせる
        const bool bReady = data.m_TerrainSettledTick != 0 &&
                            data.m_TickCount >= data.m_TerrainSettledTick + kReadyTicksAfterTerrain &&
                            !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kWastelandSceneId, bReady);
    }

    void WastelandRoutine::Leave(GameModeContext &ctx, WastelandData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        // 描画スレッドは、影・MegaGeometry のパスの間、モデルの GPU データを生のポインタで読む。Cleanup がモデルを返す
        // 瞬間に描画中のフレームが残っていると解放済みのデータを読むので、先にそれまでのフレームの描画を完了させる。
        ctx.EngineRef.GetRenderWorld().WaitForRender();
        // 届いていない読み込みの結果は捨てる
        if (data.m_pAssembly)
        {
            data.m_pAssembly->bCancelled = true;
            data.m_pAssembly.reset();
        }
        data.m_Rig.Leave(ctx);
        // 地形の Entity・モデル・カメラの実体は GameModeScope::Cleanup が返す。空と霧は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("wasteland を終了します");
    }

} // namespace Game::GameModes
