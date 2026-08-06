#include "Asset/CookedAudioFormat.h"

#include "Asset/AssetBlob.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Asset;

    void WriteLe16(Container::VariableArray<uint8_t>& bytes, size_t offset, uint16_t value)
    {
        bytes[offset] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(Container::VariableArray<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        bytes[offset] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(Container::VariableArray<uint8_t>& bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
    }

    Container::VariableArray<uint8_t> MakeAudio(uint32_t sampleRate = 44100,
                                                 uint16_t channelCount = 2,
                                                 uint16_t bitsPerSample = 16,
                                                 uint16_t blockAlignment = 4)
    {
        using namespace CookedAudioFormatV0;
        const Container::VariableArray<uint8_t> pcm{0x01, 0x80, 0x02, 0x7f, 0x03, 0x00, 0x04, 0xff};
        Container::VariableArray<uint8_t> bytes(HeaderSize + pcm.size(), 0);
        for (size_t index = 0; index < MagicSize; ++index)
        {
            bytes[HeaderOffset::Magic + index] = Magic[index];
        }
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, VersionMinor);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe64(bytes, HeaderOffset::FileSize, bytes.size());
        WriteLe64(bytes, HeaderOffset::PayloadOffset, HeaderSize);
        WriteLe64(bytes, HeaderOffset::PayloadSize, pcm.size());
        WriteLe64(bytes, HeaderOffset::PayloadHash, ComputeCookedAudioPayloadHash(pcm.data(), pcm.size()));
        WriteLe32(bytes, HeaderOffset::SampleRate, sampleRate);
        WriteLe16(bytes, HeaderOffset::ChannelCount, channelCount);
        WriteLe16(bytes, HeaderOffset::BitsPerSample, bitsPerSample);
        WriteLe16(bytes, HeaderOffset::BlockAlignment, blockAlignment);
        for (size_t index = 0; index < pcm.size(); ++index)
        {
            bytes[HeaderSize + index] = pcm[index];
        }
        return bytes;
    }

    CookedAudioParseResult Parse(const Container::VariableArray<uint8_t>& bytes)
    {
        return ParseCookedAudio(AssetBlob::CopyBytes(Container::Span<const uint8_t>(bytes.data(), bytes.size())));
    }

    void TestValidPcm16IsRetained()
    {
        const auto result = Parse(MakeAudio());
        assert(result.Succeeded());
        assert(result.Audio.SampleRate == 44100);
        assert(result.Audio.ChannelCount == 2);
        assert(result.Audio.BitsPerSample == 16);
        assert(result.Audio.BlockAlignment == 4);
        assert(result.Audio.FrameCount == 2);
        const auto pcm = result.Audio.GetPcmBytes();
        assert(pcm.size() == 8);
        assert(pcm[0] == 0x01 && pcm[1] == 0x80 && pcm[6] == 0x04 && pcm[7] == 0xff);
    }

    void TestStrictPcmContract()
    {
        assert(Parse(MakeAudio(22050)).Status == CookedAudioParseStatus::UnsupportedSampleRate);
        assert(Parse(MakeAudio(44100, 3, 16, 6)).Status == CookedAudioParseStatus::UnsupportedChannelCount);
        assert(Parse(MakeAudio(44100, 2, 8, 2)).Status == CookedAudioParseStatus::UnsupportedBitsPerSample);
        assert(Parse(MakeAudio(48000, 2, 16, 2)).Status == CookedAudioParseStatus::BlockAlignmentMismatch);

        auto misaligned = MakeAudio(48000, 1, 16, 2);
        misaligned.push_back(0);
        WriteLe64(misaligned, CookedAudioFormatV0::HeaderOffset::FileSize, misaligned.size());
        WriteLe64(misaligned, CookedAudioFormatV0::HeaderOffset::PayloadSize, 9);
        WriteLe64(misaligned,
                  CookedAudioFormatV0::HeaderOffset::PayloadHash,
                  ComputeCookedAudioPayloadHash(misaligned.data() + CookedAudioFormatV0::HeaderSize, 9));
        assert(Parse(misaligned).Status == CookedAudioParseStatus::PayloadBlockMisaligned);
    }

    void TestCorruptionIsRejected()
    {
        auto badHash = MakeAudio();
        badHash[CookedAudioFormatV0::HeaderOffset::PayloadHash] ^= 0xffu;
        assert(Parse(badHash).Status == CookedAudioParseStatus::PayloadHashMismatch);

        auto truncated = MakeAudio();
        truncated.pop_back();
        assert(Parse(truncated).Status == CookedAudioParseStatus::FileSizeMismatch);
    }
}

int main()
{
    TestValidPcm16IsRetained();
    TestStrictPcmContract();
    TestCorruptionIsRejected();
    std::cout << "AudioCookedAssetTest passed\n";
    return 0;
}
