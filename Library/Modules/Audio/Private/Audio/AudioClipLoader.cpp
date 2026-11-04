#include "Audio/AudioClipLoader.h"

#include "Asset/AssetSystem.h"
#include "Asset/CookedAudioFormat.h"

#include <utility>

namespace NorvesLib::Modules::Audio::Private
{
    AudioClipLoadResult LoadAudioClip(const Core::Asset::AssetSystem& assets,
                                      Core::Container::AnsiStringView logicalPath,
                                      Core::Container::TSharedPtr<AudioClipResource>& outClip)
    {
        outClip.reset();
        const auto resolved = assets.ResolveAsset(logicalPath, Core::Asset::AssetKind::Audio);
        if (!resolved.UsedCooked())
        {
            return AudioClipLoadResult::NotCooked;
        }
        const auto parsed = Core::Asset::ParseCookedAudio(resolved.Blob);
        if (!parsed.Succeeded())
        {
            return AudioClipLoadResult::ParseFailed;
        }
        const AudioPcmFormat format{parsed.Audio.SampleRate, parsed.Audio.ChannelCount,
                                    parsed.Audio.BitsPerSample, parsed.Audio.BlockAlignment};
        if (!format.IsSupported())
        {
            return AudioClipLoadResult::UnsupportedFormat;
        }
        const auto pcm = parsed.Audio.SourceBlob.TryCreateSubBlob(parsed.Audio.PayloadOffset, parsed.Audio.PayloadSize);
        if (!pcm.IsValid() || pcm.IsEmpty())
        {
            return AudioClipLoadResult::InvalidPayload;
        }
        auto clip = Core::Container::MakeShared<AudioClipResource>(pcm, format, parsed.Audio.FrameCount);
        if (!clip || !clip->IsValid())
        {
            return AudioClipLoadResult::InvalidPayload;
        }
        outClip = std::move(clip);
        return AudioClipLoadResult::Success;
    }
} // namespace NorvesLib::Modules::Audio::Private
