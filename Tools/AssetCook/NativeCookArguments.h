#pragma once
#include "Container/String.h"
#include "Container/VariableArray.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    // WindowsではOSのUTF16引数を使う。既存role CLIと同じ上限と変換を共有する。
    [[nodiscard]] bool CollectNativeCookArguments(int argc, const char* const* argv,
                                                  Core::Container::VariableArray<Core::Container::AnsiString>& out,
                                                  Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
