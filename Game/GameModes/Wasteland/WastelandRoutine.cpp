#include "GameModes/Wasteland/WastelandMode.h"

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/DirectionalLightComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Ecs/EcsMassWork.h"
#include "Core/Public/Ecs/EcsWorld.h"
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
#include "GameModes/Wasteland/WastelandCliffScan.h"
#include "GameModes/Wasteland/WastelandLayout.h"
#include "GameModes/Wasteland/WastelandFoliage.h"
#include "GameModes/Wasteland/WastelandNightLights.h"
#include "GameModes/Wasteland/WastelandTalus.h"
#include "GameModes/Wasteland/WastelandTerrainExport.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        /// 区画の LOD 0 の書き出し（--wasteland-export-terrain のときだけ。Directory が空なら何もしない）
        WastelandTerrainExport::State TerrainExport;
        /// 1 層目（砂）の材質
        MaterialCreateData Material;
        /// 2 層目（乾いた土）・3 層目（岩肌）と混ぜ方
        MegaTerrainBlend TerrainBlend;
        /// 灰色の地形の材質（--wasteland-gray-material）。立っていれば区画にテクスチャと混ぜ合わせを張らず、BaseColor を
        /// GrayAlbedo・粗さを GrayRoughness・金属度を 0 にする
        bool bGrayMaterial = false;
        /// 太陽の影を地形の起伏に詰める（MegaTerrainBlend::bTightSunShadow）。荒野だけ立て、夜の集落の地形の影は変えない
        bool bTightSunShadow = false;
        float GrayAlbedo = 0.0f;
        float GrayRoughness = 1.0f;
        uint32_t PendingTextureCount = 0;
        /// 小物・建物（種類ごと。テクスチャの読み込みの結果が枠を指すので、固定の配列で持つ）
        WastelandPropLoad Props[Layout::kPropKindCount];
        /// 崖錐の岩の散布に使うメッシュの形（落石の種類の読み込みで書き、MegaMesh を作れたら bAvailable を立てる）
        WastelandTalus::TalusMeshShape TalusShapes[WastelandTalus::kTalusMeshCount];
        /// 崖の面に貼るスキャンの塊の配置表と、素材ごとの読み込み（テクスチャの読み込みの結果が枠を指すので、固定の配列で持つ）
        WastelandCliffScan::Table CliffScanTable;
        WastelandPropLoad CliffScanLoads[WastelandCliffScan::kMaxAssets];
        /// 塊の素材ごとのメッシュの頂点の標本（置いた塊のワールドの範囲と地形に埋まった割合をログに出す。MegaMesh を作ると
        /// メッシュを手放すので、読み込みのときに写す）
        VariableArray<NorvesLib::Math::Vector3> CliffScanPoints[WastelandCliffScan::kMaxAssets];
        /// 置いた建物の足元と道（草・低木を撒かない範囲。PlaceProps が書く）
        VariableArray<WastelandFoliage::ExclusionCircle> FoliageBuildingCircles;
        VariableArray<WastelandFoliage::ExclusionRect> FoliageRoadRects;
        bool bCancelled = false;
        bool bPlaced = false;
        bool bPropsPlaced = false;
        /// 崖錐の岩の行を作る間だけ散布の仕事の列の予算を上げている（作り終えたら SavedMassBudget へ戻す）
        bool bMassBudgetRaised = false;
        NorvesLib::Core::Ecs::EcsMassBudget SavedMassBudget;
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
        // 日中の表示の変換（暫定。植物・地面の後の確認で見直す）。保存した 6 視点の HDR に候補の曲線と露出の補正をオフラインで掛け、
        // 晴れの写真 3 枚の固定の領域の距離で並べて、カメラだけで選べて白飛び・黒つぶれが増えない候補の中で最も近い
        // 「View の既定の演算子（ACES Filmic、肩のある曲線）・中立のグレーディング・露出の補正 −1 EV」を採った
        // （Docs/RenderingValidation/WastelandLookCamera.md。写真との距離は現像・天気・材質の差を含む作風の参考）。
        // 露出の補正は上の現像の差（kCameraOutputCompensationEV）に足す。ACES Filmic は中間調を持ち上げる曲線なので、−1 EV で
        // 中間の灰は今の NeutralLinear より約 0.7 段暗く、明部は肩で丸まる
        constexpr float kDayLookExposureOffsetEV = -1.0f;
        // 日中のグレーディングは中立（コントラスト 1・彩度 1・色温度 0）にする。コントラスト 1.35・彩度 1.2 の S 字は、同じ HDR を写真の
        // 崖と地面に合わせて比べたとき、地面の R/G と白飛びを写真より離す側に働いた（Docs/RenderingValidation/WastelandToneMapEval.md）。
        // 色と明るさの差は照明と中身で詰め、表示側の色づけで隠さない。ビネットは画面の隅を少し落とす程度
        constexpr float kGradingContrast = 1.0f;
        constexpr float kGradingContrastPivot = 0.18f;
        constexpr float kGradingSaturation = 1.0f;
        constexpr float kGradingTemperature = 0.0f;
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
        // 空気遠近: 物理空の媒質の距離をそのまま、カメラの位置から数える（掛け始めの距離を置くと、その手前の霞を空の大気より
        // 薄く数える。空の大気の地表の Mie では 300 m の光学的深さは 0.013 で、足元・近くの崖の色はほとんど変わらない）
        constexpr float kAerialPerspectiveDistanceScale = 1.0f;
        constexpr float kAerialPerspectiveStartDistance = 0.0f;
        // ワールドの y = 0 の標高[m]。DEM の高さの基準（Assets/Terrain/ValleyOfTheGods/height.json の height_offset_m。
        // 前哨地の床の標高 NAVD88）。空気遠近と高さフォグの媒質の密度をこの高度で数える。海面で数えると地表のエアロゾルが
        // 2.5 倍（尺度高さ 1.6 km）になる。高原の晴天のエアロゾルの光学的深さ（0.05〜0.1）は、この高度から上の柱の量（0.07）に合う
        constexpr float kSiteAltitudeMeters = 1452.05f;
        // 空の模型の地表の標高[m]。谷床（ref_ground_02 の足元の y ≈ 20.5 m）の標高で、地表の太陽の透過率・照度、地表の空の照度、
        // 空の放射輝度の観測点（この 100 m 上）、高さフォグの太陽の照度をこの高度で求める。海面で求めると、尺度高さ 1.6 km の
        // エアロゾルを谷床より下の分まで通すので、太陽が 0.82 倍に暗く、空の照度は低い高度の値（谷床の約 2 倍）になる
        constexpr float kSkyGroundAltitudeMeters = kSiteAltitudeMeters + 20.5f;
        // 空のエアロゾル（Mie）の高さの尺度[m]。既定の 1.2 km の 1.33 倍にして柱の量を少し増やし、写真の白みの強い地平線の空に
        // 寄せる。2.4 km では青空の明るさは 1 割も変わらず、崖の視点の遠景の帯と空の比が写真の 0.46 から 0.34 へ離れた
        constexpr float kSkyMieScaleHeight = 1600.0f;
        // 空のエアロゾル（Mie）の密度の倍率（地表の散乱と吸収の両方。既定の係数は散乱 1.0e-4・吸収 1.11e-5 /m）。谷床の観測点で
        // 写真 ground_02 の空の帯の上下の明るさの比 0.76 に合う値（Docs/RenderingValidation/WastelandAtmosphereReference.md の
        // 「空の大気の新しい設定」。方向を保つ正解 B で 0.764、1 倍では 0.824）。霞は空の大気の Mie そのもの（kHazeFogShare）
        // なので、霞もこの倍率で薄くなる
        constexpr float kSkyMieDensityScale = 0.5f;
        // 霞は空の大気の地表付近の Mie（海面で 1.11e-4 /m・尺度高さ 1.6 km。谷床で 4.4e-5 /m）そのもので、空と別の媒質を
        // 足さない。その Mie を高さフォグと空気遠近で割り振り、和が空の大気と同じ密度・同じ尺度高さになるようにする
        // （どの高さ・向きの視線でも、霞の光学的深さが空の大気の同じ視線の Mie と合う）。この割合が高さフォグの分で、残りを
        // 空気遠近の Mie の密度の倍率にする。今は 0 にして、霞をすべて空気遠近で数える。前の高さフォグの周りの光は、太陽の散乱を
        // すでに含む視線の向きの空の輝度を使い、霧の不透明度が大きい長い視線で太陽の散乱を二重に数えた。その式で
        // 0.37（谷床の霧を前の砂ぼこりの層と同じ 1.6e-5 /m に保つ割合）では、100 km 先の山が空より明るい白い影になった
        // （手本の写真では青く、空より暗い）。0 なら山は地平線の空の青に沈む。霞の散乱光の和は、空の大気の正解 B に対して
        // 0.37 で 1.02〜1.13 倍、0 で 0.85〜1.07 倍。霧の周りの光は空の多重散乱の表 Ψ_ms で数える式に直した
        // （Docs/RenderingValidation/WastelandAtmosphereReference.md の SCN-WL-SKY-SCATTER-FOG）が、割合は見直していない
        constexpr float kHazeFogShare = 0.0f;
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
        // その影（仰角 58.5°で比高 200 m の崖から 120 m 前後）に加え、遠景（far_01 の 2〜8 km）の起伏の自己影まで落とす。
        // レイトレーシングのシーンの範囲（10 km）と揃える。クリップマップは段 0 の幅を広げてこの距離を覆う
        constexpr float kSunShadowMaxDistance = 10000.0f;
        // 地形を置いてから SceneReady を出すまでの Tick の数（ページの読み込みが追いつく余裕）
        constexpr uint32_t kReadyTicksAfterTerrain = 90;
        // 1 Tick に読んで置く地形の区画の数（区画は 1 つ 37 MB 前後。読み込みと解析のメモリを数区画ぶんに抑える）
        constexpr uint32_t kTerrainTilesPerTick = 4;
        // 岩・崖・建物（WastelandLayout）の手続きの地形（中央の盆地とメサの輪）に合わせた配置は使わない。DEM の地形では
        // 前哨地の建物（造成した足元に置く）と、崖錐の岩の散布（PlaceTalusRocks）に使う崖の足元の落石だけを読む（Layout::IsUsedOnDem）
        constexpr bool kProceduralLayout = false;
        // 名前付き視点の足元で草・低木を撒かない半径[m]（目の前を株がふさがない）
        constexpr float kViewFoliageClearRadius = 2.5f;
        // 名前付き視点の足元で大きな石（14〜40 cm）を置かない半径[m]（小礫と石は足元にも置く。手本の足元の写真も礫が手前まで続く）
        constexpr float kViewGravelClearRadius = 1.5f;
        // 建物の足元で草・低木を撒かない範囲の、メッシュの範囲の外への広げ[m]
        constexpr float kBuildingFoliageClearMargin = 1.5f;
        // 名前付き視点の足元で崖錐の岩を置かない半径[m]（目の前を岩がふさがない）
        constexpr float kViewTalusClearRadius = 6.0f;
        // 崖錐の岩の底の高さを引く頂点を選ぶ元の標本の数の上限（メッシュの頂点を等間隔に間引く。WastelandTalus が凸包の頂点へ絞る）
        constexpr size_t kTalusPointSamples = 2048;
        // 崖の面の塊のワールドの範囲と埋まった割合を測る頂点の標本の数の上限（メッシュの頂点を等間隔に間引く）
        constexpr size_t kCliffScanPointSamples = 4096;
        // 崖錐の岩の行（数万）を作る間だけ使う、散布の仕事の列の 1 フレームの時間[ms]。既定の 1 ms では Debug で
        // 数百フレームかかり、行が揃う前に撮影が始まる。シーンの準備ができる前の読み込みの間だけ上げ、作り終えたら戻す
        constexpr float kTalusMassMillisecondsPerFrame = 16.0f;
        // 地形の材質は 3 層の混ぜ合わせ。素材は Poly Haven の CC0 の写真由来のテクスチャ（Scripts/Terrain/TerrainMaterials.py が
        // 落としてアルベドを較正し、Assets/AssetSets/WastelandTerrain.json が焼く）。
        // 1 層目（砂の枠）は横 2 枚の並び（8192×4096。Scripts/Terrain/TerrainMaterials.py --build が書く）。左は谷床の赤茶の土
        // red_mud_stones（1 枚 1.5 m。締まった赤い土にひび割れと小石が散る）。右は地表の分類の地図の砂（沢底と吹きだまり）の
        // red_sand（1 枚 3 m の細かな赤い砂。UV の 0.5 倍で張る。TerrainLayerBlend.glsl の TERRAIN_SAND_*）。
        // 土の皮膜は、左の土に数 m の斑で、土の枠の右半分の皮膜の層を高さで混ぜて描く（TerrainLayerBlend.glsl の TERRAIN_CRUST_*）
        constexpr const char *kSandAlbedoPath = "Assets/Textures/PolyHaven/wasteland_soil_sand/wasteland_soil_sand_diff_cal_8k.png";
        constexpr const char *kSandNormalPath = "Assets/Textures/PolyHaven/wasteland_soil_sand/wasteland_soil_sand_nor_dx_8k.png";
        constexpr const char *kSandOrmPath = "Assets/Textures/PolyHaven/wasteland_soil_sand/wasteland_soil_sand_orm_8k";
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
        // （TerrainLayerBlend.glsl の TERRAIN_PAVEMENT_*）。崖錐は全部が礫の舗石。砂（沢底と吹きだまり。地図の 4 つの重みの残り）は
        // 砂の枠の右の砂
        constexpr float kSurfaceMaskGravelToDirt = 0.5f;
        // 地図の外縁の内側 32 m で傾斜の式から地図へ移す（近景と遠景の継ぎ目に分類の段を作らない）
        constexpr float kSurfaceMaskEdgeFadeMeters = 32.0f;
        // 地図（2 m/画素）を引く位置を 2 m ずらし、画素の格子に沿った直線の境を崩す
        constexpr float kSurfaceMaskWarpMeters = 2.0f;
        // 3 層の法線マップ（red_mud_stones・red_laterite_soil_stones・cliff_side の nor_dx_4k）の接空間の傾きの 2 乗の平均。遠くで模様の 1 枚が
        // 縮んで細かい法線を落とした分を、粗さの α² に足して鏡面の広がりへ移す（Python で画像の 1/4 の画素から測った値）
        constexpr float kDetailSlopeVariance[3] = {0.393f, 0.623f, 0.283f};

        // ---- 夜の集落（night-village） ----
        // 太陽は地平線の下で、空の太陽の方向光は出さない。物理空の模型は太陽の仰角を 0〜90° に限るので、夜は空を切り、
        // 夜空と空の光を静的環境の一様な放射輝度で置く。
        // 夜空の放射輝度[cd/m^2]: 満月の夜の澄んだ空（天頂付近で 18〜19 等級/平方秒角 ≒ 2〜5e-3 cd/m^2、月に近い空はその数倍）の
        // 目安から、青みのある輝度 6.8e-3 cd/m^2 にする。上半球の一様な空の水平面の照度は π × 輝度 ≒ 0.021 lx（月明かりの 1.5 割）。
        // 8.4e-3 の灰色がかった青では、撮影の空が曇りの夕暮れに見えた
        constexpr float kNightSkyRadiance[3] = {0.0042f, 0.0068f, 0.015f};
        // 月明かり: 満月に垂直な面の照度[lx]（大気の透過を含む地表の値の目安 0.2〜0.3 lx）
        constexpr float kMoonIlluminanceNormalLux = 0.25f;
        // 月の色（リニアの sRGB）。月の光そのものは太陽よりわずかに赤いが、暗い所の見え（桿体の感度が青へ寄る）に合わせて青白くする
        constexpr float kMoonColor[3] = {0.55f, 0.72f, 1.0f};
        // 月の高さと向き（方位角は太陽と同じく +X から +Z へ測る。60° は南南東）。道の北側の建物の表（南向き）を照らし、
        // 南側の建物の表は影になる
        constexpr float kMoonAltitudeDegrees = 35.0f;
        constexpr float kMoonAzimuthDegrees = 60.0f;
        // 月明かりの影の到達距離[m]（集落と周りの谷床。遠景のビュートは影を落とさず照らすだけ）
        constexpr float kMoonShadowMaxDistance = 600.0f;
        // 自動露出の補正の曲線（Rendering3DTest の --night の扱いと同じく、測光が暗いほど暗く保つ）。月明かりだけの谷床の
        // 測光は EV100 で約 -4 なので、その点で -2.8 EV（画面の平均を中間調の 2^-2.8 倍にする）、昼の 14 EV で 0 にする。
        // -1.7 EV では画面の平均が符号化値で約 45 になり、夜でなく曇りの夕暮れに見えた
        constexpr float kNightDarkSceneEV100 = -4.0f;
        constexpr float kNightDarkSceneCompensationEV = -2.8f;
        constexpr float kNightBrightSceneEV100 = 14.0f;
        // 夜のグレーディング: 暗部を縮めない NeutralLinear のまま、コントラストを少しだけ立て、彩度を落とす（暗い所の見えは
        // 色が薄い。0.85・0.55 では月明かりだけの谷床の赤い土が夕方の色に見えた）。白の釣り合いを青へ寄せる（GradingOverride の
        // 色温度は ±1 で赤と青の利得を ±10 % 動かす）。灯りを入れた後は、自動露出が灯りの周りに合って月明かりの所は暗く沈むので、
        // 灯りの暖色が残るよう彩度 0.42・色温度 −1 から戻した（0.42・−1 では街灯の光の輪が桃色がかった灰色に見えた）
        constexpr float kNightGradingContrast = 1.1f;
        constexpr float kNightGradingContrastPivot = 0.18f;
        constexpr float kNightGradingSaturation = 0.65f;
        constexpr float kNightGradingTemperature = -0.5f;

        /// モードが組むシーンの id（SceneCatalog.h。視点の定義とシーンの準備の報告に使う）
        const char *SceneIdOf(const WastelandData &data)
        {
            return data.m_bNightVillage ? Game::Scenes::kNightVillageSceneId : Game::Scenes::kWastelandSceneId;
        }

        /// 夜の集落の空・月明かり: 物理空・霧・空気遠近・雲を切り、一様な夜空と、青白い影付きの方向光（月）を置く
        void ApplyNightEnvironment(GameModeContext &ctx, WastelandData &data)
        {
            RenderWorld &renderWorld = ctx.EngineRef.GetRenderWorld();
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = false;
            renderWorld.SetSkyAtmosphere(sky);
            VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
            fog.bEnabled = false;
            renderWorld.SetVolumetricFogParameters(fog);
            renderWorld.SetAerialPerspective(AerialPerspectiveParameters{});
            renderWorld.SetCloudLayer(CloudLayerParameters{});
            renderWorld.SetStaticEnvironmentIntensityScale(1.0f);
            renderWorld.SetStaticEnvironmentMaxRadiance(0.0f);
            renderWorld.SetStaticEnvironmentUniformRadiance(
                true, NorvesLib::Math::Vector3(kNightSkyRadiance[0], kNightSkyRadiance[1], kNightSkyRadiance[2]));
            if (data.m_pMoon == nullptr)
            {
                Entity *owner = ctx.ScopeRef.SpawnObject<Entity>();
                data.m_pMoon = owner != nullptr ? ctx.WorldRef.CreateComponent<Component::DirectionalLightComponent>(owner)
                                                : nullptr;
                if (data.m_pMoon == nullptr)
                {
                    LOG_ERROR("night-village: 月明かりの方向光を作れませんでした");
                }
            }
            if (data.m_pMoon != nullptr)
            {
                const NorvesLib::Math::Vector3 toMoon =
                    MakeSunDirectionFromAltitudeAzimuth(kMoonAltitudeDegrees, kMoonAzimuthDegrees);
                data.m_pMoon->SetIntensityUnit(Component::LightIntensityUnit::Lux);
                data.m_pMoon->SetIntensity(kMoonIlluminanceNormalLux);
                data.m_pMoon->SetLightColor(kMoonColor[0], kMoonColor[1], kMoonColor[2]);
                data.m_pMoon->SetLightDirection(-toMoon.x, -toMoon.y, -toMoon.z);
                data.m_pMoon->SetCastShadows(true);
                data.m_pMoon->SetLightVisible(true);
            }
            renderWorld.SetSunShadowMaxDistance(kMoonShadowMaxDistance);
            LOG_INFO_F("NIGHT_VILLAGE_LIGHT view=%s sun=below_horizon moon_lux=%.3f moon_altitude=%.1f moon_azimuth=%.1f "
                       "sky=(%.4f,%.4f,%.4f)",
                       data.m_ViewName.empty() ? "default" : data.m_ViewName.c_str(),
                       static_cast<double>(kMoonIlluminanceNormalLux), static_cast<double>(kMoonAltitudeDegrees),
                       static_cast<double>(kMoonAzimuthDegrees), static_cast<double>(kNightSkyRadiance[0]),
                       static_cast<double>(kNightSkyRadiance[1]), static_cast<double>(kNightSkyRadiance[2]));
        }

        /// 地表の材質の値（3 層の混ぜ合わせとテクスチャのパス、地表の分類の地図と遠景の地図の範囲）を 1 行の JSON でログへ書く
        /// （WASTELAND_TERRAIN_MATERIAL）。正解の絵の比較で Blender の再現（Scripts/Terrain/BlenderReference.py --albedo）が
        /// 同じ値を使うためのもので、値は描画へ渡す MegaTerrainBlend そのもの
        void LogTerrainMaterial(const MegaTerrainBlend &blend, bool bSurfaceMask, bool bFarMap)
        {
            VariableArray<char> json;
            char text[1024];
            const auto append = [&json](const char *pText) { json.insert(json.end(), pText, pText + std::strlen(pText)); };
            std::snprintf(text, sizeof(text),
                          "{\"source\":\"Game/GameModes/Wasteland/WastelandRoutine.cpp\",\"tile_meters\":%.9g,"
                          "\"dirt_uv_scale\":%.9g,\"rock_uv_scale\":%.9g,\"dirt_slope_degrees\":[%.9g,%.9g],"
                          "\"rock_slope_degrees\":[%.9g,%.9g],\"dirt_height_range\":[%.9g,%.9g],"
                          "\"dirt_height_bias_degrees\":%.9g,\"noise_wavelengths\":[%.9g,%.9g],"
                          "\"noise_amplitude_degrees\":%.9g,\"sand_fill_height\":%.9g,\"blend_depth\":%.9g,",
                          blend.TextureTileMeters, blend.Dirt.UVScale, blend.Rock.UVScale, blend.DirtSlopeDegrees[0],
                          blend.DirtSlopeDegrees[1], blend.RockSlopeDegrees[0], blend.RockSlopeDegrees[1],
                          blend.DirtHeightRange[0], blend.DirtHeightRange[1], blend.DirtHeightBiasDegrees,
                          blend.NoiseWavelengths[0], blend.NoiseWavelengths[1], blend.NoiseAmplitudeDegrees,
                          blend.SandFillHeight, blend.BlendDepth);
            append(text);
            std::snprintf(text, sizeof(text),
                          "\"macro_wavelengths\":[%.9g,%.9g],\"macro_brightness\":%.9g,\"macro_hue\":%.9g,"
                          "\"detail_start_distance\":%.9g,\"detail_distance_ratio\":%.9g,\"detail_max_level\":%.9g,"
                          "\"detail_slope_variance\":[%.9g,%.9g,%.9g],\"surface_mask_gravel_to_dirt\":%.9g,"
                          "\"surface_mask_edge_fade_meters\":%.9g,\"surface_mask_warp_meters\":%.9g,",
                          blend.MacroWavelengths[0], blend.MacroWavelengths[1], blend.MacroBrightness, blend.MacroHue,
                          blend.DetailStartDistance, blend.DetailDistanceRatio, blend.DetailMaxLevel,
                          blend.DetailSlopeVariance[0], blend.DetailSlopeVariance[1], blend.DetailSlopeVariance[2],
                          blend.SurfaceMaskGravelToDirt, blend.SurfaceMaskEdgeFadeMeters, blend.SurfaceMaskWarpMeters);
            append(text);
            std::snprintf(text, sizeof(text),
                          "\"surface_mask\":{\"enabled\":%s,\"texture\":\"%s\",\"min_xz\":[%.9g,%.9g],\"max_xz\":[%.9g,%.9g]},"
                          "\"far_map\":{\"enabled\":%s,\"texture\":\"%s\",\"min_xz\":[%.9g,%.9g],\"max_xz\":[%.9g,%.9g],"
                          "\"texel_meters\":%.9g},",
                          bSurfaceMask ? "true" : "false", TerrainSpec::kSurfaceMaskTexturePath, blend.SurfaceMaskMinXZ[0],
                          blend.SurfaceMaskMinXZ[1], blend.SurfaceMaskMaxXZ[0], blend.SurfaceMaskMaxXZ[1],
                          bFarMap ? "true" : "false", TerrainSpec::kFarMapTexturePath, blend.FarMapMinXZ[0],
                          blend.FarMapMinXZ[1], blend.FarMapMaxXZ[0], blend.FarMapMaxXZ[1], blend.FarMapTexelMeters);
            append(text);
            std::snprintf(text, sizeof(text),
                          "\"textures\":{\"sand\":{\"albedo\":\"%s\",\"normal\":\"%s\",\"orm\":\"%s\"},"
                          "\"dirt\":{\"albedo\":\"%s\",\"normal\":\"%s\",\"orm\":\"%s\"},"
                          "\"rock\":{\"albedo\":\"%s\",\"normal\":\"%s\",\"orm\":\"%s\"}}}",
                          kSandAlbedoPath, kSandNormalPath, kSandOrmPath, kDirtAlbedoPath, kDirtNormalPath, kDirtOrmPath,
                          kRockAlbedoPath, kRockNormalPath, kRockOrmPath);
            append(text);
            json.push_back('\0');
            LOG_INFO_F("WASTELAND_TERRAIN_MATERIAL %s", json.data());
        }

