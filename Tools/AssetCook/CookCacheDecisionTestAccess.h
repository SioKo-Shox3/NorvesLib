#pragma once
// 実I/Oの途中で入力を変える試験だけに用いるprivate seam。production入口はprobe無しで呼ぶ。
#include "CookCacheDecision.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    CookDecision DecideCookCacheWithProbe(const SingleAssetCookRequest& request, uint64_t revision, bool bAllowSkip,
                                          const CookOutputRecord* previous, const Core::Asset::AssetManifest* manifest,
                                          CookDecisionContext& out, Core::Container::AnsiString& error,
                                          void (*afterPrepare)(void*), void* probeContext);
    bool CaptureCookOutputRecordWithProbe(const CookDecisionContext& before, const Core::Asset::AssetManifest& manifest,
                                          CookOutputRecord& out, Core::Container::AnsiString& error,
                                          void (*afterPrepare)(void*), void* probeContext);
} // namespace NorvesLib::Tools::AssetCook::Detail
