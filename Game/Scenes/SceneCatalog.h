#pragma once

// ---------------------------------------------------------------------------
// シーンの一覧（Game が見せるシーンの定義を1か所に集める）
//
// 起動引数 --scene=<id>・--scene-view=<name>、ランチャーのカード、撮影スクリプト
// （Scripts/CaptureScene.ps1）が、ここの定義を共通の参照元にする。
// シーンを足すときは kSceneCatalog に1件足し、GameModeIds.h に GameMode の ID を、
// GameApplicationHandler の CreateGameModeStateMachine に GameMode の登録を足す。
// 文字列は UTF-8 の固定の文字列で、実行中に書き換えない。
// ---------------------------------------------------------------------------

#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Input/CameraPath.h"
#include "GameModes/GameModeIds.h"

#include <cstddef>
#include <cstdint>

namespace Game::Scenes
{

    /// 名前付き視点の表し方
    enum class SceneViewKind
    {
        /// 起動時のカメラを替えない（そのシーンの既定のカメラのまま）
        SceneDefault,
        /// SpringArm の角度と腕の長さ（Rendering3DTest の Maya 式の軌道カメラ）
        SpringArm,
        /// 自由飛行カメラの位置と向き
        FreePose,
    };

    /// 名前付き視点（--scene-view=<name> で選ぶ）
    struct SceneViewDescriptor
    {
        /// 視点の名前（機械が照合する識別子）
        const char *Name;
        SceneViewKind Kind;
        /// SpringArm の方位角[度]、または自由飛行カメラの Yaw[度]（0 度は +Z 向き、増えると画面の右へ回る）
        float Yaw;
        /// SpringArm の仰角[度]、または自由飛行カメラの Pitch[度]（正で上向き）
        float Pitch;
        /// SpringArm の腕の長さ[m]（SpringArm の視点だけ）
        float ArmLength;
        /// 自由飛行カメラの位置[m]（FreePose の視点だけ）
        float Position[3];
    };

    /// 自由飛行カメラの初期の視点
    struct FreeCameraPose
    {
        float Position[3];
        float Yaw;
        float Pitch;
    };

    /// シーンのカメラパス（デモ飛行）。位置と注視点のキーを時刻に沿って滑らかにつなぐ（Input/CameraPath.h）
    struct ScenePathDescriptor
    {
        const NorvesLib::Core::Input::CameraPathKey *Keys;
        std::size_t KeyCount;
        /// 一周して繰り返すか。真のとき最初のキーの時刻は 0
        bool bLoop;
        /// bLoop のときの周期[秒]。最後のキーの時刻より大きい値
        float LoopSeconds;
    };

    /// シーン1件の定義
    struct SceneDescriptor
    {
        /// --scene=<id> で指定する識別子（機械が照合する文字列。小文字・数字・ハイフン）
        const char *Id;
        /// ランチャーに出す日本語の表示名
        const char *DisplayName;
        /// ランチャーのカードに出す日本語の説明
        const char *Description;
        /// ランチャーのカードの見本画像（リポジトリのルートからの相対パス。640×360 程度の PNG）
        const char *ThumbnailPath;
        /// このシーンを担当する GameMode
        NorvesLib::Core::GameMode::GameModeId Mode;
        /// 名前付き視点の並び。先頭が既定の視点
        const SceneViewDescriptor *Views;
        std::size_t ViewCount;
        /// 読み込めないとシーンが成り立たない資産（リポジトリのルートからの相対パス）
        const char *const *RequiredAssets;
        std::size_t RequiredAssetCount;
        /// 必要な資産が欠けているときにランチャーのカードへ出す取得のコマンド（リポジトリのルートから実行する）。
        /// 資産がリポジトリに入っているシーンは nullptr
        const char *FetchCommand;
        /// カメラパス（P キー・ゲームパッドの Y で再生する）。持たないシーンは nullptr
        const ScenePathDescriptor *CameraPath;
    };

