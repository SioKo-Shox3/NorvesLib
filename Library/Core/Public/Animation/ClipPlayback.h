#pragma once
#include "Animation/AnimGraphResource.h"
namespace NorvesLib::Core::Animation
{
    // 折返し前の秒を渡す。GR11はUpdate-onlyでもこの列からイベントとroot
    // deltaを作れる。
    struct AnimClipTraversal
    {
        uint32_t Node = InvalidAnimNode;
        uint32_t Clip = InvalidAnimNode;
        double Previous = 0;
        double Current = 0;
        float Weight = 0;
        bool bLoop = true;
        Identity SyncGroup;
        bool bReverse = false;
    };
} // namespace NorvesLib::Core::Animation
