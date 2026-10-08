#pragma once
#include "CookOwnedState.h"
#include <filesystem>
namespace NorvesLib::Tools::AssetCook
{
    struct CookStateFileRequest
    {
        // Windows local-driveのASCII絶対locator。state親とRuntimeRootは既存directoryが必要。
        std::filesystem::path StatePath, RuntimeRoot;
        // 呼出間でcallerが独立に保持する。同じ保存fileからOwnerIdを採用してはならない。
        CookStateBinding ExpectedBinding;
    };
    enum class CookStateLoadResult
    {
        Missing,
        Loaded,
        Error
    };
    // 最終leafだけの不在をMissingとする。Loadedだけoutを変更し、既存出力の採用は許可しない。
    [[nodiscard]] CookStateLoadResult LoadCookOwnedState(const CookStateFileRequest& request, CookOwnedState& out,
                                                         Core::Container::AnsiString& error);
    // 外部stateの新規保存のみ。既存宛先を置換せず、同volumeの自分のhandleだけを操作する。
    // hostileなnamespace変更/電源断耐久性/production rootとのtransactionは保証しない。
    [[nodiscard]] bool WriteNewCookOwnedState(const CookStateFileRequest& request, const CookOwnedState& state,
                                              Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
