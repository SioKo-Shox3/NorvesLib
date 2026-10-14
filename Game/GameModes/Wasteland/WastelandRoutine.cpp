#include "GameModes/Wasteland/WastelandMode.h"

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"
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
#include "GameModes/Wasteland/WastelandLayout.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;
namespace TerrainSpec = NorvesLib::Core::Rendering::Terrain::WastelandTerrain;
namespace Layout = Game::GameModes::WastelandLayout;

namespace Game::GameModes
{

    /// 岩・崖・建物の 1 種類の読み込み。材質のテクスチャがそろったら MegaMesh にする
    struct WastelandPropLoad
    {
        bool bStarted = false;
        TSharedPtr<Asset::CookedMeshData> Mesh;
        TSharedPtr<IGeometryPageSource> PageSource;
        MaterialCreateData Material;
        uint32_t PendingTextureCount = 0;
        /// メッシュの範囲と表の向き（bAvailable は MegaMesh を作れたときに立てる）
        Layout::PropMeshShape Shape;
        MegaMeshHandle MegaMesh;
    };

    /// 地形の区画と、材質のテクスチャの読み込み
    struct WastelandAssembly
    {
        /// 区画を読む AssetSystem
        TSharedPtr<const Asset::AssetSystem> AssetSystem;
        /// 地形の区画のクック済みメッシュの論理パス（Tick ごとに NextTile から数区画ずつ読んで置く）
        VariableArray<String> TilePaths;
        uint32_t NextTile = 0;
        uint32_t PlacedTiles = 0;
        uint64_t PlacedClusters = 0;
        uint64_t PlacedVertices = 0;
        /// 1 層目（砂）の材質
        MaterialCreateData Material;
        /// 2 層目（乾いた土）・3 層目（岩肌）と混ぜ方
        MegaTerrainBlend TerrainBlend;
        uint32_t PendingTextureCount = 0;
        /// 小物・建物（種類ごと。テクスチャの読み込みの結果が枠を指すので、固定の配列で持つ）
        WastelandPropLoad Props[Layout::kPropKindCount];
        bool bCancelled = false;
        bool bPlaced = false;
        bool bPropsPlaced = false;
    };

