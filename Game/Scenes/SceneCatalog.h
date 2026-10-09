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

#include "GameModes/GameModeIds.h"

#include <cstddef>
#include <cstdint>

namespace Game::Scenes
{

    /// 名前付き視点（--scene-view=<name> で選ぶ）。Rendering3DTest は SpringArm の角度と腕の長さで表す。
    struct SceneViewDescriptor
    {
        /// 視点の名前（機械が照合する識別子）
        const char *Name;
        /// 起動時のカメラを替えるか。false なら、そのシーンの既定のカメラのまま
        bool bHasCamera;
        /// SpringArm の方位角[度]
        float Yaw;
        /// SpringArm の仰角[度]
        float Pitch;
        /// SpringArm の腕の長さ[m]
        float ArmLength;
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
    };

    inline constexpr SceneViewDescriptor kRendering3DTestViews[] = {
        // 起動時の既定のカメラ
        {"default", false, 0.0f, 0.0f, 0.0f},
        // 球（中心 y=0.5・半径1）へ寄り、輪郭が画面の中央の周りに来る視点
        {"near", true, 0.0f, 5.0f, 2.5f},
        // カメラの高さ約 -0.84（地面は y=-1）から地面すれすれに見る視点
        {"low", true, 20.0f, -8.0f, 6.0f},
    };

    inline constexpr const char *kRendering3DTestRequiredAssets[] = {
        "Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf",
    };

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
        },
    };

    inline constexpr std::size_t kSceneCatalogCount = sizeof(kSceneCatalog) / sizeof(kSceneCatalog[0]);

    /// 引数なしで起動したときに入るシーンの id
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

    /// シーンの名前付き視点を探す。無ければ nullptr
    inline constexpr const SceneViewDescriptor *FindSceneView(const SceneDescriptor &scene, const char *name)
    {
        for (std::size_t index = 0; index < scene.ViewCount; ++index)
        {
            if (SceneTextEquals(scene.Views[index].Name, name))
            {
                return &scene.Views[index];
            }
        }
        return nullptr;
    }

} // namespace Game::Scenes
