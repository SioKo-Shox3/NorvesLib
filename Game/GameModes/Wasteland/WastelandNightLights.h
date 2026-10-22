#pragma once

// ---------------------------------------------------------------------------
// 夜の集落（night-village）の灯り
//
// 置いた小物と建物に合わせて、次を置く（位置は生成した GLB の形から測ったローカルの座標を、小物の配置で写す）:
//   - 点光源（暖色。光束[lm]で決める）: 街灯のランタンの中、松明の火かごの上、焚き火の薪の上。街灯と焚き火を影を落とす灯
//     として出し、影を描く 4 灯（PointShadowSnapshot.h の PointShadowMaxLights）はカメラに近い順に描画側が選ぶ（手前の
//     主役に影が付き、残りは影無し）。松明は影無し（火かごが足元に硬い円盤の影を落とすため）
//   - 発光体（ブルームでにじむ）: 街灯のランタンのガラスを覆う角錐台、松明と焚き火の炎の楕円体、日干し煉瓦の家の戸口の奥の
//     室内の灯り（戸口からだけ見える板）、同じ家の 4 面の壁の窓（壁の外に立てた 2 × 2 の枡のガラス）。どれも影を落とさず（ランタンのガラスや炎の中の点光源を遮らない）、半透明の経路で
//     描いてレイトレーシングのシーンに入れない（灯りの光は点光源が出すので、RTGI の光源として二重に数えない）
//   - 揺らぎ: 松明と焚き火の点光源の光束を、周波数の違う正弦の和で時間とともに揺らす（街灯は揺らさない）。光源の明るさの
//     小さな変化では RTGI の履歴を捨てない（RenderingCoordinator.cpp の LightRevisionIntensityStepRatio）ので、
//     照り返しは揺らぎの平均へ収束する
// 街灯の小物は影を落とさない（ガラスの面が中の点光源を閉じ込めるため。WastelandRoutine.cpp の PlaceProps）。
// 壁や地面への照り返しは RTGI が点光源を当たりの面で数えて出す（追加の設定は要らない）。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/VariableArray.h"
#include "GameModes/Wasteland/WastelandLayout.h"

#include <cstdint>

namespace NorvesLib::Core::Component
{
    class PointLightComponent;
}

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}

namespace Game::GameModes::WastelandNightLights
{

    /// 灯り 1 つの点光源と揺らぎ
    struct FlickerLight
    {
        /// 点光源（非所有。実体はスコープが返す）
        NorvesLib::Core::Component::PointLightComponent *pLight = nullptr;
        /// 揺らさないときの光束[lm]
        float BaseLumens = 0.0f;
        /// 揺らぎの大きさ（光束の相対の振れ幅。0 なら揺らさない）
        float FlickerAmount = 0.0f;
        /// 揺らぎの位相[rad]（灯りごとにずらす）
        float Phase = 0.0f;
    };

    /// 灯りの状態（Place が作り、Tick が揺らし、Leave で空に戻す）
    struct State
    {
        NorvesLib::Core::Container::VariableArray<FlickerLight> Lights;
        /// 揺らぎの時刻[秒]
        float TimeSeconds = 0.0f;
        /// 次に揺らぎの光束をログへ書く時刻[秒]
        float NextLogSeconds = 0.0f;
    };

    /// 小物の種類が影を落とすか（街灯はガラスの面が中の点光源を閉じ込めるので落とさない）
    bool CastsShadow(WastelandLayout::PropKind kind);

    /// 置いた小物と建物（placed）に合わせて、点光源・発光体を置く
    void Place(NorvesLib::Core::GameMode::GameModeContext &ctx,
               const NorvesLib::Core::Container::VariableArray<WastelandLayout::PropPlacement> &placed, State &state);

    /// 松明と焚き火の点光源の光束を揺らす
    void Tick(State &state, float deltaTime);

    /// 状態を空に戻す（点光源と発光体の実体・メッシュ・材質はスコープが返す）
    void Leave(State &state);

} // namespace Game::GameModes::WastelandNightLights
