#include "Gameplay/Progression/GameplayStableId.h"

namespace NorvesLib::Modules::Gameplay
{
    namespace
    {
        bool IsValidId(const char* id, size_t length)
        {
            if (id == nullptr || length == 0 || length > MaxGameplayStableIdLength)
            {
                return false;
            }

            bool bSegmentHasCharacter = false;
            for (size_t i = 0; i < length; ++i)
            {
                const char ch = id[i];
                if (ch == '.')
                {
                    if (!bSegmentHasCharacter)
                    {
                        return false;
                    }
                    bSegmentHasCharacter = false;
                }
                else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')
                {
                    bSegmentHasCharacter = true;
                }
                else
                {
                    return false;
                }
            }
            return bSegmentHasCharacter;
        }
    }

    bool IsValidGameplayStableId(const char* id)
    {
        if (id == nullptr)
        {
            return false;
        }
        size_t length = 0;
        while (length <= MaxGameplayStableIdLength && id[length] != '\0')
        {
            ++length;
        }
        return IsValidId(id, length);
    }

    bool IsValidGameplayStableId(const Core::Container::String& id)
    {
        return IsValidId(id.data(), id.size());
    }
} // namespace NorvesLib::Modules::Gameplay
