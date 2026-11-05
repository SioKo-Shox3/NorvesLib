#include "Audio/AudioClipLoader.h"

#include "Asset/AssetSystem.h"
#include "Asset/CookedAudioFormat.h"
#include "Logging/LogMacros.h"

#include <cstring>
#include <utility>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        // 呼び出し側で範囲を確認したフィールドをリトルエンディアンで読む。
        uint16_t ReadLe16(const uint8_t* data)
        {
            return static_cast<uint16_t>(data[0]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
        }

        uint32_t ReadLe32(const uint8_t* data)
        {
            return static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[3]) << 24);
        }

        AudioClipLoadResult LoadLooseWave(const Core::Asset::AssetBlob& blob,
                                          Core::Container::TSharedPtr<AudioClipResource>& outClip)
        {
            const auto* bytes = blob.GetData();
            const size_t size = blob.GetSize();
            if (size < 4 || std::memcmp(bytes, "RIFF", 4) != 0)
            {
                return AudioClipLoadResult::NotCooked;
            }
            if (size < 12)
            {
                return AudioClipLoadResult::ParseFailed;
            }
            if (std::memcmp(bytes + 8, "WAVE", 4) != 0)
            {
                return AudioClipLoadResult::NotCooked;
            }
            if (ReadLe32(bytes + 4) != size - 8)
            {
                return AudioClipLoadResult::ParseFailed;
            }

            AudioPcmFormat format;
            uint16_t formatTag = 0;
            uint32_t byteRate = 0;
            bool bFoundFormat = false;
            bool bFoundData = false;
            size_t dataOffset = 0;
            size_t dataSize = 0;
            size_t cursor = 12;
            while (cursor < size)
            {
                if (size - cursor < 8)
                {
                    return AudioClipLoadResult::ParseFailed;
                }
                const uint32_t chunkSize = ReadLe32(bytes + cursor + 4);
                const size_t chunkOffset = cursor + 8;
                if (chunkSize > size - chunkOffset)
                {
                    return AudioClipLoadResult::ParseFailed;
                }
                if (std::memcmp(bytes + cursor, "fmt ", 4) == 0)
                {
                    if (bFoundFormat || chunkSize < 16)
                    {
                        return AudioClipLoadResult::ParseFailed;
                    }
                    formatTag = ReadLe16(bytes + chunkOffset);
                    format.ChannelCount = ReadLe16(bytes + chunkOffset + 2);
                    format.SampleRate = ReadLe32(bytes + chunkOffset + 4);
                    byteRate = ReadLe32(bytes + chunkOffset + 8);
                    format.BlockAlignment = ReadLe16(bytes + chunkOffset + 12);
                    format.BitsPerSample = ReadLe16(bytes + chunkOffset + 14);
                    bFoundFormat = true;
                }
                else if (std::memcmp(bytes + cursor, "data", 4) == 0)
                {
                    if (bFoundData)
                    {
                        return AudioClipLoadResult::ParseFailed;
                    }
                    dataOffset = chunkOffset;
                    dataSize = chunkSize;
                    bFoundData = true;
                }

                // 加算前に残量を確認し、末尾の詰めもファイル内に要求する。
                cursor = chunkOffset + chunkSize;
                if ((chunkSize & 1u) != 0)
                {
                    if (cursor == size)
                    {
                        return AudioClipLoadResult::ParseFailed;
                    }
                    ++cursor;
                }
            }
            if (!bFoundFormat || !bFoundData)
            {
                return AudioClipLoadResult::ParseFailed;
            }
            if (formatTag != 1 || !format.IsSupported())
            {
                return AudioClipLoadResult::UnsupportedFormat;
            }
            if (byteRate != format.SampleRate * format.BlockAlignment)
            {
                return AudioClipLoadResult::ParseFailed;
            }
            if (dataSize == 0 || dataSize % format.BlockAlignment != 0)
            {
                return AudioClipLoadResult::InvalidPayload;
            }
            const auto pcm = blob.TryCreateSubBlob(dataOffset, dataSize);
            if (!pcm.IsValid() || pcm.IsEmpty())
            {
                return AudioClipLoadResult::InvalidPayload;
            }
            auto clip = Core::Container::MakeShared<AudioClipResource>(pcm, format, dataSize / format.BlockAlignment);
            if (!clip || !clip->IsValid())
            {
                return AudioClipLoadResult::InvalidPayload;
            }
            outClip = std::move(clip);
            return AudioClipLoadResult::Success;
        }
    } // 無名名前空間

    AudioClipLoadResult LoadAudioClip(const Core::Asset::AssetSystem& assets,
                                      Core::Container::AnsiStringView logicalPath,
                                      Core::Container::TSharedPtr<AudioClipResource>& outClip)
    {
        outClip.reset();
        const auto resolved = assets.ResolveAsset(logicalPath, Core::Asset::AssetKind::Audio);
        if (!resolved.UsedCooked())
        {
            if (!resolved.Succeeded())
            {
                return AudioClipLoadResult::NotCooked;
            }
            const auto result = LoadLooseWave(resolved.Blob, outClip);
            if (result == AudioClipLoadResult::Success)
            {
                LOG_WARNING_F("AUDIO_COOKED_MISSING logical=\"%s\" cook されていない WAV をそのまま読みます",
                              resolved.NormalizedLogicalPath.c_str());
            }
            return result;
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
