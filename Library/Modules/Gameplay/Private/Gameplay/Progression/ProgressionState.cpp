#include "Gameplay/Progression/ProgressionState.h"

#include "Gameplay/Progression/GameplayStableId.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <utility>

namespace NorvesLib::Modules::Gameplay
{
    namespace
    {
        using String = Core::Container::String;
        using IdList = Core::Container::VariableArray<String>;

        bool Contains(const IdList& ids, const String& id)
        {
            for (const auto& candidate : ids)
            {
                if (candidate == id)
                {
                    return true;
                }
            }
            return false;
        }

        bool HasExpectedMembers(Core::JsonValue root, const char* const* keys, size_t keyCount)
        {
            if (!root.IsObject() || root.GetObjectSize() != keyCount)
            {
                return false;
            }
            for (size_t i = 0; i < keyCount; ++i)
            {
                size_t matches = 0;
                for (size_t j = 0; j < root.GetObjectSize(); ++j)
                {
                    if (root.GetMemberName(j) == keys[i])
                    {
                        ++matches;
                    }
                }
                if (matches != 1)
                {
                    return false;
                }
            }
            const auto version = root.FindMember("version");
            return version.IsIntegerLiteral() && version.AsNumber() == 1.0;
        }

        bool ReadUniqueIds(Core::JsonValue value, IdList& outIds)
        {
            if (!value.IsArray())
            {
                return false;
            }
            for (size_t i = 0; i < value.GetArraySize(); ++i)
            {
                const auto item = value.GetArrayElement(i);
                if (!item.IsString() || !IsValidGameplayStableId(item.AsString()) ||
                    Contains(outIds, item.AsString()))
                {
                    return false;
                }
                outIds.push_back(item.AsString());
            }
            return true;
        }
    }

    bool ProgressionState::LoadDefinitionFromJson(const String& text)
    {
        Core::JsonDocument document;
        const char* const keys[] = {"version", "finalShishaId", "requiredShishaIds"};
        if (!Core::JsonDocument::TryParse(text, document) ||
            !HasExpectedMembers(document.GetRoot(), keys, 3))
        {
            NORVES_LOG_ERROR("Progression", "進行条件の JSON の形式が不正です。");
            return false;
        }

        const auto root = document.GetRoot();
        const auto finalId = root.FindMember("finalShishaId");
        IdList requiredIds;
        if (!finalId.IsString() || (!finalId.AsString().empty() && !IsValidGameplayStableId(finalId.AsString())) ||
            !ReadUniqueIds(root.FindMember("requiredShishaIds"), requiredIds) ||
            Contains(requiredIds, finalId.AsString()))
        {
            NORVES_LOG_ERROR("Progression", "進行条件の ID が不正または重複しています。");
            return false;
        }

        String nextFinalId = finalId.AsString();
        m_RequiredShishaIds = std::move(requiredIds);
        m_FinalShishaId = std::move(nextFinalId);
        if (IsFinalUnlocked())
        {
            m_bUnlockAnnouncedOrRestored = true;
        }
        return true;
    }

    bool ProgressionState::LoadStateFromJson(const String& text)
    {
        Core::JsonDocument document;
        const char* const keys[] = {"version", "defeatedEnemyIds"};
        IdList defeatedIds;
        if (!Core::JsonDocument::TryParse(text, document) ||
            !HasExpectedMembers(document.GetRoot(), keys, 2) ||
            !ReadUniqueIds(document.GetRoot().FindMember("defeatedEnemyIds"), defeatedIds))
        {
            NORVES_LOG_ERROR("Progression", "撃破状態の JSON または ID が不正です。");
            return false;
        }

        m_DefeatedEnemyIds = std::move(defeatedIds);
        if (IsFinalUnlocked())
        {
            m_bUnlockAnnouncedOrRestored = true;
        }
        return true;
    }

    String ProgressionState::SaveStateToJson() const
    {
        Core::JsonWriter writer;
        writer.BeginObject();
        writer.WriteInt64("version", 1);
        writer.BeginArray("defeatedEnemyIds");
        for (const auto& id : m_DefeatedEnemyIds)
        {
            writer.WriteStringElement(id);
        }
        writer.EndArray();
        writer.EndObject();
        return writer.ToString();
    }

    void ProgressionState::MarkDefeated(const String& enemyId)
    {
        if (!IsValidGameplayStableId(enemyId) ||
            (!Contains(m_RequiredShishaIds, enemyId) && enemyId != m_FinalShishaId))
        {
            ++m_IgnoredIdCount;
            NORVES_LOG_WARNING("Progression", "不正または未定義の撃破 ID を無視しました: %s", enemyId.c_str());
            return;
        }
        if (Contains(m_DefeatedEnemyIds, enemyId))
        {
            return;
        }

        const bool bWasUnlocked = IsFinalUnlocked();
        m_DefeatedEnemyIds.push_back(enemyId);
        if (!bWasUnlocked && !m_bUnlockAnnouncedOrRestored && IsFinalUnlocked())
        {
            m_bUnlockAnnouncedOrRestored = true;
            OnFinalUnlocked.Broadcast();
        }
    }

    bool ProgressionState::IsDefeated(const String& enemyId) const
    {
        return Contains(m_DefeatedEnemyIds, enemyId);
    }

    bool ProgressionState::IsFinalUnlocked() const
    {
        if (m_RequiredShishaIds.empty())
        {
            return false;
        }
        for (const auto& id : m_RequiredShishaIds)
        {
            if (!Contains(m_DefeatedEnemyIds, id))
            {
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Modules::Gameplay
