#include <exception>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <type_traits>

#include "PhysicsSceneQueryFacade.cpp"

namespace
{
    constexpr std::uint32_t InvalidIndex = 0xFFFFFFFFU;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool IsInvalid(BodyHandle handle)
    {
        return handle.Index == InvalidIndex;
    }

    bool IsPositiveX(Vec3 value)
    {
        return value.X == 1.0F && value.Y == 0.0F && value.Z == 0.0F;
    }

    bool IsNegativeX(Vec3 value)
    {
        return value.X == -1.0F && value.Y == 0.0F && value.Z == 0.0F;
    }

    BodyDesc MakeBody(std::uint32_t stableId, float x, bool dynamic = false, bool root = true)
    {
        return { stableId, { { x, 0.0F, 0.0F }, { 1.0F, 1.0F, 1.0F } }, { 1.0F, 1.0F, 1.0F }, dynamic, root };
    }

    enum class CallbackKind : std::uint8_t { Publish, Hit };
    CallbackKind GCallbackOrder[8] {};
    std::uint32_t GCallbackCount = 0U;
    std::uint32_t GPublishedCount = 0U;
    SceneQueryFacade GHitQuery { { InvalidIndex, 0U } };
    bool GHitObservedPublishedSnapshot = false;
    BodyHandle GObservedDynamic {};
    bool GObserveBounds = false;
    float GHitPublishedX = 0.0F;

    void CapturePublish(const QueryResult& snapshot)
    {
        GCallbackOrder[GCallbackCount++] = CallbackKind::Publish;
        GPublishedCount = snapshot.Count;
    }

    void CaptureHit(const HitEvent&)
    {
        GCallbackOrder[GCallbackCount++] = CallbackKind::Hit;
        GHitObservedPublishedSnapshot = GHitQuery.PublishedSnapshot().Count == GPublishedCount;
        if (GObserveBounds)
        {
            GHitPublishedX = GHitQuery.BoundsOf(GObservedDynamic).Center.X;
        }
    }
} // namespace

static_assert(!std::is_pointer_v<decltype(QueryResult{}.Bodies)>);
static_assert(!std::is_pointer_v<decltype(QueryResult{}.Bodies[0])>);
static_assert(!std::is_pointer_v<decltype(HitEvent{}.A)>);
static_assert(std::is_trivially_copyable_v<QueryResult>);
static_assert(std::is_trivially_copyable_v<HitEvent>);
static_assert(std::is_standard_layout_v<QueryResult> && sizeof(QueryResult) == sizeof(std::uint32_t) + sizeof(BodyHandle) * 16U);
static_assert(std::is_standard_layout_v<HitEvent> && sizeof(HitEvent) == sizeof(BodyHandle) * 2U + sizeof(Vec3) + sizeof(float));

