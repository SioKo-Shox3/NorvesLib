#pragma once
#include "SingleAssetCook.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    [[nodiscard]] bool HasSkeletalRoleCliArguments(int argc, const char* const* argv);
    [[nodiscard]] bool DecodeSkeletalRoleWideArgument(Core::Container::Span<const wchar_t> units,
                                                      Core::Container::AnsiString& out,
                                                      Core::Container::AnsiString& error);
    [[nodiscard]] bool ValidateSkeletalRoleModeAgreement(
        int argc, const char* const* argv,
        const Core::Container::VariableArray<Core::Container::AnsiString>& wideTokens,
        Core::Container::AnsiString& error);
    [[nodiscard]] bool ParseSkeletalRoleUtf8Arguments(
        const Core::Container::VariableArray<Core::Container::AnsiString>& tokens, SingleAssetCookRequest& out,
        Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
