#include "AI/Navigation/NavAgentProfiles.h"

#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"

#include <cmath>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        const NavAgentProfile* FindProfile(const Core::Container::VariableArray<NavAgentProfile>& profiles,
                                           const char* name)
        {
            for (const auto& profile : profiles)
            {
                if (profile.Name == name)
                {
                    return &profile;
                }
            }
            return nullptr;
        }

        bool IsValidExtraCost(Core::JsonValue value)
        {
            return value.IsIntegerLiteral() && value.AsNumber() >= 0 && value.AsNumber() <= 1024;
        }
    }

    // Assets/AI/NavProfiles.json と同じ値
    NavAgentProfileSet::NavAgentProfileSet()
        : m_Profiles{{"Small", 0.25f, 64, 16}, {"Medium", 0.5f, 96, 8}, {"Large", 1.0f, 128, 0}}
    {
    }

    NavAgentProfileSet NavAgentProfileSet::MakeDefaults()
    {
        return NavAgentProfileSet{};
    }

    bool NavAgentProfileSet::LoadFromJson(const Core::Container::String& text)
    {
        Core::JsonDocument document;
        if (!Core::JsonDocument::TryParse(text, document))
        {
            NORVES_LOG_ERROR("AI", "エージェントのプロファイルを JSON として解析できません。");
            return false;
        }

        const auto root = document.GetRoot();
        const auto version = root.FindMember("version");
        const auto profiles = root.FindMember("profiles");
        if (root.GetObjectSize() != 2 || !version.IsIntegerLiteral() || version.AsNumber() != 1)
        {
            NORVES_LOG_ERROR("AI", "ルートには version と profiles が必要です。version は整数の 1 を指定してください。");
            return false;
        }
        if (!profiles.IsArray() || profiles.GetArraySize() == 0 || profiles.GetArraySize() > UINT32_MAX)
        {
            NORVES_LOG_ERROR("AI", "profiles には 1 件以上で件数を 32 ビット符号なし整数で表せる配列が必要です。");
            return false;
        }

        Core::Container::VariableArray<NavAgentProfile> nextProfiles;
        nextProfiles.reserve(profiles.GetArraySize());
        for (size_t i = 0; i < profiles.GetArraySize(); ++i)
        {
            const auto item = profiles.GetArrayElement(i);
            if (item.GetObjectSize() != 4)
            {
                NORVES_LOG_ERROR("AI", "profiles[%zu] には名前、半径、地形別の追加コスト 2 種の計 4 項目が必要です。", i);
                return false;
            }
            const auto name = item.FindMember("name");
            if (!name.IsString() || name.AsString().empty() ||
                name.AsString().find('\0') != Core::Container::String::npos)
            {
                NORVES_LOG_ERROR("AI", "profiles[%zu] の name には空でなくヌル文字を含まない文字列が必要です。", i);
                return false;
            }
            if (FindProfile(nextProfiles, name.AsString().c_str()))
            {
                NORVES_LOG_ERROR("AI", "profiles[%zu] の name が重複しています。", i);
                return false;
            }

            const double radius = item.FindMember("radius").AsNumber();
            if (!std::isfinite(radius) || radius <= 0 || radius > 8 || static_cast<float>(radius) == 0.0f)
            {
                NORVES_LOG_ERROR("AI", "profiles[%zu] の radius には float で正の値を保てる 8 以下の有限数が必要です。", i);
                return false;
            }
            const auto water = item.FindMember("shallowWaterExtraPer128");
            const auto grass = item.FindMember("grassExtraPer128");
            if (!IsValidExtraCost(water) || !IsValidExtraCost(grass))
            {
                NORVES_LOG_ERROR("AI", "profiles[%zu] の浅瀬と草の追加コストには 0～1024 の整数が必要です。", i);
                return false;
            }
            nextProfiles.push_back({name.AsString(), static_cast<float>(radius),
                                    static_cast<uint16_t>(water.AsNumber()), static_cast<uint16_t>(grass.AsNumber())});
        }

        m_Profiles.swap(nextProfiles);
        return true;
    }

    const NavAgentProfile* NavAgentProfileSet::Find(const char* name) const
    {
        return name ? FindProfile(m_Profiles, name) : nullptr;
    }

    uint32_t NavAgentProfileSet::GetCount() const
    {
        return static_cast<uint32_t>(m_Profiles.size());
    }

    const NavAgentProfile& NavAgentProfileSet::GetAt(uint32_t index) const
    {
        return m_Profiles[index];
    }
} // namespace NorvesLib::Modules::AI
