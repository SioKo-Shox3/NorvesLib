// 当たりの反応に使う設定を置く。
#pragma once

namespace NorvesLib::Modules::Gameplay::Combat
{
    struct CombatResponseSettings
    {
        bool bHitEffects = true;        // 当たりからエフェクトの放出要求を出す。
        bool bHitStop = true;           // 攻撃のヒントからヒットストップを要求する。
        float HitStopTimeScale = 0.0f;  // ヒットストップ中の World の倍率。0 以上。
        float MaxHitStopSeconds = 0.25f; // 1 回のヒットストップの上限。実時間の秒。
        bool bDebugDraw = false;        // 判定の形を線で出す。Debug と RelWithDebInfo だけで有効。Release では無視。既定は切。
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
