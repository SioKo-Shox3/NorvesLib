// 姿勢合成の端点・最短回転・加算基準・枝マスク・失敗時の不変性を検証する。
#include "Animation/PoseOps.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    namespace A = NorvesLib::Core::Animation;
    namespace C = NorvesLib::Core::Container;
    void CheckPose(bool value)
    {
        if (!value)
        {
            std::fputs("AnimPoseOpsTest failed\n", stderr);
            std::abort();
        }
    }
    bool NearPose(float a, float b)
    {
        return std::fabs(a - b) < 1e-5f;
    }
} // namespace
void TestAnimPoseOps()
{
    A::LocalPose base(2), target(2), reference(2);
    target[0].Translation = {4, 2, 0};
    target[1].Translation = {8, 0, 0};
    target[0].Rotation = {0, 0, 0.70710678f, 0.70710678f};
    target[0].Scale = {3, 1, 1};
    const auto* address = base.data();
    const auto capacity = base.capacity();
    CheckPose(A::PoseOps::BlendInto(base, target, 0));
    CheckPose(base[0].Translation.x == 0 && base[0].Scale.x == 1);
    const float mask[] = {1, 0};
    CheckPose(A::PoseOps::BlendInto(base, target, 0.5f, mask));
    CheckPose(NearPose(base[0].Translation.x, 2) && NearPose(base[0].Scale.x, 2) && base[1].Translation.x == 0);
    auto rotated = base[0].Rotation * NorvesLib::Math::Vector3::UnitX;
    CheckPose(NearPose(rotated.x, 0.70710678f) && NearPose(rotated.y, 0.70710678f));
    CheckPose(base.data() == address && base.capacity() == capacity);
    CheckPose(A::PoseOps::BlendInto(base, target, 1));
    CheckPose(base[0].Translation == target[0].Translation && base[0].Rotation == target[0].Rotation);
    auto before = base;
    CheckPose(A::PoseOps::AddInto(base, target, target, 1));
    CheckPose(base[0].Translation == before[0].Translation && NearPose(base[0].Rotation.z, before[0].Rotation.z));
    A::LocalPose identity(2);
    CheckPose(A::PoseOps::AddInto(identity, target, reference, 1, mask));
    CheckPose(identity[0].Translation == target[0].Translation && identity[1].Translation.x == 0);
    auto negative = target;
    for (auto& t : negative)
    {
        t.Rotation = {-t.Rotation.x, -t.Rotation.y, -t.Rotation.z, -t.Rotation.w};
    }
    CheckPose(A::PoseOps::BlendInto(base, negative, 0.5f));
    CheckPose(NearPose(base[0].Rotation.z, target[0].Rotation.z));
    before = base;
    CheckPose(!A::PoseOps::BlendInto(base, target, std::numeric_limits<float>::quiet_NaN()));
    CheckPose(base[0].Translation == before[0].Translation);
    target[1].Translation.x = std::numeric_limits<float>::infinity();
    CheckPose(!A::PoseOps::BlendInto(base, target, 0.5f));
    CheckPose(base[0].Translation == before[0].Translation);
    const int32_t parents[] = {2, -1, 1, 1};
    A::BoneMask bones;
    CheckPose(bones.Build(parents, 2));
    CheckPose(bones.Weights[0] == 1 && bones.Weights[1] == 0 && bones.Weights[2] == 1 && bones.Weights[3] == 0);
    CheckPose(bones.Build(parents, 1, 2));
    CheckPose(bones.Weights[1] == 0.5f && bones.Weights[0] == 1 && bones.Weights[2] == 1);
    CheckPose(!bones.Build(parents, 5));
    std::puts("AnimPoseOpsTest PASS");
}
