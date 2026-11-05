#include "Asset/CookedScatterFormat.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        using namespace CookedScatterFormatV0;

        uint16_t Read16(const uint8_t* data, size_t offset) noexcept
        {
            return static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) |
                                         (static_cast<uint16_t>(data[offset + 1]) << 8));
        }

        uint32_t Read32(const uint8_t* data, size_t offset) noexcept
        {
            return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        uint64_t Read64(const uint8_t* data, size_t offset) noexcept
        {
            return static_cast<uint64_t>(Read32(data, offset)) | (static_cast<uint64_t>(Read32(data, offset + 4)) << 32);
        }

        float ReadFloat(const uint8_t* data, size_t offset) noexcept
        {
            return std::bit_cast<float>(Read32(data, offset));
        }

        bool Within(uint64_t offset, uint64_t size, uint64_t total) noexcept
        {
            return offset <= total && size <= total - offset;
        }

        CookedScatterParseResult Fail(CookedScatterParseStatus status)
        {
            CookedScatterParseResult result;
            result.Status = status;
            return result;
        }

        bool ReadString(const uint8_t* data, size_t reference, uint64_t tableOffset, uint64_t tableSize,
                        Container::AnsiString& out)
        {
            const uint32_t offset = Read32(data, reference);
            const uint32_t length = Read32(data, reference + 4);
            if (length == 0 || !Within(offset, length, tableSize))
            {
                return false;
            }
            out.reserve(length);
            for (uint32_t i = 0; i < length; ++i)
            {
                out.push_back(static_cast<char>(data[static_cast<size_t>(tableOffset + offset + i)]));
            }
            return true;
        }

        bool ValidCodecBounds(const CookedScatterChunk& chunk, const CookedScatterSpecies& species,
                              float chunkSize, double originX, double originZ) noexcept
        {
            return std::isfinite(chunkSize) && chunkSize > 0.0f && std::isfinite(originX) && std::isfinite(originZ) &&
                   std::isfinite(originX + static_cast<double>(chunk.ChunkX) * chunkSize) &&
                   std::isfinite(originZ + static_cast<double>(chunk.ChunkZ) * chunkSize) &&
                   std::isfinite(chunk.MinY) && std::isfinite(chunk.MaxY) && chunk.MinY <= chunk.MaxY &&
                   std::isfinite(species.ScaleMin) && std::isfinite(species.ScaleMax) &&
                   species.ScaleMin > 0.0f && species.ScaleMin <= species.ScaleMax;
        }

        uint16_t Quantize16(double value, double min, double max) noexcept
        {
            if (min == max)
            {
                return 0;
            }
            return static_cast<uint16_t>(std::round((std::clamp)((value - min) / (max - min), 0.0, 1.0) * 65535.0));
        }

        uint8_t Quantize8(double value) noexcept
        {
            return static_cast<uint8_t>(std::round((std::clamp)(value, 0.0, 1.0) * 255.0));
        }

        double SignNotZero(double value) noexcept
        {
            return value < 0.0 ? -1.0 : 1.0;
        }
    } // namespace

    Container::Span<const uint8_t> CookedScatterData::GetInstances(uint32_t chunkIndex, uint32_t speciesIndex) const noexcept
    {
        if (!Source.IsValid() || chunkIndex >= Chunks.size() || speciesIndex >= Species.size())
        {
            return {};
        }
        const size_t index = static_cast<size_t>(chunkIndex) * Species.size() + speciesIndex;
        if (index >= SpeciesRanges.size())
        {
            return {};
        }
        const auto& range = SpeciesRanges[index];
        const auto bytes = Source.GetSpan();
        if (!Within(InstanceOffset, InstanceSize, bytes.size()) || InstanceSize % InstanceRecordSize != 0 ||
            !Within(range.Offset, range.Count, InstanceSize / InstanceRecordSize))
        {
            return {};
        }
        return {bytes.data() + static_cast<size_t>(InstanceOffset + range.Offset * InstanceRecordSize),
                static_cast<size_t>(range.Count * InstanceRecordSize)};
    }

    bool ReadScatterInstance(Container::Span<const uint8_t> bytes, InstanceRecord16& out) noexcept
    {
        if (bytes.size() < InstanceRecordSize || bytes.data() == nullptr)
        {
            return false;
        }
        InstanceRecord16 record;
        record.PosX = Read16(bytes.data(), 0);
        record.PosZ = Read16(bytes.data(), 2);
        record.PosY = Read16(bytes.data(), 4);
        record.Yaw = Read16(bytes.data(), 6);
        record.Scale = bytes[8];
        record.Rank = bytes[9];
        record.NormalOct[0] = bytes[10];
        record.NormalOct[1] = bytes[11];
        for (size_t i = 0; i < 3; ++i)
        {
            record.Tint[i] = bytes[12 + i];
        }
        record.Dry = bytes[15];
        out = record;
        return true;
    }

    bool EncodeScatterInstance(const ScatterInstanceValue& value, const CookedScatterChunk& chunk,
                               const CookedScatterSpecies& species, float chunkSizeMeters,
                               double originX, double originZ, InstanceRecord16& out) noexcept
    {
        if (!ValidCodecBounds(chunk, species, chunkSizeMeters, originX, originZ))
        {
            return false;
        }
        const double x = originX + static_cast<double>(chunk.ChunkX) * chunkSizeMeters;
        const double z = originZ + static_cast<double>(chunk.ChunkZ) * chunkSizeMeters;
        if (!std::isfinite(x + chunkSizeMeters) || !std::isfinite(z + chunkSizeMeters) ||
            !std::isfinite(value.PositionX) || !std::isfinite(value.PositionY) || !std::isfinite(value.PositionZ) ||
            value.PositionX < x || value.PositionX > x + chunkSizeMeters ||
            value.PositionZ < z || value.PositionZ > z + chunkSizeMeters ||
            value.PositionY < chunk.MinY || value.PositionY > chunk.MaxY ||
            !std::isfinite(value.YawRadians) || !std::isfinite(value.Scale) ||
            value.Scale < species.ScaleMin || value.Scale > species.ScaleMax ||
            !std::isfinite(value.Dry) || value.Dry < 0.0f || value.Dry > 1.0f)
        {
            return false;
        }
        double normal[3];
        double sum = 0.0;
        for (size_t i = 0; i < 3; ++i)
        {
            if (!std::isfinite(value.Normal[i]) || !std::isfinite(value.Tint[i]) || value.Tint[i] < 0.0f || value.Tint[i] > 1.0f)
            {
                return false;
            }
            normal[i] = value.Normal[i];
            sum += std::abs(normal[i]);
        }
        if (sum == 0.0)
        {
            return false;
        }
        for (double& component : normal)
        {
            component /= sum;
        }
        double octX = normal[0];
        double octY = normal[1];
        if (normal[2] < 0.0)
        {
            octX = (1.0 - std::abs(normal[1])) * SignNotZero(normal[0]);
            octY = (1.0 - std::abs(normal[0])) * SignNotZero(normal[1]);
        }
        constexpr double TwoPi = 6.283185307179586476925286766559;
        double yaw = std::fmod(static_cast<double>(value.YawRadians), TwoPi);
        if (yaw < 0.0)
        {
            yaw += TwoPi;
        }
        const double logMin = std::log(static_cast<double>(species.ScaleMin));
        const double logMax = std::log(static_cast<double>(species.ScaleMax));
        InstanceRecord16 record;
        record.PosX = Quantize16(value.PositionX - x, 0.0, chunkSizeMeters);
        record.PosZ = Quantize16(value.PositionZ - z, 0.0, chunkSizeMeters);
        record.PosY = Quantize16(value.PositionY, chunk.MinY, chunk.MaxY);
        // 高低差や原点が極端な場合も、成功した値の位置誤差を保証する。
        const double maxPositionError = static_cast<double>(chunkSizeMeters) / 65535.0;
        const double decodedX = x + static_cast<double>(record.PosX) * chunkSizeMeters / 65535.0;
        const double decodedZ = z + static_cast<double>(record.PosZ) * chunkSizeMeters / 65535.0;
        const double decodedY = static_cast<double>(chunk.MinY) +
                                (static_cast<double>(chunk.MaxY) - chunk.MinY) * record.PosY / 65535.0;
        if (std::abs(decodedX - value.PositionX) > maxPositionError ||
            std::abs(decodedY - value.PositionY) > maxPositionError ||
            std::abs(decodedZ - value.PositionZ) > maxPositionError)
        {
            return false;
        }
        record.Yaw = static_cast<uint16_t>(static_cast<uint32_t>(std::round(yaw * 65536.0 / TwoPi)) & 0xffffu);
        record.Scale = logMin == logMax ? 0 : Quantize8((std::log(static_cast<double>(value.Scale)) - logMin) / (logMax - logMin));
        record.Rank = value.Rank;
        record.NormalOct[0] = Quantize8(octX * 0.5 + 0.5);
        record.NormalOct[1] = Quantize8(octY * 0.5 + 0.5);
        for (size_t i = 0; i < 3; ++i)
        {
            record.Tint[i] = Quantize8(value.Tint[i]);
        }
        record.Dry = Quantize8(value.Dry);
        out = record;
        return true;
    }

    bool DecodeScatterInstance(const InstanceRecord16& record, const CookedScatterChunk& chunk,
                               const CookedScatterSpecies& species, float chunkSizeMeters,
                               double originX, double originZ, ScatterInstanceValue& out) noexcept
    {
        if (!ValidCodecBounds(chunk, species, chunkSizeMeters, originX, originZ))
        {
            return false;
        }
        ScatterInstanceValue value;
        value.PositionX = originX + static_cast<double>(chunk.ChunkX) * chunkSizeMeters +
                          static_cast<double>(record.PosX) * chunkSizeMeters / 65535.0;
        value.PositionZ = originZ + static_cast<double>(chunk.ChunkZ) * chunkSizeMeters +
                          static_cast<double>(record.PosZ) * chunkSizeMeters / 65535.0;
        value.PositionY = static_cast<double>(chunk.MinY) +
                          (static_cast<double>(chunk.MaxY) - chunk.MinY) * record.PosY / 65535.0;
        if (!std::isfinite(value.PositionX) || !std::isfinite(value.PositionZ))
        {
            return false;
        }
        constexpr double TwoPi = 6.283185307179586476925286766559;
        value.YawRadians = static_cast<float>(record.Yaw * TwoPi / 65536.0);
        const double logMin = std::log(static_cast<double>(species.ScaleMin));
        const double logMax = std::log(static_cast<double>(species.ScaleMax));
        value.Scale = static_cast<float>(std::exp(logMin + (logMax - logMin) * record.Scale / 255.0));
        value.Rank = record.Rank;
        double nx = record.NormalOct[0] * (2.0 / 255.0) - 1.0;
        double ny = record.NormalOct[1] * (2.0 / 255.0) - 1.0;
        const double nz = 1.0 - std::abs(nx) - std::abs(ny);
        if (nz < 0.0)
        {
            const double oldX = nx;
            nx = (1.0 - std::abs(ny)) * SignNotZero(oldX);
            ny = (1.0 - std::abs(oldX)) * SignNotZero(ny);
        }
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        value.Normal[0] = static_cast<float>(nx / length);
        value.Normal[1] = static_cast<float>(ny / length);
        value.Normal[2] = static_cast<float>(nz / length);
        for (size_t i = 0; i < 3; ++i)
        {
            value.Tint[i] = record.Tint[i] / 255.0f;
        }
        value.Dry = record.Dry / 255.0f;
        out = value;
        return true;
    }

    CookedScatterParseResult ParseCookedScatter(AssetBlob sourceBlob)
    {
        using Status = CookedScatterParseStatus;
        if (!sourceBlob.IsValid())
        {
            return Fail(Status::InvalidBlob);
        }
        const auto bytes = sourceBlob.GetSpan();
        if (bytes.size() < HeaderSize)
        {
            return Fail(Status::TruncatedHeader);
        }
        const uint8_t* data = bytes.data();
        if (std::memcmp(data, Magic, MagicSize) != 0)
        {
            return Fail(Status::BadMagic);
        }
        if (Read32(data, HeaderOffset::Version) != Version)
        {
            return Fail(Status::UnsupportedVersion);
        }
        if (Read32(data, HeaderOffset::EndianMarker) != EndianMarker)
        {
            return Fail(Status::BadEndian);
        }
        if (Read32(data, HeaderOffset::HeaderSize) != HeaderSize)
        {
            return Fail(Status::BadHeaderSize);
        }
        const uint64_t fileSize = Read64(data, HeaderOffset::FileSize);
        if (fileSize != bytes.size())
        {
            return Fail(Status::FileSizeMismatch);
        }
        if (Read64(data, HeaderOffset::PayloadHash) != ComputeCookedScatterFileHash(bytes))
        {
            return Fail(Status::HashMismatch);
        }
        if (Read32(data, HeaderOffset::Reserved0) != 0 || Read32(data, HeaderOffset::Reserved1) != 0 ||
            Read64(data, HeaderOffset::Reserved2) != 0 || Read64(data, HeaderOffset::Reserved3) != 0)
        {
            return Fail(Status::ReservedNonZero);
        }
        const float chunkSize = ReadFloat(data, HeaderOffset::ChunkSizeMeters);
        const double originX = std::bit_cast<double>(Read64(data, HeaderOffset::OriginX));
        const double originZ = std::bit_cast<double>(Read64(data, HeaderOffset::OriginZ));
        const uint32_t gridX = Read32(data, HeaderOffset::GridCountX);
        const uint32_t gridZ = Read32(data, HeaderOffset::GridCountZ);
        const uint32_t speciesCount = Read32(data, HeaderOffset::SpeciesCount);
        const uint32_t chunkCount = Read32(data, HeaderOffset::ChunkCount);
        if (!std::isfinite(chunkSize) || chunkSize <= 0.0f || !std::isfinite(originX) || !std::isfinite(originZ) ||
            gridX == 0 || gridZ == 0 || !std::isfinite(originX + static_cast<double>(gridX) * chunkSize) ||
            !std::isfinite(originZ + static_cast<double>(gridZ) * chunkSize))
        {
            return Fail(Status::InvalidDimensions);
        }
        if (speciesCount == 0 || speciesCount > MaxSpeciesCount)
        {
            return Fail(Status::SpeciesOutOfRange);
        }
        if (chunkCount > static_cast<uint64_t>(gridX) * gridZ)
        {
            return Fail(Status::ChunkOutOfRange);
        }

        const uint64_t speciesOffset = Read64(data, HeaderOffset::SpeciesTableOffset);
        const uint64_t speciesSize = Read64(data, HeaderOffset::SpeciesTableSize);
        const uint64_t chunkOffset = Read64(data, HeaderOffset::ChunkTableOffset);
        const uint64_t chunkTableSize = Read64(data, HeaderOffset::ChunkTableSize);
        const uint64_t rangeOffset = Read64(data, HeaderOffset::RangeTableOffset);
        const uint64_t rangeSize = Read64(data, HeaderOffset::RangeTableSize);
        const uint64_t stringOffset = Read64(data, HeaderOffset::StringTableOffset);
        const uint64_t stringSize = Read64(data, HeaderOffset::StringTableSize);
        const uint64_t instanceOffset = Read64(data, HeaderOffset::InstanceOffset);
        const uint64_t instanceSize = Read64(data, HeaderOffset::InstanceSize);
        const uint64_t rangeCount = static_cast<uint64_t>(chunkCount) * speciesCount;
        if (speciesOffset != HeaderSize || speciesSize != static_cast<uint64_t>(speciesCount) * SpeciesRecordSize ||
            !Within(speciesOffset, speciesSize, fileSize) || chunkOffset != speciesOffset + speciesSize ||
            chunkTableSize != static_cast<uint64_t>(chunkCount) * ChunkRecordSize || !Within(chunkOffset, chunkTableSize, fileSize) ||
            rangeOffset != chunkOffset + chunkTableSize || rangeSize != rangeCount * RangeRecordSize ||
            !Within(rangeOffset, rangeSize, fileSize) || stringOffset != rangeOffset + rangeSize ||
            !Within(stringOffset, stringSize, fileSize) || stringSize > UINT32_MAX ||
            stringOffset + stringSize > UINT64_MAX - (InstanceAlignment - 1) ||
            instanceOffset != ((stringOffset + stringSize + InstanceAlignment - 1) & ~uint64_t(InstanceAlignment - 1)) ||
            !Within(instanceOffset, instanceSize, fileSize) || instanceSize % InstanceRecordSize != 0 ||
            instanceOffset + instanceSize != fileSize)
        {
            return Fail(Status::TableOutOfRange);
        }
        for (uint64_t i = stringOffset + stringSize; i < instanceOffset; ++i)
        {
            if (data[static_cast<size_t>(i)] != 0)
            {
                return Fail(Status::ReservedNonZero);
            }
        }
        CookedScatterParseResult result;
        auto& scatter = result.Scatter;
        scatter.PayloadHash = Read64(data, HeaderOffset::PayloadHash);
        scatter.Seed = Read64(data, HeaderOffset::Seed);
        scatter.TerrainCookedHash = Read64(data, HeaderOffset::TerrainCookedHash);
        scatter.ChunkSizeMeters = chunkSize;
        scatter.OriginX = originX;
        scatter.OriginZ = originZ;
        scatter.GridCountX = gridX;
        scatter.GridCountZ = gridZ;
        scatter.Species.resize(speciesCount);
        for (uint32_t i = 0; i < speciesCount; ++i)
        {
            const size_t record = static_cast<size_t>(speciesOffset + static_cast<uint64_t>(i) * SpeciesRecordSize);
            auto& species = scatter.Species[i];
            const uint32_t path = Read32(data, record + SpeciesRecordOffset::RenderPath);
            species.LodCount = Read32(data, record + SpeciesRecordOffset::LodCount);
            if (path > 1 || species.LodCount == 0 || species.LodCount > MaxLodCount)
            {
                return Fail(Status::SpeciesOutOfRange);
            }
            species.RenderPath = static_cast<ScatterRenderPath>(path);
            if (!ReadString(data, record + SpeciesRecordOffset::Name, stringOffset, stringSize, species.Name))
            {
                return Fail(Status::StringOutOfRange);
            }
            for (uint32_t lod = 0; lod < MaxLodCount; ++lod)
            {
                const size_t reference = record + SpeciesRecordOffset::LodPaths + lod * 8;
                if (lod < species.LodCount)
                {
                    if (!ReadString(data, reference, stringOffset, stringSize, species.LodPaths[lod]))
                    {
                        return Fail(Status::StringOutOfRange);
                    }
                }
                else if (Read64(data, reference) != 0)
                {
                    return Fail(Status::ReservedNonZero);
                }
            }
            species.MeshRadiusMeters = ReadFloat(data, record + SpeciesRecordOffset::MeshRadiusMeters);
            species.MinSpacingMeters = ReadFloat(data, record + SpeciesRecordOffset::MinSpacingMeters);
            species.ScaleMin = ReadFloat(data, record + SpeciesRecordOffset::ScaleMin);
            species.ScaleMax = ReadFloat(data, record + SpeciesRecordOffset::ScaleMax);
            species.SlopeFollow = ReadFloat(data, record + SpeciesRecordOffset::SlopeFollow);
            species.BrightnessVariation = ReadFloat(data, record + SpeciesRecordOffset::BrightnessVariation);
            species.WindStiffness = ReadFloat(data, record + SpeciesRecordOffset::WindStiffness);
            species.WindPhaseSeed = Read32(data, record + SpeciesRecordOffset::WindPhaseSeed);
            species.Flags = Read32(data, record + SpeciesRecordOffset::Flags);
            if ((species.Flags & ~1u) != 0 || Read32(data, record + SpeciesRecordOffset::Reserved0) != 0 ||
                Read64(data, record + SpeciesRecordOffset::Reserved1) != 0) return Fail(Status::ReservedNonZero);
            if (!std::isfinite(species.MeshRadiusMeters) || species.MeshRadiusMeters < 0.0f ||
                !std::isfinite(species.MinSpacingMeters) || species.MinSpacingMeters <= 0.0f ||
                !std::isfinite(species.ScaleMin) || !std::isfinite(species.ScaleMax) || species.ScaleMin <= 0.0f ||
                species.ScaleMin > species.ScaleMax || !std::isfinite(species.SlopeFollow) ||
                species.SlopeFollow < 0.0f || species.SlopeFollow > 1.0f ||
                !std::isfinite(species.BrightnessVariation) || species.BrightnessVariation < 0.0f ||
                !std::isfinite(species.WindStiffness) || species.WindStiffness < 0.0f)
            {
                return Fail(Status::SpeciesOutOfRange);
            }
        }
        scatter.Chunks.resize(chunkCount);
        Container::VariableArray<uint64_t> keys;
        keys.reserve(chunkCount);
        for (uint32_t i = 0; i < chunkCount; ++i)
        {
            const size_t record = static_cast<size_t>(chunkOffset + static_cast<uint64_t>(i) * ChunkRecordSize);
            auto& chunk = scatter.Chunks[i];
            chunk.ChunkX = Read32(data, record + ChunkRecordOffset::ChunkX);
            chunk.ChunkZ = Read32(data, record + ChunkRecordOffset::ChunkZ);
            if (chunk.ChunkX >= gridX || chunk.ChunkZ >= gridZ)
            {
                return Fail(Status::ChunkOutOfRange);
            }
            keys.push_back((static_cast<uint64_t>(chunk.ChunkZ) << 32) | chunk.ChunkX);
            if (Read64(data, record + ChunkRecordOffset::Reserved) != 0)
            {
                return Fail(Status::ReservedNonZero);
            }
            chunk.MinY = ReadFloat(data, record + ChunkRecordOffset::MinY);
            chunk.MaxY = ReadFloat(data, record + ChunkRecordOffset::MaxY);
            if (!std::isfinite(chunk.MinY) || !std::isfinite(chunk.MaxY) || chunk.MinY > chunk.MaxY)
            {
                return Fail(Status::InvalidChunkBounds);
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                chunk.AabbMin[axis] = ReadFloat(data, record + ChunkRecordOffset::AabbMin + axis * 4);
                chunk.AabbMax[axis] = ReadFloat(data, record + ChunkRecordOffset::AabbMax + axis * 4);
                if (!std::isfinite(chunk.AabbMin[axis]) || !std::isfinite(chunk.AabbMax[axis]) ||
                    chunk.AabbMin[axis] > chunk.AabbMax[axis]) return Fail(Status::InvalidChunkBounds);
            }
        }
        if (keys.size() > 1)
        {
            std::sort(keys.begin(), keys.end());
            for (size_t i = 1; i < keys.size(); ++i)
            {
                if (keys[i - 1] == keys[i])
                {
                    return Fail(Status::ChunkOutOfRange);
                }
            }
        }
        scatter.SpeciesRanges.resize(static_cast<size_t>(rangeCount));
        for (size_t i = 0; i < scatter.SpeciesRanges.size(); ++i)
        {
            const size_t record = static_cast<size_t>(rangeOffset + i * RangeRecordSize);
            auto& range = scatter.SpeciesRanges[i];
            range.Offset = Read64(data, record + RangeRecordOffset::Offset);
            range.Count = Read64(data, record + RangeRecordOffset::Count);
            if (!Within(range.Offset, range.Count, instanceSize / InstanceRecordSize))
            {
                return Fail(Status::InstanceRangeOutOfRange);
            }
        }
        scatter.InstanceOffset = instanceOffset;
        scatter.InstanceSize = instanceSize;
        scatter.Source = std::move(sourceBlob);
        result.Status = Status::Success;
        return result;
    }

    const char* GetCookedScatterParseStatusName(CookedScatterParseStatus status) noexcept
    {
        switch (status)
        {
        case CookedScatterParseStatus::Success: return "Success";
        case CookedScatterParseStatus::InvalidBlob: return "InvalidBlob";
        case CookedScatterParseStatus::TruncatedHeader: return "TruncatedHeader";
        case CookedScatterParseStatus::BadMagic: return "BadMagic";
        case CookedScatterParseStatus::UnsupportedVersion: return "UnsupportedVersion";
        case CookedScatterParseStatus::BadEndian: return "BadEndian";
        case CookedScatterParseStatus::BadHeaderSize: return "BadHeaderSize";
        case CookedScatterParseStatus::FileSizeMismatch: return "FileSizeMismatch";
        case CookedScatterParseStatus::HashMismatch: return "HashMismatch";
        case CookedScatterParseStatus::ReservedNonZero: return "ReservedNonZero";
        case CookedScatterParseStatus::InvalidDimensions: return "InvalidDimensions";
        case CookedScatterParseStatus::TableOutOfRange: return "TableOutOfRange";
        case CookedScatterParseStatus::StringOutOfRange: return "StringOutOfRange";
        case CookedScatterParseStatus::SpeciesOutOfRange: return "SpeciesOutOfRange";
        case CookedScatterParseStatus::ChunkOutOfRange: return "ChunkOutOfRange";
        case CookedScatterParseStatus::InvalidChunkBounds: return "InvalidChunkBounds";
        case CookedScatterParseStatus::InstanceRangeOutOfRange: return "InstanceRangeOutOfRange";
        }
        return "Unknown";
    }
} // namespace NorvesLib::Core::Asset
