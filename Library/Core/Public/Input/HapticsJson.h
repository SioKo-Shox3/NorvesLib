#pragma once

#include "Input/HapticsService.h"

namespace NorvesLib::Core::Input
{
    struct HapticsConfiguration
    {
        Container::VariableArray<HapticsEffectDefinition> Effects;
        HapticsSettings Settings;
    };
    struct HapticsJsonReport
    {
        uint32_t WarningCount = 0;
        Container::String Error;
    };
    namespace HapticsJson
    {
        inline constexpr size_t MaximumTextBytes = 1024 * 1024;
        inline constexpr size_t MaximumDepth = 64;
        inline constexpr size_t MaximumEffects = 256;
        inline constexpr size_t MaximumKeysPerCurve = 256;
        inline constexpr size_t MaximumNameBytes = 128;
        // validation失敗はoutを保持（report診断は更新）。allocation例外は旧outを保って伝播する。
        bool Parse(const Container::String& json, HapticsConfiguration& out, HapticsJsonReport* report = nullptr);
        bool Write(const HapticsConfiguration& configuration, Container::String& out);
        bool IsValid(const HapticsConfiguration& configuration);
    }
} // namespace NorvesLib::Core::Input
