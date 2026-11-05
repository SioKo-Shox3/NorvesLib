#pragma once

#include "Container/String.h"
#include <cstdint>

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
} // namespace NorvesLib::Core::GameMode

namespace Game::GameModes
{
    enum class CombatSliceCaptureStatus : uint8_t
    {
        Idle,
        Pending,
        Saved,
        Failed
    };

    class CombatSliceCapture
    {
    public:
        // 保存先の親フォルダは呼び手が事前に作る。空パスでは状態を変えず false を返す。
        bool Request(NorvesLib::Core::GameMode::GameModeContext& ctx,
                     const NorvesLib::Core::Container::String& path);
        // 要求を受理した後、保存または失敗するまで毎フレーム呼ぶ。
        CombatSliceCaptureStatus Poll(NorvesLib::Core::GameMode::GameModeContext& ctx);
        CombatSliceCaptureStatus GetStatus() const;
        const NorvesLib::Core::Container::String& GetPath() const;
        // この型の状態を戻す。処理中の撮影要求は取り消せない。
        void Reset();

    private:
        CombatSliceCaptureStatus m_Status = CombatSliceCaptureStatus::Idle;
        NorvesLib::Core::Container::String m_Path;
        uint64_t m_RequestId = 0;
        uint64_t m_StartFrame = 0;
    };
} // namespace Game::GameModes
