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
                if (stb_vorbis_get_error(m_Handle.get()) != VORBIS__no_error || frames <= 0)
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

            if (frame < m_Info.TotalFrames)
            {
                const int seekResult = stb_vorbis_seek64(m_Handle.get(), static_cast<stb_vorbis_int64>(frame));
                if (stb_vorbis_get_error(m_Handle.get()) != VORBIS__no_error || !seekResult)
                {
                    m_bFailed = true;
                    return false;
                }
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
    namespace
    {
        struct OggCrcTable
        {
            uint32_t Values[256];
        };

        constexpr OggCrcTable MakeOggCrcTable() noexcept
        {
            OggCrcTable table{};
            for (uint32_t index = 0; index < 256; ++index)
            {
                uint32_t value = index << 24;
                for (int bit = 0; bit < 8; ++bit)
                {
                    value = (value << 1) ^ ((value & 0x80000000u) ? 0x04c11db7u : 0u);
                }
                table.Values[index] = value;
            }
            return table;
        }

        constexpr auto PageCrcTable = MakeOggCrcTable();
        static_assert(PageCrcTable.Values[1] == 0x04c11db7u);

        constexpr uint32_t UpdateOggCrc(uint32_t crc, uint8_t byte) noexcept
        {
            return (crc << 8) ^ PageCrcTable.Values[byte ^ (crc >> 24)];
        }

        static_assert([] {
            uint32_t crc = 0;
            constexpr uint8_t digits[] = "123456789";
            for (size_t i = 0; i < sizeof(digits) - 1; ++i)
            {
                crc = UpdateOggCrc(crc, digits[i]);
            }
            return crc;
        }() == 0x89a1897fu);

        uint32_t ReadLittleEndian32(const uint8_t* bytes) noexcept
        {
            return bytes[0] | (static_cast<uint32_t>(bytes[1]) << 8)
                | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
        }

        // stb は pull 復号時にページの CRC を調べず、EOS がなくても直前のページの granule を総フレーム数に使う。
        // 各ページの境界と CRC を検査し、同一ストリームのページが連番で入力末尾の EOS まで続くことを確かめる。
        // 入力全体を先頭から 1 回たどるため、処理時間は O(n)。
        [[nodiscard]] bool ValidateOggPages(Core::Container::Span<const uint8_t> bytes, uint64_t& outFinalGranule) noexcept
        {
            constexpr size_t fixedHeaderSize = 27;
            constexpr uint8_t continued = 0x01;
            constexpr uint8_t bos = 0x02;
            constexpr uint8_t eos = 0x04;
            uint32_t streamSerial = 0;
            uint32_t expectedSequence = 0;
            size_t offset = 0;
            while (offset < bytes.size())
            {
                const size_t remaining = bytes.size() - offset;
                const uint8_t* page = bytes.data() + offset;
                if (remaining < fixedHeaderSize || page[0] != 'O' || page[1] != 'g' || page[2] != 'g'
                    || page[3] != 'S' || page[4] != 0)
                {
                    return false;
                }

                const uint8_t flags = page[5];
                if ((flags & ~(continued | bos | eos)) != 0)
                {
                    return false;
                }

                const uint64_t granule = ReadLittleEndian32(page + 6) | (static_cast<uint64_t>(ReadLittleEndian32(page + 10)) << 32);
                const uint32_t serial = ReadLittleEndian32(page + 14);
                const uint32_t sequence = ReadLittleEndian32(page + 18);
                const uint32_t recordedCrc = ReadLittleEndian32(page + 22);
                const size_t headerSize = fixedHeaderSize + page[26];
                if (remaining < headerSize)
                {
                    return false;
                }

                size_t pageSize = headerSize;
                for (size_t i = fixedHeaderSize; i < headerSize; ++i)
                {
                    pageSize += page[i];
                }
                if (remaining < pageSize)
                {
                    return false;
                }

                uint32_t crc = 0;
                for (size_t i = 0; i < pageSize; ++i)
                {
                    crc = UpdateOggCrc(crc, (i >= 22 && i < 26) ? 0 : page[i]);
                }
                if (crc != recordedCrc)
                {
                    return false;
                }

                if (offset == 0)
                {
                    if ((flags & bos) == 0 || (flags & continued) != 0)
                    {
                        return false;
                    }
                    streamSerial = serial;
                }
                else if ((flags & bos) != 0 || serial != streamSerial || sequence != expectedSequence)
                {
                    return false;
                }
                expectedSequence = sequence + 1u;

                if ((flags & eos) != 0)
                {
                    if (pageSize != remaining || granule == UINT64_MAX)
                    {
                        return false;
                    }
                    outFinalGranule = granule;
                    return true;
                }
                offset += pageSize;
            }
            return false;
        }
    }

    Core::Container::TUniquePtr<IAudioDecoder> CreateVorbisDecoder(Core::Container::Span<const uint8_t> oggBytes)
    {
        if (oggBytes.empty() || oggBytes.data() == nullptr || oggBytes.size() > INT_MAX)
        {
            return {};
        }

        uint64_t finalGranule = 0;
        if (!ValidateOggPages(oggBytes, finalGranule))
        {
            return {};
        }

        Private::VorbisHandle handle(
            stb_vorbis_open_memory(oggBytes.data(), static_cast<int>(oggBytes.size()), nullptr, nullptr), &stb_vorbis_close);
        if (!handle || stb_vorbis_get_error(handle.get()) != VORBIS__no_error)
        {
            return {};
        }

        const auto info = stb_vorbis_get_info(handle.get());
        if (info.channels < 1 || info.channels > STB_VORBIS_MAX_CHANNELS || info.sample_rate == 0)
        {
            return {};
        }

        const auto totalFrames = stb_vorbis_stream_length_in_samples64(handle.get());
        if (stb_vorbis_get_error(handle.get()) != VORBIS__no_error || totalFrames <= 0 || static_cast<uint64_t>(totalFrames) != finalGranule)
        {
            return {};
        }

        const int seekResult = stb_vorbis_seek_start(handle.get());
        if (stb_vorbis_get_error(handle.get()) != VORBIS__no_error || !seekResult)
        {
            return {};
        }

        return Core::Container::MakeUnique<Private::VorbisDecoder>(
            std::move(handle), AudioDecoderInfo{info.sample_rate, static_cast<uint16_t>(info.channels), static_cast<uint64_t>(totalFrames)});
    }
} // namespace NorvesLib::Modules::Audio