    namespace
    {
        // 晴天の午後の太陽。手本の遠景の写真 far_01（2012-05-18、晴れに積雲）の影から決めた: 低木の影は株の高さの半分ほどで
        // 東北東へ、崖の張り出しの影は短く下と東へ落ちる。5 月 18 日の南中の 2 時間後（仰角 58.5°・方位 北から 244°）に当たる
        // （Docs/RenderingValidation/WastelandLight.md）。方位角は +X（東）から +Z（南）へ測るので、北から 244° は 154°
        constexpr float kSunAltitudeDegrees = 58.5f;
        constexpr float kSunAzimuthDegrees = 154.0f;
        // 晴天の日中の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）。晴天の f/16 の目安（EV 15）より 0.3 段明るくし、
        // 日向の地面を手本の写真と同じくらいの明るさ（リニアの 75 パーセンタイルで 0.26〜0.31）にする
        constexpr float kExposureEV100 = 14.7f;
        // 日中のグレーディング。トーンマップは暗部を縮めない NeutralLinear にし、空の光だけの影の中が黒くつぶれないようにする。
        // コントラストは軸 0.5 の S 字でわずかに立て、彩度を少し上げて空の青を残し、色温度はわずかに足すだけにする
        // （写真の日向は白みの強い昼の色）。
        // ビネットは画面の隅を少し落とす程度
        constexpr float kGradingContrast = 1.1f;
        constexpr float kGradingContrastPivot = 0.5f;
        constexpr float kGradingSaturation = 1.2f;
        constexpr float kGradingTemperature = 0.05f;
        constexpr float kVignetteIntensity = 0.25f;
        constexpr float kVignetteRadius = 0.85f;
        constexpr float kVignetteSoftness = 0.6f;
        // 背面の無いスキャン（崖の岩）とみなす、面の法線の和の水平成分と表面積の比
        constexpr float kOpenSideRatioMin = 0.2f;
        // 崖の岩（花崗岩のスキャン。アルベドの平均がリニアで 0.13 : 0.09 : 0.065）に掛ける色。メサの岩肌（rock_face、
        // 0.12 : 0.06 : 0.03）の赤みに寄せ、足元で灰色に浮かないようにする
        constexpr float kCliffTint[3] = {1.0f, 0.75f, 0.55f};
        // 地形の材質（3 層の混ぜ合わせ）に掛ける明るさ。砂（sand_01）のアルベドの平均はリニアで 0.31 と明るく、
        // 手本の谷床の赤い砂と土（コロラド高原の赤土のアルベド 0.2〜0.25）より日向が 1.5 倍ほど明るく出るので、色を変えずに
        // 水準だけ 0.22 前後へ下げる。色と素材そのものは SCN-WL-MATERIAL-PALETTE で替える
        constexpr float kTerrainAlbedoScale = 0.7f;
        // 高さフォグ: 基準の高さでの密度[1/m]と、上へ向かって薄くなる割合[1/m]。遠景の霞は空気遠近が受け持つので、
        // 谷床に薄く溜まる砂ぼこりの層にする。谷床は北から南東へ下がり、平らな所の高さは -38〜+39 m（5〜95%）なので、
        // 基準を南東の低い谷床（-40 m）に置き、100 m で 1/e に薄くして北の高い谷床（+40 m）にも半分ほど残す。
        // ビュートの頂（+100〜+200 m）より上ではほぼ消える
        constexpr float kFogDensity = 0.00015f;
        constexpr float kFogBaseHeight = -40.0f;
        constexpr float kFogFalloff = 0.01f;
        // 空気遠近: 物理空の媒質（下のエアロゾルを増やした値）の距離を 1.5 倍に数え、谷床の向こうのビュート（1〜3.5 km）が
        // 写真の遠い崖と同じくらい（崖の暗部が地平線の空の半分ほど）に霞み、形は読めるようにする。300 m より手前は掛けない
        // （足元・近くの崖の色は変えない）
        constexpr float kAerialPerspectiveDistanceScale = 1.5f;
        constexpr float kAerialPerspectiveStartDistance = 300.0f;
        // 空のエアロゾル（Mie）の高さの尺度[m]。既定の 1.2 km の 2.5 倍にして柱の量を増やし、写真の白みの強い地平線の空と、
        // 遠い崖の明るい霞に寄せる
        constexpr float kSkyMieScaleHeight = 3000.0f;
        // 雲の層: 写真の晴天の積雲（底の平らな小さな雲が空の 4 割ほど）。乾いた高原で雲底が高いので高度 2.5〜3.6 km の薄い層にし、
        // 下から見ても日の当たる側が白く残るようにする。西寄りの風でゆっくり流す
        constexpr float kCloudBottomAltitude = 2500.0f;
        constexpr float kCloudTopAltitude = 3600.0f;
        constexpr float kCloudCoverage = 0.4f;
        constexpr float kCloudWindVelocity[2] = {8.0f, 3.0f};
        // カメラの遠方面[m]。地形（4.7 × 4.6 km）の対角（約 6.6 km）の先まで描く
        constexpr float kFarPlane = 7000.0f;
        // 太陽の影の到達距離[m]。谷床の視点から、写真に写る主要な崖（主役のビュート 0.5〜2.8 km、ground_02 の Rooster Butte 3.5 km）と、
        // その影（仰角 58.5°で比高 200 m の崖から 120 m 前後）まで落とす
        constexpr float kSunShadowMaxDistance = 4000.0f;
        // 地形を置いてから SceneReady を出すまでの Tick の数（ページの読み込みが追いつく余裕）
        constexpr uint32_t kReadyTicksAfterTerrain = 90;
        // 1 Tick に読んで置く地形の区画の数（区画は 1 つ 37 MB 前後。読み込みと解析のメモリを数区画ぶんに抑える）
        constexpr uint32_t kTerrainTilesPerTick = 4;
        // 岩・崖・建物（WastelandLayout）を置くか。配置は手続きの地形（中央の盆地とメサの輪）に合わせたものなので、
        // DEM の地形では前哨地を置き直す（SCN-WL-OUTPOST）まで置かない
        constexpr bool kPlaceLayoutProps = false;
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
        // 材質の UV（ワールドの位置 ÷ 4 m。岩肌は 3 方向の投影）に掛ける倍率。岩肌は崖の大きな割れ目に合わせて 8 m で繰り返す
        constexpr float kDirtUVScale = 1.0f;
        constexpr float kRockUVScale = 0.5f;
        // 広い範囲の明るさ・色味の揺らぎ（数十〜数百 m）。同じ模様の繰り返しを、砂の明るさの斑で隠す
        constexpr float kMacroWavelengths[2] = {80.0f, 300.0f};
        constexpr float kMacroBrightness = 0.22f;
        constexpr float kMacroHue = 0.05f;
        // 距離で UV の倍率を変える: 10 m から、距離が 3 倍になるごとに模様を 3 倍に大きくする（最大 4 段で 81 倍）
        constexpr float kDetailStartDistance = 10.0f;
        constexpr float kDetailDistanceRatio = 3.0f;
        constexpr float kDetailMaxLevel = 4.0f;
        // 混ぜ方: 砂丘の風下の面（最も急な所で 20° 台）は砂のまま、メサの裾の崖錐は乾いた土、崖と外周の急な稜線は岩肌にする
        constexpr float kDirtSlopeDegrees[2] = {24.0f, 33.0f};
        constexpr float kRockSlopeDegrees[2] = {37.0f, 48.0f};
        // 盆地より高い所（崖錐・稜線の麓）ほど土へ寄せる
        constexpr float kDirtHeightRange[2] = {12.0f, 50.0f};
        constexpr float kDirtHeightBiasDegrees = 10.0f;
        constexpr float kBlendDepth = 0.3f;
        // 1 層目・2 層目・3 層目の 3 枚ずつ
        constexpr uint32_t kTerrainTextureCount = 9;

