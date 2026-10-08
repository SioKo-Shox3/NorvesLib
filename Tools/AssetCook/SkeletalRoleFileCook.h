#pragma once
#include "SingleAssetCook.h"
#include "CookCacheDecision.h"
namespace NorvesLib::Tools::AssetCook
{
    inline constexpr uint64_t SkeletalRoleFileCookerRevision = 1;
    struct SkeletalRoleFileCookResult
    {
        SkeletalRoleProfileCookResult Value;
        CookOutputRecord Record;
        Core::Container::AnsiString ReportJson;
        uint64_t DependencyFingerprint = 0;
    };
    // 常時cook。最初のwrite前の拒否は出力fileを変更しない。
    // package/manifestの二file transactionではない。write開始後のI/O失敗は部分出力を残し得る。
    // 安定filesystemで使い、同時writer・敵対的ABA・電源断回復を保証しない。
    [[nodiscard]] bool CookSkeletalRoleFile(const SingleAssetCookRequest& request, SkeletalRoleFileCookResult& out,
                                            Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
