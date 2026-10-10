#pragma once

// ---------------------------------------------------------------------------
// シーンの組み立ての完了（SceneReady）を、決定的な撮影（--capture-deterministic）へ伝える共通の窓口
//
// テクスチャ・モデルの読み込みは描画側が数えるが、読み込み後にシーンへ組み込む処理（球の生成・
// 地形の配置など）は GameMode が数える。どのシーンも次の形で使う。
//   Enter : BeginSceneAssembly を呼ぶ（組み立てが終わるまで撮影を待たせる）
//   Tick  : UpdateSceneAssembly を毎フレーム呼ぶ（組み立てが終わったら true を渡す）
//   Leave : EndSceneAssembly を呼ぶ（次のシーンを待たせない）
// 組み立てが終わった最初の1回だけ、ログに SCENE_READY を出す（撮影スクリプトが照合する）。続けて、そのときのゲームの
// フレーム番号を SCENE_READY_FRAME に出す（実時間の計測が、組み立ての後のフレームだけを数えるのに使う）。
// 準備の状態は決定的な撮影でないときも持つ（決定的な撮影のときだけ、エポックの開始を待たせる）。
// ---------------------------------------------------------------------------

#include "Core/Public/Engine/DeterministicCapture.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Logging/LogMacros.h"

namespace Game::Scenes
{

    /// シーンの入場時に呼ぶ。組み立てが終わるまで、決定的な撮影は読み込み中として扱う。
    inline void BeginSceneAssembly(NorvesLib::Core::Engine::Engine &engine)
    {
        engine.GetDeterministicCapture().SetSceneReady(false);
    }

    /// 毎フレーム呼ぶ。bAssembled が真になった最初の1回だけ SCENE_READY をログへ出す。
    inline void UpdateSceneAssembly(NorvesLib::Core::Engine::Engine &engine, const char *sceneId, bool bAssembled)
    {
        auto &capture = engine.GetDeterministicCapture();
        if (bAssembled && !capture.IsSceneReady())
        {
            LOG_INFO_F("SCENE_READY scene=%s", sceneId);
            LOG_INFO_F("SCENE_READY_FRAME scene=%s frame=%llu", sceneId,
                       static_cast<unsigned long long>(engine.GetFrameCount()));
        }
        capture.SetSceneReady(bAssembled);
    }

    /// シーンの退場時に呼ぶ。次のシーンの撮影を待たせないよう、組み立て済みに戻す。
    inline void EndSceneAssembly(NorvesLib::Core::Engine::Engine &engine)
    {
        engine.GetDeterministicCapture().SetSceneReady(true);
    }

} // namespace Game::Scenes
