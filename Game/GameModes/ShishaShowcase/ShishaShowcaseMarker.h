// 撮影の台本が読む場面の印を、共通の一行形式で記録する。
#pragma once

#include "Core/Public/Logging/LogMacros.h"

namespace Game::GameModes
{
    /// scene・stage と各キー・値には空白を入れない。keyValues は空文字列または空白区切り。
    inline void LogShishaShowcaseMarker(const char* scene, const char* stage, const char* keyValues)
    {
#if NORVES_ENABLE_LOGGING
        const bool bHasValues = keyValues != nullptr && keyValues[0] != '\0';
        LOG_INFO_F("SHISHA_SHOWCASE scene=%s stage=%s%s%s", scene, stage, bHasValues ? " " : "",
                   bHasValues ? keyValues : "");
#else
        (void)scene;
        (void)stage;
        (void)keyValues;
#endif
    }
} // namespace Game::GameModes
