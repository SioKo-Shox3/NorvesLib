// world-spaceのキャラクター移動を手組みproxyで確認する。
#include "Physics/CharacterMover.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Physics;
    namespace M = NorvesLib::Math;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Scene;
#define MOVE_CHECK(expr)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "CharacterMove %d: %s\n", __LINE__, #expr);                                           \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
    PhysicsShapeProxy Box(uint32_t id, M::Vector3 center, M::Vector3 extent)
    {
        PhysicsShapeProxy proxy;
        proxy.Collider = {id, 1};
        proxy.Shape = EPhysicsProxyShape::Box;
        proxy.Box = M::OBB(center, extent, M::Vector3::UnitX, M::Vector3::UnitY, M::Vector3::UnitZ);
        return proxy;
    }
    CharacterMoveRequest Start(float y = 0)
    {
        CharacterMoveRequest request;
        request.Shape = M::Capsule({0, y + .3f, 0}, {0, y + .5f, 0}, .3f);
        request.bWasGrounded = true;
        return request;
    }
    CharacterMoveResult Run(C::Span<const PhysicsShapeProxy> proxies, CharacterMoveRequest request, unsigned count,
                            const CharacterMoveSettings& settings = {})
    {
        CharacterMoveScratch scratch;
        CharacterMoveResult result;
        for (unsigned frame = 0; frame < count; ++frame)
        {
            MOVE_CHECK(CharacterMover::Move(proxies, settings, request, scratch, result) ==
                       S::EPhysicsSceneQueryResult::Success);
            MOVE_CHECK(!result.bStuck);
            request.Shape = result.Shape;
            request.bWasGrounded = result.bGrounded;
        }
        return result;
    }
    void EmptyWorldAndFailurePreservation()
    {
        auto request = Start(2);
        request.bWasGrounded = false;
        request.Displacement = {1, -.25f, 2};
        const auto moved = Run({}, request, 1);
        MOVE_CHECK(!moved.bGrounded && moved.Displacement == request.Displacement);
        CharacterMoveScratch scratch;
        CharacterMoveSettings settings;
        settings.SkinWidth = 0;
        CharacterMoveResult unchanged = moved;
        MOVE_CHECK(CharacterMover::Move({}, settings, request, scratch, unchanged) ==
                   S::EPhysicsSceneQueryResult::InvalidArgument);
        MOVE_CHECK(unchanged.Displacement == moved.Displacement && unchanged.Shape.PointA == moved.Shape.PointA);
    }
    void FlatAndRepeat()
    {
        const auto floor = Box(0, {0, -.5f, 0}, {20, .5f, 20});
        auto request = Start();
        request.Displacement = {5.f / 60, -.01f, 0};
        const auto first = Run({&floor, 1}, request, 60);
        const auto second = Run({&floor, 1}, request, 60);
        MOVE_CHECK(first.bGrounded && std::fabs(first.Shape.PointA.x - 5) < .01f);
        MOVE_CHECK(std::fabs(first.Shape.PointA.y - .3f) < .005f);
        MOVE_CHECK(std::bit_cast<uint32_t>(first.Shape.PointA.x) == std::bit_cast<uint32_t>(second.Shape.PointA.x));
        MOVE_CHECK(std::bit_cast<uint32_t>(first.Shape.PointA.y) == std::bit_cast<uint32_t>(second.Shape.PointA.y));
        MOVE_CHECK(std::bit_cast<uint32_t>(first.Shape.PointA.z) == std::bit_cast<uint32_t>(second.Shape.PointA.z));
    }
    void WallAndStep()
    {
        PhysicsShapeProxy proxies[] = {Box(0, {0, -.5f, 0}, {20, .5f, 20}), Box(1, {1, 1, 0}, {.025f, 1, 2})};
        auto request = Start();
        request.Displacement = {50, -.01f, 0};
        auto result = Run(proxies, request, 1);
        MOVE_CHECK(result.Shape.PointA.x < .676f && result.Shape.PointA.x > .66f && result.bGrounded);
        proxies[1] = Box(1, {1, .1f, 0}, {.2f, .1f, 2});
        request = Start();
        request.Displacement = {.1f, -.01f, 0};
        result = Run(proxies, request, 10);
        std::printf("CHARACTER_STEP low x=%.6f y=%.6f\n", result.Shape.PointA.x, result.Shape.PointA.y);
        MOVE_CHECK(result.Shape.PointA.x > .85f && result.Shape.PointA.y > .49f && result.bGrounded);
        proxies[1] = Box(1, {1, .2f, 0}, {.2f, .2f, 2});
        result = Run(proxies, request, 10);
        MOVE_CHECK(result.Shape.PointA.x < .6f && result.Shape.PointA.y < .31f);
    }
    void CeilingAndSnap()
    {
        PhysicsShapeProxy proxies[] = {Box(0, {0, -.5f, 0}, {20, .5f, 20}), Box(1, {0, 1.1f, 0}, {2, .1f, 2})};
        auto request = Start();
        request.Displacement = {0, 2, 0};
        request.bAllowGroundSnap = false;
        request.bAllowStep = false;
        auto result = Run(proxies, request, 1);
        MOVE_CHECK(result.bHitCeiling && !result.bGrounded && result.Shape.PointB.y < .701f);
        request = Start(.05f);
        request.Displacement = {};
        result = Run({proxies, 1}, request, 1);
        MOVE_CHECK(result.bGrounded && std::fabs(result.Shape.PointA.y - .302f) < .005f);
        request.bAllowGroundSnap = false;
        result = Run({proxies, 1}, request, 1);
        MOVE_CHECK(!result.bGrounded && std::fabs(result.Shape.PointA.y - .35f) < .0001f);
    }
    void DiagonalWallAndSteepSlope()
    {
        const float a = std::sqrt(.5f);
        PhysicsShapeProxy proxies[] = {Box(0, {0, -.5f, 0}, {20, .5f, 20}), Box(1, {}, {})};
        const M::Vector3 normal(-a, 0, a);
        proxies[1].Box =
            M::OBB(normal * -.325f + M::Vector3(0, 1, 0), {.025f, 2, 20}, normal, M::Vector3::UnitY, {-a, 0, -a});
        auto request = Start();
        request.Displacement = {5.f / 60, -.01f, 0};
        auto result = Run(proxies, request, 60);
        const float distance =
            std::sqrt(result.Shape.PointA.x * result.Shape.PointA.x + result.Shape.PointA.z * result.Shape.PointA.z);
        MOVE_CHECK(std::fabs(distance - 3.535534f) < .05f);
        MOVE_CHECK(std::fabs(result.Shape.PointA.x - result.Shape.PointA.z) < .005f);

        const float radians = 70 * M::Constants::PI / 180.f;
        const M::Vector3 up(-std::sin(radians), std::cos(radians), 0);
        const M::Vector3 right(std::cos(radians), std::sin(radians), 0);
        auto slope = Box(3, {}, {});
        slope.Box = M::OBB(up * -.2f, {5, .2f, 5}, right, up, M::Vector3::UnitZ);
        request = Start(1);
        request.bWasGrounded = false;
        request.Displacement = {0, -.1f, 0};
        CharacterMoveSettings settings;
        settings.StepHeight = settings.GroundSnapDistance = 0;
        result = Run({&slope, 1}, request, 20, settings);
        MOVE_CHECK(!result.bGrounded && result.Shape.PointA.x < -.1f && result.Shape.PointA.y < .5f);
    }
    void AirborneAndPlatformDelta()
    {
        auto floor = Box(0, {0, -.5f, 0}, {20, .5f, 20});
        auto request = Start(.05f);
        request.bWasGrounded = false;
        request.Displacement = {0, -.02f, 0};
        auto result = Run({&floor, 1}, request, 1);
        MOVE_CHECK(!result.bGrounded && std::fabs(result.Shape.PointA.y - .33f) < .0001f);
        request = Start(-.05f);
        request.Displacement = {};
        result = Run({&floor, 1}, request, 1);
        MOVE_CHECK(result.bGrounded && result.Shape.PointA.y >= .3f);

        // 足場アンカーの解決は呼出側。往復＋ヨー回転で得たdeltaをkernelへ渡す。
        floor.Body = {7, 1};
        CharacterMoveScratch scratch;
        request = Start();
        request.Shape.PointA.x = request.Shape.PointB.x = 1;
        M::Vector3 previous(1, 0, 0);
        for (unsigned frame = 1; frame <= 120; ++frame)
        {
            const float angle = frame * .025f;
            const M::Vector3 center(std::sin(frame * .05f), -.5f, 0);
            const M::Vector3 right(std::cos(angle), 0, -std::sin(angle));
            const M::Vector3 forward(std::sin(angle), 0, std::cos(angle));
            floor.Box = M::OBB(center, {3, .5f, 3}, right, M::Vector3::UnitY, forward);
            const M::Vector3 target(center.x + right.x, 0, right.z);
            request.PlatformDisplacement = target - previous;
            request.Displacement = {0, -.01f, 0};
            MOVE_CHECK(CharacterMover::Move({&floor, 1}, {}, request, scratch, result) ==
                       S::EPhysicsSceneQueryResult::Success);
            MOVE_CHECK(result.bGrounded && !result.bStuck && result.GroundBody == floor.Body);
            MOVE_CHECK(std::fabs(result.Shape.PointA.x - target.x) < .02f &&
                       std::fabs(result.Shape.PointA.z - target.z) < .02f);
            request.Shape = result.Shape;
            previous = target;
        }
    }
    void WalkableSlopeLimit()
    {
        CharacterMoveSettings settings;
        settings.StepHeight = 0;
        for (float degrees : {40.f, 60.f})
        {
            const float radians = degrees * M::Constants::PI / 180.f;
            const M::Vector3 up(-std::sin(radians), std::cos(radians), 0);
            const M::Vector3 right(std::cos(radians), std::sin(radians), 0);
            auto slope = Box(9, {}, {});
            slope.Box = M::OBB(up * -.5f, {20, .5f, 20}, right, up, M::Vector3::UnitZ);
            auto request = Start();
            const float initial = .302f / up.y;
            request.Shape.PointA.y = initial;
            request.Shape.PointB.y = initial + .2f;
            request.Displacement = {5.f / 60, -.01f, 0};
            const auto result = Run({&slope, 1}, request, 60, settings);
            if (degrees < settings.MaximumSlopeDegrees)
                MOVE_CHECK(result.bGrounded && result.Shape.PointA.x > 2 && result.Shape.PointA.y > initial + 1);
            else
                MOVE_CHECK(!result.bGrounded && result.Shape.PointA.x < .1f && result.Shape.PointA.y <= initial + .01f);
        }
    }
} // namespace
int main()
{
    EmptyWorldAndFailurePreservation();
    FlatAndRepeat();
    WallAndStep();
    CeilingAndSnap();
    DiagonalWallAndSteepSlope();
    AirborneAndPlatformDelta();
    WalkableSlopeLimit();
    std::puts("PhysicsCharacterMoveTest PASS");
    return 0;
}
