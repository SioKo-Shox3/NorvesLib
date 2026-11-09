#include "Asset/CookedGrassFieldFormat.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        using namespace CookedGrassFieldFormatV0;

        void WriteLe(uint8_t* data, size_t offset, uint64_t value, size_t width) noexcept
        {
            for (size_t i = 0; i < width; ++i)
            {
                data[offset + i] = static_cast<uint8_t>(value >> (i * 8));
            }
        }

        void WriteLe32(uint8_t* data, size_t offset, uint32_t value) noexcept
        {
            WriteLe(data, offset, value, 4);
        }

        void WriteLe64(uint8_t* data, size_t offset, uint64_t value) noexcept
        {
            WriteLe(data, offset, value, 8);
        }

        struct ScannedChunk
        {
            const CookedGrassFieldWriteChunk* Chunk = nullptr;
            uint8_t MaxDensity = 0;
            uint8_t MaxHeightFactor = 0;
        };
    } // namespace

    CookedGrassFieldWriteStatus WriteCookedGrassField(const CookedGrassFieldWriteDesc& desc,
                                                      Container::VariableArray<uint8_t>& outBytes)
    {
        using namespace CookedGrassFieldFormatV0;
        using Status = CookedGrassFieldWriteStatus;
        const auto fail = [&outBytes](Status status)
        {
            outBytes.clear();
            return status;
        };

        if (!std::isfinite(desc.ChunkSizeMeters) || desc.ChunkSizeMeters <= 0.0f ||
            desc.LayerAResolution == 0 || desc.LayerAResolution > MaxLayerResolution ||
            desc.LayerBResolution == 0 || desc.LayerBResolution > MaxLayerResolution ||
            desc.SpeciesNames.empty() || desc.SpeciesNames.size() > MaxSpeciesCount)
        {
            return fail(Status::InvalidFieldDesc);
        }
        if (desc.Chunks.size() > (std::numeric_limits<uint32_t>::max)())
        {
            return fail(Status::TooLarge);
        }
        for (const auto& name : desc.SpeciesNames)
        {
            if (name.empty() || name.size() > MaxSpeciesNameLength)
            {
                return fail(Status::InvalidSpeciesName);
            }
        }
        for (size_t i = 0; i < desc.SpeciesNames.size(); ++i)
        {
            const auto& name = desc.SpeciesNames[i];
            for (size_t previous = 0; previous < i; ++previous)
            {
                const auto& other = desc.SpeciesNames[previous];
                if (name.size() == other.size() && std::memcmp(name.data(), other.data(), name.size()) == 0)
                {
                    return fail(Status::DuplicateSpeciesName);
                }
            }
        }

        const uint64_t layerASize = static_cast<uint64_t>(desc.LayerAResolution) * desc.LayerAResolution * 4;
        const uint64_t layerBSize = static_cast<uint64_t>(desc.LayerBResolution) * desc.LayerBResolution * 4;
        const uint64_t chunkCount = desc.Chunks.size();
        const uint64_t speciesCount = desc.SpeciesNames.size();
        // 件数と解像度は制限済みであり、積・和と 16 バイトへの切り上げは uint64 に収まる。
        const uint64_t chunkTableSize = chunkCount * ChunkRecordSize;
        const uint64_t speciesTableOffset = HeaderSize + chunkTableSize;
        const uint64_t speciesTableSize = speciesCount * SpeciesNameRecordSize;
        const uint64_t speciesEnd = speciesTableOffset + speciesTableSize;
        const uint64_t payloadOffset = (speciesEnd + PayloadAlignment - 1) & ~uint64_t(PayloadAlignment - 1);
        const uint64_t payloadSize = chunkCount * (layerASize + layerBSize);
        const uint64_t fileSize = payloadOffset + payloadSize;

        Container::VariableArray<ScannedChunk> ordered;
        Container::VariableArray<uint8_t> bytes;
        if (chunkCount > ordered.max_size() || fileSize > bytes.max_size())
        {
            return fail(Status::TooLarge);
        }
        ordered.reserve(static_cast<size_t>(chunkCount));
        for (const auto& chunk : desc.Chunks)
        {
            if (chunk.LayerA.size() != layerASize || chunk.LayerB.size() != layerBSize ||
                chunk.LayerA.data() == nullptr || chunk.LayerB.data() == nullptr)
            {
                return fail(Status::InvalidChunk);
            }
            ScannedChunk scanned;
            scanned.Chunk = &chunk;
            bool bInvalidSpeciesId = false;
            for (size_t offset = 0; offset < chunk.LayerA.size(); offset += 4)
            {
                scanned.MaxDensity = (std::max)(scanned.MaxDensity, chunk.LayerA[offset]);
                scanned.MaxHeightFactor = (std::max)(scanned.MaxHeightFactor, chunk.LayerA[offset + 1]);
                bInvalidSpeciesId = bInvalidSpeciesId || chunk.LayerA[offset + 3] >= speciesCount;
            }
            if (scanned.MaxDensity == 0)
            {
                return fail(Status::InvalidChunk);
            }
            if (bInvalidSpeciesId)
            {
                return fail(Status::InvalidSpeciesId);
            }
            ordered.push_back(scanned);
        }
        std::sort(ordered.begin(), ordered.end(), [](const ScannedChunk& left, const ScannedChunk& right)
        {
            return left.Chunk->Z != right.Chunk->Z ? left.Chunk->Z < right.Chunk->Z : left.Chunk->X < right.Chunk->X;
        });
        for (size_t i = 1; i < ordered.size(); ++i)
        {
            if (ordered[i - 1].Chunk->X == ordered[i].Chunk->X && ordered[i - 1].Chunk->Z == ordered[i].Chunk->Z)
            {
                return fail(Status::DuplicateChunk);
            }
        }

        // 別配列へ書くため、入力のビューが outBytes を指していても読み終わるまで有効。
        // 予約欄と表の後の隙間は 0 のままにする。
        bytes.resize(static_cast<size_t>(fileSize), 0);
        uint8_t* data = bytes.data();
        std::memcpy(data, Magic, MagicSize);
        WriteLe32(data, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe32(data, HeaderOffset::Version, Version);
        WriteLe32(data, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe64(data, HeaderOffset::FileSize, fileSize);
        WriteLe32(data, HeaderOffset::ChunkSizeMeters, std::bit_cast<uint32_t>(desc.ChunkSizeMeters));
        WriteLe32(data, HeaderOffset::LayerAResolution, desc.LayerAResolution);
        WriteLe32(data, HeaderOffset::LayerBResolution, desc.LayerBResolution);
        WriteLe32(data, HeaderOffset::ChunkCount, static_cast<uint32_t>(chunkCount));
        WriteLe64(data, HeaderOffset::ChunkTableOffset, HeaderSize);
        WriteLe64(data, HeaderOffset::ChunkTableSize, chunkTableSize);
        WriteLe32(data, HeaderOffset::SpeciesCount, static_cast<uint32_t>(speciesCount));
        WriteLe64(data, HeaderOffset::SpeciesTableOffset, speciesTableOffset);
        WriteLe64(data, HeaderOffset::SpeciesTableSize, speciesTableSize);
        WriteLe64(data, HeaderOffset::PayloadOffset, payloadOffset);
        WriteLe64(data, HeaderOffset::PayloadSize, payloadSize);

        for (size_t i = 0; i < desc.SpeciesNames.size(); ++i)
        {
            const auto& name = desc.SpeciesNames[i];
            const size_t record = static_cast<size_t>(speciesTableOffset + i * SpeciesNameRecordSize);
            data[record] = static_cast<uint8_t>(name.size());
            std::memcpy(data + record + 1, name.data(), name.size());
        }
        uint64_t layerOffset = 0;
        for (size_t i = 0; i < ordered.size(); ++i)
        {
            const auto& scanned = ordered[i];
            const auto& chunk = *scanned.Chunk;
            const size_t record = HeaderSize + i * ChunkRecordSize;
            WriteLe32(data, record + ChunkRecordOffset::ChunkX, std::bit_cast<uint32_t>(chunk.X));
            WriteLe32(data, record + ChunkRecordOffset::ChunkZ, std::bit_cast<uint32_t>(chunk.Z));
            data[record + ChunkRecordOffset::MaxDensity] = scanned.MaxDensity;
            data[record + ChunkRecordOffset::MaxHeightFactor] = scanned.MaxHeightFactor;
            WriteLe64(data, record + ChunkRecordOffset::LayerAOffset, layerOffset);
            WriteLe64(data, record + ChunkRecordOffset::LayerASize, layerASize);
            std::memcpy(data + static_cast<size_t>(payloadOffset + layerOffset), chunk.LayerA.data(), chunk.LayerA.size());
            layerOffset += layerASize;
            WriteLe64(data, record + ChunkRecordOffset::LayerBOffset, layerOffset);
            WriteLe64(data, record + ChunkRecordOffset::LayerBSize, layerBSize);
            uint8_t* layerB = data + static_cast<size_t>(payloadOffset + layerOffset);
            std::memcpy(layerB, chunk.LayerB.data(), chunk.LayerB.size());
            for (size_t offset = 3; offset < chunk.LayerB.size(); offset += 4)
            {
                layerB[offset] = 0;
            }
            layerOffset += layerBSize;
        }
        WriteLe64(data, HeaderOffset::PayloadHash,
                  ComputeAssetPackagePayloadHash(data + static_cast<size_t>(payloadOffset), static_cast<size_t>(payloadSize)));
        outBytes.swap(bytes);
        return Status::Success;
    }
} // namespace NorvesLib::Core::Asset
