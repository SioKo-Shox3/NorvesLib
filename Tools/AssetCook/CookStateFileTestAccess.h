#pragma once
#include "CookStateFile.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    enum class CookStateFileFault
    {
        None,
        AfterCreate,
        PartialWrite,
        ZeroWrite,
        Flush,
        Seek,
        ReadBack,
        ByteMismatch,
        ParseVerification,
        Rename,
        CleanupDisposition
    };
    struct CookStateFileProbe
    {
        CookStateFileFault Fault = CookStateFileFault::None;
        // 最初の候補だけ差し替える。既存衝突を所有/削除しないことを試験する。
        Core::Container::AnsiString FirstTempLeaf;
        void (*BeforePublish)(const std::filesystem::path& destination, void* context) = nullptr;
        void* Context = nullptr;
    };
    [[nodiscard]] bool WriteNewCookOwnedStateForTest(const CookStateFileRequest& request, const CookOwnedState& state,
                                                     const CookStateFileProbe& probe,
                                                     Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