int main()
{
    try
    {
        const SceneHandle scene = CreateScene();
        const BodyHandle a = CreateBody(scene, MakeBody(100U, 0.0F, true));
        const BodyHandle b = CreateBody(scene, MakeBody(200U, 2.0F));
        Require(!IsInvalid(a) && !IsInvalid(b), "root bodies cannot be registered");

        const SceneQueryFacade query = MakeSceneQueryFacade(scene);
        const Contact forward = query.ContactBetween(a, b);
        const Contact reverse = query.ContactBetween(b, a);
        Require(forward.Exists && forward.Depth == 0.0F && IsPositiveX(forward.Normal), "A-to-B touching contact is incorrect");
        Require(reverse.Exists && reverse.Depth == 0.0F && IsNegativeX(reverse.Normal), "reversed contact normal is incorrect");

        Require(DestroyBody(scene, a), "registered body cannot be destroyed");
        const BodyHandle replacement = CreateBody(scene, MakeBody(300U, -4.0F));
        Require(replacement.Index == a.Index && replacement.Generation != a.Generation, "body slot is not reused with a new generation");
        Require(!IsLive(scene, a), "stale generation handle was accepted");
        Require(IsLive(scene, replacement), "replacement generation handle was rejected");
        Require(!query.ContactBetween(a, replacement).Exists, "stale generation handle was accepted by contact query");
        Require(!DestroyBody(scene, a), "stale generation handle was accepted by destroy");

        const SceneHandle firstOrderScene = CreateScene();
        CreateBody(firstOrderScene, MakeBody(30U, 4.0F));
        CreateBody(firstOrderScene, MakeBody(10U, 0.0F));
        CreateBody(firstOrderScene, MakeBody(20U, 2.0F));
        const CandidateList firstCandidates = MakeSceneQueryFacade(firstOrderScene).SapCandidates();
        Require(firstCandidates.Count == 2U, "SAP did not retain touching candidates");
        Require(firstCandidates.Pairs[0].FirstStableId == 10U && firstCandidates.Pairs[0].SecondStableId == 20U, "SAP candidate order is not deterministic");
        Require(firstCandidates.Pairs[1].FirstStableId == 20U && firstCandidates.Pairs[1].SecondStableId == 30U, "SAP candidate order is not deterministic");

        const SceneHandle secondOrderScene = CreateScene();
        CreateBody(secondOrderScene, MakeBody(20U, 2.0F));
        CreateBody(secondOrderScene, MakeBody(30U, 4.0F));
        CreateBody(secondOrderScene, MakeBody(10U, 0.0F));
        const CandidateList secondCandidates = MakeSceneQueryFacade(secondOrderScene).SapCandidates();
        Require(secondCandidates.Count == firstCandidates.Count, "SAP input permutation changed candidate count");
        for (std::uint32_t index = 0; index < firstCandidates.Count; ++index)
        {
            Require(firstCandidates.Pairs[index].FirstStableId == secondCandidates.Pairs[index].FirstStableId &&
                        firstCandidates.Pairs[index].SecondStableId == secondCandidates.Pairs[index].SecondStableId,
                    "SAP input permutation changed candidate order");
        }

        const SceneHandle eventScene = CreateScene();
        CreateBody(eventScene, MakeBody(1U, 0.0F, true));
        CreateBody(eventScene, MakeBody(2U, 1.0F));
        GHitQuery = MakeSceneQueryFacade(eventScene);
        const StepResult firstStep = StepPhysics(eventScene, CapturePublish, CaptureHit);
        Require(firstStep.EventCount == 1U, "OnHit was not emitted once for the initial overlapping pair");
        Require(GCallbackCount == 2U && GCallbackOrder[0] == CallbackKind::Publish && GCallbackOrder[1] == CallbackKind::Hit,
                "M7 query snapshot callback was not published before hit dispatch callbacks");
        Require(GPublishedCount == 2U && GHitObservedPublishedSnapshot && firstStep.QuerySnapshot.Count == 2U,
                "hit dispatch could not observe the already-published query snapshot");
        GCallbackCount = 0U;
        Require(StepPhysics(eventScene, CapturePublish, CaptureHit).EventCount == 0U && GCallbackCount == 1U,
                "OnHit was re-emitted for an unchanged pair");
        CreateBody(eventScene, MakeBody(3U, 10.0F, true));
        CreateBody(eventScene, MakeBody(4U, 11.0F));
        GCallbackCount = 0U;
        GHitObservedPublishedSnapshot = false;
        Require(StepPhysics(eventScene, CapturePublish, CaptureHit).EventCount == 1U && GCallbackCount == 2U &&
                    GCallbackOrder[0] == CallbackKind::Publish && GCallbackOrder[1] == CallbackKind::Hit &&
                    GPublishedCount == 4U && GHitObservedPublishedSnapshot,
                "new overlap pair was suppressed by scene-wide OnHit state or published too late");
        GCallbackCount = 0U;
        Require(StepPhysics(eventScene, CapturePublish, CaptureHit).EventCount == 0U && GCallbackCount == 1U,
                "new pair OnHit was re-emitted every overlapping frame");

        const SceneHandle motionScene = CreateScene();
        const BodyHandle movingBody = CreateBody(motionScene, MakeBody(500U, 0.0F, true));
        const BodyHandle staticTarget = CreateBody(motionScene, MakeBody(501U, 4.1F));
        const BodyHandle retiredVelocityBody = CreateBody(motionScene, MakeBody(502U, -10.0F));
        Require(DestroyBody(motionScene, retiredVelocityBody), "velocity stale-handle fixture setup failed");
        CreateBody(motionScene, MakeBody(503U, -10.0F));
        const SceneQueryFacade motionQuery = MakeSceneQueryFacade(motionScene);
        Require(SetVelocity(motionScene, movingBody, { 0.25F, 0.0F, 0.0F }), "live velocity command was rejected");
        Require(!SetVelocity(motionScene, retiredVelocityBody, { 0.25F, 0.0F, 0.0F }), "stale velocity command was accepted");
        const float initialX = motionQuery.BoundsOf(movingBody).Center.X;
        StepPhysics(motionScene, nullptr, nullptr);
        const float firstX = motionQuery.BoundsOf(movingBody).Center.X;
        StepPhysics(motionScene, nullptr, nullptr);
        const float secondX = motionQuery.BoundsOf(movingBody).Center.X;
        const float firstDelta = firstX - initialX;
        const float secondDelta = secondX - firstX;
        Require(firstX > initialX && std::abs(secondDelta - firstDelta) <= std::abs(firstDelta) * 0.01F,
                "velocity was not integrated deterministically at step start");
        GHitQuery = motionQuery;
        GObservedDynamic = movingBody;
        GObserveBounds = true;
        bool observedHit = false;
        for (std::uint32_t step = 0U; step < 240U && !observedHit; ++step)
        {
            const Contact before = motionQuery.ContactBetween(movingBody, staticTarget);
            GCallbackCount = 0U;
            const StepResult motionStep = StepPhysics(motionScene, CapturePublish, CaptureHit);
            const Contact after = motionQuery.ContactBetween(movingBody, staticTarget);
            if (motionStep.EventCount == 1U)
            {
                observedHit = true;
                Require(!before.Exists && after.Exists && std::abs(GHitPublishedX - motionQuery.BoundsOf(movingBody).Center.X) <= 0.0001F,
                        "integration, published query snapshot, and hit dispatch were not causally ordered");
            }
        }
        GObserveBounds = false;
        Require(observedHit, "integrated body did not produce a new target hit within 240 steps");

        const BodyHandle childDynamic = CreateBody(scene, MakeBody(400U, 8.0F, true, false));
        Require(IsInvalid(childDynamic), "child dynamic body was accepted");
        BodyDesc negativeScale = MakeBody(401U, 8.0F);
        negativeScale.Scale.X = -1.0F;
        Require(IsInvalid(CreateBody(scene, negativeScale)), "negative scale was accepted");
        negativeScale = MakeBody(402U, 8.0F);
        negativeScale.Scale.Y = -1.0F;
        Require(IsInvalid(CreateBody(scene, negativeScale)), "negative Y scale was accepted");
        negativeScale = MakeBody(403U, 8.0F);
        negativeScale.Scale.Z = -1.0F;
        Require(IsInvalid(CreateBody(scene, negativeScale)), "negative Z scale was accepted");
        BodyDesc nonUniformScale = MakeBody(402U, 8.0F);
        nonUniformScale.Scale.X = 2.0F;
        Require(IsInvalid(CreateBody(scene, nonUniformScale)), "nonuniform X scale was accepted");
        nonUniformScale = MakeBody(403U, 8.0F);
        nonUniformScale.Scale.Y = 2.0F;
        Require(IsInvalid(CreateBody(scene, nonUniformScale)), "nonuniform Y scale was accepted");
        nonUniformScale = MakeBody(404U, 8.0F);
        nonUniformScale.Scale.Z = 2.0F;
        Require(IsInvalid(CreateBody(scene, nonUniformScale)), "nonuniform Z scale was accepted");

        const Box matchingQuery = { { -1.0F, 0.0F, 0.0F }, { 4.0F, 2.0F, 2.0F } };
        const Box emptyQuery = { { 100.0F, 0.0F, 0.0F }, { 1.0F, 1.0F, 1.0F } };
        Require(query.Overlap(matchingQuery).Count == 2U && GameQueryConsumer(query, matchingQuery) == 2U &&
                    query.Overlap(emptyQuery).Count == 0U && GameQueryConsumer(query, emptyQuery) == 0U,
                "game query consumer did not preserve distinct facade overlap results");
        DestroyScene(scene);
        DestroyScene(firstOrderScene);
        DestroyScene(secondOrderScene);
        DestroyScene(eventScene);
        DestroyScene(motionScene);
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