    inline constexpr SceneViewDescriptor kRendering3DTestViews[] = {
        // 起動時の既定のカメラ
        {"overview", SceneViewKind::SceneDefault, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}},
        // 球（中心 y=0.5・半径1）へ寄り、輪郭が画面の中央の周りに来る視点
        {"near", SceneViewKind::SpringArm, 0.0f, 5.0f, 2.5f, {0.0f, 0.0f, 0.0f}},
        // カメラの高さ約 -0.84（地面は y=-1）から地面すれすれに見る視点
        {"low", SceneViewKind::SpringArm, 20.0f, -8.0f, 6.0f, {0.0f, 0.0f, 0.0f}},
    };

    inline constexpr const char *kRendering3DTestRequiredAssets[] = {
        "Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf",
    };

    /// バナナのシーンの視点。床に敷いたバナナの全体・手前の山・床すれすれ（座標は +Z 向きが Yaw 0）
    inline constexpr SceneViewDescriptor kMegaBananasViews[] = {
        // 床の手前の上から、敷き詰めたバナナと手前の山を見渡す視点。起動時の既定
        {"overview", SceneViewKind::FreePose, 0.0f, -17.0f, 0.0f, {0.0f, 2.6f, -5.5f}},
        // 手前の山の斜面へ数十 cm まで寄る視点
        {"close", SceneViewKind::FreePose, 0.0f, -14.0f, 0.0f, {0.3f, 0.45f, 2.0f}},
        // 床すれすれから、並んだバナナと山を見る視点
        {"low", SceneViewKind::FreePose, 8.0f, 2.0f, 0.0f, {-0.5f, 0.1f, -0.5f}},
        // 目の高さ（1.7 m）から、敷き詰めたバナナの先の地平線を見る視点。10 万本級の規模で、遠くまで並ぶ本数を見る
        {"horizon", SceneViewKind::FreePose, 0.0f, 0.0f, 0.0f, {0.0f, 1.7f, -15.0f}},
    };

    // 手前の山（原点の前方 3 m）を囲む一周のデモ飛行。位置は山の中心から 6 m の円（高さ 1.6〜2.2 m）を 2.5 秒ごとに
    // 45° ずつ進み（周期 20 秒）、注視点は山の中心の周り 1.2 m の円（高さ 0.3 m）を、位置より 90° 先の向きで動く。
    inline constexpr NorvesLib::Core::Input::CameraPathKey kMegaBananasPathKeys[] = {
        {0.0f, {0.0000f, 1.6f, -3.0000f}, {1.2000f, 0.3f, 3.0000f}},
        {2.5f, {4.2426f, 2.2f, -1.2426f}, {0.8485f, 0.3f, 3.8485f}},
        {5.0f, {6.0000f, 1.6f, 3.0000f}, {0.0000f, 0.3f, 4.2000f}},
        {7.5f, {4.2426f, 2.2f, 7.2426f}, {-0.8485f, 0.3f, 3.8485f}},
        {10.0f, {0.0000f, 1.6f, 9.0000f}, {-1.2000f, 0.3f, 3.0000f}},
        {12.5f, {-4.2426f, 2.2f, 7.2426f}, {-0.8485f, 0.3f, 2.1515f}},
        {15.0f, {-6.0000f, 1.6f, 3.0000f}, {0.0000f, 0.3f, 1.8000f}},
        {17.5f, {-4.2426f, 2.2f, -1.2426f}, {0.8485f, 0.3f, 2.1515f}},
    };

    inline constexpr ScenePathDescriptor kMegaBananasPath = {
        kMegaBananasPathKeys,
        sizeof(kMegaBananasPathKeys) / sizeof(kMegaBananasPathKeys[0]),
        true,
        20.0f,
    };

    /// バナナのシーンの id と、読み込めないとシーンが成り立たない資産（生成した GLB。ComfyUI で作る）
    inline constexpr const char *kMegaBananasSceneId = "mega-bananas";
    inline constexpr const char *kMegaBananasRequiredAssets[] = {
        "Assets/Models/Generated/banana/banana.glb",
    };

    /// 荒野のシーンの視点。地形は Valley of the Gods の DEM を切り出した 4.7 × 4.6 km の格子（X -2600〜2099・Z -2499〜2100）で、
    /// 東が +X・北が -Z（座標は +Z 向きが Yaw 0。Yaw 90 が西、-90 が東）。高さ 0 は前哨地の候補 A（X 1199.5・Z 150.5）の床。
    /// 主役のビュートの頂は Battleship Rock（X -1420・Z -949、高さ 199 m）・Franklin Butte（X -510・Z -319、128 m）・
    /// Rooster Butte（X 890・Z 831、123 m）・Setting Hen Butte（X 1290・Z 1381、99 m）。
    /// 位置の y は、その (x,z) の地形の高さ（Rendering/Terrain/TerrainHeightField.h の格子）からの高さ[m]
    inline constexpr SceneViewDescriptor kWastelandViews[] = {
        // 南の谷床の上空 80 m から、北の Franklin Butte（1.4 km）と奥の Battleship Rock（2.3 km）と、その間の谷床を見渡す視点。
        // 太陽（西南西）は左から当たる。起動時の既定
        {"overview", SceneViewKind::FreePose, 170.0f, -3.0f, 0.0f, {-600.0f, 80.0f, 1100.0f}},
        // Franklin Butte の南南東 590 m の谷床の人の目の高さ（1.7 m）から、北北西の Franklin Butte と奥の Battleship Rock を見上げる視点
        {"ground", SceneViewKind::FreePose, 153.0f, 4.0f, 0.0f, {-300.0f, 1.7f, 250.0f}},
        // Battleship Rock の頂（高さ 199 m）から、南東の谷床と Franklin・Rooster・Setting Hen の各ビュートを見下ろす視点
        {"ridge", SceneViewKind::FreePose, -50.0f, -7.0f, 0.0f, {-1400.0f, 2.0f, -940.0f}},
        // Battleship Rock の南西面の崖を、南西 500 m の崖錐の裾の目の高さ（2 m）から北東向きに見上げる視点。
        // 太陽は西南西にあるので、崖の南西面は順光になる
        {"cliff", SceneViewKind::FreePose, -135.0f, 12.0f, 0.0f, {-1900.0f, 2.0f, -500.0f}},
        // 前哨地（中心 X 1199.5・Z 150.5。Rendering/Terrain/TerrainHeightField.h の WastelandTerrain::kOutpost*）の北の入り口の
        // 道の西の縁の人の目の高さから、南へ延びる道の両側の建物と南の端の門、その奥の Rooster Butte（南南西 790 m）と
        // Setting Hen Butte（南 1.3 km）を見る視点
        {"outpost", SceneViewKind::FreePose, 8.0f, 2.0f, 0.0f, {1197.0f, 1.7f, 100.5f}},
        // 草・低木の一角（Game/GameModes/Wasteland/WastelandFoliage.cpp。谷床の (250, 1650) の 120 m 四方）の南南東の端の目の高さから、
        // 北北西（Franklin Butte の方）へ枯れ草とブラックブラッシュ・モルモンティーの低木の群れを見下ろす視点
        {"grass", SceneViewKind::FreePose, 159.0f, -6.0f, 0.0f, {266.0f, 1.6f, 1692.0f}},
        // grass と同じ構図で、草・低木の風の時計だけを 1.4 秒ずらした 2 枚目（揺れを比べる）
        {"grass_gust", SceneViewKind::FreePose, 159.0f, -6.0f, 0.0f, {266.0f, 1.6f, 1692.0f}},
        // 以下は手本の写真（Assets/Reference/ValleyOfTheGods、Docs/RenderingValidation/WastelandReference.md）の構図に寄せた視点。
        // 位置・向きは写真の GPS と写った地形の方位からの推定で、写真と同じではない。画角と太陽の時刻は荒野のモードが
        // 視点ごとに替える（WastelandRoutine.cpp の kReferenceShots）。比べるのは Scripts/Terrain/CompareReference.py
        // far_01: 写真の GPS（Battleship Rock の北東 700 m の谷床）の南東 85 m から、北北西（方位 328.6°）2.7 km 先の Castle Butte と
        // 背後の Cedar Mesa の縁を見る。位置・向き・画角は遠景の地形（DEM 4 枚）の稜線と写真の輪郭を照合して決めた
        // （Scripts/Terrain/FitReferenceView.py。城の頂・2 本の尖塔・両脇の崖の段が重なる）
        {"ref_far_01", SceneViewKind::FreePose, 148.6f, 3.5f, 0.0f, {-903.5f, 1.7f, -1608.5f}},
        // far_02: Setting Hen Butte の南南西 680 m の谷床から北（方位 5°）の Rooster Butte と Setting Hen Butte を見る
        // （写真の GPS の位置では 2 つのビュートの左右が写真と逆になるので、左右の並びと見かけの高さから南へ置いた）
        {"ref_far_02", SceneViewKind::FreePose, 185.0f, 1.0f, 0.0f, {1099.5f, 1.7f, 2000.5f}},
        // cliff_01: Battleship Rock の頂の西北西 1.35 km、DEM の西の端の谷床から東南東（方位 118.5°）を見上げる。位置・向き・画角は
        // DEM の稜線と写真の輪郭を照合して決めた（Scripts/Terrain/FitReferenceView.py。輪郭が最も合うのは DEM の 150 m 外なので、端に置いた）
        {"ref_cliff_01", SceneViewKind::FreePose, -61.5f, 7.9f, 0.0f, {-2595.0f, 1.7f, -1550.0f}},
        // cliff_02: 写真の元のパノラマの GPS の東北東 290 m の高まり（GPS の位置では手前の起伏が Franklin Butte を隠す）から、
        // 南東（方位 146°）、2.6 km 先の Franklin Butte を望遠で見る
        {"ref_cliff_02", SceneViewKind::FreePose, -34.3f, 0.0f, 0.0f, {-1966.5f, 1.7f, -2454.5f}},
        // ground_01: 撮影位置の記録が無い。南東の平らな谷床の目の高さから西を広角で見る、構図の型だけを合わせた視点
        {"ref_ground_01", SceneViewKind::FreePose, 90.0f, -2.0f, 0.0f, {300.0f, 1.5f, 1800.0f}},
        // ground_02: 写真の GPS（Battleship Rock の北 940 m の沢の中）から南東（方位 148°）の Franklin Butte と Rooster Butte を見る
        {"ref_ground_02", SceneViewKind::FreePose, -32.4f, 1.4f, 0.0f, {-1292.5f, 1.7f, -1894.5f}},
        // 以下は低木 1 株を比べる撮影（--wasteland-hide=all-but-plant。Scripts/Terrain/ReferenceCheck.py --plant）の視点。
        // 株は南東の平らな谷床の (360, 1965)（Game/GameModes/Wasteland/WastelandFoliage.h の kReferencePlantX・Z）。
        // 向き（Yaw 124°。西北西）は、共通の照明の既定の太陽（仰角 58.5°、西南西）の水平の向きから 60° 離れ、上から日の当たる
        // 樹冠と手前の陰の面、太陽を背にした葉、株の東北東の足元へ落ちる影が 1 枚に入る
        // plant_near: 株の 2.5 m 手前の高さ 1.5 m から株を見下ろす。画の上の端を地平線より 5° 下にして、遠くの地形を入れない
        // （最も細かい段の地形の撮影で、地平線まで入れるとジオメトリのページが 10 分で常駐しきらない）
        {"plant_near", SceneViewKind::FreePose, 124.0f, -35.0f, 0.0f, {362.1f, 1.5f, 1966.4f}},
        // plant_far: 同じ向きで株の 30 m 手前の目の高さから見る（遠くでの見え方）
        {"plant_far", SceneViewKind::FreePose, 124.0f, -2.5f, 0.0f, {384.9f, 1.7f, 1981.8f}},
        // cloud_sky: 雲の書き出し（--cloud-dump。Scripts/Terrain/ReferenceCheck.py --cloud-dump・--cloud）の空だけを写す視点。
        // 南東の平らな谷床の目の高さから、南南西を仰角 45° で見上げる（既定の 60° の画角の下の端が仰角 15° で、地形が入らない）。
        // 共通の照明の既定の太陽（仰角 58.5°、西南西）は画の右上の外の近くにあり、太陽の側の雲の縁と太陽を背にした雲が 1 枚に入る
        {"cloud_sky", SceneViewKind::FreePose, 14.0f, 45.0f, 0.0f, {300.0f, 1.5f, 1800.0f}},
    };

    /// 荒野のシーンの id。地形は DEM の格子からクックで焼く（格子と区画は Scripts/Terrain と Scripts/CookAssets.ps1 で作る）
    inline constexpr const char *kWastelandSceneId = "wasteland";

    /// 夜の集落のシーンの視点。座標は荒野と同じ DEM の格子（東が +X・北が -Z。Yaw 90 が西、-90 が東）。集落は前哨地の候補 B
    /// （X 1049.5・Z -449.5。Game/GameModes/Wasteland/WastelandLayout.h の kNightVillageCenter*）の東西の道沿いで、西 1.6 km に
    /// Franklin Butte、西北西 2.5 km に Battleship Rock がある。月は南南東の 35°。位置の y は、その (x,z) の地形の高さからの高さ[m]
    inline constexpr SceneViewDescriptor kNightVillageViews[] = {
        // 集落の東南東の上空 16 m から、西北西へ道と両側の建物、その奥の Franklin Butte と Battleship Rock の影を見渡す視点。起動時の既定
        {"overview", SceneViewKind::FreePose, 110.0f, -11.0f, 0.0f, {1104.5f, 16.0f, -427.5f}},
        // 道の東の端寄りの道の上の目の高さ（1.7 m）から、西へ道沿いの建物と街灯、中ほどの広場を見る視点
        {"street", SceneViewKind::FreePose, 90.0f, 1.5f, 0.0f, {1087.5f, 1.7f, -448.5f}},
        // 焚き火の広場の南の縁の目の高さ（焚き火から約 7 m）から、北へ焚き火と井戸と松明の柱、その奥の道の北側の建物を見る視点
        {"square", SceneViewKind::FreePose, -172.0f, -8.0f, 0.0f, {1046.5f, 1.7f, -441.0f}},
    };

    /// 夜の集落のシーンの id と、読み込めないとシーンが成り立たない資産（生成した建物と小物の GLB。ComfyUI で作る）
    inline constexpr const char *kNightVillageSceneId = "night-village";
    inline constexpr const char *kNightVillageRequiredAssets[] = {
        "Assets/Models/Generated/adobe_ruin/adobe_ruin.glb",
        "Assets/Models/Generated/wooden_shack/wooden_shack.glb",
        "Assets/Models/Generated/street_lamp/street_lamp.glb",
        "Assets/Models/Generated/campfire_ring/campfire_ring.glb",
    };

    /// 材質ギャラリーの視点。展示場は石の床の 70 × 50 m（中心 x -4・z 10）で、台座の列は z 0・5・10・15 に 1 列 10〜11 基、
    /// 画面の左（+X）から右（-X）へ並び、どれも -Z（通路の側）を向く。金属度 × 粗さの球のひな壇は x -20 を中心に z 2〜7.5 にある。
    /// 座標は +Z 向きが Yaw 0（Yaw 90 が -X 向き）。位置の y は床からの高さ[m]
    inline constexpr SceneViewDescriptor kMaterialGalleryViews[] = {
        // 展示場の手前の右寄りの高さ 9 m から、台座の 4 列と奥のひな壇を斜めに見渡す視点。起動時の既定
        {"overview", SceneViewKind::FreePose, 42.0f, -17.0f, 0.0f, {14.0f, 9.0f, -12.0f}},
        // 1 列目の通路の目の高さから、中央の台座 5 基の板・球と名札を見る視点
        {"textures", SceneViewKind::FreePose, 0.0f, -11.0f, 0.0f, {0.0f, 2.0f, -5.5f}},
        // ひな壇の正面から、金属度 × 粗さの 6 × 6 の球を見る視点
        {"spheres", SceneViewKind::FreePose, 0.0f, -8.0f, 0.0f, {-20.0f, 2.6f, -4.5f}},
        // 2 列目の castle_brick_01 の板へ 1.1 m まで寄り、斜めから視差オクルージョンの凹凸を見る視点
        {"close", SceneViewKind::FreePose, 42.5f, -27.4f, 0.0f, {10.65f, 1.62f, 4.24f}},
    };
    /// 材質ギャラリーのシーンの id と、読み込めないとシーンが成り立たない資産（展示場の床。Poly Haven の CC0 を落とす）
    inline constexpr const char *kMaterialGallerySceneId = "material-gallery";
    inline constexpr const char *kMaterialGalleryRequiredAssets[] = {
        "Assets/Textures/PolyHaven/stone_floor/stone_floor_diff_4k.jpg",
        "Assets/Textures/PolyHaven/castle_brick_01/castle_brick_01_disp_4k.jpg",
    };
    /// 確認用の sandbox の id。シーンの一覧に載るのは開発ビルドだけ
    inline constexpr const char *kSandboxSceneId = "sandbox";

    /// 負荷シーン mega-stress の id。シーンの一覧に載るのは開発ビルドだけ
    inline constexpr const char *kMegaStressSceneId = "mega-stress";

