#include "Gameplay/Progression/DefeatGate.h"

#include "Gameplay/Progression/GameplayStableId.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Modules::Gameplay
{
    void DefeatGate::BeginDefeat(const Core::Container::String& enemyId)
    {
        if (IsValidGameplayStableId(enemyId))
        {
            for (const auto& defeat : m_Defeats)
            {
                if (defeat.EnemyId == enemyId)
                {
                    ++m_IgnoredBeginCount;
                    NORVES_LOG_WARNING("DefeatGate", "撃破開始の重複を無視しました: %s", enemyId.c_str());
                    return;
                }
            }
            m_Defeats.push_back({enemyId, false});
            return;
        }

        ++m_IgnoredBeginCount;
        NORVES_LOG_WARNING("DefeatGate", "不正な撃破 ID を無視しました: %s", enemyId.c_str());
    }

    DefeatHoldHandle DefeatGate::AddHold(const Core::Container::String& enemyId, Core::EntityHandle owner)
    {
        if (!owner.IsValid() || m_NextToken == 0)
        {
            return {};
        }
        for (size_t defeatIndex = 0; defeatIndex < m_Defeats.size(); ++defeatIndex)
        {
            if (m_Defeats[defeatIndex].EnemyId != enemyId || m_Defeats[defeatIndex].bCompleted)
            {
                continue;
            }

            size_t holdIndex = 0;
            while (holdIndex < m_Holds.size() && m_Holds[holdIndex].Token != 0)
            {
                ++holdIndex;
            }
            if (holdIndex == m_Holds.size())
            {
                m_Holds.emplace_back();
            }
            const uint64_t token = m_NextToken++;
            m_Holds[holdIndex] = {owner, defeatIndex, token};
            DefeatHoldHandle handle;
            handle.m_Gate = this;
            handle.m_Token = token;
            return handle;
        }
        return {};
    }

    void DefeatGate::Release(DefeatHoldHandle handle)
    {
        if (handle.m_Gate != this || handle.m_Token == 0)
        {
            return;
        }
        for (auto& hold : m_Holds)
        {
            if (hold.Token == handle.m_Token)
            {
                hold.Token = 0;
                return;
            }
        }
    }

    void DefeatGate::Update(const Core::Delegate<bool, Core::EntityHandle>& isOwnerAlive)
    {
        if (m_bUpdating || !isOwnerAlive.IsBound())
        {
            return;
        }
        m_bUpdating = true;
        struct UpdateGuard
        {
            bool& bUpdating;
            ~UpdateGuard() { bUpdating = false; }
        } guard{m_bUpdating};

        // 通知中に新しく始まった撃破は、同じ更新で完了せず次の安全点を待つ。
        const size_t defeatCount = m_Defeats.size();
        const size_t holdCount = m_Holds.size();
        for (size_t i = 0; i < holdCount; ++i)
        {
            const uint64_t token = m_Holds[i].Token;
            if (token == 0)
            {
                continue;
            }
            const auto owner = m_Holds[i].Owner;
            if (!isOwnerAlive.Invoke(owner) && m_Holds[i].Token == token)
            {
                m_Holds[i].Token = 0;
            }
        }

        for (size_t i = 0; i < defeatCount; ++i)
        {
            if (m_Defeats[i].bCompleted)
            {
                continue;
            }
            bool bHasHold = false;
            for (const auto& hold : m_Holds)
            {
                if (hold.Token != 0 && hold.DefeatIndex == i)
                {
                    bHasHold = true;
                    break;
                }
            }
            if (!bHasHold)
            {
                // 通知による配列の拡張や再入に先立って受付を閉じる。
                const Core::Container::String enemyId = m_Defeats[i].EnemyId;
                m_Defeats[i].bCompleted = true;
                OnDefeatCompleted.Broadcast(enemyId);
            }
        }
    }
} // namespace NorvesLib::Modules::Gameplay
