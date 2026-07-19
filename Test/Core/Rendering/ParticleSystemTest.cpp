#include "Particle/ParticleSystem.h"
#include "Rendering/SceneProxy.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Particle;
using namespace NorvesLib::Core::Rendering;

namespace NorvesLib::Core::Particle
{
    class ParticleSystemTestAccess
    {
    public:
        static ParticleEmitterHandle SetGeneration(
            ParticleSystem& system,
            ParticleEmitterHandle handle,
            uint32_t generation)
        {
            system.m_Emitters[handle.Index].Generation = generation;
            return {handle.Index, generation};
        }
    };
} // namespace NorvesLib::Core::Particle

namespace
{
    constexpr float kTolerance = 0.00001f;

    bool NearlyEqual(float left, float right)
    {
        return std::fabs(left - right) < kTolerance;
    }

    VariableArray<BoardProxy> Collect(const ParticleSystem& system)
    {
        VariableArray<BoardProxy> proxies;
        system.AppendBoardProxies(proxies);
        return proxies;
    }

    ParticleEmitterDesc MakeEmitter()
    {
        ParticleEmitterDesc desc;
        desc.Position = Math::Vector3(10.0f, 20.0f, 30.0f);
        desc.VelocityMin = Math::Vector3(1.0f, 2.0f, 3.0f);
        desc.VelocityMax = Math::Vector3(5.0f, 6.0f, 7.0f);
        desc.Gravity = Math::Vector3(0.0f, -10.0f, 0.0f);
        desc.SpawnRate = 2.0f;
        desc.Lifetime = 2.0f;
        desc.MaxCount = 8u;
        desc.Color = Math::Vector4(0.25f, 0.5f, 0.75f, 1.0f);
        desc.SizePx = Math::Vector2(16.0f, 24.0f);
        return desc;
    }

    Math::Vector3 GetPosition(const BoardProxy& proxy)
    {
        return proxy.WorldTransform.GetTranslationRow();
    }

    void AssertSameParticle(const BoardProxy& left, const BoardProxy& right)
    {
        assert(left.ObjectId == right.ObjectId);
        assert(left.ComponentId == right.ComponentId);
        assert(left.Tint == right.Tint);
        assert(left.SizePx == right.SizePx);
        assert(left.Texture == right.Texture);
        assert(GetPosition(left) == GetPosition(right));
    }
}

