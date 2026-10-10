#include "Debug/EngineChecks.h"

#include "Logging/LogMacros.h"

namespace NorvesLib::Debug
{
    namespace
    {
        constexpr size_t kMaxNameLength = 64;
    } // namespace

    bool EngineChecks::IsValidName(const char *name)
    {
        if (name == nullptr || name[0] < 'A' || name[0] > 'Z')
        {
            return false;
        }
        size_t length = 0;
        for (const char *p = name; *p != '\0'; ++p, ++length)
        {
            const bool bAllowed = (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_';
            if (!bAllowed || length >= kMaxNameLength)
            {
                return false;
            }
        }
        return true;
    }

#if NORVES_BUILD_DEVELOPMENT

    bool EngineChecks::Enable(const char *name)
    {
        if (!IsValidName(name))
        {
            return false;
        }
        if (!IsEnabled(name))
        {
            m_Enabled.push_back(NorvesLib::Core::Container::String(name));
        }
        return true;
    }

    bool EngineChecks::EnableList(const char *list)
    {
        if (list == nullptr || list[0] == '\0')
        {
            return false;
        }
        bool bAllValid = true;
        const char *begin = list;
        for (const char *p = list;; ++p)
        {
            if (*p != ',' && *p != '\0')
            {
                continue;
            }
            // 名前は 64 文字までなので、切り出し用の領域も固定長で足りる（長いものは不正として落とす）
            char name[kMaxNameLength + 1] = {};
            const size_t length = static_cast<size_t>(p - begin);
            if (length > kMaxNameLength)
            {
                bAllValid = false;
            }
            else
            {
                for (size_t i = 0; i < length; ++i)
                {
                    name[i] = begin[i];
                }
                bAllValid = Enable(name) && bAllValid;
            }
            if (*p == '\0')
            {
                break;
            }
            begin = p + 1;
        }
        return bAllValid;
    }

    bool EngineChecks::IsEnabled(const char *name) const
    {
        if (name == nullptr)
        {
            return false;
        }
        for (const auto &enabled : m_Enabled)
        {
            if (enabled == name)
            {
                return true;
            }
        }
        return false;
    }

    void EngineChecks::Report(const char *name, bool bPass, uint32_t count)
    {
        if (!IsEnabled(name))
        {
            return;
        }
        if (bPass)
        {
            LOG_INFO_F("ENGINE_CHECK name=%s result=pass count=%u", name, count);
        }
        else
        {
            ++m_FailCount;
            LOG_ERROR_F("ENGINE_CHECK name=%s result=fail count=%u", name, count);
        }
    }

#else

    bool EngineChecks::Enable(const char *)
    {
        return false;
    }

    bool EngineChecks::EnableList(const char *)
    {
        return false;
    }

    bool EngineChecks::IsEnabled(const char *) const
    {
        return false;
    }

    void EngineChecks::Report(const char *, bool, uint32_t)
    {
    }

#endif

} // namespace NorvesLib::Debug