        /// 物理空・太陽・高さフォグ・空気遠近・雲の層
        void ApplyEnvironment(GameModeContext &ctx)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = kSunAltitudeDegrees;
            sky.SunAzimuthDegrees = kSunAzimuthDegrees;
            sky.MieScaleHeightMeters = kSkyMieScaleHeight;
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(sky);

            VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
            fog.bEnabled = true;
            fog.DensityAtBaseHeight = kFogDensity;
            fog.BaseHeight = kFogBaseHeight;
            fog.HeightFalloffPerUnit = kFogFalloff;
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(fog);

            AerialPerspectiveParameters aerialPerspective;
            aerialPerspective.bEnabled = true;
            aerialPerspective.DistanceScale = kAerialPerspectiveDistanceScale;
            aerialPerspective.StartDistanceMeters = kAerialPerspectiveStartDistance;
            ctx.EngineRef.GetRenderWorld().SetAerialPerspective(aerialPerspective);

            CloudLayerParameters cloudLayer;
            cloudLayer.bEnabled = true;
            cloudLayer.BottomAltitudeMeters = kCloudBottomAltitude;
            cloudLayer.TopAltitudeMeters = kCloudTopAltitude;
            cloudLayer.Coverage = kCloudCoverage;
            cloudLayer.WindVelocityX = kCloudWindVelocity[0];
            cloudLayer.WindVelocityZ = kCloudWindVelocity[1];
            ctx.EngineRef.GetRenderWorld().SetCloudLayer(cloudLayer);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
            ctx.EngineRef.GetRenderWorld().SetSunShadowMaxDistance(kSunShadowMaxDistance);
        }

        /// 空・霧・空気遠近・雲の層の設定を、ほかのモードへ持ち越さないよう既定へ戻す
        void ResetEnvironment(GameModeContext &ctx)
        {
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(MakeDefaultSkyAtmosphereParameters());
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetAerialPerspective(AerialPerspectiveParameters{});
            ctx.EngineRef.GetRenderWorld().SetCloudLayer(CloudLayerParameters{});
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
            ctx.EngineRef.GetRenderWorld().SetSunShadowMaxDistance(0.0f);
        }

        /// DEM の区画 (tileX, tileZ) のクック済みメッシュの論理パス
        String MakeDemTileLogicalPath(uint32_t tileX, uint32_t tileZ)
        {
            char suffix[32] = {};
            std::snprintf(suffix, sizeof(suffix), "%u_%u.generated", static_cast<unsigned int>(tileX),
                          static_cast<unsigned int>(tileZ));
            return String(TerrainSpec::kDemCookedMeshLogicalPathPrefix) + suffix;
        }

