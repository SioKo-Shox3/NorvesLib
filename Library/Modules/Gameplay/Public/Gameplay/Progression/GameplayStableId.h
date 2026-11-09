#pragma once

#include "Container/String.h"

#include <cstddef>

namespace NorvesLib::Modules::Gameplay
{
    inline constexpr size_t MaxGameplayStableIdLength = 128;

    /// ObjectId はロード時に採番し直されるため、保存や照合には安定した文字列 ID を使う。
    /// ID は小文字英数字とアンダースコアの空でない区間をドットで区切り、長さは 128 以下とする。
    bool IsValidGameplayStableId(const char* id);
    bool IsValidGameplayStableId(const Core::Container::String& id);
} // namespace NorvesLib::Modules::Gameplay
