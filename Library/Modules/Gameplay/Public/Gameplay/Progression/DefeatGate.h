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

        /// 同じ ID の再開始と不正な ID は無視し、診断の数を増やす。
        void BeginDefeat(const Core::Container::String& enemyId);
        DefeatHoldHandle AddHold(const Core::Container::String& enemyId, Core::EntityHandle owner);
        void Release(DefeatHoldHandle handle);

        /// 生存しない所有者の保留を外し、保留のない撃破を一度だけ完了する。
        /// 未バインドの問い合わせでは所有者を判定できないため、更新しない。
        void Update(const Core::Delegate<bool, Core::EntityHandle>& isOwnerAlive);
        size_t GetIgnoredBeginCount() const { return m_IgnoredBeginCount; }

        Core::MulticastDelegate<const Core::Container::String&> OnDefeatCompleted;

    private:
        struct Defeat
        {
            Core::Container::String EnemyId;
            bool bCompleted = false;
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
