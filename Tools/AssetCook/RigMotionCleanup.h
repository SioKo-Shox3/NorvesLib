#pragma once
#include "Animation/SkeletalRoleProfile.h"
#include "Resource/RigAuthoring.h"
#include "Text/JsonWriter.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    // JSONのclip object内へ計測結果を追記し、明示指定されたmetadata/時間補正だけを適用する。
    bool ApplyRigMotionCleanup(const Core::Skeletal::RigAuthoringCpu&, const Core::Animation::SkeletalRoleProfile&,
                               Core::Skeletal::SkeletalAnimationClip&, Core::JsonWriter&,
                               Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
