#pragma once

#include "Core/Public/Container/PointerTypes.h"

namespace NorvesLib::Core
{
    class SkeletalAssetResource;
}

#if defined(NORVES_GAME_AUDIO)
namespace NorvesLib::Modules::Audio
{
    class AudioClipResource;
}
#endif

namespace Game::GameModes
{
    void EmitM9WorldSmokeMarker(const char* format, ...);

    struct M9WorldAcceptanceConfig
    {
        bool bRequested = false;
        bool bAssetsReady = false;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> SkeletalAsset;

#if defined(NORVES_GAME_AUDIO)
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Modules::Audio::AudioClipResource> EffectClip;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Modules::Audio::AudioClipResource> LoopClip;
#endif
    };
} // namespace Game::GameModes