int main()
{
    {
        ParticleSystem first(0xC001D00Du);
        ParticleSystem second(0xC001D00Du);
        ParticleEmitterDesc desc = MakeEmitter();
        const ParticleEmitterHandle firstHandle = first.CreateEmitter(desc);
        const ParticleEmitterHandle secondHandle = second.CreateEmitter(desc);
        assert(firstHandle.IsValid());
        assert(firstHandle == secondHandle);

        first.Tick(1.0f);
        second.Tick(1.0f);
        const VariableArray<BoardProxy> firstProxies = Collect(first);
        const VariableArray<BoardProxy> secondProxies = Collect(second);
        assert(firstProxies.size() == 2u);
        assert(secondProxies.size() == 2u);
        for (uint32_t index = 0; index < firstProxies.size(); ++index)
        {
            assert(firstProxies[index].ComponentId == secondProxies[index].ComponentId);
            assert(GetPosition(firstProxies[index]) == GetPosition(secondProxies[index]));
        }

        first.Tick(0.5f);
        const VariableArray<BoardProxy> advancedProxies = Collect(first);
        const Math::Vector3 firstPosition = GetPosition(advancedProxies[0]);
        assert(NearlyEqual(firstPosition.x, 11.52690804f));
        assert(NearlyEqual(firstPosition.z, 32.37258577f));
        const Math::Vector3 secondPosition = GetPosition(advancedProxies[1]);
        assert(NearlyEqual(secondPosition.x, 10.66035128f));
        assert(NearlyEqual(secondPosition.z, 33.25962448f));
        assert(GetPosition(advancedProxies[2]) == desc.Position);
        assert(firstProxies[0].IsValid());
        assert(firstProxies[0].Space == BoardSpace::ScreenSpace);
        assert(firstProxies[0].LayerMask == RenderLayer::UI);
        assert(firstProxies[0].BlendModeProp == BlendMode::Translucent);
    }

    {
        ParticleSystem system;
        ParticleEmitterDesc desc = MakeEmitter();
        desc.SpawnRate = 1.0f;
        desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMin = Math::Vector3(1.0f, 0.0f, 0.0f);
        desc.VelocityMax = desc.VelocityMin;
        desc.Gravity = Math::Vector3(0.0f, -10.0f, 0.0f);
        const ParticleEmitterHandle handle = system.CreateEmitter(desc);
        system.Tick(1.0f);
        system.Tick(0.5f);
        const VariableArray<BoardProxy> proxies = Collect(system);
        assert(proxies.size() == 1u);
        const Math::Vector3 position = GetPosition(proxies[0]);
        assert(NearlyEqual(position.x, 0.5f));
        assert(NearlyEqual(position.y, -2.5f));

        const VariableArray<BoardProxy> beforeInvalidDelta = Collect(system);
        system.Tick(-1.0f);
        system.Tick(std::numeric_limits<float>::infinity());
        system.Tick(std::numeric_limits<float>::quiet_NaN());
        assert(Collect(system)[0].WorldTransform.GetTranslationRow() == beforeInvalidDelta[0].WorldTransform.GetTranslationRow());

        assert(system.SetEmitterEnabled(handle, false));
        system.Tick(1.0f);
        assert(Collect(system).size() == 1u);
        system.Tick(1.0f);
        assert(Collect(system).empty());
    }

    {
        ParticleSystem system;
        ParticleEmitterDesc desc = MakeEmitter();
        desc.SpawnRate = 2.0f;
        desc.Lifetime = 1.0f;
        const ParticleEmitterHandle handle = system.CreateEmitter(desc);
        system.Tick(1.0f);
        assert(Collect(system).size() == 2u);

        ParticleEmitterDesc updated = desc;
        updated.Color = Math::Vector4(1.0f, 0.0f, 0.0f, 0.5f);
        updated.SizePx = Math::Vector2(32.0f, 48.0f);
        updated.Gravity = Math::Vector3(0.0f, 0.0f, 0.0f);
        updated.Lifetime = 0.25f;
        updated.MaxCount = 1u;
        assert(system.UpdateEmitter(handle, updated));
        assert(Collect(system).size() == 1u);
        system.Tick(0.5f);
        const VariableArray<BoardProxy> proxies = Collect(system);
        assert(proxies.size() == 1u);
        assert(proxies[0].Tint == updated.Color);
        assert(proxies[0].SizePx == updated.SizePx);

        ParticleEmitterDesc invalid = updated;
        invalid.Lifetime = 0.0f;
        assert(!system.UpdateEmitter(handle, invalid));
        const VariableArray<BoardProxy> beforeInvalidUpdate = Collect(system);
        system.Tick(0.1f);
        assert(Collect(system).size() == beforeInvalidUpdate.size());

        assert(system.DestroyEmitter(handle));
        assert(!system.DestroyEmitter(handle));
        assert(!system.UpdateEmitter(handle, updated));
        system.Clear();
        assert(!system.SetEmitterEnabled(handle, true));
    }

    {
        ParticleEmitterDesc desc = MakeEmitter();
        desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMin = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMax = Math::Vector3(1.0f, 1.0f, 1.0f);
        desc.Gravity = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.SpawnRate = 1.0f;
        ParticleSystem baseline;
        ParticleSystem invalidDelta;
        baseline.CreateEmitter(desc);
        invalidDelta.CreateEmitter(desc);
        baseline.Tick(0.25f);
        invalidDelta.Tick(0.25f);
        invalidDelta.Tick(-1.0f);
        invalidDelta.Tick(std::numeric_limits<float>::infinity());
        invalidDelta.Tick(std::numeric_limits<float>::quiet_NaN());
        baseline.Tick(0.75f);
        invalidDelta.Tick(0.75f);
        AssertSameParticle(Collect(baseline)[0], Collect(invalidDelta)[0]);

        ParticleSystem disabled;
        const ParticleEmitterHandle disabledHandle = disabled.CreateEmitter(desc);
        disabled.Tick(0.25f);
        assert(disabled.SetEmitterEnabled(disabledHandle, false));
        disabled.Tick(1.0f);
        assert(disabled.SetEmitterEnabled(disabledHandle, true));
        disabled.Tick(0.75f);
        AssertSameParticle(Collect(baseline)[0], Collect(disabled)[0]);
    }

    {
        ParticleSystem system;
        ParticleEmitterDesc desc = MakeEmitter();
        desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMin = Math::Vector3(2.0f, 3.0f, 4.0f);
        desc.VelocityMax = desc.VelocityMin;
        desc.Gravity = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.SpawnRate = 1.0f;
        desc.Lifetime = 2.0f;
        const ParticleEmitterHandle handle = system.CreateEmitter(desc);
        system.Tick(1.0f);

        ParticleEmitterDesc updated = desc;
        updated.Gravity = Math::Vector3(0.0f, -2.0f, 0.0f);
        updated.Color = Math::Vector4(0.1f, 0.2f, 0.3f, 0.4f);
        updated.SizePx = Math::Vector2(31.0f, 17.0f);
        updated.Texture = TextureHandle{37u};
        assert(system.UpdateEmitter(handle, updated));
        system.Tick(0.5f);
        const BoardProxy live = Collect(system)[0];
        assert(live.Tint == updated.Color);
        assert(live.SizePx == updated.SizePx);
        assert(live.Texture == updated.Texture);
        assert(NearlyEqual(GetPosition(live).x, 1.0f));
        assert(NearlyEqual(GetPosition(live).y, 1.0f));
        assert(NearlyEqual(GetPosition(live).z, 2.0f));

        updated.Lifetime = 0.4f;
        assert(system.UpdateEmitter(handle, updated));
        assert(Collect(system).empty());
    }

    {
        ParticleSystem system;
        ParticleEmitterDesc desc = MakeEmitter();
        desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMin = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMax = Math::Vector3(1.0f, 1.0f, 1.0f);
        desc.Gravity = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.SpawnRate = 3.0f;
        desc.MaxCount = 3u;
        const ParticleEmitterHandle handle = system.CreateEmitter(desc);
        system.Tick(1.0f);
        system.Tick(0.5f);
        const VariableArray<BoardProxy> beforeShrink = Collect(system);
        assert(beforeShrink.size() == 3u);
        ParticleEmitterDesc shrunken = desc;
        shrunken.MaxCount = 2u;
        assert(system.UpdateEmitter(handle, shrunken));
        const VariableArray<BoardProxy> afterShrink = Collect(system);
        assert(afterShrink.size() == 2u);
        AssertSameParticle(beforeShrink[0], afterShrink[0]);
        AssertSameParticle(beforeShrink[1], afterShrink[1]);
        assert(GetPosition(beforeShrink[2]) != GetPosition(afterShrink[1]));

        system.Clear();
        assert(!system.UpdateEmitter(handle, desc));
        const ParticleEmitterHandle replacement = system.CreateEmitter(desc);
        assert(replacement.Index == handle.Index);
        assert(replacement.Generation != handle.Generation);
        assert(!system.SetEmitterEnabled(handle, true));
    }

    {
        ParticleSystem system;
        const ParticleEmitterHandle originalHandle = system.CreateEmitter(MakeEmitter());
        const ParticleEmitterHandle wrappingHandle = ParticleSystemTestAccess::SetGeneration(
            system,
            originalHandle,
            std::numeric_limits<uint32_t>::max());
        assert(system.DestroyEmitter(wrappingHandle));
        const ParticleEmitterHandle replacementHandle = system.CreateEmitter(MakeEmitter());
        assert(replacementHandle.IsValid());
        assert(replacementHandle.Index != originalHandle.Index);
        assert(!system.SetEmitterEnabled(originalHandle, true));
        assert(!system.SetEmitterEnabled(wrappingHandle, true));
    }

    {
        ParticleEmitterDesc desc = MakeEmitter();
        desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMin = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.VelocityMax = Math::Vector3(1.0f, 1.0f, 1.0f);
        desc.Gravity = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.SpawnRate = 1.0f;
        ParticleSystem system;
        const ParticleEmitterHandle first = system.CreateEmitter(desc);
        assert(system.DestroyEmitter(first));
        const ParticleEmitterHandle reused = system.CreateEmitter(desc);
        assert(reused.Index == first.Index);
        assert(reused.Generation != first.Generation);
        system.Tick(1.0f);
        system.Tick(0.5f);
        const BoardProxy recycled = Collect(system)[0];
        assert((recycled.ComponentId & (uint64_t{1} << 63u)) != 0u);
        assert(NearlyEqual(GetPosition(recycled).x, 0.11698759f));
    }

    std::cout << "ParticleSystemTest passed\\n";
    return 0;
}
