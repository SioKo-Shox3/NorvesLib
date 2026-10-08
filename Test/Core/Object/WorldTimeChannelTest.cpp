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
    class TimeEntity : public Entity
    {
      public:
        float Delta = -1;
        void Tick(float delta) override
        {
            Delta = delta;
        }
    };
    class TimeProbe : public Component::Component
    {
      public:
        Engine::TimeChannel Channel = Engine::TimeChannel::World;
        float Delta = -1;
        unsigned Calls = 0;
        Engine::TimeChannel GetTimeChannel() const noexcept override
        {
            return Channel;
        }
        void Tick(float delta) override
        {
            Delta = delta;
            ++Calls;
        }
    };
    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 1e-6f;
    }
} // namespace
int main()
{
    World world;
    world.Initialize();
    auto* root = world.SpawnEntity<TimeEntity>();
    auto* child = world.SpawnEntity<TimeEntity>(root);
    assert(root && child && root->SetCustomTimeDilation(.25f));
    assert(!root->SetCustomTimeDilation(-1) && !root->SetCustomTimeDilation(std::nanf("")) &&
           root->GetCustomTimeDilation() == .25f);
    TimeProbe* probes[5]{};
    const Engine::TimeChannel channels[] = {Engine::TimeChannel::World, Engine::TimeChannel::Animation,
                                            Engine::TimeChannel::Unscaled, Engine::TimeChannel::Particle,
                                            Engine::TimeChannel::Unscaled};
    const Component::ETickGroup groups[] = {Component::ETickGroup::Default, Component::ETickGroup::Animation,
                                            Component::ETickGroup::Input, Component::ETickGroup::Default,
                                            Component::ETickGroup::Camera};
    for (unsigned i = 0; i < 5; ++i)
    {
        probes[i] = world.CreateComponent<TimeProbe>(root);
        assert(probes[i]);
        probes[i]->Channel = channels[i];
        assert(probes[i]->SetTickGroup(groups[i]));
    }
    Engine::FrameTimes times;
    times.Unscaled = .08f;
    times.World = .04f;
    times.Animation = .02f;
    times.Particle = .06f;
    times.Audio = .07f;
    times.PhysicsDeltaNanoseconds = 30000000;
    world.Tick(times);
    world.LateTick(times);
    assert(Near(root->Delta, .01f) && Near(child->Delta, .04f));
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
    assert(Near(child->Delta, .1f));
    Component::AnimatorComponent animator;
    Component::SkinnedMeshComponent skinned;
    Component::CameraComponent camera;
    Component::SpringArmComponent arm;
    assert(animator.GetTimeChannel() == Engine::TimeChannel::Animation &&
           skinned.GetTimeChannel() == Engine::TimeChannel::Animation);
    assert(camera.GetTimeChannel() == Engine::TimeChannel::Unscaled &&
           arm.GetTimeChannel() == Engine::TimeChannel::Unscaled);
    world.Finalize();
    std::cout << "WorldTimeChannelTest passed\n";
}
