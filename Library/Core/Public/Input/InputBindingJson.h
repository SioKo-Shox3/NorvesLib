#pragma once
#include "Input/InputBindingSet.h"

namespace NorvesLib::Core::Input
{
    struct InputBindingJsonReport
    {
        uint32_t WarningCount = 0;
        Container::String Error;
    };
    namespace InputBindingJson
    {
        inline constexpr size_t MaximumTextBytes = 1024 * 1024;
        inline constexpr size_t MaximumDepth = 64;
        // validation失敗はfalse。allocation例外は伝播する。
        // Defaultsの失敗はoutを維持、Overridesの失敗はdefaultsのcopyへ退避する。
        bool ParseDefaults(const Container::String& json, InputBindingSet& out, InputBindingJsonReport* report = nullptr);
        bool ApplyOverrides(const InputBindingSet& defaults, const Container::String& json,
            InputBindingSet& out, InputBindingJsonReport* report = nullptr);
        bool WriteDefaults(const InputBindingSet& settings, Container::String& out);
        // 構造/型変更は差分にしない。失敗時outは維持する。
        bool WriteOverrides(const InputBindingSet& defaults, const InputBindingSet& current, Container::String& out);
    }
}
