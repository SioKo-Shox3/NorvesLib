#pragma once

#include "Audio/ISoundSystem.h"
#include "Audio/SoundBankTypes.h"
#include "Container/Span.h"
#include "Delegate/Delegate.h"

namespace NorvesLib::Modules::Audio::Private
{
    // パスは呼び出し中だけ借用する。解決できない場合は空の共有ポインタを返す。
    using SoundBankClipResolver = Core::Delegate<Core::Container::TSharedPtr<AudioClipResource>,
                                                 const Core::Container::String&>;

    struct SoundBankParseResult
    {
        bool bBankAccepted = false;
        Core::Container::VariableArray<SoundDefinition> Definitions;
        SoundBankLoadReport Report;
    };

    [[nodiscard]] SoundBankParseResult ParseSoundBank(const Core::Container::String& jsonText,
                                                       const SoundBankClipResolver& resolveClip);
    // cook 済み raw のバイト列を受け取り、先頭の UTF-8 BOM は除去する。
    [[nodiscard]] SoundBankParseResult ParseSoundBank(Core::Container::Span<const uint8_t> jsonBytes,
                                                       const SoundBankClipResolver& resolveClip);
} // namespace NorvesLib::Modules::Audio::Private