#if NORVES_BUILD_DEVELOPMENT
    inline constexpr SceneViewDescriptor kSandboxViews[] = {
        // 地面の手前から奥の地平線まで見渡す視点。起動時の既定
        {"overview", SceneViewKind::FreePose, 0.0f, -15.0f, 0.0f, {0.0f, 4.0f, -12.0f}},
        // 地面すれすれから斜めに見る視点
        {"low", SceneViewKind::FreePose, 35.0f, 3.0f, 0.0f, {0.0f, 0.6f, -5.0f}},
        // 真上から地面の格子を見下ろす視点（画面の上が +Z、右が -X）
        {"top", SceneViewKind::FreePose, 0.0f, -89.0f, 0.0f, {0.0f, 24.0f, 0.0f}},
    };

    // 地面を囲む一周のデモ飛行。位置は原点から 16 m の円（高さ 4〜5 m）を 2.5 秒ごとに 45° ずつ進み（周期 20 秒）、
    // 注視点は原点の周り 4 m の円（高さ 1 m）を、位置より 90° 先の向きで動く。位置の角度 θ は -Z 向きを 0 とし、
    // 位置 = (16 sinθ, 高さ, -16 cosθ)、注視点 = (4 sin(θ+90°), 1, -4 cos(θ+90°))。
    inline constexpr NorvesLib::Core::Input::CameraPathKey kSandboxPathKeys[] = {
        {0.0f, {0.0f, 4.0f, -16.0f}, {4.0f, 1.0f, 0.0f}},
        {2.5f, {11.3137f, 5.0f, -11.3137f}, {2.8284f, 1.0f, 2.8284f}},
        {5.0f, {16.0f, 4.0f, 0.0f}, {0.0f, 1.0f, 4.0f}},
        {7.5f, {11.3137f, 5.0f, 11.3137f}, {-2.8284f, 1.0f, 2.8284f}},
        {10.0f, {0.0f, 4.0f, 16.0f}, {-4.0f, 1.0f, 0.0f}},
        {12.5f, {-11.3137f, 5.0f, 11.3137f}, {-2.8284f, 1.0f, -2.8284f}},
        {15.0f, {-16.0f, 4.0f, 0.0f}, {0.0f, 1.0f, -4.0f}},
        {17.5f, {-11.3137f, 5.0f, -11.3137f}, {2.8284f, 1.0f, -2.8284f}},
    };

    inline constexpr ScenePathDescriptor kSandboxPath = {
        kSandboxPathKeys,
        sizeof(kSandboxPathKeys) / sizeof(kSandboxPathKeys[0]),
        true,
        20.0f,
    };
