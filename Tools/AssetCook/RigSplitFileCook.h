#pragma once
#include "RigSplitCook.h"
namespace NorvesLib::Tools::AssetCook
{
    struct RigSplitFileCookResult
    {
        Core::Container::AnsiString ManifestJson;
        uint64_t SourceHash = 0;
        uint32_t JointCount = 0, ClipCount = 0, TextureCount = 0;
    };
    // 新規出力directory専用。全packageを検証してからdirectoryを一度だけ公開する。
    [[nodiscard]] bool CookRigSplitFile(const RigSplitCookRequest& request, const std::filesystem::path& destination,
                                        RigSplitFileCookResult& out, Core::Container::AnsiString& error);
    [[nodiscard]] bool RunRigSplitFileCommand(int argc, const char* const* argv, int& exitCode);
} // namespace NorvesLib::Tools::AssetCook
