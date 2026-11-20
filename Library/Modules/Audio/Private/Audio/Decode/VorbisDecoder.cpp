#include "Audio/AudioDecoder.h"

#include <algorithm>
#include <climits>
#include <utility>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

// 宣言だけを読む場合はチャンネル上限が公開されないため、同梱版と同じ既定値を使う。
#ifndef STB_VORBIS_MAX_CHANNELS
#define STB_VORBIS_MAX_CHANNELS 16
#endif

namespace NorvesLib::Modules::Audio::Private
{
    using VorbisHandle = Core::Container::TUniquePtr<stb_vorbis, decltype(&stb_vorbis_close)>;

    class VorbisDecoder final : public IAudioDecoder
    {
    public:
        VorbisDecoder(VorbisHandle handle, AudioDecoderInfo info) noexcept
            : m_Handle(std::move(handle)), m_Info(info)
        {
        }

        ~VorbisDecoder() override = default;
        VorbisDecoder(const VorbisDecoder&) = delete;
        VorbisDecoder& operator=(const VorbisDecoder&) = delete;
        VorbisDecoder(VorbisDecoder&&) = delete;
        VorbisDecoder& operator=(VorbisDecoder&&) = delete;

        [[nodiscard]] AudioDecoderInfo GetInfo() const noexcept override
        {
            return m_Info;
        }

        [[nodiscard]] uint32_t DecodeFrames(int16_t* outInterleaved, uint32_t maxFrames) noexcept override
        {
            if (m_bFailed || outInterleaved == nullptr || maxFrames == 0)
            {
                return 0;
            }

            const auto requested = static_cast<uint32_t>(std::min<uint64_t>(maxFrames, m_Info.TotalFrames - m_Position));
            // stb 内部の int による加算にも余裕を残して、サンプル数を区切る。
            const uint32_t maxChunkFrames = (INT_MAX / 2) / m_Info.ChannelCount;
            uint32_t written = 0;
            while (written < requested)
            {
                const uint32_t chunk = std::min(requested - written, maxChunkFrames);
                const int numShorts = static_cast<int>(chunk * m_Info.ChannelCount);
                const int frames = stb_vorbis_get_samples_short_interleaved(
                    m_Handle.get(), m_Info.ChannelCount, outInterleaved, numShorts);
                if (frames <= 0)
                {
                    m_bFailed = true;
                    break;
                }

                written += static_cast<uint32_t>(frames);
                outInterleaved += frames * m_Info.ChannelCount;
            }

            m_Position += written;
            return written;
        }

        [[nodiscard]] bool SeekToFrame(uint64_t frame) noexcept override
        {
            if (m_bFailed || frame > m_Info.TotalFrames)
            {
                return false;
            }

            if (frame < m_Info.TotalFrames && !stb_vorbis_seek64(m_Handle.get(), static_cast<stb_vorbis_int64>(frame)))
            {
                m_bFailed = true;
                return false;
            }

            m_Position = frame;
            return true;
        }

        [[nodiscard]] bool Rewind() noexcept override
        {
            return SeekToFrame(0);
        }

        [[nodiscard]] bool HasFailed() const noexcept override
        {
            return m_bFailed;
        }

    private:
        VorbisHandle m_Handle;
        AudioDecoderInfo m_Info;
        uint64_t m_Position = 0;
        bool m_bFailed = false;
    };
} // namespace NorvesLib::Modules::Audio::Private

namespace NorvesLib::Modules::Audio
{
    Core::Container::TUniquePtr<IAudioDecoder> CreateVorbisDecoder(Core::Container::Span<const uint8_t> oggBytes)
    {
        if (oggBytes.empty() || oggBytes.data() == nullptr || oggBytes.size() > INT_MAX)
        {
            return {};
        }

        Private::VorbisHandle handle(
            stb_vorbis_open_memory(oggBytes.data(), static_cast<int>(oggBytes.size()), nullptr, nullptr), &stb_vorbis_close);
        if (!handle)
        {
            return {};
        }

        const auto info = stb_vorbis_get_info(handle.get());
        if (info.channels < 1 || info.channels > STB_VORBIS_MAX_CHANNELS || info.sample_rate == 0)
        {
            return {};
        }

        const auto totalFrames = stb_vorbis_stream_length_in_samples64(handle.get());
        if (totalFrames <= 0 || !stb_vorbis_seek_start(handle.get()))
        {
            return {};
        }

        return Core::Container::MakeUnique<Private::VorbisDecoder>(
            std::move(handle), AudioDecoderInfo{info.sample_rate, static_cast<uint16_t>(info.channels), static_cast<uint64_t>(totalFrames)});
    }
} // namespace NorvesLib::Modules::Audio
