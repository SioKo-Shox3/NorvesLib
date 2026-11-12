#pragma once
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    // role または FK 出力の引数がある場合だけ処理する。旧 mode の argv と診断は変えない。
    [[nodiscard]] bool RunSkeletalRoleFileCommand(int argc, const char* const* argv, int& exitCode);
} // namespace NorvesLib::Tools::AssetCook

namespace NorvesLib::Tools::AssetCook::Detail
{
    // cook と同じ検査で BVH のバイト列だけを読む。成功時だけ out を置き換える。
    [[nodiscard]] bool LoadSkeletalBvhFileBytes(const std::filesystem::path& path, size_t maximum,
        Core::Container::VariableArray<uint8_t>& out, Core::Container::AnsiString& error);
    // --dump-fk の引数(先頭はプログラム名の UTF-8 トークン列)を解析し、BVH を読んで FK の JSON を返す。
    // 成功時だけ json を置き換える。cook も書き込みもしない。
    [[nodiscard]] bool RunSkeletalBvhFkDump(
        const Core::Container::VariableArray<Core::Container::AnsiString>& tokens,
        Core::Container::AnsiString& json, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
