#pragma once
#include "CookDependencySnapshot.h"
#include "Animation/SkeletalClipRetarget.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    [[nodiscard]] inline bool HasRigRetarget(const SingleAssetCookRequest& r)
    {
        return !r.RetargetSkeletonPath.empty() || !r.RetargetProfilePath.empty() || !r.RetargetSourceClip.empty() ||
               !r.RetargetClipName.empty();
    }
    [[nodiscard]] inline bool IsBvhRetargetSource(const SingleAssetCookRequest& r)
    {
        const auto extension = r.InputPath.extension().native();
        return HasRigRetarget(r) && extension.size() == 4 && extension[0] == '.' &&
               (extension[1] == 'b' || extension[1] == 'B') && (extension[2] == 'v' || extension[2] == 'V') &&
               (extension[3] == 'h' || extension[3] == 'H');
    }
    [[nodiscard]] bool CookRigRetargetBank(const SingleAssetCookRequest&, Core::Container::Span<const uint8_t> source,
                                           const CookDependencySnapshot&,
                                           Core::Container::VariableArray<uint8_t>& payload,
                                           Core::Container::AnsiString& error);
    [[nodiscard]] bool RunRigRetargetCommand(int argc, const char* const* argv, int& exitCode);
} // namespace NorvesLib::Tools::AssetCook::Detail
