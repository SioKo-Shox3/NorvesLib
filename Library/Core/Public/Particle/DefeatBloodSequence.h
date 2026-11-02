#pragma once

/**
 * @file DefeatBloodSequence.h
 * @brief 撃破の血しぶき・滲み・消滅を相対時刻で管理する CPU の時間割
 */

#include "Container/VariableArray.h"
#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core::Particle
{
    enum class DefeatBloodCueType : uint8_t
    {
        Spray,
        Bleed,
        Vanish,
        Finished
    };

    struct DefeatBloodTimeline
    {
        float SprayDelaySeconds = 0.0f;      // 合図から血しぶきまで[s]
        float BleedDelaySeconds = 0.35f;     // 合図から滲みの始まりまで[s]
        float BleedDurationSeconds = 2.5f;   // 滲みが広がりきるまで[s]
        float VanishDelaySeconds = 6.0f;     // 合図から消滅の始まりまで[s]
        float VanishDurationSeconds = 1.5f;  // 消滅にかける時間[s]
        float MaxBleedRadiusMeters = 0.6f;   // 滲みの最大の半径[m]

        /// 全部有限、遅れは 0 以上、長さと半径は正、Spray ≤ Bleed ≤ Vanish なら true
        bool IsValid() const;
    };

    struct DefeatBloodCue
    {
        DefeatBloodCueType Type = DefeatBloodCueType::Spray;
        double TimeSeconds = 0.0;   // 合図からの時間割の時刻（刻みの幅によらない値そのもの）
        uint64_t TickIndex = 0u;    // 出した刻みの通し番号。Start の後の最初の Tick が 1
    };

    class DefeatBloodSequence
    {
    public:
        /// 時間割が不正・位置や法線が非有限・法線の長さが 0 なら false で、状態を変えない。法線は正規化して持つ。
        /// 再び呼ぶと最初からやり直す。
        bool Start(const DefeatBloodTimeline& timeline, const Math::Vector3& position, const Math::Vector3& normal);
        /// 経過時間を dt だけ進め、届いた合図を時刻順にすべて outCues の後ろへ足す。
        /// 負・非有限の dt は 0 とするが、刻みの番号は進む。Start 前・終わった後は何もしない。
        void Tick(float deltaSeconds, Container::VariableArray<DefeatBloodCue>& outCues);
        bool IsActive() const;    // Start 後で Finished を出す前
        bool IsFinished() const;  // Finished を出した後
        double GetElapsedSeconds() const;
        float GetBleedMask() const;
        float GetBleedRadiusMeters() const;
        float GetOpacity() const;
        const Math::Vector3& GetPosition() const;
        const Math::Vector3& GetNormal() const;
        const DefeatBloodTimeline& GetTimeline() const;

        /// 滲みの始まりより前は 0、長さの間は t=(elapsed−BleedDelay)/BleedDuration で 1−(1−t)^2、その後は 1。消滅の間も縮めない。
        static float EvaluateBleedMask(const DefeatBloodTimeline& timeline, double elapsedSeconds);
        /// 消滅の始まりより前は 1、長さの間で 1 → 0 へ直線、その後は 0。
        static float EvaluateOpacity(const DefeatBloodTimeline& timeline, double elapsedSeconds);

    private:
        DefeatBloodTimeline m_Timeline;
        Math::Vector3 m_Position;
        Math::Vector3 m_Normal;
        double m_ElapsedSeconds = 0.0;
        uint64_t m_TickIndex = 0u;
        uint8_t m_NextCueIndex = 0u;
        bool m_bStarted = false;
    };
} // namespace NorvesLib::Core::Particle
