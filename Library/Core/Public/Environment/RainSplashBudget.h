// 雨の着水と樹冠からの滴りに使う予算の設定と見積りを定義する。
#pragma once

#include "Container/String.h"

namespace NorvesLib::Core::Environment
{
    inline constexpr const char* DefaultRainSplashBudgetPath = "Assets/Data/Vfx/RainSplashBudget.json";

    /// 着水と滴りの上限と調整値。既定値はデータで変更できる。
    struct RainSplashBudgetSettings
    {
        float MaxSplashesPerSecond = 4000.0f;          // 着水する子の組の秒あたりの上限。
        float MaxAliveSplashes = 2000.0f;              // 同時に生きる Splash の上限。
        float GrassDensityForMinScale = 1.0f;          // 草による倍率が最小になる密度。
        float GrassMinScale = 0.25f;                   // 草の密度が高い所での確率の倍率。
        float CanopyMinClearanceMeters = 3.0f;         // 滴りを出す屋根と地面の差の下限。
        float CanopyDripsPerSecondAtFullRain = 30.0f;  // 雨量 1 かつ全て樹冠の下での秒あたりの滴りの目安。
    };

    /// 1 フレームの見積りの入力。
    struct RainSplashBudgetInput
    {
        float RainIntensity = 0.0f;            // 雨量 [0, 1]。
        float ActiveParticleCount = 0.0f;       // 近距離の雨の有効な粒の数。
        float FallSpeedMetersPerSecond = 0.0f; // 落ちる速さ。
        float BoxHeightMeters = 0.0f;           // 箱の高さ。
        float SplashLifetimeSeconds = 0.0f;     // Splash の寿命。
        float GrassDensity = 0.0f;              // カメラ位置の草の密度 [0, 1]。
    };

    /// 着水と樹冠下の滴りの見積り。
    struct RainSplashBudgetResult
    {
        float HitsPerSecond = 0.0f;       // 秒あたりの当たりの数。単精度の最大値まで。
        float ChildProbability = 0.0f;    // Splash と Droplet の組を生む確率。
        float SplashesPerSecond = 0.0f;   // 秒あたりに生まれる組の数。
        float DripProbability = 0.0f;     // 樹冠下の当たり 1 回あたりの滴りの確率。
    };

    /// 着水予算の既定値を返す。
    RainSplashBudgetSettings MakeDefaultRainSplashBudgetSettings();

    /// 非有限または範囲外の欄を既定値へ戻す。
    RainSplashBudgetSettings SanitizeRainSplashBudgetSettings(const RainSplashBudgetSettings& in);

    /// 当たりの見積りから着水の確率と樹冠下の滴りの確率を求める。
    RainSplashBudgetResult ComputeRainSplashBudget(const RainSplashBudgetInput& input, const RainSplashBudgetSettings& settings);

    /// JSON を既定値に重ねて読む。解析失敗時は警告し、既定値を設定して false を返す。
    bool TryLoadRainSplashBudgetFromJsonText(const Container::String& text, RainSplashBudgetSettings& outSettings);

    /// UTF-8 の BOM を除いてファイルを読む。失敗時は警告し、既定値を返す。
    RainSplashBudgetSettings LoadRainSplashBudgetFile(const Container::String& utf8Path);
} // 名前空間 NorvesLib::Core::Environment
