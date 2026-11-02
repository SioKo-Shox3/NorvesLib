#include "Particle/VfxEffectSystem.h"

namespace NorvesLib::Core::Particle
{
    VfxEffectHandle VfxEffectSystem::CreateInstance(const VfxEffectAsset&, const VfxInstanceDesc&, Container::String*)
    {
        return {};
    }

    bool VfxEffectSystem::DestroyInstance(VfxEffectHandle)
    {
        return false;
    }

    void VfxEffectSystem::Clear()
    {
    }

    bool VfxEffectSystem::IsLiveInstance(VfxEffectHandle) const
    {
        return false;
    }

    bool VfxEffectSystem::SetInstancePosition(VfxEffectHandle, const Math::Vector3&)
    {
        return false;
    }

    bool VfxEffectSystem::SetInstanceEnabled(VfxEffectHandle, bool)
    {
        return false;
    }

    bool VfxEffectSystem::SetModuleParameter(VfxEffectHandle, uint32_t, uint32_t, const char*, const VfxValue&, Container::String*)
    {
        return false;
    }

    bool VfxEffectSystem::SetEmitterProperty(VfxEffectHandle, uint32_t, const char*, const VfxValue&, Container::String*)
    {
        return false;
    }

    bool VfxEffectSystem::RaiseEvent(VfxEffectHandle, const char*, const Math::Vector3&, double)
    {
        return false;
    }

    void VfxEffectSystem::Tick(float)
    {
    }

    double VfxEffectSystem::GetInstanceTime(VfxEffectHandle) const
    {
        return -1.0;
    }

    uint32_t VfxEffectSystem::GetEmitterCount(VfxEffectHandle) const
    {
        return 0u;
    }

    bool VfxEffectSystem::CopyParticles(VfxEffectHandle, uint32_t, VariableArray<VfxParticleState>&) const
    {
        return false;
    }

    void VfxEffectSystem::AppendBoardProxies(VariableArray<Rendering::BoardProxy>&) const
    {
    }

    uint32_t VfxEffectSystem::ComputeFlipbookFrame(const VfxFlipbookImportSettings&, float, float)
    {
        return 0u;
    }

    Math::Vector4 VfxEffectSystem::ComputeFlipbookUVRect(const VfxFlipbookImportSettings&, uint32_t)
    {
        return Math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    }
} // namespace NorvesLib::Core::Particle
