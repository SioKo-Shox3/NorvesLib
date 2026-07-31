#include <cstdint>

struct Vec3
{
    float X;
    float Y;
    float Z;
};

struct Box
{
    Vec3 Center;
    Vec3 HalfExtent;
};

struct SceneHandle
{
    std::uint32_t Index;
    std::uint32_t Generation;
};

struct BodyHandle
{
    std::uint32_t Index;
    std::uint32_t Generation;
};

struct BodyDesc
{
    std::uint32_t StableId;
    Box Bounds;
    Vec3 Scale;
    bool Dynamic;
    bool Root;
};

struct Contact
{
    bool Exists;
    BodyHandle A;
    BodyHandle B;
    Vec3 Normal;
    float Depth;
};

struct CandidatePair
{
    std::uint32_t FirstStableId;
    std::uint32_t SecondStableId;
};

struct CandidateList
{
    std::uint32_t Count;
    CandidatePair Pairs[16];
};

struct QueryResult
{
    std::uint32_t Count;
    BodyHandle Bodies[16];
};

struct HitEvent
{
    BodyHandle A;
    BodyHandle B;
    Vec3 Normal;
    float Depth;
};

using QuerySnapshotObserver = void(*)(const QueryResult&);
using HitObserver = void(*)(const HitEvent&);

struct StepResult
{
    QueryResult QuerySnapshot;
    std::uint32_t EventCount;
    HitEvent Events[16];
};

class SceneQueryFacade
{
public:
    explicit SceneQueryFacade(SceneHandle)
    {
    }

    QueryResult Overlap(const Box&) const
    {
        return {};
    }

    Contact ContactBetween(BodyHandle, BodyHandle) const
    {
        return {};
    }

    CandidateList SapCandidates() const
    {
        return {};
    }

    QueryResult PublishedSnapshot() const
    {
        return {};
    }

    Box BoundsOf(BodyHandle) const
    {
        return {};
    }
};

SceneHandle CreateScene()
{
    return {};
}

void DestroyScene(SceneHandle)
{
}

BodyHandle CreateBody(SceneHandle, const BodyDesc&)
{
    return {};
}

bool DestroyBody(SceneHandle, BodyHandle)
{
    return false;
}

bool IsLive(SceneHandle, BodyHandle)
{
    return false;
}

bool SetVelocity(SceneHandle, BodyHandle, Vec3)
{
    return false;
}

SceneQueryFacade MakeSceneQueryFacade(SceneHandle)
{
    return SceneQueryFacade({});
}

StepResult StepPhysics(SceneHandle, QuerySnapshotObserver, HitObserver)
{
    return {};
}

std::uint32_t GameQueryConsumer(const SceneQueryFacade&, const Box&)
{
    return 0;
}
