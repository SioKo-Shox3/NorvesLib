#include "Asset/CookedScatterFormat.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        using namespace CookedScatterFormatV0;

        void WriteLe(uint8_t* data, size_t offset, uint64_t value, size_t width) noexcept
        {
            for (size_t i = 0; i < width; ++i)
            {
                data[offset + i] = static_cast<uint8_t>(value >> (i * 8));
            }
        }

        void WriteFloat(uint8_t* data, size_t offset, float value) noexcept
        {
            WriteLe(data, offset, std::bit_cast<uint32_t>(value), 4);
        }

        bool Within(uint64_t offset, uint64_t count, uint64_t total) noexcept
        {
            return offset <= total && count <= total - offset;
        }

        bool AddStringSize(const Container::AnsiString& value, uint64_t& size) noexcept
        {
            if (value.empty() || value.size() > UINT32_MAX - size)
            {
                return false;
            }
            size += value.size();
            return true;
        }

        void WriteString(uint8_t* data, size_t reference, size_t stringStart, uint32_t& stringSize,
                         const Container::AnsiString& value) noexcept
        {
            WriteLe(data, reference, stringSize, 4);
            WriteLe(data, reference + 4, value.size(), 4);
            std::memcpy(data + stringStart + stringSize, value.data(), value.size());
            stringSize += static_cast<uint32_t>(value.size());
        }
    } // namespace

    CookedScatterWriteStatus WriteCookedScatter(const CookedScatterWriteDesc& desc,
                                                Container::VariableArray<uint8_t>& outBytes)
    {
        using Status = CookedScatterWriteStatus;
        outBytes.clear();
        if (!std::isfinite(desc.ChunkSizeMeters) || desc.ChunkSizeMeters <= 0.0f ||
            !std::isfinite(desc.OriginX) || !std::isfinite(desc.OriginZ) || desc.GridCountX == 0 || desc.GridCountZ == 0 ||
            !std::isfinite(desc.OriginX + static_cast<double>(desc.GridCountX) * desc.ChunkSizeMeters) ||
            !std::isfinite(desc.OriginZ + static_cast<double>(desc.GridCountZ) * desc.ChunkSizeMeters))
        {
            return Status::InvalidDimensions;
        }
        if (desc.Species.empty() || desc.Species.size() > MaxSpeciesCount)
        {
            return Status::InvalidSpecies;
        }
        if (desc.Chunks.size() > UINT32_MAX)
        {
            return Status::TooLarge;
        }
        if (desc.Chunks.size() > static_cast<uint64_t>(desc.GridCountX) * desc.GridCountZ)
        {
            return Status::InvalidChunk;
        }
        const uint64_t speciesCount = desc.Species.size();
        const uint64_t chunkCount = desc.Chunks.size();
        const uint64_t rangeCount = chunkCount * speciesCount;
        if (desc.SpeciesRanges.size() != rangeCount)
        {
            return Status::InvalidRanges;
        }
        uint64_t stringSize = 0;
        for (const auto& species : desc.Species)
        {
            if (static_cast<uint32_t>(species.RenderPath) > 1 || species.LodCount == 0 || species.LodCount > MaxLodCount ||
                (species.Flags & ~1u) != 0 || !std::isfinite(species.MeshRadiusMeters) || species.MeshRadiusMeters < 0.0f ||
                !std::isfinite(species.MinSpacingMeters) || species.MinSpacingMeters <= 0.0f ||
                !std::isfinite(species.ScaleMin) || !std::isfinite(species.ScaleMax) || species.ScaleMin <= 0.0f ||
                species.ScaleMin > species.ScaleMax || !std::isfinite(species.SlopeFollow) ||
                species.SlopeFollow < 0.0f || species.SlopeFollow > 1.0f ||
                !std::isfinite(species.BrightnessVariation) || species.BrightnessVariation < 0.0f ||
                !std::isfinite(species.WindStiffness) || species.WindStiffness < 0.0f ||
                !AddStringSize(species.Name, stringSize))
            {
                return Status::InvalidSpecies;
            }
            for (uint32_t lod = 0; lod < MaxLodCount; ++lod)
            {
                if ((lod < species.LodCount && !AddStringSize(species.LodPaths[lod], stringSize)) ||
                    (lod >= species.LodCount && !species.LodPaths[lod].empty()))
                {
                    return Status::InvalidSpecies;
                }
            }
        }
        Container::VariableArray<uint64_t> keys;
        keys.reserve(static_cast<size_t>(chunkCount));
        for (const auto& chunk : desc.Chunks)
        {
            if (chunk.ChunkX >= desc.GridCountX || chunk.ChunkZ >= desc.GridCountZ ||
                !std::isfinite(chunk.MinY) || !std::isfinite(chunk.MaxY) || chunk.MinY > chunk.MaxY)
            {
                return Status::InvalidChunk;
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(chunk.AabbMin[axis]) || !std::isfinite(chunk.AabbMax[axis]) ||
                    chunk.AabbMin[axis] > chunk.AabbMax[axis])
                {
                    return Status::InvalidChunk;
                }
            }
            keys.push_back((static_cast<uint64_t>(chunk.ChunkZ) << 32) | chunk.ChunkX);
        }
        if (keys.size() > 1)
        {
            std::sort(keys.begin(), keys.end());
            for (size_t i = 1; i < keys.size(); ++i)
            {
                if (keys[i - 1] == keys[i])
                {
                    return Status::InvalidChunk;
                }
            }
        }
        for (const auto& range : desc.SpeciesRanges)
        {
            if (!Within(range.Offset, range.Count, desc.Instances.size()))
            {
                return Status::InvalidRanges;
            }
        }

        // 件数の上限により、文字列表までの表の積と和は uint64 に収まる。
        const uint64_t speciesOffset = HeaderSize;
        const uint64_t speciesSize = speciesCount * SpeciesRecordSize;
        const uint64_t chunkOffset = speciesOffset + speciesSize;
        const uint64_t chunkSize = chunkCount * ChunkRecordSize;
        const uint64_t rangeOffset = chunkOffset + chunkSize;
        const uint64_t rangeSize = rangeCount * RangeRecordSize;
        const uint64_t stringOffset = rangeOffset + rangeSize;
        const uint64_t instanceOffset = (stringOffset + stringSize + InstanceAlignment - 1) & ~uint64_t(InstanceAlignment - 1);
        if (desc.Instances.size() > (UINT64_MAX - instanceOffset) / InstanceRecordSize)
        {
            return Status::TooLarge;
        }
        const uint64_t instanceSize = static_cast<uint64_t>(desc.Instances.size()) * InstanceRecordSize;
        const uint64_t fileSize = instanceOffset + instanceSize;
        Container::VariableArray<uint8_t> bytes;
        if (fileSize > bytes.max_size())
        {
            return Status::TooLarge;
        }
        bytes.resize(static_cast<size_t>(fileSize), 0);
        uint8_t* data = bytes.data();
        std::memcpy(data, Magic, MagicSize);
        WriteLe(data, HeaderOffset::HeaderSize, HeaderSize, 4);
        WriteLe(data, HeaderOffset::Version, Version, 4);
        WriteLe(data, HeaderOffset::EndianMarker, EndianMarker, 4);
        WriteLe(data, HeaderOffset::FileSize, fileSize, 8);
        WriteLe(data, HeaderOffset::Seed, desc.Seed, 8);
        WriteLe(data, HeaderOffset::TerrainCookedHash, desc.TerrainCookedHash, 8);
        WriteFloat(data, HeaderOffset::ChunkSizeMeters, desc.ChunkSizeMeters);
        WriteLe(data, HeaderOffset::GridCountX, desc.GridCountX, 4);
        WriteLe(data, HeaderOffset::GridCountZ, desc.GridCountZ, 4);
        WriteLe(data, HeaderOffset::SpeciesCount, speciesCount, 4);
        WriteLe(data, HeaderOffset::ChunkCount, chunkCount, 4);
        WriteLe(data, HeaderOffset::OriginX, std::bit_cast<uint64_t>(desc.OriginX), 8);
        WriteLe(data, HeaderOffset::OriginZ, std::bit_cast<uint64_t>(desc.OriginZ), 8);
        WriteLe(data, HeaderOffset::SpeciesTableOffset, speciesOffset, 8);
        WriteLe(data, HeaderOffset::SpeciesTableSize, speciesSize, 8);
        WriteLe(data, HeaderOffset::ChunkTableOffset, chunkOffset, 8);
        WriteLe(data, HeaderOffset::ChunkTableSize, chunkSize, 8);
        WriteLe(data, HeaderOffset::RangeTableOffset, rangeOffset, 8);
        WriteLe(data, HeaderOffset::RangeTableSize, rangeSize, 8);
        WriteLe(data, HeaderOffset::StringTableOffset, stringOffset, 8);
        WriteLe(data, HeaderOffset::StringTableSize, stringSize, 8);
        WriteLe(data, HeaderOffset::InstanceOffset, instanceOffset, 8);
        WriteLe(data, HeaderOffset::InstanceSize, instanceSize, 8);
        uint32_t writtenStrings = 0;
        for (size_t i = 0; i < desc.Species.size(); ++i)
        {
            const auto& species = desc.Species[i];
            const size_t record = static_cast<size_t>(speciesOffset + i * SpeciesRecordSize);
            WriteString(data, record + SpeciesRecordOffset::Name, static_cast<size_t>(stringOffset), writtenStrings, species.Name);
            WriteLe(data, record + SpeciesRecordOffset::RenderPath, static_cast<uint32_t>(species.RenderPath), 4);
            WriteLe(data, record + SpeciesRecordOffset::LodCount, species.LodCount, 4);
            for (uint32_t lod = 0; lod < species.LodCount; ++lod)
            {
                WriteString(data, record + SpeciesRecordOffset::LodPaths + lod * 8,
                            static_cast<size_t>(stringOffset), writtenStrings, species.LodPaths[lod]);
            }
            WriteFloat(data, record + SpeciesRecordOffset::MeshRadiusMeters, species.MeshRadiusMeters);
            WriteFloat(data, record + SpeciesRecordOffset::MinSpacingMeters, species.MinSpacingMeters);
            WriteFloat(data, record + SpeciesRecordOffset::ScaleMin, species.ScaleMin);
            WriteFloat(data, record + SpeciesRecordOffset::ScaleMax, species.ScaleMax);
            WriteFloat(data, record + SpeciesRecordOffset::SlopeFollow, species.SlopeFollow);
            WriteFloat(data, record + SpeciesRecordOffset::BrightnessVariation, species.BrightnessVariation);
            WriteFloat(data, record + SpeciesRecordOffset::WindStiffness, species.WindStiffness);
            WriteLe(data, record + SpeciesRecordOffset::WindPhaseSeed, species.WindPhaseSeed, 4);
            WriteLe(data, record + SpeciesRecordOffset::Flags, species.Flags, 4);
        }
        for (size_t i = 0; i < desc.Chunks.size(); ++i)
        {
            const auto& chunk = desc.Chunks[i];
            const size_t record = static_cast<size_t>(chunkOffset + i * ChunkRecordSize);
            WriteLe(data, record + ChunkRecordOffset::ChunkX, chunk.ChunkX, 4);
            WriteLe(data, record + ChunkRecordOffset::ChunkZ, chunk.ChunkZ, 4);
            WriteFloat(data, record + ChunkRecordOffset::MinY, chunk.MinY);
            WriteFloat(data, record + ChunkRecordOffset::MaxY, chunk.MaxY);
            for (size_t axis = 0; axis < 3; ++axis)
            {
                WriteFloat(data, record + ChunkRecordOffset::AabbMin + axis * 4, chunk.AabbMin[axis]);
                WriteFloat(data, record + ChunkRecordOffset::AabbMax + axis * 4, chunk.AabbMax[axis]);
            }
        }
        for (size_t i = 0; i < desc.SpeciesRanges.size(); ++i)
        {
            const size_t record = static_cast<size_t>(rangeOffset + i * RangeRecordSize);
            WriteLe(data, record + RangeRecordOffset::Offset, desc.SpeciesRanges[i].Offset, 8);
            WriteLe(data, record + RangeRecordOffset::Count, desc.SpeciesRanges[i].Count, 8);
        }
        for (size_t i = 0; i < desc.Instances.size(); ++i)
        {
            const auto& instance = desc.Instances[i];
            const size_t record = static_cast<size_t>(instanceOffset + i * InstanceRecordSize);
            WriteLe(data, record, instance.PosX, 2);
            WriteLe(data, record + 2, instance.PosZ, 2);
            WriteLe(data, record + 4, instance.PosY, 2);
            WriteLe(data, record + 6, instance.Yaw, 2);
            data[record + 8] = instance.Scale;
            data[record + 9] = instance.Rank;
            data[record + 10] = instance.NormalOct[0];
            data[record + 11] = instance.NormalOct[1];
            for (size_t channel = 0; channel < 3; ++channel)
            {
                data[record + 12 + channel] = instance.Tint[channel];
            }
            data[record + 15] = instance.Dry;
        }
        WriteLe(data, HeaderOffset::PayloadHash, ComputeCookedScatterFileHash(Container::Span<const uint8_t>(bytes)), 8);
        outBytes = std::move(bytes);
        return Status::Success;
    }
} // namespace NorvesLib::Core::Asset
