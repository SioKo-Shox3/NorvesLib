#pragma once

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}

namespace Game::GameModes
{
    struct Rendering3DTestData;

    /**
     * @brief 検証用の骨付きのパネルを、石畳の材質で地面の上へ 1 枚置く（--startup-skinned-probe）。
     *
     * 起動画面にはスキニングの物が無いので、ビジビリティバッファの ID のラスタ・幾何の解決がスキニングの塊を
     * 描けること（VIS_RASTER の skinned_chunks が 0 でない）と、GBuffer の経路との Albedo・Normal・Velocity の
     * 差を、Game の撮影の入口（Scripts/CaptureStartupScene.ps1 -SkinnedProbe）で確かめるために使う。
     * 既定は置かない（起動画面は変えない）。
     *
     * 骨 1 本が Y 軸まわりに ±35° 揺れる（周期 2 秒・ループ）。パネルは 8x8 の格子で、表と裏の両方を張る。
     * @return 置けたら true。資源を作れない・材質が無いときは false（ログを出し、何も置かない）
     */
    bool SpawnStartupSkinnedProbe(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data);

    /**
     * @brief 骨付きのパネルの姿勢を、再生ではなく時刻から決める（毎 Tick 呼ぶ。置いていなければ何もしない）。
     *
     * 決定的な撮影（--capture-deterministic）では、読み込み完了の時点から数えた固定刻みの時間を使う。
     * 同じコードを 2 回撮ると、またビジビリティバッファの on・off で撮り比べても、同じ時刻の同じ姿勢になる。
     */
    void UpdateStartupSkinnedProbe(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data);
} // namespace Game::GameModes
