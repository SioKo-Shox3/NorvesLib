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

    /// 確認用の sandbox の id。シーンの一覧に載るのは開発ビルドだけ
    inline constexpr const char *kSandboxSceneId = "sandbox";

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
