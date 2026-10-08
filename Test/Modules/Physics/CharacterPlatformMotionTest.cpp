#include "Physics/CharacterPlatformMotion.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace
{
    namespace M = NorvesLib::Math;
    using namespace NorvesLib::Modules::Physics;
#define ANCHOR_CHECK(expr)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "CharacterAnchor %d: %s\n", __LINE__, #expr);                                         \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
    void Near(const M::Vector3& a, const M::Vector3& b)
    {
        ANCHOR_CHECK((a - b).Length() < 1e-4f);
    }
    void KnownAffineFrames()
    {
        M::Transform owner({10, 2, 3}, M::Quaternion(M::Vector3::UnitY, M::Constants::HALF_PI), {2, 3, -1});
        M::Transform local({1, 0, 0}, M::Quaternion(M::Vector3::UnitZ, M::Constants::HALF_PI));
        M::Vector3 world, anchor;
        float yaw = 0, again = 0;
        ANCHOR_CHECK(ResolveCharacterAnchor(owner, local, {1, 2, 3}, world, yaw));
        Near(world, {7, 5, 5});
        ANCHOR_CHECK(std::fabs(yaw - M::Constants::HALF_PI) < 1e-4f);
        ANCHOR_CHECK(CaptureCharacterAnchor(owner, local, world, anchor, again));
        Near(anchor, {1, 2, 3});
        owner.rotation =
            M::Quaternion(owner.rotation.x * 2, owner.rotation.y * 2, owner.rotation.z * 2, owner.rotation.w * 2);
        ANCHOR_CHECK(ResolveCharacterAnchor(owner, local, {1, 2, 3}, world, yaw));
        Near(world, {-2, 14, 11});
        ANCHOR_CHECK(CaptureCharacterAnchor(owner, local, world, anchor, again));
        Near(anchor, {1, 2, 3});
        owner.scale.z = 1;
        ANCHOR_CHECK(ResolveCharacterAnchor(owner, local, {1, 2, 3}, world, again));
        ANCHOR_CHECK(std::fabs(again - yaw) < 1e-4f);
    }
    void TranslationYawAndFailure()
    {
        M::Transform owner, local;
        M::Vector3 anchor, world;
        float yaw;
        ANCHOR_CHECK(CaptureCharacterAnchor(owner, local, {1, 0, 0}, anchor, yaw));
        for (unsigned frame = 0; frame <= 120; ++frame)
        {
            const float angle = frame * .01f, shift = std::sin(frame * .02f);
            owner.position = {shift, 0, 0};
            owner.rotation = M::Quaternion(M::Vector3::UnitY, angle);
            ANCHOR_CHECK(ResolveCharacterAnchor(owner, local, anchor, world, yaw));
            Near(world, {shift + std::cos(angle), 0, -std::sin(angle)});
            ANCHOR_CHECK(std::fabs(yaw - angle) < 1e-4f);
        }
        const auto unchanged = world;
        const float oldYaw = yaw;
        owner.scale.x = 0;
        ANCHOR_CHECK(!ResolveCharacterAnchor(owner, local, anchor, world, yaw));
        ANCHOR_CHECK(world == unchanged && yaw == oldYaw);
        ANCHOR_CHECK(!CaptureCharacterAnchor(owner, local, world, anchor, yaw));
    }
} // namespace
int main()
{
    KnownAffineFrames();
    TranslationYawAndFailure();
    std::puts("CharacterPlatformMotionTest PASS");
    return 0;
}
