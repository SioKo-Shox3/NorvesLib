#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Delegate/MulticastDelegate.h"

#include <cstddef>

namespace NorvesLib::Modules::Gameplay
{
    /// モード遷移をまたぐ撃破状態と、最後のシ者の解放条件を保持する。
    class ProgressionState
    {
    public:
        /// 不正な定義・状態は適用せず false を返す。状態中の未知 ID は将来の定義変更に備えて保持する。
        bool LoadDefinitionFromJson(const Core::Container::String& text);
        bool LoadStateFromJson(const Core::Container::String& text);
        Core::Container::String SaveStateToJson() const;

        void MarkDefeated(const Core::Container::String& enemyId);
        bool IsDefeated(const Core::Container::String& enemyId) const;

        /// 必要数が未決の空定義で最後のシ者を解放しないため、必要 ID が空なら false。
        bool IsFinalUnlocked() const;
        size_t GetIgnoredIdCount() const { return m_IgnoredIdCount; }

        /// 初めて解放されたときだけ通知する。読込時に条件を満たす場合は通知せず解放済みとする。
        Core::MulticastDelegate<> OnFinalUnlocked;

    private:
        using IdList = Core::Container::VariableArray<Core::Container::String>;

        IdList m_RequiredShishaIds;
        Core::Container::String m_FinalShishaId;
        IdList m_DefeatedEnemyIds;
        size_t m_IgnoredIdCount = 0;
        bool m_bUnlockAnnouncedOrRestored = false;
    };
} // namespace NorvesLib::Modules::Gameplay
