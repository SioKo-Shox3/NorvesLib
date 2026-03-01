#pragma once

#include "Core/Public/GameMode/TGameMode.h"
#include "Rendering3DTestRoutine.h"
#include "Rendering3DTestData.h"

namespace Game::GameModes
{

    /**
     * @brief 3Dレンダリングテスト用ゲームモード
     *
     * TGameModeテンプレートを使用して、ロジックとデータを分離した
     * 3Dレンダリングテスト用のゲームモードを定義します。
     *
     * Object→MeshComponent→MeshProxy→GBufferPassの自動描画パイプラインを
     * テストするためのモードです。
     */
    using Rendering3DTestMode = NorvesLib::Core::GameMode::TGameMode<Rendering3DTestRoutine, Rendering3DTestData>;

} // namespace Game::GameModes
