#include "Asset/CookedSkeletalFormat.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        namespace Format = CookedSkeletalFormatV0;
        namespace HeaderOffset = Format::HeaderOffset;

        struct Section
        {
            size_t Offset = 0;
            size_t Size = 0;
        };

        bool CheckedAdd(size_t a, size_t b, size_t& out)
        {
            if (a > std::numeric_limits<size_t>::max() - b)
            {
                return false;
            }
            out = a + b;
            return true;
        }

        bool CheckedMultiply(size_t a, size_t b, size_t& out)
        {
            if (a != 0 && b > std::numeric_limits<size_t>::max() / a)
            {
                return false;
            }
            out = a * b;
            return true;
        }

        bool AlignUp(size_t value, size_t alignment, size_t& out)
        {
            size_t adjusted = 0;
            if (alignment == 0 || !CheckedAdd(value, alignment - 1, adjusted))
            {
                return false;
            }
            out = adjusted / alignment * alignment;
            return true;
        }

        bool IsInvertibleMatrix(const Container::FixedArray<float, 16>& matrix)
        {
            Container::FixedArray<float, 16> reduced = matrix;
            for (size_t column = 0; column < 4; ++column)
            {
                size_t pivotRow = column;
                float pivotMagnitude = std::fabs(reduced[pivotRow * 4 + column]);
                for (size_t row = column + 1; row < 4; ++row)
                {
                    const float magnitude = std::fabs(reduced[row * 4 + column]);
                    if (magnitude > pivotMagnitude)
                    {
                        pivotRow = row;
                        pivotMagnitude = magnitude;
                    }
                }
                if (!std::isfinite(pivotMagnitude) || pivotMagnitude <= 0.000001f)
                {
                    return false;
                }
                if (pivotRow != column)
                {
                    for (size_t element = 0; element < 4; ++element)
                    {
                        const float temporary = reduced[column * 4 + element];
                        reduced[column * 4 + element] = reduced[pivotRow * 4 + element];
                        reduced[pivotRow * 4 + element] = temporary;
                    }
                }
                const float pivot = reduced[column * 4 + column];
                for (size_t row = column + 1; row < 4; ++row)
                {
                    const float factor = reduced[row * 4 + column] / pivot;
                    for (size_t element = column; element < 4; ++element)
                    {
                        reduced[row * 4 + element] -= factor * reduced[column * 4 + element];
                    }
                }
            }
            return true;
        }

        uint16_t ReadLe16(const uint8_t* data, size_t offset)
        {
            return static_cast<uint16_t>(data[offset]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
        }

        uint32_t ReadLe32(const uint8_t* data, size_t offset)
        {
            return static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        uint64_t ReadLe64(const uint8_t* data, size_t offset)
        {
            return static_cast<uint64_t>(ReadLe32(data, offset)) |
                   (static_cast<uint64_t>(ReadLe32(data, offset + 4)) << 32);
        }

        float ReadFloat(const uint8_t* data, size_t offset)
        {
            const uint32_t bits = ReadLe32(data, offset);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        bool ReadSize(const uint8_t* data, size_t offset, size_t& out)
        {
            const uint64_t value = ReadLe64(data, offset);
            if (value > std::numeric_limits<size_t>::max())
            {
                return false;
            }
            out = static_cast<size_t>(value);
            return true;
        }

        bool ReadSection(const uint8_t* data, size_t offsetField, size_t sizeField, Section& out)
        {
            return ReadSize(data, offsetField, out.Offset) && ReadSize(data, sizeField, out.Size);
        }

        bool ValidateSection(const Section& section, size_t fileSize, size_t alignment)
        {
            size_t end = 0;
            return section.Offset >= Format::HeaderSize &&
                   (alignment == 0 || section.Offset % alignment == 0) &&
                   CheckedAdd(section.Offset, section.Size, end) && end <= fileSize;
        }

        bool ValidateRecordSection(const Section& section, size_t count, size_t recordSize)
        {
            size_t expectedSize = 0;
            return CheckedMultiply(count, recordSize, expectedSize) && section.Size == expectedSize;
        }

        bool ValidateRange(size_t offset, size_t count, size_t total)
        {
            size_t end = 0;
            return CheckedAdd(offset, count, end) && end <= total;
        }

        bool ReadString(const uint8_t* data,
                        const Section& stringSection,
                        size_t offset,
                        size_t length,
                        Container::String& out)
        {
            if (!ValidateRange(offset, length, stringSection.Size))
            {
                return false;
            }
            out.clear();
            out.reserve(length);
            for (size_t index = 0; index < length; ++index)
            {
                const uint8_t character = data[stringSection.Offset + offset + index];
                if (character < 0x20u || character > 0x7eu)
                {
                    return false;
                }
                out.push_back(static_cast<Container::String::value_type>(character));
            }
            return true;
        }

        CookedSkeletalParseResult Fail(CookedSkeletalParseStatus status)
        {
            CookedSkeletalParseResult result;
            result.Status = status;
            return result;
        }
    } // namespace

    CookedSkeletalParseResult ParseCookedSkeletal(const AssetBlob& blob)
    {
        if (!blob.IsValid())
        {
            return Fail(CookedSkeletalParseStatus::InvalidBlob);
        }
        if (blob.IsEmpty())
        {
            return Fail(CookedSkeletalParseStatus::EmptyBlob);
        }
        if (blob.GetSize() < Format::HeaderSize)
        {
            return Fail(CookedSkeletalParseStatus::HeaderTooSmall);
        }

        const uint8_t* data = blob.GetData();
        if (std::memcmp(data + HeaderOffset::Magic, Format::Magic, Format::MagicSize) != 0)
        {
            return Fail(CookedSkeletalParseStatus::BadMagic);
        }
        const uint16_t versionMinor = ReadLe16(data, HeaderOffset::VersionMinor);
        if (ReadLe16(data, HeaderOffset::VersionMajor) != Format::VersionMajor ||
            (versionMinor != Format::LegacyVersionMinor && versionMinor != Format::VersionMinor))
        {
            return Fail(CookedSkeletalParseStatus::UnsupportedVersion);
        }
        if (ReadLe32(data, HeaderOffset::HeaderSize) != Format::HeaderSize ||
            ReadLe32(data, HeaderOffset::EndianMarker) != Format::EndianMarker ||
            ReadLe32(data, HeaderOffset::VertexRecordSize) != Format::VertexRecordSize ||
            ReadLe32(data, HeaderOffset::JointRecordSize) != Format::JointRecordSize ||
            ReadLe32(data, HeaderOffset::ClipRecordSize) != Format::ClipRecordSize ||
            ReadLe32(data, HeaderOffset::ChannelRecordSize) != Format::ChannelRecordSize ||
            ReadLe32(data, HeaderOffset::SampleRecordSize) != Format::SampleRecordSize)
        {
            return Fail(CookedSkeletalParseStatus::InvalidHeader);
        }

        size_t declaredFileSize = 0;
        if (!ReadSize(data, HeaderOffset::FileSize, declaredFileSize) || declaredFileSize != blob.GetSize())
        {
            return Fail(CookedSkeletalParseStatus::FileSizeMismatch);
        }

        Section vertexSection;
        Section indexSection;
        Section jointSection;
        Section clipSection;
        Section channelSection;
        Section sampleSection;
        Section stringSection;
        if (!ReadSection(data, HeaderOffset::VertexOffset, HeaderOffset::VertexSize, vertexSection) ||
            !ReadSection(data, HeaderOffset::IndexOffset, HeaderOffset::IndexSize, indexSection) ||
            !ReadSection(data, HeaderOffset::JointOffset, HeaderOffset::JointSize, jointSection) ||
            !ReadSection(data, HeaderOffset::ClipOffset, HeaderOffset::ClipSize, clipSection) ||
            !ReadSection(data, HeaderOffset::ChannelOffset, HeaderOffset::ChannelSize, channelSection) ||
            !ReadSection(data, HeaderOffset::SampleOffset, HeaderOffset::SampleSize, sampleSection) ||
            !ReadSection(data, HeaderOffset::StringOffset, HeaderOffset::StringSize, stringSection) ||
            !ValidateSection(vertexSection, declaredFileSize, Format::SectionAlignment) ||
            !ValidateSection(indexSection, declaredFileSize, Format::SectionAlignment) ||
            !ValidateSection(jointSection, declaredFileSize, Format::SectionAlignment) ||
            !ValidateSection(clipSection, declaredFileSize, Format::SectionAlignment) ||
            !ValidateSection(channelSection, declaredFileSize, Format::SectionAlignment) ||
            !ValidateSection(sampleSection, declaredFileSize, Format::SectionAlignment) ||
            !ValidateSection(stringSection, declaredFileSize, Format::SectionAlignment))
        {
            return Fail(CookedSkeletalParseStatus::SectionOutOfRange);
        }

        size_t vertexEnd = 0;
        size_t indexEnd = 0;
        size_t jointEnd = 0;
        size_t clipEnd = 0;
        size_t channelEnd = 0;
        size_t sampleEnd = 0;
        size_t stringEnd = 0;
        if (!CheckedAdd(vertexSection.Offset, vertexSection.Size, vertexEnd) ||
            !CheckedAdd(indexSection.Offset, indexSection.Size, indexEnd) ||
            !CheckedAdd(jointSection.Offset, jointSection.Size, jointEnd) ||
            !CheckedAdd(clipSection.Offset, clipSection.Size, clipEnd) ||
            !CheckedAdd(channelSection.Offset, channelSection.Size, channelEnd) ||
            !CheckedAdd(sampleSection.Offset, sampleSection.Size, sampleEnd) ||
            !CheckedAdd(stringSection.Offset, stringSection.Size, stringEnd) ||
            indexSection.Offset < vertexEnd || jointSection.Offset < indexEnd || clipSection.Offset < jointEnd ||
            channelSection.Offset < clipEnd || sampleSection.Offset < channelEnd ||
            stringSection.Offset < sampleEnd || stringEnd != declaredFileSize)
        {
            return Fail(CookedSkeletalParseStatus::SectionOutOfRange);
        }

        const size_t vertexCount = ReadLe32(data, HeaderOffset::VertexCount);
        const size_t indexCount = ReadLe32(data, HeaderOffset::IndexCount);
        const size_t jointCount = ReadLe32(data, HeaderOffset::JointCount);
        const size_t clipCount = ReadLe32(data, HeaderOffset::ClipCount);
        const size_t channelCount = ReadLe32(data, HeaderOffset::ChannelCount);
        const size_t sampleCount = ReadLe32(data, HeaderOffset::SampleCount);
        if (!ValidateRecordSection(vertexSection, vertexCount, Format::VertexRecordSize) ||
            !ValidateRecordSection(indexSection, indexCount, sizeof(uint32_t)) ||
            !ValidateRecordSection(jointSection, jointCount, Format::JointRecordSize) ||
            !ValidateRecordSection(clipSection, clipCount, Format::ClipRecordSize) ||
            !ValidateRecordSection(channelSection, channelCount, Format::ChannelRecordSize) ||
            !ValidateRecordSection(sampleSection, sampleCount, Format::SampleRecordSize) || clipCount != 1 ||
            vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 || jointCount == 0 || jointCount > 128 ||
            channelCount == 0 || sampleCount == 0)
        {
            return Fail(CookedSkeletalParseStatus::InvalidRecord);
        }

        size_t expectedSectionOffset = Format::HeaderSize;
        const Section sections[] = {
            vertexSection, indexSection, jointSection, clipSection, channelSection, sampleSection, stringSection};
        for (size_t sectionIndex = 0; sectionIndex < sizeof(sections) / sizeof(sections[0]); ++sectionIndex)
        {
            const Section& section = sections[sectionIndex];
            if (section.Offset != expectedSectionOffset ||
                !CheckedAdd(section.Offset, section.Size, expectedSectionOffset))
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
            if (sectionIndex + 1 < sizeof(sections) / sizeof(sections[0]))
            {
                const size_t sectionEnd = expectedSectionOffset;
                if (!AlignUp(sectionEnd, Format::SectionAlignment, expectedSectionOffset))
                {
                    return Fail(CookedSkeletalParseStatus::InvalidRecord);
                }
                for (size_t paddingOffset = sectionEnd; paddingOffset < expectedSectionOffset; ++paddingOffset)
                {
                    if (data[paddingOffset] != 0)
                    {
                        return Fail(CookedSkeletalParseStatus::InvalidRecord);
                    }
                }
            }
        }

        const uint64_t expectedHash = ReadLe64(data, HeaderOffset::PayloadHash);
        const uint64_t actualHash = versionMinor == Format::LegacyVersionMinor
            ? ComputeCookedSkeletalPayloadHash(data + Format::HeaderSize, declaredFileSize - Format::HeaderSize)
            : ComputeCookedSkeletalV01Hash(data + HeaderOffset::MeshNodeGlobalTransform,
                                           data + Format::HeaderSize,
                                           declaredFileSize - Format::HeaderSize);
        if (expectedHash != actualHash)
        {
            return Fail(CookedSkeletalParseStatus::PayloadHashMismatch);
        }

        Skeletal::SkeletalGltfData skeletal;
        skeletal.Vertices.resize(vertexCount);
        skeletal.Indices.resize(indexCount);
        skeletal.Joints.resize(jointCount);
        skeletal.Clips.resize(clipCount);
        if (versionMinor == Format::LegacyVersionMinor)
        {
            for (size_t byteIndex = HeaderOffset::MeshNodeGlobalTransform; byteIndex < Format::HeaderSize; ++byteIndex)
            {
                if (data[byteIndex] != 0)
                {
                    return Fail(CookedSkeletalParseStatus::InvalidHeader);
                }
            }
        }
        else
        {
            for (size_t element = 0; element < 16; ++element)
            {
                skeletal.MeshNodeGlobalTransform[element] =
                    ReadFloat(data, HeaderOffset::MeshNodeGlobalTransform + element * sizeof(float));
                if (!std::isfinite(skeletal.MeshNodeGlobalTransform[element]))
                {
                    return Fail(CookedSkeletalParseStatus::InvalidRecord);
                }
            }
            if (!IsInvertibleMatrix(skeletal.MeshNodeGlobalTransform))
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
        }

        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            const size_t record = vertexSection.Offset + vertexIndex * Format::VertexRecordSize;
            Skeletal::SkeletalVertex& vertex = skeletal.Vertices[vertexIndex];
            vertex.Position = {ReadFloat(data, record + Format::VertexOffset::Position + 0),
                               ReadFloat(data, record + Format::VertexOffset::Position + 4),
                               ReadFloat(data, record + Format::VertexOffset::Position + 8)};
            vertex.Normal = {ReadFloat(data, record + Format::VertexOffset::Normal + 0),
                             ReadFloat(data, record + Format::VertexOffset::Normal + 4),
                             ReadFloat(data, record + Format::VertexOffset::Normal + 8)};
            vertex.TexCoord = {ReadFloat(data, record + Format::VertexOffset::TexCoord + 0),
                               ReadFloat(data, record + Format::VertexOffset::TexCoord + 4)};
            for (size_t influence = 0; influence < 4; ++influence)
            {
                vertex.JointIndices[influence] =
                    ReadLe32(data, record + Format::VertexOffset::JointIndices + influence * sizeof(uint32_t));
                vertex.JointWeights[influence] =
                    ReadFloat(data, record + Format::VertexOffset::JointWeights + influence * sizeof(float));
                if (vertex.JointIndices[influence] >= jointCount)
                {
                    return Fail(CookedSkeletalParseStatus::InvalidRecord);
                }
            }
            float weightSum = 0.0f;
            if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) ||
                !std::isfinite(vertex.Position.Z) || !std::isfinite(vertex.Normal.X) ||
                !std::isfinite(vertex.Normal.Y) || !std::isfinite(vertex.Normal.Z) ||
                !std::isfinite(vertex.TexCoord.U) || !std::isfinite(vertex.TexCoord.V))
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
            for (float weight : vertex.JointWeights)
            {
                if (!std::isfinite(weight) || weight < 0.0f)
                {
                    return Fail(CookedSkeletalParseStatus::InvalidRecord);
                }
                weightSum += weight;
            }
            if (!std::isfinite(weightSum) || std::fabs(weightSum - 1.0f) > 0.001f)
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
        }

        for (size_t index = 0; index < indexCount; ++index)
        {
            skeletal.Indices[index] = ReadLe32(data, indexSection.Offset + index * sizeof(uint32_t));
            if (skeletal.Indices[index] >= vertexCount)
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
        }

        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            const size_t record = jointSection.Offset + jointIndex * Format::JointRecordSize;
            Skeletal::SkeletalJoint& joint = skeletal.Joints[jointIndex];
            joint.ParentIndex = static_cast<int32_t>(ReadLe32(data, record + Format::JointOffset::ParentIndex));
            if (joint.ParentIndex < -1 || joint.ParentIndex >= static_cast<int32_t>(jointCount) ||
                !ReadString(data, stringSection,
                            ReadLe32(data, record + Format::JointOffset::NameOffset),
                            ReadLe32(data, record + Format::JointOffset::NameSize), joint.Name))
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
            for (size_t element = 0; element < 16; ++element)
            {
                joint.InverseBindMatrix[element] = ReadFloat(
                    data, record + Format::JointOffset::InverseBindMatrix + element * sizeof(float));
                if (!std::isfinite(joint.InverseBindMatrix[element]))
                {
                    return Fail(CookedSkeletalParseStatus::InvalidRecord);
                }
            }
        }

        size_t rootCount = 0;
        size_t rootJointIndex = jointCount;
        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            if (skeletal.Joints[jointIndex].ParentIndex < 0)
            {
                ++rootCount;
                rootJointIndex = jointIndex;
            }
        }
        if (rootCount != 1)
        {
            return Fail(CookedSkeletalParseStatus::InvalidRecord);
        }

        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            int32_t ancestor = static_cast<int32_t>(jointIndex);
            size_t depth = 0;
            while (ancestor != static_cast<int32_t>(rootJointIndex))
            {
                if (ancestor < 0 || depth++ >= jointCount)
                {
                    return Fail(CookedSkeletalParseStatus::InvalidRecord);
                }
                ancestor = skeletal.Joints[static_cast<size_t>(ancestor)].ParentIndex;
            }
        }

        Container::VariableArray<Skeletal::SkeletalAnimationChannel> channels(channelCount);
        Container::VariableArray<uint8_t> animatedPaths(jointCount * 3, 0);
        size_t expectedFirstSample = 0;
        float maximumSampleTime = 0.0f;
        for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex)
        {
            const size_t record = channelSection.Offset + channelIndex * Format::ChannelRecordSize;
            Skeletal::SkeletalAnimationChannel& channel = channels[channelIndex];
            channel.JointIndex = ReadLe32(data, record + Format::ChannelOffset::JointIndex);
            const uint32_t path = ReadLe32(data, record + Format::ChannelOffset::Path);
            const uint32_t interpolation = ReadLe32(data, record + Format::ChannelOffset::Interpolation);
            const size_t firstSample = ReadLe32(data, record + Format::ChannelOffset::SampleOffset);
            const size_t channelSampleCount = ReadLe32(data, record + Format::ChannelOffset::SampleCount);
            if (channel.JointIndex >= jointCount || path > static_cast<uint32_t>(Skeletal::SkeletalAnimationPath::Scale) ||
                interpolation > static_cast<uint32_t>(Skeletal::SkeletalAnimationInterpolation::Step) ||
                channelSampleCount == 0 || firstSample != expectedFirstSample ||
                !ValidateRange(firstSample, channelSampleCount, sampleCount))
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
            channel.Path = static_cast<Skeletal::SkeletalAnimationPath>(path);
            channel.Interpolation = static_cast<Skeletal::SkeletalAnimationInterpolation>(interpolation);
            const size_t uniquePathIndex = static_cast<size_t>(channel.JointIndex) * 3 + path;
            if (animatedPaths[uniquePathIndex] != 0)
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
            animatedPaths[uniquePathIndex] = 1;
            channel.Samples.resize(channelSampleCount);
            for (size_t sampleIndex = 0; sampleIndex < channelSampleCount; ++sampleIndex)
            {
                const size_t sampleRecord =
                    sampleSection.Offset + (firstSample + sampleIndex) * Format::SampleRecordSize;
                Skeletal::SkeletalAnimationSample& sample = channel.Samples[sampleIndex];
                sample.TimeSeconds = ReadFloat(data, sampleRecord + Format::SampleOffset::Time);
                sample.Value = {ReadFloat(data, sampleRecord + Format::SampleOffset::Value + 0),
                                ReadFloat(data, sampleRecord + Format::SampleOffset::Value + 4),
                                ReadFloat(data, sampleRecord + Format::SampleOffset::Value + 8),
                                ReadFloat(data, sampleRecord + Format::SampleOffset::Value + 12)};
                if (!std::isfinite(sample.TimeSeconds) || sample.TimeSeconds < 0.0f ||
                    (sampleIndex > 0 && sample.TimeSeconds <= channel.Samples[sampleIndex - 1].TimeSeconds) ||
                    !std::isfinite(sample.Value.X) || !std::isfinite(sample.Value.Y) ||
                    !std::isfinite(sample.Value.Z) || !std::isfinite(sample.Value.W))
                {
                    return Fail(CookedSkeletalParseStatus::InvalidRecord);
                }
                maximumSampleTime = std::fmax(maximumSampleTime, sample.TimeSeconds);
            }
            expectedFirstSample += channelSampleCount;
        }
        if (expectedFirstSample != sampleCount)
        {
            return Fail(CookedSkeletalParseStatus::InvalidRecord);
        }

        for (size_t clipIndex = 0; clipIndex < clipCount; ++clipIndex)
        {
            const size_t record = clipSection.Offset + clipIndex * Format::ClipRecordSize;
            Skeletal::SkeletalAnimationClip& clip = skeletal.Clips[clipIndex];
            const uint64_t nameOffsetValue = ReadLe64(data, record + Format::ClipOffset::NameOffset);
            if (nameOffsetValue > std::numeric_limits<size_t>::max() ||
                !ReadString(data, stringSection, static_cast<size_t>(nameOffsetValue),
                            ReadLe32(data, record + Format::ClipOffset::NameSize), clip.Name))
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
            clip.DurationSeconds = ReadFloat(data, record + Format::ClipOffset::Duration);
            const size_t firstChannel = ReadLe32(data, record + Format::ClipOffset::ChannelOffset);
            const size_t clipChannelCount = ReadLe32(data, record + Format::ClipOffset::ChannelCount);
            if (!std::isfinite(clip.DurationSeconds) || clip.DurationSeconds < 0.0f || firstChannel != 0 ||
                clipChannelCount != channelCount || !ValidateRange(firstChannel, clipChannelCount, channelCount) ||
                std::fabs(clip.DurationSeconds - maximumSampleTime) > 0.000001f)
            {
                return Fail(CookedSkeletalParseStatus::InvalidRecord);
            }
            clip.Channels.reserve(clipChannelCount);
            for (size_t channelIndex = 0; channelIndex < clipChannelCount; ++channelIndex)
            {
                clip.Channels.push_back(std::move(channels[firstChannel + channelIndex]));
            }
        }

        CookedSkeletalParseResult result;
        result.Status = CookedSkeletalParseStatus::Success;
        result.Data.SourceBlob = blob;
        result.Data.PayloadHash = expectedHash;
        result.Data.Skeletal = std::move(skeletal);
        return result;
    }
} // namespace NorvesLib::Core::Asset
