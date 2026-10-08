#pragma once
namespace NorvesLib::Tools::AssetCook
{
    // 新しい4引数がある場合だけ処理する。旧modeのargvと診断は変えない。
    [[nodiscard]] bool RunSkeletalRoleFileCommand(int argc, const char* const* argv, int& exitCode);
} // namespace NorvesLib::Tools::AssetCook
