#include "Component/AnimatorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/Component.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/SpringArmComponent.h"
#include "Object/World.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core;
namespace
{
    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using ProbeBase = Component::Component;

    // channel は型ごとの登録に持つので、channel ごとに型を分ける
    class TimeProbe : public ProbeBase
    {
        REFLECTION_CLASS(TimeProbe, ProbeBase)

      public:
        float Delta = -1;
        unsigned Calls = 0;
        bool Throw = false;
        ProbeBase* Foreign = nullptr;
        static void RunTick(ProbeBase& component, float delta)
        {
            static_cast<TimeProbe&>(component).OnTick(delta);
        }
        void OnTick(float delta)
        {
            auto* world = GetOwner()->GetWorld();
            float queried = -1;
            assert(world->HasActiveTickTimeContext() && world->TryGetComponentTickDelta(*this, queried) &&
                   queried == delta);
            if (Foreign)
            {
                float untouched = 42;
                assert(!world->TryGetComponentTickDelta(*Foreign, untouched) && untouched == 42);
            }
            if (Throw)
                throw 1;
            Delta = delta;
            ++Calls;
        }
    };
    IMPLEMENT_CLASS(TimeProbe, ProbeBase)

    class TimeWorldProbe : public TimeProbe
    {
        REFLECTION_CLASS(TimeWorldProbe, TimeProbe)
    };
    IMPLEMENT_CLASS(TimeWorldProbe, TimeProbe)
    class TimeAnimationProbe : public TimeProbe
    {
        REFLECTION_CLASS(TimeAnimationProbe, TimeProbe)
    };
    IMPLEMENT_CLASS(TimeAnimationProbe, TimeProbe)
    class TimeUnscaledProbe : public TimeProbe
    {
        REFLECTION_CLASS(TimeUnscaledProbe, TimeProbe)
    };
    IMPLEMENT_CLASS(TimeUnscaledProbe, TimeProbe)
    class TimeParticleProbe : public TimeProbe
    {
        REFLECTION_CLASS(TimeParticleProbe, TimeProbe)
    };
    IMPLEMENT_CLASS(TimeParticleProbe, TimeProbe)

    void RegisterProbeTicks(World& world)
    {
        const struct
        {
            const IClass* Class;
            Engine::TimeChannel Channel;
        } entries[] = {{TimeWorldProbe::StaticClass(), Engine::TimeChannel::World},
                       {TimeAnimationProbe::StaticClass(), Engine::TimeChannel::Animation},
                       {TimeUnscaledProbe::StaticClass(), Engine::TimeChannel::Unscaled},
                       {TimeParticleProbe::StaticClass(), Engine::TimeChannel::Particle}};
        for (const auto& entry : entries)
        {
            Component::ComponentTickRegistration registration;
            registration.Name = "Test.TimeProbe";
            registration.Run = &TimeProbe::RunTick;
            registration.TimeChannel = entry.Channel;
            world.RegisterComponentTick(entry.Class, registration);
        }
    }

