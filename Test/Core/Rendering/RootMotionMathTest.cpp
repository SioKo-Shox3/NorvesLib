// 平面差分の合成と逆再生をエンジン非依存で検証する。
#include "Animation/RootMotion.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace
{
    void RootCheck(bool ok)
    {
        if (!ok)
        {
            std::fputs("RootMotionMathTest failed\n", stderr);
            std::abort();
        }
    }
    bool CloseRoot(double a, double b)
    {
        return std::fabs(a - b) < 1e-8;
    }
} // namespace
void TestRootMotionMath()
{
    using namespace NorvesLib::Core::Animation;
    const RootMotionDelta a{2, 3, .8}, b{-1, 2, -.2};
    const auto identity = ComposeRootMotion(a, InverseRootMotion(a));
    RootCheck(CloseRoot(identity.X, 0) && CloseRoot(identity.Z, 0) && CloseRoot(identity.Yaw, 0));
    const auto restored = ComposeRootMotion(a, RootMotionBetween(a, b));
    RootCheck(CloseRoot(restored.X, b.X) && CloseRoot(restored.Z, b.Z) && CloseRoot(restored.Yaw, b.Yaw));
    RootMotionDelta circle;
    RootCheck(RepeatRootMotion({1, 0, 1.5707963267948966}, 4, circle));
    RootCheck(CloseRoot(circle.X, 0) && CloseRoot(circle.Z, 0) && CloseRoot(circle.Yaw, 6.283185307179586));
    RootCheck(RepeatRootMotion({1, 0, 1.5707963267948966}, -4, circle));
    RootCheck(CloseRoot(circle.X, 0) && CloseRoot(circle.Z, 0) && CloseRoot(circle.Yaw, -6.283185307179586));
    std::puts("RootMotionMathTest PASS");
}
#if defined(NORVES_ROOT_MOTION_STANDALONE)
int main()
{
    TestRootMotionMath();
}
#endif