#endif

#if NORVES_BUILD_DEVELOPMENT
    inline constexpr SceneViewDescriptor kMegaStressViews[] = {
        // 手前の端から、並んだ Entity の列を奥へ見渡す視点。起動時の既定
        {"overview", SceneViewKind::FreePose, 0.0f, -8.0f, 0.0f, {0.0f, 7.0f, 0.0f}},
        // 手前の列へ寄り、メッシュの種類を見分ける視点
        {"close", SceneViewKind::FreePose, 0.0f, -3.0f, 0.0f, {0.0f, 2.5f, 2.0f}},
        // 高い所から、列の広がりを見下ろす視点
        {"aerial", SceneViewKind::FreePose, 0.0f, -38.0f, 0.0f, {0.0f, 110.0f, -40.0f}},
    };

    /// 読み込めないとシーンが成り立たない資産（置くメッシュの一覧と、リポジトリに入っている代表のソース）
    inline constexpr const char *kMegaStressRequiredAssets[] = {
        "Assets/AssetSets/MegaStressMeshes.json",
        "Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf",
        "Assets/Models/Cottage_Clean/Cottage_Clean.gltf",
    };
#endif

    inline constexpr SceneDescriptor kSceneCatalog[] = {
        {
            "rendering3dtest",
            "描画テスト",
            "天球・地面・球・岩・小屋で、物理ベースの空と光、影、間接光を確かめる基準のシーン。",
            "Assets/Launcher/Thumbnails/rendering3dtest.png",
            Game::GameModes::Rendering3DTest,
            kRendering3DTestViews,
            sizeof(kRendering3DTestViews) / sizeof(kRendering3DTestViews[0]),
            kRendering3DTestRequiredAssets,
            sizeof(kRendering3DTestRequiredAssets) / sizeof(kRendering3DTestRequiredAssets[0]),
            nullptr,
            nullptr,
        },
        {
            kMegaBananasSceneId,
            "バナナの山",
            "生成した高ポリのバナナを、仮想化ジオメトリで床いっぱいに敷き詰めるシーン。近づくほど細かい形が出る。",
            "Assets/Launcher/Thumbnails/mega-bananas.png",
            Game::GameModes::MegaBananas,
            kMegaBananasViews,
            sizeof(kMegaBananasViews) / sizeof(kMegaBananasViews[0]),
            kMegaBananasRequiredAssets,
            sizeof(kMegaBananasRequiredAssets) / sizeof(kMegaBananasRequiredAssets[0]),
            "python Scripts/GenerateSceneModels.py --ids banana",
            &kMegaBananasPath,
        },
        {
            kWastelandSceneId,
            "荒野",
            "高さ場から焼いた約 2 km 四方の起伏のある地形（盆地・砂丘・メサ・稜線）を、仮想化ジオメトリで描くシーン。",
            "Assets/Launcher/Thumbnails/wasteland.png",
            Game::GameModes::Wasteland,
            kWastelandViews,
            sizeof(kWastelandViews) / sizeof(kWastelandViews[0]),
            nullptr,
            0,
            nullptr,
            nullptr,
        },
        {
            kNightVillageSceneId,
            "夜の集落",
            "荒野の谷床の一角に、生成した建物と小物で組んだ道沿いの集落。月明かりと夜空の下を、自動露出で夜の明るさに合わせて描く。",
            "Assets/Launcher/Thumbnails/night-village.png",
            Game::GameModes::NightVillage,
            kNightVillageViews,
            sizeof(kNightVillageViews) / sizeof(kNightVillageViews[0]),
            kNightVillageRequiredAssets,
            sizeof(kNightVillageRequiredAssets) / sizeof(kNightVillageRequiredAssets[0]),
            "python Scripts/GenerateSceneModels.py --ids adobe_ruin,wooden_shack,watchtower,water_tower,stone_arch,"
            "street_lamp,torch_post,campfire_ring,well,barrel,crate,cart",
            nullptr,
        },
        {
            kMaterialGallerySceneId,
            "材質ギャラリー",
            "石の床の展示場に、Poly Haven の地面・壁・木・金属・屋根と荒野の材質を、視差を付けた板と球で並べる。金属度 × 粗さの球の格子もある。",
            "Assets/Launcher/Thumbnails/material-gallery.png",
            Game::GameModes::MaterialGallery,
            kMaterialGalleryViews,
            sizeof(kMaterialGalleryViews) / sizeof(kMaterialGalleryViews[0]),
            kMaterialGalleryRequiredAssets,
            sizeof(kMaterialGalleryRequiredAssets) / sizeof(kMaterialGalleryRequiredAssets[0]),
            "powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/FetchPolyHavenTextures.ps1 -Set gallery",
            nullptr,
        },
#if NORVES_BUILD_DEVELOPMENT
        // 開発ビルドだけ。ランチャーの先頭のシーンにはしない（並びの最後に置く）
        {
            kSandboxSceneId,
            "サンドボックス",
            "空と平らな地面だけの確認用シーン。自由飛行カメラの動きを確かめる。開発ビルドだけ。",
            "Assets/Launcher/Thumbnails/sandbox.png",
            Game::GameModes::Sandbox,
            kSandboxViews,
            sizeof(kSandboxViews) / sizeof(kSandboxViews[0]),
            nullptr,
            0,
            nullptr,
            &kSandboxPath,
        },
        // 開発ビルドだけ。サンドボックスの次（並びの最後）に置く
        {
            kMegaStressSceneId,
            "負荷（仮想化ジオメトリ）",
            "起動画面の岩・小屋・球・スキャン資産と生成した建物・果物を、個別の Entity として床に並べる負荷シーン。--stress-entities=<n> で数を変える。開発ビルドだけ。",
            "Assets/Launcher/Thumbnails/mega-stress.png",
            Game::GameModes::MegaStress,
            kMegaStressViews,
            sizeof(kMegaStressViews) / sizeof(kMegaStressViews[0]),
            kMegaStressRequiredAssets,
            sizeof(kMegaStressRequiredAssets) / sizeof(kMegaStressRequiredAssets[0]),
            nullptr,
            nullptr,
        },
#endif
    };

    inline constexpr std::size_t kSceneCatalogCount = sizeof(kSceneCatalog) / sizeof(kSceneCatalog[0]);

    /// --scene=<id> で直接入る、カードの無い「シーン」の id。ランチャーそのもの（引数なしの起動で入る）
    inline constexpr const char *kLauncherSceneId = "launcher";

    /// ランチャーを使えないビルド（ImGui なし）と、--scene を省いたデバッグ用の引数（--animation-debug）で入るシーンの id
    inline constexpr const char *kDefaultSceneId = "rendering3dtest";

    /// 2つの C 文字列が等しいか（標準ライブラリの文字列型を使わない比較）
    inline constexpr bool SceneTextEquals(const char *a, const char *b)
    {
        if (a == nullptr || b == nullptr)
        {
            return false;
        }
        while (*a != '\0' && *a == *b)
        {
            ++a;
            ++b;
        }
        return *a == *b;
    }

    /// id のシーンを探す。無ければ nullptr
    inline constexpr const SceneDescriptor *FindScene(const char *id)
    {
        for (std::size_t index = 0; index < kSceneCatalogCount; ++index)
        {
            if (SceneTextEquals(kSceneCatalog[index].Id, id))
            {
                return &kSceneCatalog[index];
            }
        }
        return nullptr;
    }

    /// シーンの名前付き視点を探す。無ければ nullptr。
    /// "default" は先頭の視点（起動時の既定）の別名として受け付ける
    inline constexpr const SceneViewDescriptor *FindSceneView(const SceneDescriptor &scene, const char *name)
    {
        if (scene.ViewCount > 0 && SceneTextEquals("default", name))
        {
            return &scene.Views[0];
        }
        for (std::size_t index = 0; index < scene.ViewCount; ++index)
        {
            if (SceneTextEquals(scene.Views[index].Name, name))
            {
                return &scene.Views[index];
            }
        }
        return nullptr;
    }

    /// 自由飛行カメラの初期の視点を、視点の名前から求める。名前が空・未知・自由飛行の視点でないときは、
    /// そのシーンで最初の自由飛行の視点にする。自由飛行の視点がひとつも無ければ false
    inline bool ResolveFreeCameraPose(const SceneDescriptor &scene, const char *viewName, FreeCameraPose &outPose)
    {
        const SceneViewDescriptor *view = nullptr;
        if (viewName != nullptr && viewName[0] != '\0')
        {
            view = FindSceneView(scene, viewName);
        }
        if (view == nullptr || view->Kind != SceneViewKind::FreePose)
        {
            view = nullptr;
            for (std::size_t index = 0; index < scene.ViewCount; ++index)
            {
                if (scene.Views[index].Kind == SceneViewKind::FreePose)
                {
                    view = &scene.Views[index];
                    break;
                }
            }
        }
        if (view == nullptr)
        {
            return false;
        }
        outPose.Position[0] = view->Position[0];
        outPose.Position[1] = view->Position[1];
        outPose.Position[2] = view->Position[2];
        outPose.Yaw = view->Yaw;
        outPose.Pitch = view->Pitch;
        return true;
    }

} // namespace Game::Scenes
