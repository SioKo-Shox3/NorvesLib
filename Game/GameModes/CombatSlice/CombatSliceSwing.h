#pragma once

#include <cstdint>

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}

namespace Game::GameModes
{
    struct CombatSliceSceneIds;

    namespace CombatSliceSwingParams
    {
        inline constexpr float StartYawDegrees = -40.0f;
        inline constexpr float EndYawDegrees = 120.0f;
        inline constexpr float DurationSeconds = 0.45f;
    }

    class CombatSliceSwing
    {
    public:
        // 次の Tick から剣の取り付けを待ち、既に開始済みなら false を返す。
        bool Start();
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, const CombatSliceSceneIds& ids, float worldDt);
        bool IsSwinging() const;
        bool IsFinished() const;
        bool IsWindowOpen() const;
        bool IsFailed() const;
        uint32_t GetCompletedSwingCount() const;
        float GetYawDegrees() const;
        // 窓を閉じて開始角へ戻す。完了回数は維持する。
        void Reset(NorvesLib::Core::GameMode::GameModeContext& ctx, const CombatSliceSceneIds& ids);

    private:
        enum class State : uint8_t
        {
            Idle,
            WaitingForAttachment,
            Sweeping,
            FinalPose,
            Finished,
            Failed
        };

        State m_State = State::Idle;
        double m_ElapsedSeconds = 0.0;
        float m_YawDegrees = CombatSliceSwingParams::StartYawDegrees;
        uint32_t m_CompletedSwingCount = 0;
    };
} // namespace Game::GameModes
