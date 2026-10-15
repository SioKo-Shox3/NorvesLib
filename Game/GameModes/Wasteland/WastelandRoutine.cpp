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
        // （写真の時刻の記録 7:26 はこの影の短さと合わないので使わない。Docs/RenderingValidation/WastelandLight.md）。
        // 方位角は +X（東）から +Z（南）へ測るので、北から 244° は 154°
        constexpr float kSunAltitudeDegrees = 58.5f;
        constexpr float kSunAzimuthDegrees = 154.0f;
        /// 手本の写真の構図に寄せた視点（SceneCatalog.h の ref_*）で替える画角と太陽
        struct WastelandReferenceShot
        {
            /// 視点の名前（SceneCatalog.h の kWastelandViews）
            const char *ViewName;
            /// 縦の画角[度]。16:9 の画面の横の画角が、写真の横の画角（焦点距離と撮像素子の大きさからの推定）と同じになる値
            float VerticalFieldOfViewDegrees;
            /// 写真の日時と影から推定した太陽の仰角[度]と方位角[度]（方位角は上と同じく +X から +Z へ測る）
            float SunAltitudeDegrees;
            float SunAzimuthDegrees;
        };
        // 写真ごとの推定（根拠は Docs/RenderingValidation/WastelandReference.md）。どれも写真と同じ条件ではない
        constexpr WastelandReferenceShot kReferenceShots[] = {
            // far_01: APS-C の 55 mm（横 23°）。遠景の稜線と写真の輪郭の照合で横 22.5°。太陽は基準の光と同じ
            // （5 月 18 日の南中の 2 時間後）
            {"ref_far_01", 12.77f, kSunAltitudeDegrees, kSunAzimuthDegrees},
            // far_02: 1/2.5 型の 6.1 mm（横 50°）。5 月 11 日 16 時ごろ（記録の 22:12 を UTC とみる）、西南西の 45°
            {"ref_far_02", 29.5f, 45.0f, 160.0f},
            // cliff_01: 焦点距離の記録が無く、DEM の稜線と写真の輪郭の照合から横 32°。9 月 23 日の日没の 1 時間前、西の 10°
            {"ref_cliff_01", 18.3f, 10.0f, 172.0f},
            // cliff_02: パノラマの切り出しで、ビュートの見かけの高さから横 11°。11 月 4 日 15:19、南西の 24°
            {"ref_cliff_02", 6.1f, 24.0f, 132.0f},
            // ground_01: 35 mm 換算 26 mm（横 69°）。3 月 17 日 13:27、南の 51°（写真は曇天で、雲は替えない）
            {"ref_ground_01", 42.3f, 51.0f, 90.0f},
            // ground_02: 35 mm 換算 110 mm（横 19°）。10 月 13 日 14:52、南南西の 38°
            {"ref_ground_02", 10.5f, 38.0f, 125.0f},
        };

        /// 視点の名前に対応する手本の構図（無ければ nullptr）
        const WastelandReferenceShot *FindReferenceShot(const String &viewName)
        {
            for (const WastelandReferenceShot &shot : kReferenceShots)
            {
                if (viewName == shot.ViewName)
                {
                    return &shot;
                }
            }
            return nullptr;
        }
        // 晴天の日中の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）。手本の写真の記録（f/8・1/200 秒・ISO 100）と
        // 同じ EV 13.6 にする。晴天の f/16 の目安（EV 15）より 1.4 段明るく、写真と同じく空が白みを帯びて明るく写る
        constexpr float kExposureEV100 = 13.6f;
        // カメラの JPEG の現像の明るさ[EV]。エンジンの露出は飽和に基づく式（2^-EV100 / 1.2 で白に届く）で、中間の灰（白の 12.8 %）は
        // そのまま表示の 0.128 になる。カメラの JPEG は標準出力感度（ISO 12232）で同じ露出の中間の灰を sRGB 118（表示のリニアで 0.18）に
        // 置くので、同じ記録の露出でも中間調が 0.18 / 0.128 = 1.41 倍（+0.49 段）明るく写る。露出の値は記録のまま、この現像の差だけを補う
        constexpr float kCameraOutputCompensationEV = 0.49f;
        // 日中のグレーディング。トーンマップは暗部を縮めない NeutralLinear にし、空の光だけの影の中が黒くつぶれないようにする。
        // コントラストはカメラの JPEG の階調に寄せて、中間の灰（0.18）を軸にした S 字で立てる（空と雲は明るく、影と崖の暗部は
        // 締まる）。彩度を少し上げて空の青を残し、色温度はわずかに足すだけにする（写真の日向は白みの強い昼の色）。
        // ビネットは画面の隅を少し落とす程度
        constexpr float kGradingContrast = 1.35f;
        constexpr float kGradingContrastPivot = 0.18f;
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
        // 地形の材質（3 層の混ぜ合わせ）に掛ける明るさ。3 層のアルベドの画像は、線形の平均を実在の値へ較正してある
        // （Scripts/Terrain/TerrainMaterials.py: 細粒の赤い土 0.16・岩屑と礫 0.17・赤褐色の砂岩 0.22）ので掛けない
        constexpr float kTerrainAlbedoScale = 1.0f;
        // 高さフォグ: 基準の高さでの密度[1/m]と、上へ向かって薄くなる割合[1/m]。遠景の霞は空気遠近が受け持つので、
        // 谷床に薄く溜まる砂ぼこりの層にする。谷床は北から南東へ下がり、平らな所の高さは -38〜+39 m（5〜95%）なので、
        // 基準を南東の低い谷床（-40 m）に置き、100 m で 1/e に薄くして北の高い谷床（+40 m）にも半分ほど残す。
        // ビュートの頂（+100〜+200 m）より上ではほぼ消える。手本の写真は乾いた澄んだ空気で、3〜5 km 先の崖も赤く濃く写るので、
        // 谷床の高さで 3 km の光学的深さ 0.06（透過 94 %）の薄い層にとどめる（0.00015 では 0.3 で、空気遠近と合わせて崖の
        // 半分が空の色に置き換わり白く霞んだ）
        constexpr float kFogDensity = 0.00003f;
        constexpr float kFogBaseHeight = -40.0f;
        constexpr float kFogFalloff = 0.01f;
        // 空気遠近: 物理空の媒質の距離をそのまま数える（エアロゾルは下の倍率で地表の澄んだ空気の濃さにする）。谷床の向こうの
        // ビュート（1〜3.5 km）が写真の遠い崖と同じく赤みを残して少し霞み、形は読めるようにする。300 m より手前は掛けない
        // （足元・近くの崖の色は変えない）
        constexpr float kAerialPerspectiveDistanceScale = 1.0f;
        constexpr float kAerialPerspectiveStartDistance = 300.0f;
        // ワールドの y = 0 の標高[m]。DEM の高さの基準（Assets/Terrain/ValleyOfTheGods/height.json の height_offset_m。
        // 前哨地の床の標高 NAVD88）。空気遠近の媒質の密度をこの高度で数える。海面で数えると地表のエアロゾルが 2.5 倍
        // （尺度高さ 1.6 km）になり、3 km 先の崖の透過が 0.84 でなく 0.69 になる。高原の晴天のエアロゾルの光学的深さ
        // （0.05〜0.1）は、この高度から上の柱の量（0.07）に合う
        constexpr float kSiteAltitudeMeters = 1452.05f;
        // 空気遠近で数えるエアロゾルの密度の倍率。空の模型（海面で Mie 1.11e-4 /m・尺度高さ 1.6 km）はこの標高で 4.5e-5 /m
        // （Rayleigh と合わせた視程 約 70 km）になり、3〜5 km 先の崖の赤みが空の色の霞で白く薄まった。コロラド高原の澄んだ空気は
        // 標準視程 約 180 km（IMPROVE の国立公園の観測）で、Rayleigh（約 1.1e-5 /m）を除くエアロゾルは約 1e-5 /m なので、
        // 1e-5 ÷ 4.5e-5 ≒ 0.22 倍にする。空（放射輝度 LUT）の柱の量は変えない
        constexpr float kAerialPerspectiveMieDensityScale = 0.22f;
        // 空のエアロゾル（Mie）の高さの尺度[m]。既定の 1.2 km の 1.33 倍にして柱の量を少し増やし、写真の白みの強い地平線の空に
        // 寄せる。2.4 km では青空の明るさは 1 割も変わらず、崖の視点の遠景の帯と空の比が写真の 0.46 から 0.34 へ離れた
        constexpr float kSkyMieScaleHeight = 1600.0f;
        // 太陽の円盤の輝度[cd/m^2]。空の模型はこれを大気の上端の値として透過率を掛けるので、大気圏外の太陽照度（約 134 klx）を
        // 円盤の立体角（6.87e-5 sr）で割った 1.95e9 にする。既定の 1.6e9（約 110 klx）は地表で測った値に近く、透過率を二重に
        // 掛けて空・雲・日向がそろって 2 割ほど暗くなる
        constexpr float kSunLuminanceNits = 1.95e9f;
        // 雲の層: 写真の晴天の積雲（底の平らな雲が空の半分ほど）。乾いた高原で雲底が高いので高度 2.5〜3.6 km の層にし、
        // 西寄りの風でゆっくり流す。形の大きさを既定（9 km）の半分にして、青空の隙間を挟んだ塊に分ける（既定の大きさでは薄く
        // 広がった層が空全体に掛かり、灰色の曇り空に見える）。消散係数は既定（0.01 /m）の 6 割で、塊の縁が白く締まり、底も
        // 灰色に沈まない。被覆は 0.4 で空の 2 割に満たず、0.5 で一面の曇りにつながるので、写真の半分ほどに合う 0.43 にする
        constexpr float kCloudBottomAltitude = 2500.0f;
        constexpr float kCloudTopAltitude = 3600.0f;
        constexpr float kCloudCoverage = 0.43f;
        constexpr float kCloudExtinctionPerMeter = 0.006f;
        constexpr float kCloudShapeScale = 4500.0f;
        constexpr float kCloudWindVelocity[2] = {8.0f, 3.0f};
        // カメラの遠方面[m]。遠景は 1/3 秒角の DEM 2 枚（西経 109〜110°・北緯 36〜38°）まで覆い、どの視点からも地形の最も遠い点は
        // 約 160 km（Scripts/Terrain/HorizonSkyline.py の TRACE_M）。体積フォグ・雲・RT の影などのパスは深度 0.999999 以上を空と
        // みなすので、地形はすべてその手前に入れる。深度は 1 - 近方面 × (1/距離 - 1/遠方面) なので、近方面 0.5 m・遠方面 400 km では
        // 空とみなされるのは 222 km より奥（遠方面 100 km では 83 km より奥の山が霞も影も掛からずに地平線に浮いた）
        constexpr float kFarPlane = 400000.0f;
        // カメラの近方面[m]。深度は前から奥へ 0〜1 の標準の向きで、奥の分解能は距離の 2 乗 ÷ 近方面に比例する。既定の 0.1 m では
        // 30 km 先で約 0.5 km・100 km 先で約 6 km になり遠くの稜線の前後が入れ替わるので、5 倍の 0.5 m にする（目の高さ 1.7 m の
        // 足元までは描ける）
        constexpr float kNearPlane = 0.5f;
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
        // 地形の材質は 3 層の混ぜ合わせ。素材は Poly Haven の CC0 の写真由来のテクスチャ（Scripts/Terrain/TerrainMaterials.py が
        // 落としてアルベドを較正し、Assets/AssetSets/WastelandTerrain.json が焼く）。
        // 1 層目（砂の枠）は谷床の赤茶の土 red_mud_stones（1 枚 1.5 m。締まった赤い土にひび割れと小石が散る）。
        // 土の皮膜は、この層に数 m の斑で、土の枠の右半分の皮膜の層を高さで混ぜて描く（TerrainLayerBlend.glsl の TERRAIN_CRUST_*）
        constexpr const char *kSandAlbedoPath = "Assets/Textures/PolyHaven/red_mud_stones/red_mud_stones_diff_cal_4k.png";
        constexpr const char *kSandNormalPath = "Assets/Textures/PolyHaven/red_mud_stones/red_mud_stones_nor_dx_4k.jpg";
        constexpr const char *kSandOrmPath = "Assets/Textures/PolyHaven/red_mud_stones/red_mud_stones_orm_4k";
        // 2 層目（土の枠）は横 2 枚の並び（8192×4096。Scripts/Terrain/TerrainMaterials.py --build が書く）。左は崖錐と礫の地表の
        // 礫の舗石 red_laterite_soil_stones（1 枚 2 m。礫は数 mm〜数 cm）で、谷床では土と数 m〜数十 m の斑で入れ替わる
        // （TerrainLayerBlend.glsl の ComputeTerrainSurfaceMaskWeights）。右は土の皮膜 cracked_red_ground（1 枚 2 m。こぶ状に
        // 盛り上がった暗い褐色の皮膜とその割れ目）
        constexpr const char *kDirtAlbedoPath = "Assets/Textures/PolyHaven/wasteland_dirt_crust/wasteland_dirt_crust_diff_cal_8k.png";
        constexpr const char *kDirtNormalPath = "Assets/Textures/PolyHaven/wasteland_dirt_crust/wasteland_dirt_crust_nor_dx_8k.png";
        constexpr const char *kDirtOrmPath = "Assets/Textures/PolyHaven/wasteland_dirt_crust/wasteland_dirt_crust_orm_8k";
        // 3 層目（岩肌の枠）は露岩の赤褐色の砂岩 cliff_side（1 枚 1.8 m。層理と割れ目の間隔は 10〜40 cm）
        constexpr const char *kRockAlbedoPath = "Assets/Textures/PolyHaven/cliff_side/cliff_side_diff_cal_4k.png";
        constexpr const char *kRockNormalPath = "Assets/Textures/PolyHaven/cliff_side/cliff_side_nor_dx_4k.jpg";
        constexpr const char *kRockOrmPath = "Assets/Textures/PolyHaven/cliff_side/cliff_side_orm_4k";
        // 模様を実寸で張る: 1 層目の 1 枚の広さ（m）と、2・3 層目の UV の倍率（1 層目の広さ ÷ その層の 1 枚の広さ）。
        // 岩肌は 3 方向の投影なので、崖の面でも層理と割れ目が実寸の間隔になる
        constexpr float kSandTileMeters = 1.5f;
        constexpr float kDirtUVScale = kSandTileMeters / 2.0f;
        constexpr float kRockUVScale = kSandTileMeters / 1.8f;
        // 明るさ・色味の揺らぎ（数十〜数百 m と、その 1/4 の 20 m）。層ごとに別の位相で掛けるので、揺らぎは地表の分類の境で
        // 入れ替わり、全域を 1 つの斑の模様で覆わない（TerrainLayerBlend.glsl の TerrainMacroVariation）
        constexpr float kMacroWavelengths[2] = {80.0f, 300.0f};
        constexpr float kMacroBrightness = 0.2f;
        constexpr float kMacroHue = 0.05f;
        // 距離で模様を大きくする段は使わない（最大 0 段）。どの距離でも礫の粒と砂岩の割れ目を実寸で張り、遠くはミップで平均の色にする。
        // 模様の繰り返しは層ごとの色の揺らぎ（20 m・80 m・300 m）で崩す
        constexpr float kDetailStartDistance = 10.0f;
        constexpr float kDetailDistanceRatio = 3.0f;
        constexpr float kDetailMaxLevel = 0.0f;
        // 混ぜ方: 砂丘の風下の面（最も急な所で 20° 台）は砂のまま、メサの裾の崖錐は乾いた土、崖と外周の急な稜線は岩肌にする
        constexpr float kDirtSlopeDegrees[2] = {24.0f, 33.0f};
        constexpr float kRockSlopeDegrees[2] = {37.0f, 48.0f};
        // 盆地より高い所（崖錐・稜線の麓）ほど土へ寄せる
        constexpr float kDirtHeightRange[2] = {12.0f, 50.0f};
        constexpr float kDirtHeightBiasDegrees = 10.0f;
        constexpr float kBlendDepth = 0.3f;
        // 1 層目・2 層目・3 層目の 3 枚ずつ
        constexpr uint32_t kTerrainTextureCount = 9;
        // 地表の分類の地図（Scripts/Terrain/TerrainMasks.py）の写し方。DEM の格子の範囲は地図の重みで、外（遠景）は上の傾斜の式で混ぜる。
        // 礫の地表は平均で半分を礫の舗石（土の枠）・半分を赤茶の土（砂の枠）にし、2 つは数 m〜数十 m の斑で入れ替わる
        // （TerrainLayerBlend.glsl の TERRAIN_PAVEMENT_*）。崖錐は全部が礫の舗石
        constexpr float kSurfaceMaskGravelToDirt = 0.5f;
        // 地図の外縁の内側 32 m で傾斜の式から地図へ移す（近景と遠景の継ぎ目に分類の段を作らない）
        constexpr float kSurfaceMaskEdgeFadeMeters = 32.0f;
        // 地図（2 m/画素）を引く位置を 2 m ずらし、画素の格子に沿った直線の境を崩す
        constexpr float kSurfaceMaskWarpMeters = 2.0f;
        // 3 層の法線マップ（red_mud_stones・red_laterite_soil_stones・cliff_side の nor_dx_4k）の接空間の傾きの 2 乗の平均。遠くで模様の 1 枚が
        // 縮んで細かい法線を落とした分を、粗さの α² に足して鏡面の広がりへ移す（Python で画像の 1/4 の画素から測った値）
        constexpr float kDetailSlopeVariance[3] = {0.393f, 0.623f, 0.283f};

        /// 物理空・太陽・高さフォグ・空気遠近・雲の層
        void ApplyEnvironment(GameModeContext &ctx, float sunAltitudeDegrees, float sunAzimuthDegrees)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = sunAltitudeDegrees;
            sky.SunAzimuthDegrees = sunAzimuthDegrees;
            sky.MieScaleHeightMeters = kSkyMieScaleHeight;
            sky.SunLuminanceNits = kSunLuminanceNits;
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
            aerialPerspective.OriginAltitudeMeters = kSiteAltitudeMeters;
            aerialPerspective.MieDensityScale = kAerialPerspectiveMieDensityScale;
            ctx.EngineRef.GetRenderWorld().SetAerialPerspective(aerialPerspective);

            CloudLayerParameters cloudLayer;
            cloudLayer.bEnabled = true;
            cloudLayer.BottomAltitudeMeters = kCloudBottomAltitude;
            cloudLayer.TopAltitudeMeters = kCloudTopAltitude;
            cloudLayer.Coverage = kCloudCoverage;
            cloudLayer.ExtinctionPerMeter = kCloudExtinctionPerMeter;
            cloudLayer.ShapeScaleMeters = kCloudShapeScale;
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

        /// 視点の高さ（地形からの高さ）に、その (x,z) の地形の高さを足す。格子が無い・範囲の外なら地面を 0 とする
        void LiftPoseToTerrain(const Terrain::TerrainHeightField *pHeightField, const String &viewName,
                               Game::Scenes::FreeCameraPose &pose)
        {
            const char *viewLabel = viewName.empty() ? "default" : viewName.c_str();
            float groundHeight = 0.0f;
            if (pHeightField && pHeightField->Contains(pose.Position[0], pose.Position[2]))
            {
                groundHeight = pHeightField->SampleHeight(pose.Position[0], pose.Position[2]);
            }
            else if (pHeightField)
            {
                LOG_WARNING_F("WASTELAND_VIEW_OUT_OF_RANGE view=%s x=%.1f z=%.1f 視点が地形の格子の外です", viewLabel,
                              static_cast<double>(pose.Position[0]), static_cast<double>(pose.Position[2]));
            }
            pose.Position[1] += groundHeight;
            LOG_INFO_F("WASTELAND_VIEW view=%s x=%.1f z=%.1f ground=%.2f eye=%.2f", viewLabel,
                       static_cast<double>(pose.Position[0]), static_cast<double>(pose.Position[2]),
                       static_cast<double>(groundHeight), static_cast<double>(pose.Position[1]));
        }

        /// 撮影の視点の切り替えの依頼（Scenes/SceneViewSwitch.h）があれば、カメラ・画角・太陽をその視点へ置き直す。
        /// 地形・材質は読み直さない（Enter と同じ視点の解釈で、置き直すものだけを替える）
        void ApplyRequestedView(GameModeContext &ctx, WastelandData &data)
        {
            Game::Scenes::SceneViewSwitch *pViewSwitch = data.m_pViewSwitch;
            if (pViewSwitch == nullptr || !pViewSwitch->IsRequested())
            {
                return;
            }
            const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kWastelandSceneId);
            const Game::Scenes::SceneViewDescriptor *view =
                scene != nullptr ? Game::Scenes::FindSceneView(*scene, pViewSwitch->GetViewName().c_str()) : nullptr;
            Component::CameraComponent *camera = data.m_Rig.GetCamera();
            if (view == nullptr || view->Kind != Game::Scenes::SceneViewKind::FreePose || camera == nullptr)
            {
                LOG_ERROR_F("SCENE_VIEW_UNKNOWN scene=wasteland view=\"%s\" 視点を置き直せません",
                            pViewSwitch->GetViewName().c_str());
                pViewSwitch->Complete(false);
                return;
            }
            data.m_ViewName = pViewSwitch->GetViewName();
            Game::Scenes::FreeCameraPose pose{};
            pose.Position[0] = view->Position[0];
            pose.Position[1] = view->Position[1];
            pose.Position[2] = view->Position[2];
            pose.Yaw = view->Yaw;
            pose.Pitch = view->Pitch;
            LiftPoseToTerrain(data.m_pHeightField.get(), data.m_ViewName, pose);
            data.m_Rig.PlaceAt(pose);

            // 手本の写真の構図の視点では、画角と太陽を写真の推定に替え、それ以外は基準の値へ戻す
            const WastelandReferenceShot *referenceShot = FindReferenceShot(data.m_ViewName);
            ApplyEnvironment(ctx, referenceShot ? referenceShot->SunAltitudeDegrees : kSunAltitudeDegrees,
                             referenceShot ? referenceShot->SunAzimuthDegrees : kSunAzimuthDegrees);
            camera->SetFieldOfView(referenceShot ? referenceShot->VerticalFieldOfViewDegrees : data.m_DefaultFieldOfView);
            LOG_INFO_F("SCENE_VIEW_SWITCHED scene=wasteland view=%s", data.m_ViewName.c_str());
            pViewSwitch->Complete(true);
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

            // 遠景（DEM の格子の外側を、近景の外周と縫い合わせた粗い段で覆う）。記述やクック済みが無ければ近景だけで描く
            VariableArray<Terrain::TerrainHorizonMesh> horizonMeshes;
            AnsiString horizonError;
            if (!Terrain::LoadTerrainHorizonMeshes(TerrainSpec::kHorizonDescriptorPath, horizonMeshes, horizonError))
            {
                LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s 遠景の地形の記述を読めません（%s）。"
                              "Scripts/Terrain/HorizonDem.py --build で作り、Scripts/CookAssets.ps1 で焼くと描けます",
                              TerrainSpec::kHorizonDescriptorPath, horizonError.c_str());
                return true;
            }
            const size_t nearTileCount = assembly.TilePaths.size();
            for (const Terrain::TerrainHorizonMesh &mesh : horizonMeshes)
            {
                String path = String(TerrainSpec::kHorizonCookedMeshLogicalPathPrefix) + String(mesh.Name.c_str()) +
                              ".generated";
                if (!assetSystem.FindCookedVariant(AnsiString(path.c_str()), Asset::AssetKind::Model).ShouldUseCooked())
                {
                    LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s 遠景のクック済みメッシュがありません。"
                                  "Scripts/CookAssets.ps1 で焼くと描けます（遠景は置きません）",
                                  path.c_str());
                    assembly.TilePaths.resize(nearTileCount);
                    return true;
                }
                assembly.TilePaths.push_back(std::move(path));
            }
            LOG_INFO_F("WASTELAND_HORIZON_MESHES count=%zu", horizonMeshes.size());
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
        LiftPoseToTerrain(data.m_pHeightField.get(), data.m_ViewName, pose);

        // 手本の写真の構図の視点では、太陽を写真の時刻の推定に替える
        const WastelandReferenceShot *referenceShot = FindReferenceShot(data.m_ViewName);
        ApplyEnvironment(ctx, referenceShot ? referenceShot->SunAltitudeDegrees : kSunAltitudeDegrees,
                         referenceShot ? referenceShot->SunAzimuthDegrees : kSunAzimuthDegrees);

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
                blend.TextureTileMeters = kSandTileMeters;
                blend.DetailSlopeVariance[0] = kDetailSlopeVariance[0];
                blend.DetailSlopeVariance[1] = kDetailSlopeVariance[1];
                blend.DetailSlopeVariance[2] = kDetailSlopeVariance[2];
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
                // 地表の分類の地図。記述が無ければ傾斜の式だけで混ぜる
                Terrain::TerrainSurfaceMaskLayout maskLayout;
                AnsiString maskError;
                if (Terrain::LoadTerrainSurfaceMaskLayout(TerrainSpec::kSurfaceMaskDescriptorPath, maskLayout, maskError))
                {
                    blend.SurfaceMaskMinXZ[0] = maskLayout.MinX;
                    blend.SurfaceMaskMinXZ[1] = maskLayout.MinZ;
                    blend.SurfaceMaskMaxXZ[0] = maskLayout.MaxX;
                    blend.SurfaceMaskMaxXZ[1] = maskLayout.MaxZ;
                    blend.SurfaceMaskGravelToDirt = kSurfaceMaskGravelToDirt;
                    blend.SurfaceMaskEdgeFadeMeters = kSurfaceMaskEdgeFadeMeters;
                    blend.SurfaceMaskWarpMeters = kSurfaceMaskWarpMeters;
                    ++assembly->PendingTextureCount;
                    LoadMaterialTexture(textures, assembly, TerrainSpec::kSurfaceMaskTexturePath,
                                        &blend.SurfaceMaskTexture, nullptr, pPending);
                    LOG_INFO_F("WASTELAND_SURFACE_MASK x=[%.1f,%.1f] z=[%.1f,%.1f] size=%ux%u", maskLayout.MinX,
                               maskLayout.MaxX, maskLayout.MinZ, maskLayout.MaxZ, maskLayout.Width, maskLayout.Height);
                }
                else
                {
                    LOG_WARNING_F("COOKED_MODEL_TEXTURES_MISSING scene=wasteland path=%s 地表の分類の地図の記述を読めません（%s）。"
                                  "Scripts/Terrain/TerrainMasks.py --build で作り、Scripts/CookAssets.ps1 で焼くと地図で混ぜます",
                                  TerrainSpec::kSurfaceMaskDescriptorPath, maskError.c_str());
                }
                // 遠景の地図（分類の重みと法線を 16 m の画素へ前もって縮小した地図）。記述が無ければ遠くも画素ごとの式で描く
                Terrain::TerrainSurfaceMaskLayout farLayout;
                AnsiString farError;
                if (Terrain::LoadTerrainFarMapLayout(TerrainSpec::kFarMapDescriptorPath, farLayout, farError))
                {
                    blend.FarMapMinXZ[0] = farLayout.MinX;
                    blend.FarMapMinXZ[1] = farLayout.MinZ;
                    blend.FarMapMaxXZ[0] = farLayout.MaxX;
                    blend.FarMapMaxXZ[1] = farLayout.MaxZ;
                    blend.FarMapTexelMeters = (farLayout.MaxX - farLayout.MinX) / static_cast<float>(farLayout.Width);
                    ++assembly->PendingTextureCount;
                    LoadMaterialTexture(textures, assembly, TerrainSpec::kFarMapTexturePath, &blend.FarMapTexture, nullptr,
                                        pPending);
                    LOG_INFO_F("WASTELAND_FAR_MAP x=[%.1f,%.1f] z=[%.1f,%.1f] size=%ux%u texel=%.1f", farLayout.MinX,
                               farLayout.MaxX, farLayout.MinZ, farLayout.MaxZ, farLayout.Width, farLayout.Height,
                               blend.FarMapTexelMeters);
                }
                else
                {
                    LOG_WARNING_F("COOKED_MODEL_TEXTURES_MISSING scene=wasteland path=%s 遠景の地図の記述を読めません（%s）。"
                                  "Scripts/Terrain/FarMacro.py --build で作り、Scripts/CookAssets.ps1 で焼くと遠くを地図で描きます",
                                  TerrainSpec::kFarMapDescriptorPath, farError.c_str());
                }
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
        camera->SetNearPlane(kNearPlane);
        camera->SetFarPlane(kFarPlane);
        data.m_DefaultFieldOfView = camera->GetFieldOfView();
        if (referenceShot)
        {
            camera->SetFieldOfView(referenceShot->VerticalFieldOfViewDegrees);
        }
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), kExposureEV100));
        camera->SetExposureCompensation(kCameraOutputCompensationEV);
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
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        ApplyRequestedView(ctx, data);
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        // カメラの台本（--camera-script）は、地形を置いてページの読み込みの余裕を待ってから流す。読み込みの間の Tick は
        // 描画より速く回るので、そのまま流すと台本が組み立ての間に流れ切り、動いている所を撮れない
        const bool bHoldScript = data.m_Rig.IsScriptRunning() &&
                                 (data.m_TerrainSettledTick == 0 ||
                                  data.m_TickCount < data.m_TerrainSettledTick + kReadyTicksAfterTerrain);
        data.m_Rig.Tick(ctx, deltaTime, bSuspended || bHoldScript);

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