        /// 格子の並びから DEM の全区画の論理パスを並べ、どれもクック済みがあるかを確かめる（中身はまだ読まない）。
        /// 1 つでも無いときは理由を出して false
        bool CollectTerrainTiles(const Asset::AssetSystem &assetSystem, const Terrain::TerrainGridLayout &layout,
                                 WastelandAssembly &assembly)
        {
            const uint32_t tilesX = TerrainSpec::DemTileCount(layout.Columns);
            const uint32_t tilesZ = TerrainSpec::DemTileCount(layout.Rows);
            if (tilesX == 0 || tilesZ == 0)
            {
                LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland 地形の格子の大きさ（%u × %u 頂点）から区画を作れません",
                              layout.Columns, layout.Rows);
                return false;
            }
            for (uint32_t tileZ = 0; tileZ < tilesZ; ++tileZ)
            {
                for (uint32_t tileX = 0; tileX < tilesX; ++tileX)
                {
                    String path = MakeDemTileLogicalPath(tileX, tileZ);
                    if (!assetSystem.FindCookedVariant(AnsiString(path.c_str()), Asset::AssetKind::Model).ShouldUseCooked())
                    {
                        LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s 地形のクック済みメッシュがありません。"
                                      "Scripts/CookAssets.ps1 で焼くと描けます",
                                      path.c_str());
                        return false;
                    }
                    assembly.TilePaths.push_back(std::move(path));
                }
            }
            LOG_INFO_F("WASTELAND_TERRAIN_TILES tiles_x=%u tiles_z=%u grid=%ux%u", tilesX, tilesZ, layout.Columns,
                       layout.Rows);
            return true;
        }

        /// 材質のテクスチャを 1 枚、非同期で読む。結果は主スレッドで target（assembly の中の枠）へ入れ、*pPending を減らす。
        /// pNormalTwoChannel は法線のときだけ渡し、クック済みの法線が BC5 かを入れる
        void LoadMaterialTexture(TextureResources &textures, const TSharedPtr<WastelandAssembly> &assembly,
                                 const char *path, TextureHandle *target, bool *pNormalTwoChannel, uint32_t *pPending)
        {
            TextureResources *pTextures = &textures;
            const String pathText(path);
            textures.LoadTextureAsync(
                pathText,
                [pTextures, assembly, pathText, target, pNormalTwoChannel, pPending](TextureHandle handle)
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
                    --*pPending;
                });
        }

        /// 地形の区画 1 つを読み、MegaMesh にして置く。解析したメッシュはこの関数の中で手放す（ページはページの読み込み元が読む）
        void PlaceTerrainTile(GameModeContext &ctx, WastelandAssembly &assembly, const String &logicalPath)
        {
            const Asset::AssetResolveResult resolved =
                assembly.AssetSystem->ResolveAsset(logicalPath, Asset::AssetKind::Model);
            if (!resolved.UsedCooked())
            {
                LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s 地形のクック済みメッシュを読めません",
                              logicalPath.c_str());
                return;
            }
            Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty() || parsed.Mesh.Vertices.empty())
            {
                LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s status=%u "
                              "地形のクック済みメッシュを解析できません",
                              logicalPath.c_str(), static_cast<unsigned int>(parsed.Status));
                return;
            }
            const Asset::CookedMeshData &mesh = parsed.Mesh;
            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            MegaMeshCreateInfo createInfo;
            if (!BuildMegaMeshCreateInfoFromCookedMesh(mesh, createInfo))
            {
                LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s MegaMesh の入力を作れませんでした", logicalPath.c_str());
                return;
            }
            Asset::AssetCookedRange pageRange;
            if (mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
            {
                createInfo.PageSource = MakeCookedMeshPageSource(mesh, assembly.AssetSystem->GetCookedFileReader(),
                                                                 pageRange.Request, pageRange.BaseOffset);
            }
            createInfo.Material.AlbedoTexture = assembly.Material.AlbedoTexture;
            createInfo.Material.NormalTexture = assembly.Material.NormalTexture;
            createInfo.Material.ORMTexture = assembly.Material.ORMTexture;
            createInfo.Material.bNormalTwoChannel = assembly.Material.bNormalTwoChannel;
            createInfo.Material.TerrainBlend = assembly.TerrainBlend;
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                createInfo.Material.BaseColor[channel] = kTerrainAlbedoScale;
            }
            createInfo.DebugName = logicalPath.c_str();
            const MegaMeshHandle megaMesh = megaGeometry.CreateMegaMesh(createInfo);
            if (!megaMesh.IsValid())
            {
                LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s MegaMesh を作れませんでした", logicalPath.c_str());
                return;
            }
            const ModelHandle model = megaGeometry.RegisterModel(megaMesh, logicalPath.c_str(), logicalPath);
            if (!model.IsValid())
            {
                megaGeometry.ReleaseMegaMesh(megaMesh);
                LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s モデルとして登録できませんでした", logicalPath.c_str());
                return;
            }
            // モデルはシーンを出るときにスコープが返す
            ctx.ScopeRef.TrackModel(model);

            // 区画の頂点はワールドの座標で焼いてあるので、原点に置く
            Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
            auto *component =
                object != nullptr ? ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(object) : nullptr;
            if (component == nullptr)
            {
                LOG_WARNING_F("WASTELAND_TERRAIN_SKIPPED path=%s Entity を置けませんでした", logicalPath.c_str());
                return;
            }
            object->SetPosition(0.0f, 0.0f, 0.0f);
            component->SetMegaMeshHandle(megaMesh);
            component->SetCastShadow(true);
            assembly.PlacedClusters += mesh.Clusters.size();
            assembly.PlacedVertices += mesh.Vertices.size();
            ++assembly.PlacedTiles;
        }

        /// 地形の区画を最大 kTerrainTilesPerTick 個読んで置く。全区画を処理し終えたら true
        bool PlaceNextTerrainTiles(GameModeContext &ctx, WastelandAssembly &assembly)
        {
            const uint32_t tileCount = static_cast<uint32_t>(assembly.TilePaths.size());
            const uint32_t end = std::min(tileCount, assembly.NextTile + kTerrainTilesPerTick);
            for (; assembly.NextTile < end; ++assembly.NextTile)
            {
                PlaceTerrainTile(ctx, assembly, assembly.TilePaths[assembly.NextTile]);
            }
            if (assembly.NextTile < tileCount)
            {
                return false;
            }
            LOG_INFO_F("WASTELAND_TERRAIN tiles=%u placed=%u clusters=%llu vertices=%llu", tileCount, assembly.PlacedTiles,
                       static_cast<unsigned long long>(assembly.PlacedClusters),
                       static_cast<unsigned long long>(assembly.PlacedVertices));
            return true;
        }

        /// 常駐の粗い段の三角形の、面積を掛けた法線の和から表の向きを決める。閉じた岩では和が 0 に近く、
        /// 背面の無い崖のスキャンでは表の側を向く。比が小さいときは +Z を表とする
        void MeasureFrontYaw(const Asset::CookedMeshData &mesh, Layout::PropMeshShape &shape, float &outRatio)
        {
            outRatio = 0.0f;
            const size_t begin = mesh.FallbackIndexOffset;
            const size_t end = begin + mesh.FallbackIndexCount;
            if (mesh.FallbackIndexCount < 3 || end > mesh.Indices.size())
            {
                return;
            }
            double sumX = 0.0;
            double sumZ = 0.0;
            double totalArea = 0.0;
            for (size_t i = begin; i + 2 < end; i += 3)
            {
                const uint32_t ia = mesh.Indices[i];
                const uint32_t ib = mesh.Indices[i + 1];
                const uint32_t ic = mesh.Indices[i + 2];
                if (ia >= mesh.Vertices.size() || ib >= mesh.Vertices.size() || ic >= mesh.Vertices.size())
                {
                    continue;
                }
                const Asset::CookedMeshVertex &a = mesh.Vertices[ia];
                const Asset::CookedMeshVertex &b = mesh.Vertices[ib];
                const Asset::CookedMeshVertex &c = mesh.Vertices[ic];
                const double e1x = b.Position.X - a.Position.X;
                const double e1y = b.Position.Y - a.Position.Y;
                const double e1z = b.Position.Z - a.Position.Z;
                const double e2x = c.Position.X - a.Position.X;
                const double e2y = c.Position.Y - a.Position.Y;
                const double e2z = c.Position.Z - a.Position.Z;
                const double cx = e1y * e2z - e1z * e2y;
                const double cy = e1z * e2x - e1x * e2z;
                const double cz = e1x * e2y - e1y * e2x;
                const double area = std::sqrt(cx * cx + cy * cy + cz * cz);
                // 三角形の向きは巻き方でなく頂点の法線の平均で決める（巻き方の規約によらない）
                const double nx = static_cast<double>(a.Normal.X) + b.Normal.X + c.Normal.X;
                const double ny = static_cast<double>(a.Normal.Y) + b.Normal.Y + c.Normal.Y;
                const double nz = static_cast<double>(a.Normal.Z) + b.Normal.Z + c.Normal.Z;
                const double nLength = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (!(area > 0.0) || !(nLength > 0.0))
                {
                    continue;
                }
                sumX += nx / nLength * area;
                sumZ += nz / nLength * area;
                totalArea += area;
            }
            if (!(totalArea > 0.0))
            {
                return;
            }
            outRatio = static_cast<float>(std::sqrt(sumX * sumX + sumZ * sumZ) / totalArea);
            if (outRatio >= kOpenSideRatioMin)
            {
                shape.FrontYaw = static_cast<float>(std::atan2(sumX, sumZ));
            }
        }

        /// 小物・建物の全種類のクック済みメッシュを解決・解析し、材質のテクスチャの読み込みを始める。
        /// クック済みが無い・壊れている種類は理由を出して読み飛ばす（その種類を置かずに続ける）
        void StartPropLoads(GameModeContext &ctx, const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                            const TSharedPtr<WastelandAssembly> &assembly)
        {
            auto &textures = ctx.RenderResourcesRef.Textures();
            uint32_t started = 0;
            for (uint32_t kindIndex = 0; kindIndex < Layout::kPropKindCount; ++kindIndex)
            {
                const Layout::PropKindSpec &spec = Layout::GetPropKindSpec(static_cast<Layout::PropKind>(kindIndex));
                WastelandPropLoad &load = assembly->Props[kindIndex];
                const Asset::AssetResolveResult resolved = assetSystem->ResolveAsset(spec.MeshPath, Asset::AssetKind::Model);
                if (!resolved.UsedCooked())
                {
                    LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland id=%s path=%s クック済みのメッシュがありません。"
                                  "Scripts/CookAssets.ps1 で焼くと置けます",
                                  spec.Id, spec.MeshPath);
                    continue;
                }
                bool bTexturesCooked = true;
                for (const char *path : {spec.AlbedoPath, spec.NormalPath, spec.OrmPath})
                {
                    if (!assetSystem->FindCookedVariant(AnsiString(path), Asset::AssetKind::Texture).ShouldUseCooked())
                    {
                        LOG_WARNING_F("COOKED_MODEL_TEXTURES_MISSING scene=wasteland id=%s path=%s "
                                      "クック済みのテクスチャがありません",
                                      spec.Id, path);
                        bTexturesCooked = false;
                    }
                }
                if (!bTexturesCooked)
                {
                    continue;
                }
                Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
                if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty() || parsed.Mesh.Vertices.empty())
                {
                    LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland id=%s path=%s status=%u "
                                  "クック済みのメッシュを解析できません",
                                  spec.Id, spec.MeshPath, static_cast<unsigned int>(parsed.Status));
                    continue;
                }
                Asset::AssetCookedRange pageRange;
                if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
                {
                    load.PageSource = MakeCookedMeshPageSource(parsed.Mesh, assetSystem->GetCookedFileReader(),
                                                               pageRange.Request, pageRange.BaseOffset);
                }
                load.Mesh = MakeShared<Asset::CookedMeshData>(std::move(parsed.Mesh));

                // ローカルの範囲（拡大前）と表の向き
                Layout::PropMeshShape &shape = load.Shape;
                shape.MinX = shape.MinY = shape.MinZ = std::numeric_limits<float>::max();
                shape.MaxX = shape.MaxY = shape.MaxZ = std::numeric_limits<float>::lowest();
                for (const Asset::CookedMeshVertex &vertex : load.Mesh->Vertices)
                {
                    shape.MinX = std::min(shape.MinX, vertex.Position.X);
                    shape.MaxX = std::max(shape.MaxX, vertex.Position.X);
                    shape.MinY = std::min(shape.MinY, vertex.Position.Y);
                    shape.MaxY = std::max(shape.MaxY, vertex.Position.Y);
                    shape.MinZ = std::min(shape.MinZ, vertex.Position.Z);
                    shape.MaxZ = std::max(shape.MaxZ, vertex.Position.Z);
                }
                float openRatio = 0.0f;
                if (!spec.bBuilding)
                {
                    MeasureFrontYaw(*load.Mesh, shape, openRatio);
                }
                LOG_INFO_F("WASTELAND_PROP_MESH id=%s clusters=%u pages=%zu size=%.2fx%.2fx%.2f open_ratio=%.3f "
                           "front_yaw_deg=%.1f",
                           spec.Id, static_cast<unsigned int>(load.Mesh->Clusters.size()), load.Mesh->Pages.size(),
                           static_cast<double>(shape.MaxX - shape.MinX), static_cast<double>(shape.MaxY - shape.MinY),
                           static_cast<double>(shape.MaxZ - shape.MinZ), static_cast<double>(openRatio),
                           static_cast<double>(shape.FrontYaw * 180.0f / 3.14159265f));

                load.Material.DebugName = spec.Id;
                load.PendingTextureCount = 3;
                load.bStarted = true;
                LoadMaterialTexture(textures, assembly, spec.AlbedoPath, &load.Material.AlbedoTexture, nullptr,
                                    &load.PendingTextureCount);
                LoadMaterialTexture(textures, assembly, spec.NormalPath, &load.Material.NormalTexture,
                                    &load.Material.bNormalTwoChannel, &load.PendingTextureCount);
                LoadMaterialTexture(textures, assembly, spec.OrmPath, &load.Material.ORMTexture, nullptr,
                                    &load.PendingTextureCount);
                ++started;
            }
            LOG_INFO_F("WASTELAND_PROP_LOADS kinds=%u started=%u", Layout::kPropKindCount, started);
        }

        /// 読み込みを始めた種類の材質のテクスチャがすべて届いたか
        bool ArePropTexturesLoaded(const WastelandAssembly &assembly)
        {
            for (const WastelandPropLoad &load : assembly.Props)
            {
                if (load.bStarted && load.PendingTextureCount != 0)
                {
                    return false;
                }
            }
            return true;
        }

        /// 材質がそろった種類を MegaMesh にし、高さ場から決めた配置に Entity を置く
        void PlaceProps(GameModeContext &ctx, const Terrain::TerrainHeightField &field, WastelandAssembly &assembly)
        {
            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            Layout::PropMeshShape shapes[Layout::kPropKindCount];
            for (uint32_t kindIndex = 0; kindIndex < Layout::kPropKindCount; ++kindIndex)
            {
                WastelandPropLoad &load = assembly.Props[kindIndex];
                const Layout::PropKindSpec &spec = Layout::GetPropKindSpec(static_cast<Layout::PropKind>(kindIndex));
                if (!load.bStarted)
                {
                    continue;
                }
                const MaterialCreateData &material = load.Material;
                if (!material.AlbedoTexture.IsValid() || !material.NormalTexture.IsValid() ||
                    !material.ORMTexture.IsValid())
                {
                    LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s 材質のテクスチャを読めないため置きません"
                                  "（albedo=%d normal=%d orm=%d）",
                                  spec.Id, material.AlbedoTexture.IsValid() ? 1 : 0, material.NormalTexture.IsValid() ? 1 : 0,
                                  material.ORMTexture.IsValid() ? 1 : 0);
                    load.Mesh.reset();
                    continue;
                }
                MegaMeshCreateInfo createInfo;
                if (!BuildMegaMeshCreateInfoFromCookedMesh(*load.Mesh, createInfo))
                {
                    LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s MegaMesh の入力を作れませんでした", spec.Id);
                    load.Mesh.reset();
                    continue;
                }
                createInfo.PageSource = load.PageSource;
                createInfo.Material.AlbedoTexture = material.AlbedoTexture;
                createInfo.Material.NormalTexture = material.NormalTexture;
                createInfo.Material.ORMTexture = material.ORMTexture;
                createInfo.Material.bNormalTwoChannel = material.bNormalTwoChannel;
                const Layout::PropKind kind = static_cast<Layout::PropKind>(kindIndex);
                if (kind == Layout::PropKind::Cliff01 || kind == Layout::PropKind::Cliff02)
                {
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        createInfo.Material.BaseColor[channel] = kCliffTint[channel];
                    }
                }
                createInfo.DebugName = spec.Id;
                const MegaMeshHandle megaMesh = megaGeometry.CreateMegaMesh(createInfo);
                load.Mesh.reset();
                if (!megaMesh.IsValid())
                {
                    LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s MegaMesh を作れませんでした", spec.Id);
                    continue;
                }
                const ModelHandle model = megaGeometry.RegisterModel(megaMesh, spec.Id, spec.MeshPath);
                if (!model.IsValid())
                {
                    megaGeometry.ReleaseMegaMesh(megaMesh);
                    LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s モデルとして登録できませんでした", spec.Id);
                    continue;
                }
                // 同じ種類の全 Entity が 1 つの Model を共有する。シーンを出るときにスコープが返す
                ctx.ScopeRef.TrackModel(model);
                load.MegaMesh = megaMesh;
                load.Shape.bAvailable = true;
                shapes[kindIndex] = load.Shape;
            }

            VariableArray<Layout::PropPlacement> placements;
            Layout::LayoutStats stats;
            Layout::BuildPlacements(field, shapes, placements, stats);

            uint32_t placedPerKind[Layout::kPropKindCount] = {};
            uint32_t placed = 0;
            for (const Layout::PropPlacement &placement : placements)
            {
                const uint32_t kindIndex = static_cast<uint32_t>(placement.Kind);
                Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
                auto *component =
                    object != nullptr ? ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(object) : nullptr;
                if (component == nullptr)
                {
                    continue;
                }
                object->SetPosition(placement.X, placement.Y, placement.Z);
                object->SetScale(placement.Scale, placement.Scale, placement.Scale);
                object->SetRotation(NorvesLib::Math::Quaternion(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f), placement.Yaw));
                component->SetMegaMeshHandle(assembly.Props[kindIndex].MegaMesh);
                component->SetCastShadow(true);
                ++placedPerKind[kindIndex];
                ++placed;
            }
            LOG_INFO_F("WASTELAND_PROPS placed=%u planned=%zu buildings=%u foot_rays=%u foot_hits=%u cliffs=%u "
                       "foot_boulders=%u basin_boulders=%u",
                       placed, placements.size(), stats.Buildings, stats.FootRays, stats.FootHits, stats.Cliffs,
                       stats.FootBoulders, stats.BasinBoulders);
            for (uint32_t kindIndex = 0; kindIndex < Layout::kPropKindCount; ++kindIndex)
            {
                LOG_INFO_F("WASTELAND_PROP_KIND id=%s placed=%u",
                           Layout::GetPropKindSpec(static_cast<Layout::PropKind>(kindIndex)).Id, placedPerKind[kindIndex]);
            }
            // 建物の最下点の高さと、その位置の地形の高さ（最下点が地面より下 = 埋まっている）を確かめられるよう出す
            for (const Layout::PropPlacement &placement : placements)
            {
                if (Layout::GetPropKindSpec(placement.Kind).bBuilding)
                {
                    const float bottom = placement.Y + shapes[static_cast<uint32_t>(placement.Kind)].MinY * placement.Scale;
                    LOG_INFO_F("WASTELAND_BUILDING id=%s x=%.1f z=%.1f bottom=%.2f ground=%.2f yaw_deg=%.1f",
                               Layout::GetPropKindSpec(placement.Kind).Id, static_cast<double>(placement.X),
                               static_cast<double>(placement.Z), static_cast<double>(bottom),
                               static_cast<double>(field.SampleHeight(placement.X, placement.Z)),
                               static_cast<double>(placement.Yaw * 180.0f / 3.14159265f));
                }
            }
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
        // 地形の高さ場（DEM の格子）を 1 回だけ読み、シーンの間だけ持つ。クッカーが区画を焼いたのと同じファイル・同じ標本の規則
        AnsiString gridError;
        data.m_pHeightField = Terrain::TerrainHeightField::LoadGrid(TerrainSpec::kDemGridDescriptorPath, gridError);
        if (!data.m_pHeightField)
        {
            LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s 地形の格子を読めません（%s）。"
                          "Scripts/Terrain/CropDem.py --build で作ると描けます",
                          TerrainSpec::kDemGridDescriptorPath, gridError.c_str());
        }
        // 視点の高さは地形からの高さ。格子が無い・範囲の外なら地面を 0 とする
        float groundHeight = 0.0f;
        if (data.m_pHeightField && data.m_pHeightField->Contains(pose.Position[0], pose.Position[2]))
        {
            groundHeight = data.m_pHeightField->SampleHeight(pose.Position[0], pose.Position[2]);
        }
        else if (data.m_pHeightField)
        {
            LOG_WARNING_F("WASTELAND_VIEW_OUT_OF_RANGE view=%s x=%.1f z=%.1f 視点が地形の格子の外です",
                          data.m_ViewName.empty() ? "default" : data.m_ViewName.c_str(),
                          static_cast<double>(pose.Position[0]), static_cast<double>(pose.Position[2]));
        }
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
        else if (data.m_pHeightField)
        {
            auto assembly = MakeShared<WastelandAssembly>();
            assembly->AssetSystem = assetSystem;
            if (CollectTerrainTiles(*assetSystem, data.m_pHeightField->GetGridLayout(), *assembly))
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
                blend.MacroWavelengths[0] = kMacroWavelengths[0];
                blend.MacroWavelengths[1] = kMacroWavelengths[1];
                blend.MacroBrightness = kMacroBrightness;
                blend.MacroHue = kMacroHue;
                blend.DetailStartDistance = kDetailStartDistance;
                blend.DetailDistanceRatio = kDetailDistanceRatio;
                blend.DetailMaxLevel = kDetailMaxLevel;
                blend.TextureTileMeters = TerrainSpec::kTextureTileMeters;
                assembly->PendingTextureCount = kTerrainTextureCount;
                auto &textures = ctx.RenderResourcesRef.Textures();
                MaterialCreateData &sand = assembly->Material;
                uint32_t *pPending = &assembly->PendingTextureCount;
                LoadMaterialTexture(textures, assembly, kSandAlbedoPath, &sand.AlbedoTexture, nullptr, pPending);
                LoadMaterialTexture(textures, assembly, kSandNormalPath, &sand.NormalTexture, &sand.bNormalTwoChannel,
                                    pPending);
                LoadMaterialTexture(textures, assembly, kSandOrmPath, &sand.ORMTexture, nullptr, pPending);
                LoadMaterialTexture(textures, assembly, kDirtAlbedoPath, &blend.Dirt.AlbedoTexture, nullptr, pPending);
                LoadMaterialTexture(textures, assembly, kDirtNormalPath, &blend.Dirt.NormalTexture,
                                    &blend.Dirt.bNormalTwoChannel, pPending);
                LoadMaterialTexture(textures, assembly, kDirtOrmPath, &blend.Dirt.ORMTexture, nullptr, pPending);
                LoadMaterialTexture(textures, assembly, kRockAlbedoPath, &blend.Rock.AlbedoTexture, nullptr, pPending);
                LoadMaterialTexture(textures, assembly, kRockNormalPath, &blend.Rock.NormalTexture,
                                    &blend.Rock.bNormalTwoChannel, pPending);
                LoadMaterialTexture(textures, assembly, kRockOrmPath, &blend.Rock.ORMTexture, nullptr, pPending);
                // 岩・崖・建物。地形の上に据えるので、地形を置けるときだけ読む
                if (kPlaceLayoutProps)
                {
                    StartPropLoads(ctx, assetSystem, assembly);
                }
                else
                {
                    assembly->bPropsPlaced = true;
                }
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
            data.m_pHeightField.reset();
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
        // 日中のトーンマップとグレーディング（このシーンのカメラだけ。ほかのシーンのカメラは各自のひな形を使う）
        CameraProxy &proxyTemplate = data.m_Rig.ProxyTemplate();
        proxyTemplate.ToneMapCurve = CameraToneMapCurve::NeutralLinear;
        proxyTemplate.GradingOverride.bEnabled = true;
        proxyTemplate.GradingOverride.Contrast = kGradingContrast;
        proxyTemplate.GradingOverride.ContrastPivot = kGradingContrastPivot;
        proxyTemplate.GradingOverride.Saturation = kGradingSaturation;
        proxyTemplate.GradingOverride.Temperature = kGradingTemperature;
        proxyTemplate.GradingOverride.VignetteIntensity = kVignetteIntensity;
        proxyTemplate.GradingOverride.VignetteRadius = kVignetteRadius;
        proxyTemplate.GradingOverride.VignetteSoftness = kVignetteSoftness;
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
            else
            {
                WastelandAssembly &assembly = *data.m_pAssembly;
                if (assembly.PendingTextureCount == 0 && !assembly.bPlaced)
                {
                    assembly.bPlaced = PlaceNextTerrainTiles(ctx, assembly);
                }
                if (assembly.bPlaced && ArePropTexturesLoaded(assembly) && !assembly.bPropsPlaced)
                {
                    PlaceProps(ctx, *data.m_pHeightField, assembly);
                    assembly.bPropsPlaced = true;
                }
                if (assembly.bPlaced && assembly.bPropsPlaced)
                {
                    data.m_TerrainSettledTick = data.m_TickCount;
                }
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
        data.m_pHeightField.reset();
        data.m_Rig.Leave(ctx);
        // 地形の Entity・モデル・カメラの実体は GameModeScope::Cleanup が返す。空と霧は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("wasteland を終了します");
    }

} // namespace Game::GameModes
