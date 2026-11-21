// 匂いは雨の影響を受けない。
// 水平距離 d の無風の濃さ S exp(−d/L_calm) と風の分布を、水平風速の smoothstep で補間する。
// 風下距離 a ≥ 0 では煙 S exp(−a/L_down − c²/(2σ²)) と拡散 B = S F exp(−d/L_up) の大きい方を採る。
// c は横風距離、σ は風下へ進むほど base + spread × a で広がる煙の幅。
// a < 0 の拡散は B × lerp(1, UpwindAxisRatio, |a|/d)。同じ距離なら横風より真の風上を薄くする。
#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Modules::AI::Perception
{
    struct SmellTuning
    {
        float DownwindLengthMeters = 40.0f;     // 風下で煙が減衰する長さ（m）
        float UpwindLengthMeters = 3.0f;        // 風上への拡散が減衰する長さ（m）
        float UpwindFactor = 0.1f;              // 横風の境での拡散の倍率
        float UpwindAxisRatio = 0.5f;           // 横風を 1 とした真の風上の拡散の割合
        float CrossSigmaBaseMeters = 2.0f;      // 源での煙の幅（m）
        float CrossSigmaSpreadPerMeter = 0.15f; // 風下に 1 m 進むごとの幅の増分（m）
        float CalmWindSpeed = 0.3f;             // 等方分布だけを使う風速の上限（m/s）
        float FullWindSpeed = 1.5f;             // 風の分布だけを使う風速の下限（m/s）
        float CalmLengthMeters = 8.0f;          // 無風で匂いが減衰する長さ（m）
        float WindFilterSeconds = 1.0f;         // 水平風の平滑化の時定数（s）
    };

    struct WindDirectionFilter
    {
        Math::Vector3 Filtered; // 平滑化した水平風（m/s）
        bool bInitialized = false;

        /// 水平風を零に戻し、次の有効な入力で初期化する。
        void Reset();
        /// 水平風を指数平滑する。初回と時定数が零以下のときは入力をそのまま採る。
        /// 時間刻みが零以下、または入力に非有限値があれば状態を保つ。
        Math::Vector3 Update(const Math::Vector3& wind, float deltaSeconds, float timeConstantSeconds);
    };

    struct SmellQuery
    {
        Math::Vector3 Source;       // 匂いの源の位置（m）
        Math::Vector3 Receiver;     // 嗅ぎ手の鼻の位置（m）
        float Strength = 0.0f;      // 源の強さ
        Math::Vector3 FilteredWind; // 平滑化した水平風（m/s）
    };

    struct SmellResult
    {
        float Concentration = 0.0f;  // 鼻での濃さ（源の強さと同じ単位）
        float AlongWindMeters = 0.0f; // 風下を正とした距離（m）
        float CrossWindMeters = 0.0f; // 風の軸からの水平距離（m）
        float WindWeight = 0.0f;      // 風の分布の割合（0 で無風、1 で有風の分布のみ）
    };

    /// 水平面での匂いの濃さを求める。非有限の入力や正でない強さでは零を返す。
    SmellResult EvaluateSmell(const SmellQuery& query, const SmellTuning& tuning);
}
