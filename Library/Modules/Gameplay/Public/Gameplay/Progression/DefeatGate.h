#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Delegate/Delegate.h"
#include "Delegate/MulticastDelegate.h"
#include "Object/EntityHandle.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Modules::Gameplay
{
    class DefeatGate;

    /// 保留の識別子。IsValid は発行された形式かを示し、解放済みかどうかは示さない。
    class DefeatHoldHandle
    {
    public:
        bool IsValid() const { return m_Gate != nullptr && m_Token != 0; }

    private:
        friend class DefeatGate;
        const DefeatGate* m_Gate = nullptr;
        uint64_t m_Token = 0;
    };

    /// 撃破開始から保留の解消までを管理し、安全点で撃破完了を通知する。
    class DefeatGate
    {
    public:
        DefeatGate() = default;
        DefeatGate(const DefeatGate&) = delete;
        DefeatGate& operator=(const DefeatGate&) = delete;
        DefeatGate(DefeatGate&&) = delete;
        DefeatGate& operator=(DefeatGate&&) = delete;

        /// 同じ ID は完了後に受け付け直した場合のみ再開始でき、不正な ID と重複は無視して診断の数を増やす。
        /// bCountsForProgression が true の場合、完了を進行にも通知する。
        void BeginDefeat(const Core::Container::String& enemyId, bool bCountsForProgression = true);

        /// 完了済みの ID を受け付け直す。不正・未登録の ID は無視する。
        /// 保留中の場合は受け付け直さず、診断の数を増やす。
        void Rearm(const Core::Container::String& enemyId);
        DefeatHoldHandle AddHold(const Core::Container::String& enemyId, Core::EntityHandle owner);
        void Release(DefeatHoldHandle handle);

        /// 生存しない所有者の保留を外し、保留のない撃破を一度だけ完了する。
        /// 未バインドの問い合わせでは所有者を判定できないため、更新しない。
        void Update(const Core::Delegate<bool, Core::EntityHandle>& isOwnerAlive);
        size_t GetIgnoredBeginCount() const { return m_IgnoredBeginCount; }

        /// 進行に数える撃破の完了だけを通知する。
        Core::MulticastDelegate<const Core::Container::String&> OnProgressionDefeatCompleted;

        /// 進行の対象にかかわらず、すべての撃破完了を通知する。
        Core::MulticastDelegate<const Core::Container::String&> OnDefeatCompleted;

    private:
        struct Defeat
        {
            Core::Container::String EnemyId;
            bool bCompleted = false;
            bool bCountsForProgression = true;
            bool bRearmed = false;
            bool bRetired = false;
        };

        struct Hold
        {
            Core::EntityHandle Owner;
            size_t DefeatIndex = 0;
            uint64_t Token = 0;
        };

        Core::Container::VariableArray<Defeat> m_Defeats;
        Core::Container::VariableArray<Hold> m_Holds;
        uint64_t m_NextToken = 1;
        size_t m_IgnoredBeginCount = 0;
        bool m_bUpdating = false;
    };
} // namespace NorvesLib::Modules::Gameplay