#if NORVES_BUILD_DEVELOPMENT
        /// 大気の入力の感度の調べ（開発ビルドだけ）: 環境変数 NORVES_WASTELAND_ATMOS_INPUT の
        /// "albedo=<r>,<g>,<b>;mie=<密度の倍率>;mie_h=<尺度高さ[m]>"（どの項目も省ける）で空の大気の入力を差し替える。
        /// 既定の値は変えない（Docs/RenderingValidation/WastelandAtmosphereReference.md の SCN-WL-ATMOS-INPUT）
        void ApplyAtmosphereInputOverride(SkyAtmosphereParameters &sky)
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char *value = std::getenv("NORVES_WASTELAND_ATMOS_INPUT");
            if (value == nullptr || value[0] == '\0')
            {
                return;
            }
            // sscanf の非推奨の警告も同じ push の中で抑える（pop は関数の末尾）
            float albedo[3] = {0.0f, 0.0f, 0.0f};
            float mieDensity = 0.0f;
            float mieHeight = 0.0f;
            const char *cursor = value;
            while (*cursor != '\0')
            {
                if (std::sscanf(cursor, "albedo=%f,%f,%f", &albedo[0], &albedo[1], &albedo[2]) == 3)
                {
                    sky.GroundAlbedo.x = albedo[0];
                    sky.GroundAlbedo.y = albedo[1];
                    sky.GroundAlbedo.z = albedo[2];
                }
                else if (std::sscanf(cursor, "mie_h=%f", &mieHeight) == 1)
                {
                    sky.MieScaleHeightMeters = mieHeight;
                }
                else if (std::sscanf(cursor, "mie=%f", &mieDensity) == 1)
                {
                    sky.MieDensityScale = mieDensity;
                }
                const char *next = std::strchr(cursor, ';');
                if (next == nullptr)
                {
                    break;
                }
                cursor = next + 1;
            }
            LOG_INFO_F("WASTELAND_ATMOS_INPUT albedo=%.4g,%.4g,%.4g mie=%.4g mie_h=%.6g", sky.GroundAlbedo.x,
                       sky.GroundAlbedo.y, sky.GroundAlbedo.z, sky.MieDensityScale, sky.MieScaleHeightMeters);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        }
