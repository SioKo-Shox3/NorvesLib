#pragma once

namespace Game::GameModes
{
    // Rendering3DTest のデバッグ描画(テストAABB)を毎フレーム投入する。
    // 実装(.cpp)は NorvesEngine.h を include するが、namespace Resource を使う Routine TU
    // (GLTFAnalyzer.h)へ class Resource を持ち込むと C2757 衝突するため、この呼び出しを別TUに分離する。
    void SubmitRendering3DTestDebugDraw();
} // namespace Game::GameModes