    TimeProbe* CreateProbe(World& world, Entity* owner, Engine::TimeChannel channel)
    {
        switch (channel)
        {
        case Engine::TimeChannel::Animation:
            return world.CreateComponent<TimeAnimationProbe>(owner);
        case Engine::TimeChannel::Unscaled:
            return world.CreateComponent<TimeUnscaledProbe>(owner);
        case Engine::TimeChannel::Particle:
            return world.CreateComponent<TimeParticleProbe>(owner);
        default:
            return world.CreateComponent<TimeWorldProbe>(owner);
        }
    }
    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 1e-6f;
    }
} // namespace
int main()
{
    World world;
    world.Initialize();
    RegisterProbeTicks(world);
    auto* root = world.SpawnEntity<Entity>();
    auto* child = world.SpawnEntity<Entity>(root);
    assert(root && child && root->SetCustomTimeDilation(.25f));
    assert(!root->SetCustomTimeDilation(-1) && !root->SetCustomTimeDilation(std::nanf("")) &&
           root->GetCustomTimeDilation() == .25f);
    TimeProbe* probes[5]{};
    const Engine::TimeChannel channels[] = {Engine::TimeChannel::World, Engine::TimeChannel::Animation,
                                            Engine::TimeChannel::Unscaled, Engine::TimeChannel::Particle,
                                            Engine::TimeChannel::Unscaled};
    const Component::ETickGroup groups[] = {Component::ETickGroup::Default, Component::ETickGroup::Animation,
                                            Component::ETickGroup::Input, Component::ETickGroup::PostPhysics,
                                            Component::ETickGroup::Camera};
    for (unsigned i = 0; i < 5; ++i)
    {
        probes[i] = CreateProbe(world, root, channels[i]);
        assert(probes[i]);
        assert(probes[i]->SetTickGroup(groups[i]));
    }
    // 時間倍率は子へ継承しない（子の World の channel は倍率の掛からない delta）
    TimeProbe* childProbe = CreateProbe(world, child, Engine::TimeChannel::World);
    assert(childProbe);
    World foreignWorld;
    foreignWorld.Initialize();
    RegisterProbeTicks(foreignWorld);
    auto* foreignEntity = foreignWorld.SpawnEntity<Entity>();
    auto* foreignProbe = CreateProbe(foreignWorld, foreignEntity, Engine::TimeChannel::World);
    assert(foreignEntity && foreignProbe);
    probes[0]->Foreign = foreignProbe;
    float untouched = 42;
    assert(!world.HasActiveTickTimeContext() && !world.TryGetComponentTickDelta(*probes[0], untouched) &&
           untouched == 42);
    Engine::FrameTimes times;
    times.Unscaled = .08f;
    times.World = .04f;
    times.Animation = .02f;
    times.Particle = .06f;
    times.Audio = .07f;
    times.PhysicsDeltaNanoseconds = 30000000;
    world.Tick(times);
    world.LateTick(times);
    assert(Near(childProbe->Delta, .04f));
    assert(Near(probes[0]->Delta, .01f) && Near(probes[1]->Delta, .005f));
    assert(Near(probes[2]->Delta, .08f) && Near(probes[3]->Delta, .06f) && Near(probes[4]->Delta, .08f));
    times.World = times.Animation = 0;
    times.PhysicsDeltaNanoseconds = 0;
    world.Tick(times);
    world.LateTick(times);
    assert(probes[0]->Delta == 0 && probes[1]->Delta == 0 && probes[0]->Calls == 2);
    assert(probes[2]->Delta == .08f && probes[4]->Delta == .08f);
    world.Tick(.1f);
    world.LateTick(.1f);
    assert(Near(probes[0]->Delta, .025f) && Near(probes[1]->Delta, .025f));
    assert(probes[2]->Delta == .1f && probes[3]->Delta == .1f && probes[4]->Delta == .1f);
    assert(Near(childProbe->Delta, .1f));
    Component::AnimatorComponent animator;
    Component::SkinnedMeshComponent skinned;
    Component::CameraComponent camera;
    Component::SpringArmComponent arm;
    assert(world.GetComponentTimeChannel(animator) == Engine::TimeChannel::Animation &&
           world.GetComponentTimeChannel(skinned) == Engine::TimeChannel::Animation);
    assert(world.GetComponentTimeChannel(camera) == Engine::TimeChannel::Unscaled &&
           world.GetComponentTimeChannel(arm) == Engine::TimeChannel::Unscaled);
    assert(!world.HasActiveTickTimeContext() && !world.TryGetComponentTickDelta(*probes[0], untouched) &&
           untouched == 42);
    probes[0]->Throw = true;
    bool caught = false;
    try
    {
        world.Tick(times);
    }
    catch (int)
    {
        caught = true;
    }
    assert(caught && !world.HasActiveTickTimeContext() && !world.TryGetComponentTickDelta(*probes[0], untouched) &&
           untouched == 42);
    probes[0]->Throw = false;
    world.Tick(times);
    world.LateTick(times);
    world.Finalize();
    foreignWorld.Finalize();
    std::cout << "WorldTimeChannelTest passed\n";
    return 0;
}