#endif

        /// 物理空・太陽・高さフォグ・空気遠近・雲の層（hideMask で外したものは点けない）
        void ApplyEnvironment(GameModeContext &ctx, float sunAltitudeDegrees, float sunAzimuthDegrees, uint32_t hideMask)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = sunAltitudeDegrees;
            sky.SunAzimuthDegrees = sunAzimuthDegrees;
            sky.MieScaleHeightMeters = kSkyMieScaleHeight;
            sky.MieDensityScale = kSkyMieDensityScale;
            sky.SunLuminanceNits = kSunLuminanceNits;
            sky.GroundAltitudeMeters = kSkyGroundAltitudeMeters;
#if NORVES_BUILD_DEVELOPMENT
            ApplyAtmosphereInputOverride(sky);
#endif
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(sky);

            // 霞の Mie を高さフォグと空気遠近で割り振る（kHazeFogShare）。高さフォグはワールドの y = 0 を基準に、その標高の
            // 空の大気の Mie の割合ぶんの密度で、空の Mie と同じ尺度高さで薄くする
            const float siteMieExtinction =
                GetAtmosphereGroundExtinction(sky).Mie * std::exp(-kSiteAltitudeMeters / sky.MieScaleHeightMeters);
            VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
            fog.bEnabled = (hideMask & WastelandHide::Fog) == 0;
            fog.DensityAtBaseHeight = kHazeFogShare * siteMieExtinction;
            fog.BaseHeight = 0.0f;
            fog.HeightFalloffPerUnit = 1.0f / sky.MieScaleHeightMeters;
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(fog);

            AerialPerspectiveParameters aerialPerspective;
            aerialPerspective.bEnabled = (hideMask & WastelandHide::Aerial) == 0;
            aerialPerspective.DistanceScale = kAerialPerspectiveDistanceScale;
            aerialPerspective.StartDistanceMeters = kAerialPerspectiveStartDistance;
            aerialPerspective.OriginAltitudeMeters = kSiteAltitudeMeters;
            aerialPerspective.MieDensityScale = 1.0f - kHazeFogShare;
            ctx.EngineRef.GetRenderWorld().SetAerialPerspective(aerialPerspective);

            CloudLayerParameters cloudLayer;
            cloudLayer.bEnabled = (hideMask & WastelandHide::Clouds) == 0;
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
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentUniformRadiance(false, NorvesLib::Math::Vector3::Zero);
            ctx.EngineRef.GetRenderWorld().SetSunShadowMaxDistance(0.0f);
            ctx.EngineRef.GetRenderWorld().SetFoliage(FoliageScene{});
        }

        /// 視点の空・太陽・霧を置く: 共通の照明（--wasteland-common-light）ならその一様な空と太陽、それ以外は物理空と
        /// 手本の写真の時刻の太陽（手本の構図の視点だけ。ほかは基準の太陽）
        void ApplyViewEnvironment(GameModeContext &ctx, WastelandData &data)
        {
            if (data.m_bNightVillage)
            {
                ApplyNightEnvironment(ctx, data);
                return;
            }
            if (data.m_CommonLight.Opts.bEnabled)
            {
                WastelandCommonLight::ApplyEnvironment(ctx, data.m_CommonLight, data.m_ViewName);
                ctx.EngineRef.GetRenderWorld().SetSunShadowMaxDistance(kSunShadowMaxDistance);
                return;
            }
            const WastelandReferenceShot *referenceShot = FindReferenceShot(data.m_ViewName);
            ApplyEnvironment(ctx, referenceShot ? referenceShot->SunAltitudeDegrees : kSunAltitudeDegrees,
                             referenceShot ? referenceShot->SunAzimuthDegrees : kSunAzimuthDegrees, data.m_HideMask);
        }

        /// 草・低木を撒かない範囲を散布へ渡す: 名前付き視点の足元（目の前を株がふさがない）と、置いた建物の足元・道
        void ApplyFoliageExclusions(WastelandData &data, const WastelandAssembly *pAssembly)
        {
            VariableArray<WastelandFoliage::ExclusionCircle> circles;
            VariableArray<WastelandFoliage::ExclusionRect> rects;
            VariableArray<WastelandFoliage::ExclusionCircle> viewGravelClearings;
            const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(SceneIdOf(data));
            if (scene != nullptr)
            {
                for (std::size_t index = 0; index < scene->ViewCount; ++index)
                {
                    const Game::Scenes::SceneViewDescriptor &view = scene->Views[index];
                    if (view.Kind == Game::Scenes::SceneViewKind::FreePose)
                    {
                        circles.push_back({view.Position[0], view.Position[2], kViewFoliageClearRadius});
                        viewGravelClearings.push_back({view.Position[0], view.Position[2], kViewGravelClearRadius});
                    }
                }
            }
            if (pAssembly != nullptr)
            {
                circles.insert(circles.end(), pAssembly->FoliageBuildingCircles.begin(),
                               pAssembly->FoliageBuildingCircles.end());
                rects.insert(rects.end(), pAssembly->FoliageRoadRects.begin(), pAssembly->FoliageRoadRects.end());
            }
            // 石と礫は、建物の足元・道から外し、視点の足元では大きな石だけを外す
            VariableArray<WastelandFoliage::ExclusionCircle> gravelCircles;
            if (pAssembly != nullptr)
            {
                gravelCircles = pAssembly->FoliageBuildingCircles;
            }
            data.m_GravelScatter.SetExclusions(std::move(gravelCircles), rects, std::move(viewGravelClearings));
            data.m_FoliageScatter.SetExclusions(std::move(circles), std::move(rects));
        }

        /// 低木 1 株の撮影（--wasteland-hide=all-but-plant）の株を作り、配置をログへ出す。正解の絵
        /// （Scripts/Terrain/BlenderReference.py --plant）が同じ形を置けるよう、撮影の保存先があれば（開発ビルドだけ）
        /// 株のワールドの三角形を <保存先>/reference_plant.ply に書く
        void PlaceReferencePlant(WastelandData &data)
        {
            data.m_ReferencePlant = WastelandFoliage::MakeReferencePlant(*data.m_pHeightField);
            const NorvesLib::Core::Rendering::FoliageInstance &instance = (*data.m_ReferencePlant.Instances)[0];
            WastelandFoliage::ReferencePlantExport record;
            bool bExported = false;
            const String plyName("reference_plant.ply");
            if (!data.m_CaptureDirectory.empty())
            {
                bExported = WastelandFoliage::ExportReferencePlant(
                    data.m_ReferencePlant, data.m_CaptureDirectory + String("/") + plyName, record);
                if (!bExported)
                {
                    LOG_WARNING_F("WASTELAND_PLANT_EXPORT_FAILED dir=%s 比べる株のメッシュを書けません",
                                  data.m_CaptureDirectory.c_str());
                }
            }
            // 株の根元の位置[m]・Y 軸まわりの回転[rad]（foliage.vert: ローカルの (x, z) をワールドの (x cos + z sin, z cos − x sin)
            // へ写す）・横と高さの倍率・色の揺らぎと揺れの位相・風の強さ・粗さ・地面の高さ。ply は書いたときだけ（書いた頂点・
            // 三角形の数と広がり）
            LOG_INFO_F("WASTELAND_PLANT_PLACEMENT id=%s reference=1 x=%.5f y=%.5f z=%.5f yaw_rad=%.8f width_scale=%.6f "
                       "height_scale=%.6f color_variation=%.6f wind_phase=%.6f wind_strength=%.3f roughness=%.3f ground=%.5f "
                       "ply=%s vertices=%llu triangles=%llu crown_radius=%.5f height=%.5f",
                       data.m_ReferencePlant.DebugName, static_cast<double>(instance.Position[0]),
                       static_cast<double>(instance.Position[1]), static_cast<double>(instance.Position[2]),
                       static_cast<double>(instance.Yaw), static_cast<double>(instance.WidthScale),
                       static_cast<double>(instance.HeightScale), static_cast<double>(instance.ColorVariation),
                       static_cast<double>(instance.WindPhase), static_cast<double>(data.m_ReferencePlant.WindStrength),
                       static_cast<double>(data.m_ReferencePlant.Roughness),
                       static_cast<double>(data.m_pHeightField->SampleHeight(instance.Position[0], instance.Position[2])),
                       bExported ? plyName.c_str() : "none", static_cast<unsigned long long>(record.VertexCount),
                       static_cast<unsigned long long>(record.TriangleCount), static_cast<double>(record.CrownRadius),
                       static_cast<double>(record.Height));
        }

        /// カメラの (x,z) の周りの草・低木と石・礫の区画をそろえ、区画が変わったか bForcePublish なら、草・低木の束の後ろに
        /// 石・礫の束を並べて RenderWorld へ渡す（--wasteland-hide で外したものは渡さない）
        void UpdateFoliage(GameModeContext &ctx, WastelandData &data, float cameraX, float cameraZ, bool bForcePublish)
        {
            if (!data.m_FoliageScatter.IsInitialized())
            {
                return;
            }
            const bool bFoliageChanged = data.m_FoliageScatter.Update(cameraX, cameraZ, data.m_Foliage);
            const bool bGravelChanged = data.m_GravelScatter.Update(cameraX, cameraZ, data.m_GravelBatches);
            if (bFoliageChanged || bGravelChanged || bForcePublish)
            {
                data.m_Foliage.Wind.TimeOffsetSeconds = WastelandFoliage::WindTimeOffsetForView(data.m_ViewName);
                FoliageScene published = data.m_Foliage;
                const bool bReferencePlantOnly = (data.m_HideMask & WastelandHide::ReferencePlantOnly) != 0;
                if ((data.m_HideMask & WastelandHide::Foliage) != 0 || bReferencePlantOnly)
                {
                    published.Batches.clear();
                }
                // 低木 1 株の撮影（all-but-plant）: 撒いた草・低木の代わりに比べる 1 株だけを渡す
                if (bReferencePlantOnly && data.m_ReferencePlant.Mesh)
                {
                    published.Batches.push_back(data.m_ReferencePlant);
                }
                if ((data.m_HideMask & WastelandHide::Gravel) == 0)
                {
                    published.Batches.insert(published.Batches.end(), data.m_GravelBatches.begin(),
                                             data.m_GravelBatches.end());
                }
                // 撮影の照合（ReferenceCheck.py --aov）が、外した草・石を渡していないことを確かめる
                LOG_INFO_F("WASTELAND_FOLIAGE_PUBLISHED batches=%zu foliage_hidden=%d gravel_hidden=%d reference_plant=%d",
                           published.Batches.size(), (data.m_HideMask & WastelandHide::Foliage) != 0 ? 1 : 0,
                           (data.m_HideMask & WastelandHide::Gravel) != 0 ? 1 : 0,
                           bReferencePlantOnly && data.m_ReferencePlant.Mesh ? 1 : 0);
                ctx.EngineRef.GetRenderWorld().SetFoliage(published);
            }
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
            const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(SceneIdOf(data));
            const Game::Scenes::SceneViewDescriptor *view =
                scene != nullptr ? Game::Scenes::FindSceneView(*scene, pViewSwitch->GetViewName().c_str()) : nullptr;
            Component::CameraComponent *camera = data.m_Rig.GetCamera();
            if (view == nullptr || view->Kind != Game::Scenes::SceneViewKind::FreePose || camera == nullptr)
            {
                LOG_ERROR_F("SCENE_VIEW_UNKNOWN scene=%s view=\"%s\" 視点を置き直せません", SceneIdOf(data),
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
            // 灰色の板（--wasteland-gray-plate）: 視点の前に板を置き直し、カメラを板へ向ける
            WastelandCommonLight::PlacePlate(ctx, data.m_CommonLight, data.m_pHeightField.get(), pose);
            data.m_Rig.PlaceAt(pose);

            // 手本の写真の構図の視点では、画角と太陽を写真の推定に替え、それ以外は基準の値へ戻す
            const WastelandReferenceShot *referenceShot = FindReferenceShot(data.m_ViewName);
            ApplyViewEnvironment(ctx, data);
            // 草・低木: 新しい視点の周りの区画をそろえ、風の時計を替える（grass_gust は grass と同じ構図で時刻だけを変えた 2 枚目）
            UpdateFoliage(ctx, data, pose.Position[0], pose.Position[2], true);
            camera->SetFieldOfView(referenceShot ? referenceShot->VerticalFieldOfViewDegrees : data.m_DefaultFieldOfView);
            LOG_INFO_F("SCENE_VIEW_SWITCHED scene=%s view=%s", SceneIdOf(data), data.m_ViewName.c_str());
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
                                 const char *path, TextureHandle *target, bool *pNormalTwoChannel, uint32_t *pPending,
                                 TextureColorEncoding colorEncoding = TextureColorEncoding::Default)
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
                },
                colorEncoding);
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
            if (assembly.TerrainExport.IsEnabled())
            {
                WastelandTerrainExport::ExportTile(assembly.TerrainExport, logicalPath, mesh);
            }
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
            if (assembly.bGrayMaterial)
            {
                // 灰色の材質: 既定の白のアルベド・平らな法線のテクスチャに、BaseColor（GBuffer のアルベドと RTGI・DDGI の
                // 当たりの反射率の両方に使われる）と粗さ・金属度の定数を掛ける。法線は頂点の補間した法線のまま
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    createInfo.Material.BaseColor[channel] = assembly.GrayAlbedo;
                }
                createInfo.Material.Roughness = assembly.GrayRoughness;
                createInfo.Material.Metallic = 0.0f;
                // 混ぜ合わせは張らないが、影と接触影は地形として扱う（色だけを替え、影の経路はテクスチャの地形と同じにする）
                createInfo.Material.TerrainBlend.bTerrainSurface = true;
                createInfo.Material.TerrainBlend.bTightSunShadow = assembly.bTightSunShadow;
            }
            else
            {
                createInfo.Material.AlbedoTexture = assembly.Material.AlbedoTexture;
                createInfo.Material.NormalTexture = assembly.Material.NormalTexture;
                createInfo.Material.ORMTexture = assembly.Material.ORMTexture;
                createInfo.Material.bNormalTwoChannel = assembly.Material.bNormalTwoChannel;
                createInfo.Material.TerrainBlend = assembly.TerrainBlend;
                createInfo.Material.TerrainBlend.bTightSunShadow = assembly.bTightSunShadow;
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    createInfo.Material.BaseColor[channel] = kTerrainAlbedoScale;
                }
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
            if (assembly.TerrainExport.IsEnabled())
            {
                WastelandTerrainExport::WriteManifest(assembly.TerrainExport);
            }
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

        /// クック済みのメッシュを解決・解析して load.Mesh と load.PageSource に入れる（材質のテクスチャ 3 枚がクック済みかも確かめる）。
        /// クック済みが無い・壊れているときは理由を出して false（その種類を置かずに続ける）
        bool ResolvePropMesh(const TSharedPtr<const Asset::AssetSystem> &assetSystem, WastelandPropLoad &load, const char *id,
                             const char *meshPath, const char *albedoPath, const char *normalPath, const char *ormPath)
        {
            const Asset::AssetResolveResult resolved = assetSystem->ResolveAsset(meshPath, Asset::AssetKind::Model);
            if (!resolved.UsedCooked())
            {
                LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland id=%s path=%s クック済みのメッシュがありません。"
                              "Scripts/CookAssets.ps1 で焼くと置けます",
                              id, meshPath);
                return false;
            }
            bool bTexturesCooked = true;
            for (const char *path : {albedoPath, normalPath, ormPath})
            {
                if (!assetSystem->FindCookedVariant(AnsiString(path), Asset::AssetKind::Texture).ShouldUseCooked())
                {
                    LOG_WARNING_F("COOKED_MODEL_TEXTURES_MISSING scene=wasteland id=%s path=%s "
                                  "クック済みのテクスチャがありません",
                                  id, path);
                    bTexturesCooked = false;
                }
            }
            if (!bTexturesCooked)
            {
                return false;
            }
            Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty() || parsed.Mesh.Vertices.empty())
            {
                LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland id=%s path=%s status=%u "
                              "クック済みのメッシュを解析できません",
                              id, meshPath, static_cast<unsigned int>(parsed.Status));
                return false;
            }
            Asset::AssetCookedRange pageRange;
            if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
            {
                load.PageSource = MakeCookedMeshPageSource(parsed.Mesh, assetSystem->GetCookedFileReader(),
                                                           pageRange.Request, pageRange.BaseOffset);
            }
            load.Mesh = MakeShared<Asset::CookedMeshData>(std::move(parsed.Mesh));
            return true;
        }

        /// 材質のテクスチャ 3 枚（アルベド・法線・ORM）の読み込みを始め、load を読み込み中にする
        void StartPropTextures(TextureResources &textures, const TSharedPtr<WastelandAssembly> &assembly,
                               WastelandPropLoad &load, const char *id, const char *albedoPath, const char *normalPath,
                               const char *ormPath)
        {
            load.Material.DebugName = id;
            load.PendingTextureCount = 3;
            load.bStarted = true;
            // glTF の模型の基本色は sRGB で符号化されているので、sRGB の形式のまま上げる（標本の前にハードが線形へ戻す）
            LoadMaterialTexture(textures, assembly, albedoPath, &load.Material.AlbedoTexture, nullptr,
                                &load.PendingTextureCount, TextureColorEncoding::Srgb);
            LoadMaterialTexture(textures, assembly, normalPath, &load.Material.NormalTexture,
                                &load.Material.bNormalTwoChannel, &load.PendingTextureCount);
            LoadMaterialTexture(textures, assembly, ormPath, &load.Material.ORMTexture, nullptr,
                                &load.PendingTextureCount);
        }

        /// 小物・建物の置く種類（前哨地の建物と崖の足元の落石。夜の集落では集落の建物・小物と崖の足元の落石）のクック済みメッシュを
        /// 解決・解析し、材質のテクスチャの読み込みを始める。クック済みが無い・壊れている種類は理由を出して読み飛ばす（その種類を置かずに続ける）
        void StartPropLoads(GameModeContext &ctx, const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                            const TSharedPtr<WastelandAssembly> &assembly, bool bNightVillage)
        {
            auto &textures = ctx.RenderResourcesRef.Textures();
            uint32_t started = 0;
            for (uint32_t kindIndex = 0; kindIndex < Layout::kPropKindCount; ++kindIndex)
            {
                const Layout::PropKind usedKind = static_cast<Layout::PropKind>(kindIndex);
                const bool bUsed = bNightVillage ? Layout::IsUsedInNightVillage(usedKind)
                                                 : (kProceduralLayout && !Layout::IsNightVillagePropKind(usedKind)) ||
                                                       Layout::IsUsedOnDem(usedKind);
                if (!bUsed)
                {
                    continue;
                }
                const Layout::PropKindSpec &spec = Layout::GetPropKindSpec(static_cast<Layout::PropKind>(kindIndex));
                WastelandPropLoad &load = assembly->Props[kindIndex];
                if (!ResolvePropMesh(assetSystem, load, spec.Id, spec.MeshPath, spec.AlbedoPath, spec.NormalPath,
                                     spec.OrmPath))
                {
                    continue;
                }

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
                // 崖錐の岩の散布が底の高さを引く頂点の標本（メッシュは MegaMesh を作ると手放すので、ここで写す）
                const Layout::PropKind kind = static_cast<Layout::PropKind>(kindIndex);
                if (Layout::IsTalusKind(kind))
                {
                    WastelandTalus::TalusMeshShape &talus =
                        assembly->TalusShapes[static_cast<uint32_t>(kind == Layout::PropKind::TalusCrag
                                                                        ? WastelandTalus::TalusMesh::Crag
                                                                        : WastelandTalus::TalusMesh::Slab)];
                    talus.Min = NorvesLib::Math::Vector3(shape.MinX, shape.MinY, shape.MinZ);
                    talus.Max = NorvesLib::Math::Vector3(shape.MaxX, shape.MaxY, shape.MaxZ);
                    const size_t vertexCount = load.Mesh->Vertices.size();
                    const size_t stride = std::max<size_t>(1, (vertexCount + kTalusPointSamples - 1) / kTalusPointSamples);
                    talus.Points.clear();
                    for (size_t index = 0; index < vertexCount; index += stride)
                    {
                        const Asset::CookedMeshVertex &vertex = load.Mesh->Vertices[index];
                        talus.Points.push_back(NorvesLib::Math::Vector3(vertex.Position.X, vertex.Position.Y, vertex.Position.Z));
                    }
                }
                LOG_INFO_F("WASTELAND_PROP_MESH id=%s clusters=%u pages=%zu size=%.2fx%.2fx%.2f open_ratio=%.3f "
                           "front_yaw_deg=%.1f",
                           spec.Id, static_cast<unsigned int>(load.Mesh->Clusters.size()), load.Mesh->Pages.size(),
                           static_cast<double>(shape.MaxX - shape.MinX), static_cast<double>(shape.MaxY - shape.MinY),
                           static_cast<double>(shape.MaxZ - shape.MinZ), static_cast<double>(openRatio),
                           static_cast<double>(shape.FrontYaw * 180.0f / 3.14159265f));

                StartPropTextures(textures, assembly, load, spec.Id, spec.AlbedoPath, spec.NormalPath, spec.OrmPath);
                ++started;
            }
            LOG_INFO_F("WASTELAND_PROP_LOADS kinds=%u started=%u", Layout::kPropKindCount, started);
        }

        /// 崖の面に貼るスキャンの塊の配置表（WastelandCliffScan.h）を読み、素材ごとにクック済みメッシュを解決・解析して、
        /// 材質のテクスチャの読み込みを始める。配置表が無い・壊れているときは理由を出して塊を置かずに続ける
        void StartCliffScanLoads(GameModeContext &ctx, const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                                 const TSharedPtr<WastelandAssembly> &assembly)
        {
            String error;
            if (!WastelandCliffScan::LoadTable(assembly->CliffScanTable, error))
            {
                LOG_WARNING_F("WASTELAND_CLIFF_SCAN_TABLE_MISSING %s 崖の面のスキャンの塊を置きません", error.c_str());
                return;
            }
            const WastelandCliffScan::Table &table = assembly->CliffScanTable;
            auto &textures = ctx.RenderResourcesRef.Textures();
            uint32_t started = 0;
            for (uint32_t assetIndex = 0; assetIndex < table.Assets.size(); ++assetIndex)
            {
                const WastelandCliffScan::AssetSpec &spec = table.Assets[assetIndex];
                WastelandPropLoad &load = assembly->CliffScanLoads[assetIndex];
                if (!ResolvePropMesh(assetSystem, load, spec.Id.c_str(), spec.MeshPath.c_str(), spec.AlbedoPath.c_str(),
                                     spec.NormalPath.c_str(), spec.OrmPath.c_str()))
                {
                    continue;
                }
                VariableArray<NorvesLib::Math::Vector3> &points = assembly->CliffScanPoints[assetIndex];
                const size_t vertexCount = load.Mesh->Vertices.size();
                const size_t stride =
                    std::max<size_t>(1, (vertexCount + kCliffScanPointSamples - 1) / kCliffScanPointSamples);
                points.clear();
                for (size_t index = 0; index < vertexCount; index += stride)
                {
                    const Asset::CookedMeshVertex &vertex = load.Mesh->Vertices[index];
                    points.push_back(NorvesLib::Math::Vector3(vertex.Position.X, vertex.Position.Y, vertex.Position.Z));
                }
                StartPropTextures(textures, assembly, load, spec.Id.c_str(), spec.AlbedoPath.c_str(),
                                  spec.NormalPath.c_str(), spec.OrmPath.c_str());
                ++started;
            }
            LOG_INFO_F("WASTELAND_CLIFF_SCAN_TABLE path=%s assets=%zu started=%u placements=%zu",
                       WastelandCliffScan::TablePath().c_str(), table.Assets.size(), started, table.Placements.size());
        }

        /// 読み込みを始めた種類（小物・建物と崖の面の塊）の材質のテクスチャがすべて届いたか
        bool ArePropTexturesLoaded(const WastelandAssembly &assembly)
        {
            for (const WastelandPropLoad &load : assembly.Props)
            {
                if (load.bStarted && load.PendingTextureCount != 0)
                {
                    return false;
                }
            }
            for (const WastelandPropLoad &load : assembly.CliffScanLoads)
            {
                if (load.bStarted && load.PendingTextureCount != 0)
                {
                    return false;
                }
            }
            return true;
        }

        /// 崖錐の岩の行を作る間だけ上げた、散布の仕事の列の予算を戻す
        void RestoreMassBudget(GameModeContext &ctx, WastelandAssembly &assembly)
        {
            if (assembly.bMassBudgetRaised)
            {
                ctx.WorldRef.GetEcsWorld().GetMassWork().SetBudget(assembly.SavedMassBudget);
                assembly.bMassBudgetRaised = false;
            }
        }

        /// 崖の下の崖錐に、剥がれ落ちた岩の群れを散布の Object（Entity 1 つと軽い行）で置く。影を落とす大岩と、影を落とさない
        /// 小片の 2 つに分ける。名前付き視点の足元と、置いた建物の足元は空ける
        void PlaceTalusRocks(GameModeContext &ctx, const Terrain::TerrainHeightField &field,
                             const Terrain::TerrainSurfaceMask *pMask, WastelandAssembly &assembly, const char *sceneId)
        {
            const WastelandTalus::TalusMeshShape &crag =
                assembly.TalusShapes[static_cast<uint32_t>(WastelandTalus::TalusMesh::Crag)];
            if (pMask == nullptr || !crag.bAvailable)
            {
                LOG_WARNING_F("WASTELAND_TALUS_SKIPPED mask=%d crag=%d 地表の分類の地図か岩塊のメッシュが無いため、"
                              "崖錐の岩を置きません",
                              pMask != nullptr ? 1 : 0, crag.bAvailable ? 1 : 0);
                return;
            }
            VariableArray<WastelandTalus::TalusExclusion> exclusions;
            const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(sceneId);
            if (scene != nullptr)
            {
                for (std::size_t index = 0; index < scene->ViewCount; ++index)
                {
                    const Game::Scenes::SceneViewDescriptor &view = scene->Views[index];
                    if (view.Kind == Game::Scenes::SceneViewKind::FreePose)
                    {
                        exclusions.push_back({view.Position[0], view.Position[2], kViewTalusClearRadius});
                    }
                }
            }
            for (const WastelandFoliage::ExclusionCircle &circle : assembly.FoliageBuildingCircles)
            {
                exclusions.push_back({circle.X, circle.Z, circle.Radius});
            }

            VariableArray<Component::MegaScatterInstance> casters;
            VariableArray<Component::MegaScatterInstance> fragments;
            WastelandTalus::TalusStats stats;
            WastelandTalus::BuildTalusRocks(field, *pMask, assembly.TalusShapes, exclusions, casters, fragments, stats);

            const MegaMeshHandle meshes[WastelandTalus::kTalusMeshCount] = {
                assembly.Props[static_cast<uint32_t>(Layout::PropKind::TalusCrag)].MegaMesh,
                assembly.Props[static_cast<uint32_t>(Layout::PropKind::TalusSlab)].MegaMesh,
            };
            // 散布 1 つ（Entity 1 つと軽い行）を置き、置いた行の数を返す（置けなければ 0）
            const auto spawnScatter = [&](const VariableArray<Component::MegaScatterInstance> &instances, bool bCastShadow,
                                          uint32_t &outRanges) -> uint32_t
            {
                outRanges = 0;
                if (instances.empty())
                {
                    return 0;
                }
                Entity *owner = ctx.ScopeRef.SpawnObject<Entity>();
                auto *scatter =
                    owner != nullptr ? ctx.WorldRef.CreateComponent<Component::MegaGeometryScatterComponent>(owner) : nullptr;
                uint32_t placed = 0;
                if (scatter != nullptr)
                {
                    scatter->SetMegaMeshHandles(Span<const MegaMeshHandle>(meshes, WastelandTalus::kTalusMeshCount));
                    scatter->SetCastShadow(bCastShadow);
                    if (scatter->SetInstances(Span<const Component::MegaScatterInstance>(instances.data(), instances.size())))
                    {
                        placed = scatter->GetInstanceCount();
                        outRanges = scatter->GetRangeCount();
                    }
                }
                if (placed == 0 && owner != nullptr)
                {
                    LOG_WARNING_F("WASTELAND_TALUS_SKIPPED cast_shadow=%d 崖錐の岩の散布を作れませんでした", bCastShadow ? 1 : 0);
                    ctx.ScopeRef.Untrack(owner);
                    ctx.WorldRef.RemoveObject(owner);
                }
                return placed;
            };
            uint32_t casterRanges = 0;
            uint32_t fragmentRanges = 0;
            const uint32_t placedCasters = spawnScatter(casters, true, casterRanges);
            const uint32_t placedFragments = spawnScatter(fragments, false, fragmentRanges);
            // 行を作り終えるまで（シーンの準備ができる前）だけ、仕事の列の予算を上げる
            if (placedCasters + placedFragments != 0 && !assembly.bMassBudgetRaised)
            {
                Ecs::EcsMassWorkQueue &massWork = ctx.WorldRef.GetEcsWorld().GetMassWork();
                assembly.SavedMassBudget = massWork.GetBudget();
                Ecs::EcsMassBudget budget = assembly.SavedMassBudget;
                budget.MillisecondsPerFrame = std::max(budget.MillisecondsPerFrame, kTalusMassMillisecondsPerFrame);
                massWork.SetBudget(budget);
                assembly.bMassBudgetRaised = true;
            }
            LOG_INFO_F("WASTELAND_TALUS casters=%u fragments_placed=%u planned=%zu+%zu ranges=%u+%u cells=%u clusters=%u "
                       "main=%u fragments=%u toe=%u rejected=%u crag=%u slab=%u max_boulder_m=%.2f caster_candidates=%u "
                       "min_caster_m=%.2f support=%u+%u",
                       placedCasters, placedFragments, casters.size(), fragments.size(), casterRanges, fragmentRanges,
                       stats.CellsVisited, stats.Clusters, stats.MainBoulders, stats.Fragments, stats.ToeRocks, stats.Rejected,
                       stats.PerMesh[static_cast<uint32_t>(WastelandTalus::TalusMesh::Crag)],
                       stats.PerMesh[static_cast<uint32_t>(WastelandTalus::TalusMesh::Slab)],
                       static_cast<double>(stats.MaxBoulderHeight), stats.CasterCandidates,
                       static_cast<double>(stats.MinCasterHeight), stats.SupportPoints[0], stats.SupportPoints[1]);
        }

        /// 材質のテクスチャがそろった読み込みを MegaMesh にしてモデルとして登録し、load.MegaMesh に入れる（解析したメッシュは手放す）。
        /// pTint は材質の BaseColor に掛ける色（無ければ nullptr）。作れないときは理由を出して false
        bool CreatePropMegaMesh(GameModeContext &ctx, WastelandPropLoad &load, const char *id, const char *meshPath,
                                const float *pTint)
        {
            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            const MaterialCreateData &material = load.Material;
            if (!material.AlbedoTexture.IsValid() || !material.NormalTexture.IsValid() || !material.ORMTexture.IsValid())
            {
                LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s 材質のテクスチャを読めないため置きません"
                              "（albedo=%d normal=%d orm=%d）",
                              id, material.AlbedoTexture.IsValid() ? 1 : 0, material.NormalTexture.IsValid() ? 1 : 0,
                              material.ORMTexture.IsValid() ? 1 : 0);
                load.Mesh.reset();
                return false;
            }
            MegaMeshCreateInfo createInfo;
            if (!BuildMegaMeshCreateInfoFromCookedMesh(*load.Mesh, createInfo))
            {
                LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s MegaMesh の入力を作れませんでした", id);
                load.Mesh.reset();
                return false;
            }
            createInfo.PageSource = load.PageSource;
            createInfo.Material.AlbedoTexture = material.AlbedoTexture;
            createInfo.Material.NormalTexture = material.NormalTexture;
            createInfo.Material.ORMTexture = material.ORMTexture;
            createInfo.Material.bNormalTwoChannel = material.bNormalTwoChannel;
            if (pTint != nullptr)
            {
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    createInfo.Material.BaseColor[channel] = pTint[channel];
                }
            }
            createInfo.DebugName = id;
            const MegaMeshHandle megaMesh = megaGeometry.CreateMegaMesh(createInfo);
            load.Mesh.reset();
            if (!megaMesh.IsValid())
            {
                LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s MegaMesh を作れませんでした", id);
                return false;
            }
            const ModelHandle model = megaGeometry.RegisterModel(megaMesh, id, meshPath);
            if (!model.IsValid())
            {
                megaGeometry.ReleaseMegaMesh(megaMesh);
                LOG_WARNING_F("WASTELAND_PROP_SKIPPED id=%s モデルとして登録できませんでした", id);
                return false;
            }
            // 同じ種類の全 Entity が 1 つの Model を共有する。シーンを出るときにスコープが返す
            ctx.ScopeRef.TrackModel(model);
            load.MegaMesh = megaMesh;
            return true;
        }

        /// 配置表の崖の面の塊を置く（素材ごとに MegaMesh を作り、塊ごとに Entity を置く）。置いた塊ごとに、頂点の標本から求めた
        /// ワールドの範囲と、地形の高さより下にある標本の割合（地形に埋まった割合）を出す（Scripts/Terrain/BlenderCliffScan.py の
        /// 記録と比べる）
        void PlaceCliffScans(GameModeContext &ctx, const Terrain::TerrainHeightField &field, WastelandAssembly &assembly)
        {
            const WastelandCliffScan::Table &table = assembly.CliffScanTable;
            for (uint32_t assetIndex = 0; assetIndex < table.Assets.size(); ++assetIndex)
            {
                WastelandPropLoad &load = assembly.CliffScanLoads[assetIndex];
                if (load.bStarted)
                {
                    const WastelandCliffScan::AssetSpec &spec = table.Assets[assetIndex];
                    CreatePropMegaMesh(ctx, load, spec.Id.c_str(), spec.MeshPath.c_str(), nullptr);
                }
            }
            uint32_t placed = 0;
            for (const WastelandCliffScan::Placement &placement : table.Placements)
            {
                const WastelandCliffScan::AssetSpec &spec = table.Assets[placement.AssetIndex];
                const WastelandPropLoad &load = assembly.CliffScanLoads[placement.AssetIndex];
                if (!load.MegaMesh.IsValid())
                {
                    LOG_WARNING_F("WASTELAND_CLIFF_SCAN_SKIPPED name=%s asset=%s 素材を読めないため置きません",
                                  placement.Name.c_str(), spec.Id.c_str());
                    continue;
                }
                Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
                auto *component =
                    object != nullptr ? ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(object) : nullptr;
                if (component == nullptr)
                {
                    continue;
                }
                object->SetPosition(placement.Position.x, placement.Position.y, placement.Position.z);
                object->SetScale(placement.Scale, placement.Scale, placement.Scale);
                object->SetRotation(placement.Rotation);
                component->SetMegaMeshHandle(load.MegaMesh);
                component->SetCastShadow(true);
                ++placed;

                // Entity の行ベクトルのワールド行列は、ローカルの向きを四元数の標準の回転の逆（共役の回転）で写す
                const NorvesLib::Math::Quaternion &q = placement.Rotation;
                const NorvesLib::Math::Quaternion toWorld(-q.x, -q.y, -q.z, q.w);
                NorvesLib::Math::Vector3 lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                            std::numeric_limits<float>::max());
                NorvesLib::Math::Vector3 hi(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                            std::numeric_limits<float>::lowest());
                uint32_t inside = 0;
                const VariableArray<NorvesLib::Math::Vector3> &points = assembly.CliffScanPoints[placement.AssetIndex];
                for (const NorvesLib::Math::Vector3 &local : points)
                {
                    const NorvesLib::Math::Vector3 rotated = toWorld * local;
                    const NorvesLib::Math::Vector3 world(placement.Position.x + rotated.x * placement.Scale,
                                                         placement.Position.y + rotated.y * placement.Scale,
                                                         placement.Position.z + rotated.z * placement.Scale);
                    lo = NorvesLib::Math::Vector3(std::min(lo.x, world.x), std::min(lo.y, world.y), std::min(lo.z, world.z));
                    hi = NorvesLib::Math::Vector3(std::max(hi.x, world.x), std::max(hi.y, world.y), std::max(hi.z, world.z));
                    if (world.y < field.SampleHeight(world.x, world.z))
                    {
                        ++inside;
                    }
                }
                LOG_INFO_F("WASTELAND_CLIFF_SCAN_PLACEMENT name=%s asset=%s x=%.3f y=%.3f z=%.3f qx=%.6f qy=%.6f qz=%.6f "
                           "qw=%.6f scale=%.4f min=%.2f,%.2f,%.2f max=%.2f,%.2f,%.2f inside=%.3f samples=%zu",
                           placement.Name.c_str(), spec.Id.c_str(), static_cast<double>(placement.Position.x),
                           static_cast<double>(placement.Position.y), static_cast<double>(placement.Position.z),
                           static_cast<double>(q.x), static_cast<double>(q.y), static_cast<double>(q.z),
                           static_cast<double>(q.w), static_cast<double>(placement.Scale), static_cast<double>(lo.x),
                           static_cast<double>(lo.y), static_cast<double>(lo.z), static_cast<double>(hi.x),
                           static_cast<double>(hi.y), static_cast<double>(hi.z),
                           points.empty() ? 0.0 : static_cast<double>(inside) / static_cast<double>(points.size()),
                           points.size());
            }
            LOG_INFO_F("WASTELAND_CLIFF_SCAN placed=%u planned=%zu assets=%zu", placed, table.Placements.size(),
                       table.Assets.size());
        }

        /// 材質がそろった種類を MegaMesh にし、高さ場から決めた配置に Entity を置き、崖錐の岩を散布で置く。
        /// hideMask で外した建物・崖錐の岩は置かない（草・低木を撒かない範囲は外さないときと同じに残す）。
        /// bNightVillage なら前哨地の代わりに夜の集落の建物と小物を置き、置いた小物と建物に合わせて灯り（pNightLights）を置く
        void PlaceProps(GameModeContext &ctx, const Terrain::TerrainHeightField &field,
                        const Terrain::TerrainSurfaceMask *pMask, WastelandAssembly &assembly, uint32_t hideMask,
                        bool bNightVillage, WastelandNightLights::State *pNightLights)
        {
            Layout::PropMeshShape shapes[Layout::kPropKindCount];
            for (uint32_t kindIndex = 0; kindIndex < Layout::kPropKindCount; ++kindIndex)
            {
                WastelandPropLoad &load = assembly.Props[kindIndex];
                const Layout::PropKindSpec &spec = Layout::GetPropKindSpec(static_cast<Layout::PropKind>(kindIndex));
                if (!load.bStarted)
                {
                    continue;
                }
                const Layout::PropKind kind = static_cast<Layout::PropKind>(kindIndex);
                const bool bCliff = kind == Layout::PropKind::Cliff01 || kind == Layout::PropKind::Cliff02;
                if (!CreatePropMegaMesh(ctx, load, spec.Id, spec.MeshPath, bCliff ? kCliffTint : nullptr))
                {
                    continue;
                }
                load.Shape.bAvailable = true;
                shapes[kindIndex] = load.Shape;
                if (Layout::IsTalusKind(kind))
                {
                    assembly
                        .TalusShapes[static_cast<uint32_t>(kind == Layout::PropKind::TalusCrag ? WastelandTalus::TalusMesh::Crag
                                                                                               : WastelandTalus::TalusMesh::Slab)]
                        .bAvailable = true;
                }
            }

            VariableArray<Layout::PropPlacement> placements;
            Layout::LayoutStats stats;
            if (bNightVillage)
            {
                Layout::BuildNightVillagePlacements(field, shapes, placements, stats);
            }
            else
            {
                Layout::BuildPlacements(field, shapes, kProceduralLayout, placements, stats);
            }

            // 外す建物: 全部（buildings）か、比べる 1 棟のほか（all-but-building）
            const auto isHiddenBuilding = [hideMask](const Layout::PropPlacement &placement)
            {
                if (!Layout::GetPropKindSpec(placement.Kind).bBuilding)
                {
                    return false;
                }
                return (hideMask & WastelandHide::Buildings) != 0 ||
                       ((hideMask & WastelandHide::ReferenceBuildingOnly) != 0 && !placement.bReferenceBuilding);
            };
            uint32_t placedPerKind[Layout::kPropKindCount] = {};
            uint32_t placed = 0;
            VariableArray<Layout::PropPlacement> placedPlacements;
            for (const Layout::PropPlacement &placement : placements)
            {
                const uint32_t kindIndex = static_cast<uint32_t>(placement.Kind);
                if (isHiddenBuilding(placement))
                {
                    continue;
                }
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
                component->SetCastShadow(WastelandNightLights::CastsShadow(placement.Kind));
                ++placedPerKind[kindIndex];
                ++placed;
                placedPlacements.push_back(placement);
            }
            if (bNightVillage && pNightLights != nullptr)
            {
                WastelandNightLights::Place(ctx, placedPlacements, *pNightLights);
            }
            LOG_INFO_F("WASTELAND_PROPS placed=%u planned=%zu buildings=%u foot_rays=%u foot_hits=%u cliffs=%u "
                       "foot_boulders=%u basin_boulders=%u cliff_view_samples=%u village_props=%u",
                       placed, placements.size(), stats.Buildings, stats.FootRays, stats.FootHits, stats.Cliffs,
                       stats.FootBoulders, stats.BasinBoulders, stats.CliffViewSamples, stats.VillageProps);
            for (uint32_t kindIndex = 0; kindIndex < Layout::kPropKindCount; ++kindIndex)
            {
                LOG_INFO_F("WASTELAND_PROP_KIND id=%s placed=%u",
                           Layout::GetPropKindSpec(static_cast<Layout::PropKind>(kindIndex)).Id, placedPerKind[kindIndex]);
            }
            // 建物の足元と道を、草・低木を撒かない範囲として残す（建物の原点を中心に、メッシュの範囲を囲む円）
            assembly.FoliageBuildingCircles.clear();
            assembly.FoliageRoadRects.clear();
            for (const Layout::PropPlacement &placement : placements)
            {
                if (!Layout::GetPropKindSpec(placement.Kind).bBuilding)
                {
                    continue;
                }
                const Layout::PropMeshShape &shape = shapes[static_cast<uint32_t>(placement.Kind)];
                const float extentX = std::max(std::fabs(shape.MinX), std::fabs(shape.MaxX));
                const float extentZ = std::max(std::fabs(shape.MinZ), std::fabs(shape.MaxZ));
                assembly.FoliageBuildingCircles.push_back(
                    {placement.X, placement.Z,
                     std::sqrt(extentX * extentX + extentZ * extentZ) * placement.Scale + kBuildingFoliageClearMargin});
            }
            if (stats.Buildings > 0 && bNightVillage)
            {
                Layout::RoadClearArea areas[Layout::kNightVillageClearAreaCount];
                Layout::GetNightVillageClearAreas(areas);
                for (const Layout::RoadClearArea &area : areas)
                {
                    assembly.FoliageRoadRects.push_back(
                        {area.CenterX, area.CenterZ, area.AlongX, area.AlongZ, area.HalfLength, area.HalfWidth});
                }
            }
            else if (stats.Buildings > 0)
            {
                const Layout::RoadClearArea road = Layout::GetRoadClearArea();
                assembly.FoliageRoadRects.push_back(
                    {road.CenterX, road.CenterZ, road.AlongX, road.AlongZ, road.HalfLength, road.HalfWidth});
            }
            // 建物の最下点の高さと、その位置の地形の高さ（最下点が地面より下 = 埋まっている）を確かめられるよう出す（置いた建物だけ）。
            // 続く WASTELAND_BUILDING_PLACEMENT は正解の絵（Scripts/Terrain/BlenderReference.py --building）が同じ glTF を同じ配置で
            // 置くための値: Entity の位置[m]・Y 軸まわりの回転[rad]（ローカルの (x, z) をワールドの
            // (x cos − z sin, z cos + x sin) へ写す。WastelandLayout.cpp の RotateXZ）・一様な拡大率
            bool bReferenceBuildingPlaced = false;
            for (const Layout::PropPlacement &placement : placements)
            {
                if (Layout::GetPropKindSpec(placement.Kind).bBuilding && !isHiddenBuilding(placement))
                {
                    const Layout::PropKindSpec &spec = Layout::GetPropKindSpec(placement.Kind);
                    const float minY = shapes[static_cast<uint32_t>(placement.Kind)].MinY;
                    const float bottom = placement.Y + minY * placement.Scale;
                    LOG_INFO_F("WASTELAND_BUILDING id=%s x=%.1f z=%.1f bottom=%.2f ground=%.2f yaw_deg=%.1f", spec.Id,
                               static_cast<double>(placement.X), static_cast<double>(placement.Z),
                               static_cast<double>(bottom), static_cast<double>(field.SampleHeight(placement.X, placement.Z)),
                               static_cast<double>(placement.Yaw * 180.0f / 3.14159265f));
                    LOG_INFO_F("WASTELAND_BUILDING_PLACEMENT id=%s gltf=%s reference=%d x=%.5f y=%.5f z=%.5f yaw_rad=%.8f "
                               "scale=%.6f mesh_min_y=%.5f",
                               spec.Id, spec.MeshPath, placement.bReferenceBuilding ? 1 : 0, static_cast<double>(placement.X),
                               static_cast<double>(placement.Y), static_cast<double>(placement.Z),
                               static_cast<double>(placement.Yaw), static_cast<double>(placement.Scale),
                               static_cast<double>(minY));
                    bReferenceBuildingPlaced = bReferenceBuildingPlaced || placement.bReferenceBuilding;
                }
            }
            if ((hideMask & WastelandHide::ReferenceBuildingOnly) != 0 && !bReferenceBuildingPlaced)
            {
                LOG_WARNING("WASTELAND_REFERENCE_BUILDING_MISSING 比べる 1 棟を置けませんでした（DEM の前哨地でない・資産が無い）");
            }
            if ((hideMask & WastelandHide::Rocks) == 0)
            {
                PlaceTalusRocks(ctx, field, pMask, assembly,
                                bNightVillage ? Game::Scenes::kNightVillageSceneId : Game::Scenes::kWastelandSceneId);
            }
            // 崖の面の塊は、読み込みを始めたとき（荒野で岩を外さないとき）だけ置く
            if (!assembly.CliffScanTable.Placements.empty())
            {
                PlaceCliffScans(ctx, field, assembly);
            }
        }
    } // namespace

    bool WastelandHide::Parse(const String &value, uint32_t &outMask, String &outError)
    {
        struct Name
        {
            const TCHAR *Text;
            uint32_t Flag;
        };
        static constexpr Name kNames[] = {
            {TEXT("clouds"), Clouds}, {TEXT("foliage"), Foliage},     {TEXT("rocks"), Rocks},
            {TEXT("gravel"), Gravel}, {TEXT("buildings"), Buildings}, {TEXT("aerial"), Aerial},
            {TEXT("fog"), Fog},       {TEXT("all"), All},
            {TEXT("all-but-building"), (All & ~static_cast<uint32_t>(Buildings)) | ReferenceBuildingOnly},
            {TEXT("all-but-plant"), (All & ~static_cast<uint32_t>(Foliage)) | ReferencePlantOnly},
        };
        outMask = 0;
        outError.clear();
        size_t start = 0;
        while (true)
        {
            const size_t end = value.find(TEXT('+'), start);
            const String token = value.substr(start, end == String::npos ? String::npos : end - start);
            uint32_t flag = 0;
            for (const Name &name : kNames)
            {
                if (token == name.Text)
                {
                    flag = name.Flag;
                    break;
                }
            }
            if (flag == 0)
            {
                outError = token;
                return false;
            }
            outMask |= flag;
            if (end == String::npos)
            {
                return true;
            }
            start = end + 1;
        }
    }

    GameModeEnterResult WastelandRoutine::Enter(GameModeContext &ctx, WastelandData &data)
    {
        const char *sceneId = SceneIdOf(data);
        LOG_INFO_F("%s を開始します", sceneId);
        data.m_pMoon = nullptr;
        data.m_TickCount = 0;
        data.m_TerrainSettledTick = 0;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        // 外すもの（--wasteland-hide）を、撮影の照合が読めるようにログへ残す
        if (data.m_HideMask != 0)
        {
            LOG_INFO_F("WASTELAND_HIDE mask=%u clouds=%d foliage=%d rocks=%d gravel=%d buildings=%d aerial=%d fog=%d "
                       "reference_building_only=%d reference_plant_only=%d",
                       data.m_HideMask, (data.m_HideMask & WastelandHide::Clouds) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::Foliage) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::Rocks) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::Gravel) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::Buildings) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::Aerial) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::Fog) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::ReferenceBuildingOnly) != 0 ? 1 : 0,
                       (data.m_HideMask & WastelandHide::ReferencePlantOnly) != 0 ? 1 : 0);
        }
        // 地形を最も細かい段で描く指定（--wasteland-finest-lod）と、地形の書き出し先（--wasteland-export-terrain）
        if (data.m_bFinestLod)
        {
            LOG_INFO("WASTELAND_FINEST_LOD lod=0 地形を最も細かい段で描きます");
        }
        if (!data.m_TerrainExportDir.empty())
        {
            LOG_INFO_F("WASTELAND_TERRAIN_EXPORT_BEGIN dir=%s", data.m_TerrainExportDir.c_str());
        }
        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(sceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR_F("%s: シーンの一覧に視点の定義がありません", sceneId);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        // 地形の高さ場（DEM の格子に前哨地の造成を掛けたもの）を 1 回だけ読み、シーンの間だけ持つ。クッカーが区画を焼いたのと
        // 同じファイル・同じ造成・同じ標本の規則
        AnsiString gridError;
        data.m_pHeightField = TerrainSpec::LoadDemGrid(gridError);
        if (!data.m_pHeightField)
        {
            LOG_WARNING_F("COOKED_MODEL_MISSING scene=wasteland path=%s 地形の格子を読めません（%s）。"
                          "Scripts/Terrain/CropDem.py --build で作ると描けます",
                          TerrainSpec::kDemGridDescriptorPath, gridError.c_str());
        }
        LiftPoseToTerrain(data.m_pHeightField.get(), data.m_ViewName, pose);

        // 共通の照明と灰色の板（起動引数で選んだときだけ）。定義が読めなければモードを失敗にする（黙って既定の照明で撮らない）
        if (!WastelandCommonLight::Enter(ctx, data.m_CommonLight))
        {
            WastelandCommonLight::Leave(ctx, data.m_CommonLight);
            data.m_pHeightField.reset();
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        WastelandCommonLight::PlacePlate(ctx, data.m_CommonLight, data.m_pHeightField.get(), pose);

        // 草・低木（高さ場の上に、地表の分類の地図で撒く）。高さ場が無ければ置かない。地図が読めなければ傾斜と密度の斑だけで撒く
        data.m_Foliage = NorvesLib::Core::Rendering::FoliageScene{};
        data.m_FoliageScatter.Reset();
        data.m_ReferencePlant = NorvesLib::Core::Rendering::FoliageBatch{};
        data.m_GravelBatches.clear();
        data.m_GravelScatter.Reset();
        if (data.m_pHeightField)
        {
            AnsiString maskError;
            TSharedPtr<const Terrain::TerrainSurfaceMask> surfaceMask =
                Terrain::TerrainSurfaceMask::Load(TerrainSpec::kSurfaceMaskDescriptorPath, maskError);
            if (!surfaceMask)
            {
                LOG_WARNING_F("WASTELAND_FOLIAGE_MASK_MISSING path=%s 地表の分類の地図を読めません（%s）。"
                              "草・低木は傾斜と密度の斑だけで撒きます",
                              TerrainSpec::kSurfaceMaskDescriptorPath, maskError.c_str());
            }
            data.m_pSurfaceMask = surfaceMask;
            // 石と礫は地図の礫・崖錐・沢底の重みで置くので、地図が無ければ置かない
            data.m_GravelScatter.Initialize(data.m_pHeightField, surfaceMask);
            data.m_FoliageScatter.Initialize(data.m_pHeightField, std::move(surfaceMask));
            if ((data.m_HideMask & WastelandHide::ReferencePlantOnly) != 0)
            {
                PlaceReferencePlant(data);
            }
            ApplyFoliageExclusions(data, nullptr);
            data.m_Foliage.Wind = WastelandFoliage::MakeWind();
            UpdateFoliage(ctx, data, pose.Position[0], pose.Position[2], true);
        }

        // 手本の写真の構図の視点では、太陽を写真の時刻の推定に替える（共通の照明なら、その定義の視点ごとの向き）
        const WastelandReferenceShot *referenceShot = FindReferenceShot(data.m_ViewName);
        ApplyViewEnvironment(ctx, data);

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
            assembly->TerrainExport.Directory = data.m_TerrainExportDir;
            assembly->bTightSunShadow = !data.m_bNightVillage;
            // 灰色の材質の値は、共通の照明の Enter が読んだ CommonLight.json の gray_material から引く
            if (data.m_CommonLight.Opts.bGrayMaterial && data.m_CommonLight.bLoaded)
            {
                assembly->bGrayMaterial = true;
                assembly->GrayAlbedo = data.m_CommonLight.Def.GrayMaterialAlbedo;
                assembly->GrayRoughness = data.m_CommonLight.Def.GrayMaterialRoughness;
            }
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
                const bool bSurfaceMask =
                    Terrain::LoadTerrainSurfaceMaskLayout(TerrainSpec::kSurfaceMaskDescriptorPath, maskLayout, maskError);
                if (bSurfaceMask)
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
                const bool bFarMap = Terrain::LoadTerrainFarMapLayout(TerrainSpec::kFarMapDescriptorPath, farLayout, farError);
                if (bFarMap)
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
                LogTerrainMaterial(blend, bSurfaceMask, bFarMap);
                // 岩・崖・建物。地形の上に据えるので、地形を置けるときだけ読む
                StartPropLoads(ctx, assetSystem, assembly, data.m_bNightVillage);
                // 崖の面のスキャンの塊（配置表）。荒野だけで、岩を外す撮影（--wasteland-hide=rocks・all）では読まない
                if (!data.m_bNightVillage && (data.m_HideMask & WastelandHide::Rocks) == 0)
                {
                    StartCliffScanLoads(ctx, assetSystem, assembly);
                }
                data.m_pAssembly = assembly;
            }
        }

        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        data.m_Rig.SetScriptCapture(data.m_pScriptCapture);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("wasteland: カメラを作れませんでした");
            if (data.m_pAssembly)
            {
                data.m_pAssembly->bCancelled = true;
                data.m_pAssembly.reset();
            }
            data.m_pHeightField.reset();
            WastelandCommonLight::Leave(ctx, data.m_CommonLight);
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
        if (data.m_bNightVillage)
        {
            // 夜は自動露出（補正の曲線はひな形の AutoExposureCurve）
            camera->SetExposureMode(CameraExposureMode::Auto);
            camera->SetExposureCompensation(0.0f);
        }
        else
        {
            camera->SetExposureCompensation(kCameraOutputCompensationEV + kDayLookExposureOffsetEV);
        }
        // 昼の荒野の既定は TAA（遠景の崖の細部とエイリアスを 2 倍の解像度の正解と比べて決めた。
        // Docs/RenderingValidation/WastelandFarBlurMotion.md）。夜の村の既定は FXAA のまま
        const bool bTemporalAA = data.m_AntiAliasing == WastelandAntiAliasing::TemporalAA ||
                                 (data.m_AntiAliasing == WastelandAntiAliasing::Default && !data.m_bNightVillage);
        camera->SetAntiAliasingMode(bTemporalAA ? CameraAntiAliasingMode::TemporalAA : CameraAntiAliasingMode::FXAA);
        // 遠景のぼけの切り分けの撮影の内部解像度（--wasteland-render-scale）
        if (data.m_RenderScale < 1.0f)
        {
            ctx.EngineRef.GetRenderWorld().SetRenderScale(data.m_RenderScale);
        }
        if (data.m_AntiAliasing != WastelandAntiAliasing::Default || data.m_RenderScale < 1.0f)
        {
            static constexpr const char *kAntiAliasingNames[] = {"default", "fxaa", "taa", "none"};
            LOG_INFO_F("WASTELAND_BLUR_CASE aa=%s render_scale=%.3f",
                       kAntiAliasingNames[static_cast<uint32_t>(data.m_AntiAliasing)],
                       static_cast<double>(data.m_RenderScale));
        }
        // 日中のトーンマップとグレーディング（このシーンのカメラだけ。ほかのシーンのカメラは各自のひな形を使う）
        CameraProxy &proxyTemplate = data.m_Rig.ProxyTemplate();
        proxyTemplate.GradingOverride.bEnabled = true;
        if (data.m_bNightVillage)
        {
            // 夜は暗部を縮めない NeutralLinear
            proxyTemplate.ToneMapCurve = CameraToneMapCurve::NeutralLinear;
            // 夜: 測光が暗いほど暗く保つ自動露出の補正と、彩度を落とした夜のグレーディング
            proxyTemplate.AutoExposureCurve.bEnabled = true;
            proxyTemplate.AutoExposureCurve.DarkEV100 = kNightDarkSceneEV100;
            proxyTemplate.AutoExposureCurve.DarkCompensation = kNightDarkSceneCompensationEV;
            proxyTemplate.AutoExposureCurve.BrightEV100 = kNightBrightSceneEV100;
            proxyTemplate.AutoExposureCurve.BrightCompensation = 0.0f;
            proxyTemplate.GradingOverride.Contrast = kNightGradingContrast;
            proxyTemplate.GradingOverride.ContrastPivot = kNightGradingContrastPivot;
            proxyTemplate.GradingOverride.Saturation = kNightGradingSaturation;
            proxyTemplate.GradingOverride.Temperature = kNightGradingTemperature;
        }
        else
        {
            // 昼は View の既定の演算子（ACES Filmic）。--tone-map で View の演算子を替えた撮影ではそちらになる
            proxyTemplate.ToneMapCurve = CameraToneMapCurve::ViewDefault;
            proxyTemplate.GradingOverride.Contrast = kGradingContrast;
            proxyTemplate.GradingOverride.ContrastPivot = kGradingContrastPivot;
            proxyTemplate.GradingOverride.Saturation = kGradingSaturation;
            proxyTemplate.GradingOverride.Temperature = kGradingTemperature;
        }
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
        // 夜の集落の松明と焚き火の揺らぎ
        WastelandNightLights::Tick(data.m_NightLights, deltaTime);
        // 草・低木: カメラが区画を跨いだら、近づいた区画を作り、離れた区画を捨てる
        {
            const NorvesLib::Math::Vector3 cameraPosition = data.m_Rig.GetController().GetPosition();
            UpdateFoliage(ctx, data, cameraPosition.x, cameraPosition.z, false);
        }

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
                    PlaceProps(ctx, *data.m_pHeightField, data.m_pSurfaceMask.get(), assembly, data.m_HideMask,
                               data.m_bNightVillage, &data.m_NightLights);
                    assembly.bPropsPlaced = true;
                    // 置いた建物の足元と道から草・低木を外す（区画を作り直す）
                    if (!assembly.FoliageBuildingCircles.empty() || !assembly.FoliageRoadRects.empty())
                    {
                        ApplyFoliageExclusions(data, &assembly);
                        const NorvesLib::Math::Vector3 cameraPosition = data.m_Rig.GetController().GetPosition();
                        UpdateFoliage(ctx, data, cameraPosition.x, cameraPosition.z, false);
                    }
                }
                // 崖錐の岩の行を作り終えるまで待つ（行が揃う前に撮影を始めない）
                if (assembly.bPlaced && assembly.bPropsPlaced &&
                    !ctx.WorldRef.GetEcsWorld().GetMassWork().HasPendingWork())
                {
                    RestoreMassBudget(ctx, assembly);
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
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, SceneIdOf(data), bReady);
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
            RestoreMassBudget(ctx, *data.m_pAssembly);
            data.m_pAssembly->bCancelled = true;
            data.m_pAssembly.reset();
        }
        data.m_pHeightField.reset();
        data.m_pSurfaceMask.reset();
        data.m_Foliage = NorvesLib::Core::Rendering::FoliageScene{};
        data.m_FoliageScatter.Reset();
        data.m_ReferencePlant = NorvesLib::Core::Rendering::FoliageBatch{};
        data.m_GravelBatches.clear();
        data.m_GravelScatter.Reset();
        data.m_Rig.Leave(ctx);
        // 地形の Entity・モデル・カメラの実体は GameModeScope::Cleanup が返す。空と霧は RenderWorld の設定なので戻す。
        WastelandCommonLight::Leave(ctx, data.m_CommonLight);
        // 月明かりと灯りの実体はスコープが返す
        data.m_pMoon = nullptr;
        WastelandNightLights::Leave(data.m_NightLights);
        ResetEnvironment(ctx);
        if (data.m_RenderScale < 1.0f)
        {
            ctx.EngineRef.GetRenderWorld().SetRenderScale(1.0f);
        }
        LOG_INFO_F("%s を終了します", SceneIdOf(data));
    }

} // namespace Game::GameModes
