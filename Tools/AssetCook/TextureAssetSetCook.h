#pragma once
#include "CookManagedUpdate.h"
#include <filesystem>
namespace NorvesLib::Tools::AssetCook
{
    struct TextureAssetSetCookRequest
    {
        std::filesystem::path SpecPath, SourceRoot, RuntimeRoot, ManifestPath;
    };
    enum class TextureAssetSetCookResult
    {
        Created,
        Updated,
        NoChange,
        NeedsRecovery,
        StorePublishedButError,
        CommittedButError,
        Busy,
        Conflict,
        Error
    };
    // 成功3種だけoutを更新する。errorはrequest/outと独立に渡す。
    [[nodiscard]] TextureAssetSetCookResult CookTextureAssetSetWithOutcome(const TextureAssetSetCookRequest& request,
                                                                           CookManagedBootstrapOutcome& out,
                                                                           Core::Container::AnsiString& error);
    // 既存caller用の委譲だけを残す。別のcook/増分/公開経路は持たない。
    [[nodiscard]] bool CookTextureAssetSet(const TextureAssetSetCookRequest& request,
                                           Core::Container::AnsiString& error);
    // --asset-setまたは--recoverを含む場合だけ処理し、呼出元に終了codeを返す。
    [[nodiscard]] bool RunTextureAssetSetCommand(int argc, const char* const* argv, int& exitCode);
    namespace Detail
    {
        // raw componentを先に検査し、安全なdot移動だけをcwdから解決する。I/Oや所有取得は行わない。
        [[nodiscard]] bool ResolveTextureCliPath(const std::filesystem::path& path, const std::filesystem::path& cwd,
                                                 std::filesystem::path& out, Core::Container::AnsiString& error,
                                                 bool bDirectory = false);
    } // 名前空間 Detail
} // 名前空間 NorvesLib::Tools::AssetCook
