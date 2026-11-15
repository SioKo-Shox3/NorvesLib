#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstdint>

namespace NorvesLib::Core::AssetImport
{
    // clip節で指定したキー。呼出側が明示したキーと同じビットで突き合わせる。
    enum ClipImportKey : uint32_t
    {
        ClipKeyRootJoint = 1u << 0,
        ClipKeyLoop = 1u << 1,
        ClipKeyLoopThreshold = 1u << 2,
        ClipKeyAuthoredFps = 1u << 3,
        ClipKeySourceFps = 1u << 4,
        ClipKeyTimeScale = 1u << 5,
        ClipKeyAnalysis = 1u << 6,
    };
    // Skeletal::RigClipLoopModeと同じ順序。
    enum class ClipLoopSetting : uint8_t { Auto, Loop, Once };
    struct ClipImportSettings
    {
        uint32_t Keys = 0; // 0なら節が無い（空objectを含む）。
        Container::AnsiString RootJoint; // UTF-8の関節名。
        ClipLoopSetting Loop = ClipLoopSetting::Auto;
        double LoopThreshold = 0.01;
        double AuthoredFps = 0, SourceFps = 0;
        double TimeScale = 1;
        bool bAnalysis = true;
        [[nodiscard]] bool IsPresent() const noexcept { return Keys != 0; }
    };
    inline constexpr uint32_t ClipImportSettingsCanonicalVersion = 1;
    // 節が無ければ空。あればLEのu32版とu32 Keysに続けて指定された値をビット順に並べる。
    [[nodiscard]] Container::VariableArray<uint8_t> EncodeCanonicalClipSettings(const ClipImportSettings& settings);
    // 節が無ければ入力を返す。あればLEのu64長と正規化bytesをFNV-1aで連結する。
    [[nodiscard]] uint64_t AppendClipImportSettingsHash(uint64_t hash, const ClipImportSettings& settings);
}
