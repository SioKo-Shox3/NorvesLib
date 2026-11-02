#pragma once

#include "Core/Public/Container/PointerTypes.h"
#include "GameModes/Rendering3DTest/M9SkeletalPreparation.h"

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
        // 比較は再生を止めたまま、エンジンの決定的な撮影へ委ねる。
        bool bCompare = false;
        bool bCompareCaptured = false;
        NorvesLib::Core::Container::String ComparePngPath;
        NorvesLib::Core::Container::AnsiString SkeletalPath = "Models/M9Skinned/ValidU8Float.gltf";
        NorvesLib::Core::Container::String ClipName = _T("Wave");
        M9SkeletalPreparation Preparation;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimationClipResource> SelectedClip;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> SkeletalAsset;

#if defined(NORVES_GAME_AUDIO)
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Modules::Audio::AudioClipResource> EffectClip;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Modules::Audio::AudioClipResource> LoopClip;
#endif
    };

    void ObserveM9WorldCompareCapture(M9WorldAcceptanceConfig& config);
} // namespace Game::GameModes
