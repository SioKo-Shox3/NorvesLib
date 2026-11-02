#include "Particle/VfxEffectSerialization.h"

namespace NorvesLib::Core::Particle
{
    bool LoadVfxEffectFromJson(const Container::String&, VfxEffectAsset&, Container::String& outError)
    {
        outError = "not_implemented";
        return false;
    }

    bool CookVfxEffect(const VfxEffectAsset&, VariableArray<uint8_t>&, Container::String& outError)
    {
        outError = "not_implemented";
        return false;
    }

    bool LoadCookedVfxEffect(Container::Span<const uint8_t>, VfxEffectAsset&, Container::String& outError)
    {
        outError = "not_implemented";
        return false;
    }
} // namespace NorvesLib::Core::Particle
