#include "Vegetation/GrassField.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Vegetation
{
    namespace
    {
        float Saturate(float value)
        {
            return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        }

        float Lerp(float a, float b, float fraction)
        {
            return a + (b - a) * fraction;
        }

        int32_t ClampPixel(int32_t value, uint32_t resolution)
        {
            if (value < 0)
            {
                return 0;
            }
            const int32_t last = static_cast<int32_t>(resolution - 1);
            return value > last ? last : value;
        }

        bool IsValidResolution(uint32_t resolution)
        {
            // 描画データのバイト位置で表せる大きさに制限し、積のオーバーフローも避ける。
            return resolution > 0 && static_cast<uint64_t>(resolution) * resolution <= UINT32_MAX / 4;
        }

        void CopyColor(const Math::Vector3& color, float (&outColor)[4])
        {
            outColor[0] = color.x;
            outColor[1] = color.y;
            outColor[2] = color.z;
            outColor[3] = 0.0f;
        }
    } // namespace

    float ComputeEffectiveWetness(float globalWetness, float moistureBias)
    {
        return Saturate(globalWetness * (0.6f + 0.8f * moistureBias));
    }

    GrassField::GrassField(const GrassFieldDesc& desc)
        : m_Desc(desc)
    {
    }

    const GrassFieldDesc& GrassField::GetDesc() const
    {
        return m_Desc;
    }

    uint64_t GrassField::MakeChunkKey(GrassChunkCoord coord)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(coord.X)) << 32) |
               static_cast<uint32_t>(coord.Z);
    }

    bool GrassField::SetChunk(GrassChunkCoord coord, Container::Span<const uint8_t> layerA,
                              Container::Span<const uint8_t> layerB)
    {
        if (!std::isfinite(m_Desc.ChunkSizeMeters) || m_Desc.ChunkSizeMeters <= 0.0f ||
            !IsValidResolution(m_Desc.LayerAResolution) || !IsValidResolution(m_Desc.LayerBResolution))
        {
            return false;
        }
        if (m_Desc.ChunkSizeMeters / static_cast<float>(m_Desc.LayerAResolution) <= 0.0f ||
            m_Desc.ChunkSizeMeters / static_cast<float>(m_Desc.LayerBResolution) <= 0.0f)
        {
            return false;
        }
        const uint64_t layerABytes = static_cast<uint64_t>(m_Desc.LayerAResolution) * m_Desc.LayerAResolution * 4;
        const uint64_t layerBBytes = static_cast<uint64_t>(m_Desc.LayerBResolution) * m_Desc.LayerBResolution * 4;
        if (layerA.size() != layerABytes || layerB.size() != layerBBytes ||
            layerA.data() == nullptr || layerB.data() == nullptr)
        {
            return false;
        }

        bool bHasGrass = false;
        for (uint64_t offset = 0; offset < layerABytes; offset += 4)
        {
            if (layerA[offset + 3] > 63)
            {
                return false;
            }
            bHasGrass = bHasGrass || layerA[offset] != 0;
        }
        if (!bHasGrass)
        {
            m_Chunks.erase(MakeChunkKey(coord));
        }
        else
        {
            // 連結後の画素配列全体も、描画データの 32bit バイト位置に収める。
            const uint64_t chunkCount = static_cast<uint64_t>(m_Chunks.size()) + (HasChunk(coord) ? 0 : 1);
            if (chunkCount > UINT32_MAX / layerABytes || chunkCount > UINT32_MAX / layerBBytes)
            {
                return false;
            }
            Chunk chunk;
            chunk.Coord = coord;
            chunk.LayerA.assign(layerA.begin(), layerA.end());
            chunk.LayerB.assign(layerB.begin(), layerB.end());
            for (uint64_t offset = 3; offset < layerBBytes; offset += 4)
            {
                chunk.LayerB[offset] = 0;
            }
            m_Chunks.insert_or_assign(MakeChunkKey(coord), std::move(chunk));
        }
        ++m_Revision;
        return true;
    }

    bool GrassField::RemoveChunk(GrassChunkCoord coord)
    {
        const bool bRemoved = m_Chunks.erase(MakeChunkKey(coord)) != 0;
        ++m_Revision;
        return bRemoved;
    }

    bool GrassField::HasChunk(GrassChunkCoord coord) const
    {
        return FindChunk(coord) != nullptr;
    }

    uint32_t GrassField::GetChunkCount() const
    {
        return static_cast<uint32_t>(m_Chunks.size());
    }

    void GrassField::SetSpeciesTable(const GrassSpeciesTable& table)
    {
        m_SpeciesTable = table;
        ++m_Revision;
    }

    const GrassSpeciesTable& GrassField::GetSpeciesTable() const
    {
        return m_SpeciesTable;
    }

    void GrassField::SetGroundHeightSource(const IGroundHeightSource* pSource)
    {
        m_pGroundHeightSource = pSource;
    }

    uint64_t GrassField::GetRevision() const
    {
        return m_Revision;
    }

    const GrassField::Chunk* GrassField::FindChunk(GrassChunkCoord coord) const
    {
        const auto it = m_Chunks.find(MakeChunkKey(coord));
        return it == m_Chunks.end() ? nullptr : &it->second;
    }

    bool GrassField::TryGetChunkCoord(float x, float z, GrassChunkCoord& outCoord) const
    {
        if (!std::isfinite(x) || !std::isfinite(z) ||
            !std::isfinite(m_Desc.ChunkSizeMeters) || m_Desc.ChunkSizeMeters <= 0.0f)
        {
            return false;
        }
        const double chunkX = std::floor(static_cast<double>(x) / m_Desc.ChunkSizeMeters);
        const double chunkZ = std::floor(static_cast<double>(z) / m_Desc.ChunkSizeMeters);
        if (chunkX < INT32_MIN || chunkX > INT32_MAX || chunkZ < INT32_MIN || chunkZ > INT32_MAX)
        {
            return false;
        }
        outCoord = {static_cast<int32_t>(chunkX), static_cast<int32_t>(chunkZ)};
        return true;
    }

    // 世界の画素を読み、欠損時は −X、+X、−Z、+Z、−X−Z、+X−Z、−X+Z、+X+Z の順に探す。
    // 代用を世界座標だけで決めて凹んだ角でも一致させ、片側だけ欠けた縁では縁の画素へ寄せる。
    const uint8_t* GrassField::ReadPixel(const Chunk& chunk, int32_t pixelX, int32_t pixelZ,
                                        bool bLayerA) const
    {
        const uint32_t resolution = bLayerA ? m_Desc.LayerAResolution : m_Desc.LayerBResolution;
        const int64_t size = static_cast<int64_t>(resolution);
        const int64_t gx = static_cast<int64_t>(chunk.Coord.X) * size + pixelX;
        const int64_t gz = static_cast<int64_t>(chunk.Coord.Z) * size + pixelZ;
        const auto readWorldPixel = [this, size, resolution, bLayerA](int64_t x, int64_t z) -> const uint8_t*
        {
            // 負の余りがある場合は商を下げ、負座標でもチャンク内の画素を非負にする。
            const int64_t chunkX = x / size - (x % size < 0 ? 1 : 0);
            const int64_t chunkZ = z / size - (z % size < 0 ? 1 : 0);
            if (chunkX < INT32_MIN || chunkX > INT32_MAX || chunkZ < INT32_MIN || chunkZ > INT32_MAX)
            {
                return nullptr;
            }
            const Chunk* pSource = FindChunk({static_cast<int32_t>(chunkX), static_cast<int32_t>(chunkZ)});
            if (pSource == nullptr)
            {
                return nullptr;
            }
            const uint64_t sourceX = static_cast<uint64_t>(x - chunkX * size);
            const uint64_t sourceZ = static_cast<uint64_t>(z - chunkZ * size);
            const auto& pixels = bLayerA ? pSource->LayerA : pSource->LayerB;
            return pixels.data() + (sourceZ * resolution + sourceX) * 4;
        };
        if (const uint8_t* pPixel = readWorldPixel(gx, gz))
        {
            return pPixel;
        }
        const int32_t neighbors[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1},
                                         {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        for (const auto& neighbor : neighbors)
        {
            if (const uint8_t* pPixel = readWorldPixel(gx + neighbor[0], gz + neighbor[1]))
            {
                return pPixel;
            }
        }

        // 双線形補間の読取りでは元のチャンクが近傍にあるが、範囲外の呼出しにも安全に備える。
        const auto& pixels = bLayerA ? chunk.LayerA : chunk.LayerB;
        const uint64_t offset = (static_cast<uint64_t>(ClampPixel(pixelZ, resolution)) * resolution +
                                 ClampPixel(pixelX, resolution)) * 4;
        return pixels.data() + offset;
    }

    Math::Vector3 GrassField::SampleLayer(const Chunk& chunk, float localX, float localZ, bool bLayerA) const
    {
        const uint32_t resolution = bLayerA ? m_Desc.LayerAResolution : m_Desc.LayerBResolution;
        const float pixelWidth = m_Desc.ChunkSizeMeters / static_cast<float>(resolution);
        const float u = localX / pixelWidth - 0.5f;
        const float v = localZ / pixelWidth - 0.5f;
        const int32_t i = static_cast<int32_t>(std::floor(u));
        const int32_t j = static_cast<int32_t>(std::floor(v));
        const float fractionX = u - static_cast<float>(i);
        const float fractionZ = v - static_cast<float>(j);
        const uint8_t* p00 = ReadPixel(chunk, i, j, bLayerA);
        const uint8_t* p10 = ReadPixel(chunk, i + 1, j, bLayerA);
        const uint8_t* p01 = ReadPixel(chunk, i, j + 1, bLayerA);
        const uint8_t* p11 = ReadPixel(chunk, i + 1, j + 1, bLayerA);

        // 隣接の有無によらず、各成分を X、Z の順で同じ演算列により補間する。
        float channels[3];
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            const float row0 = Lerp(static_cast<float>(p00[channel]), static_cast<float>(p10[channel]), fractionX);
            const float row1 = Lerp(static_cast<float>(p01[channel]), static_cast<float>(p11[channel]), fractionX);
            channels[channel] = Lerp(row0, row1, fractionZ);
        }
        return Math::Vector3(channels[0], channels[1], channels[2]);
    }

    GrassSample GrassField::Sample(float x, float z) const
    {
        GrassSample sample;
        GrassChunkCoord coord;
        if (!TryGetChunkCoord(x, z, coord))
        {
            return sample;
        }
        const Chunk* pChunk = FindChunk(coord);
        if (pChunk == nullptr)
        {
            return sample;
        }

        // 大きな座標でも原点の丸めで画素範囲を逸脱しないよう、差を取ってから float に戻す。
        const float localX = static_cast<float>(static_cast<double>(x) -
                                                static_cast<double>(coord.X) * m_Desc.ChunkSizeMeters);
        const float localZ = static_cast<float>(static_cast<double>(z) -
                                                static_cast<double>(coord.Z) * m_Desc.ChunkSizeMeters);
        const Math::Vector3 layerA = SampleLayer(*pChunk, localX, localZ, true);
        const float pixelWidth = m_Desc.ChunkSizeMeters / static_cast<float>(m_Desc.LayerAResolution);
        const int32_t pixelX = ClampPixel(static_cast<int32_t>(std::floor(localX / pixelWidth)), m_Desc.LayerAResolution);
        const int32_t pixelZ = ClampPixel(static_cast<int32_t>(std::floor(localZ / pixelWidth)), m_Desc.LayerAResolution);
        const uint64_t offset = (static_cast<uint64_t>(pixelZ) * m_Desc.LayerAResolution + pixelX) * 4;
        sample.bValid = true;
        sample.Density = layerA.x / 255.0f;
        sample.Moisture = layerA.z / 255.0f;
        sample.SpeciesId = pChunk->LayerA[offset + 3];
        const GrassSpecies* pSpecies = m_SpeciesTable.Find(sample.SpeciesId);
        if (sample.Density > 0.0f && pSpecies != nullptr)
        {
            sample.HeightMeters = Lerp(pSpecies->HeightMinMeters, pSpecies->HeightMaxMeters, layerA.y / 255.0f);
        }
        const Math::Vector3 layerB = SampleLayer(*pChunk, localX, localZ, false);
        sample.Tint = Math::Vector3(layerB.x / 128.0f, layerB.y / 128.0f, layerB.z / 128.0f);
        return sample;
    }

    float GrassField::GetGroundHeight(float x, float z) const
    {
        const IGroundHeightSource* pSource = m_pGroundHeightSource != nullptr ? m_pGroundHeightSource : &m_DefaultGround;
        return pSource->GetGroundHeight(x, z);
    }

    float GrassField::QueryTopHeight(float x, float z) const
    {
        return GetGroundHeight(x, z) + Sample(x, z).HeightMeters;
    }

    float GrassField::QueryOcclusion(const Math::Vector3& a, const Math::Vector3& b) const
    {
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) ||
            !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z))
        {
            return 1.0f;
        }
        const Math::Vector3 delta = b - a;
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (!std::isfinite(length) || length == 0.0f)
        {
            return 1.0f;
        }
        // 約 524 km を超える区間は刻みを伸ばし、積分回数と整数変換の範囲を制限する。
        constexpr uint64_t MaxOcclusionSteps = 1u << 20;
        const double stepCount = std::ceil(static_cast<double>(length) / 0.5);
        const uint64_t limitedCount = stepCount >= static_cast<double>(MaxOcclusionSteps) ?
            MaxOcclusionSteps : static_cast<uint64_t>(stepCount);
        const uint64_t count = limitedCount > 0 ? limitedCount : 1;
        const float stepLength = length / static_cast<float>(count);
        float tau = 0.0f;
        for (uint64_t index = 0; index < count; ++index)
        {
            const float fraction = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
            const Math::Vector3 point = a + delta * fraction;
            const GrassSample sample = Sample(point.x, point.z);
            const GrassSpecies* pSpecies = m_SpeciesTable.Find(sample.SpeciesId);
            const float height = sample.HeightMeters;
            const float above = point.y - GetGroundHeight(point.x, point.z);
            if (height > 0.0f && pSpecies != nullptr)
            {
                const float sigma = sample.Density * pSpecies->Opacity * Saturate((height - above) / height);
                tau += sigma * stepLength;
            }
        }
        return std::exp(-tau);
    }

    GrassSurface GrassField::QuerySurface(float x, float z) const
    {
        const GrassSample sample = Sample(x, z);
        return {sample.bValid, sample.SpeciesId, sample.Moisture};
    }

    float GrassField::GetCameraClearance(const Math::Vector3& position) const
    {
        return Sample(position.x, position.z).HeightMeters;
    }

    Container::TSharedPtr<const Rendering::GrassFieldRenderData> GrassField::BuildRenderData() const
    {
        auto data = Container::MakeShared<Rendering::GrassFieldRenderData>();
        data->ChunkSizeMeters = m_Desc.ChunkSizeMeters;
        data->LayerAResolution = m_Desc.LayerAResolution;
        data->LayerBResolution = m_Desc.LayerBResolution;
        data->ContentRevision = m_Revision;

        Container::VariableArray<const Chunk*> chunks;
        chunks.reserve(m_Chunks.size());
        for (const auto& entry : m_Chunks)
        {
            chunks.push_back(&entry.second);
        }
        std::sort(chunks.begin(), chunks.end(), [](const Chunk* pLeft, const Chunk* pRight)
        {
            return pLeft->Coord.Z < pRight->Coord.Z ||
                   (pLeft->Coord.Z == pRight->Coord.Z && pLeft->Coord.X < pRight->Coord.X);
        });
        data->Chunks.reserve(chunks.size());
        for (const Chunk* pChunk : chunks)
        {
            Rendering::GrassFieldChunkRenderData chunkData;
            chunkData.ChunkX = pChunk->Coord.X;
            chunkData.ChunkZ = pChunk->Coord.Z;
            chunkData.LayerAByteOffset = static_cast<uint32_t>(data->LayerAPixels.size());
            chunkData.LayerBByteOffset = static_cast<uint32_t>(data->LayerBPixels.size());
            data->Chunks.push_back(chunkData);
            data->LayerAPixels.insert(data->LayerAPixels.end(), pChunk->LayerA.begin(), pChunk->LayerA.end());
            data->LayerBPixels.insert(data->LayerBPixels.end(), pChunk->LayerB.begin(), pChunk->LayerB.end());
        }

        data->Species.reserve(m_SpeciesTable.GetCount());
        for (const GrassSpecies& species : m_SpeciesTable.GetAll())
        {
            Rendering::GrassSpeciesGpuData gpu{};
            CopyColor(species.RootColor, gpu.RootColor);
            CopyColor(species.TipColor, gpu.TipColor);
            CopyColor(species.WetColor, gpu.WetColor);
            gpu.HeightMinMeters = species.HeightMinMeters;
            gpu.HeightMaxMeters = species.HeightMaxMeters;
            gpu.BladeWidthMeters = species.BladeWidthMeters;
            gpu.Bend = species.Bend;
            gpu.Stiffness = species.Stiffness;
            gpu.Roughness = species.Roughness;
            gpu.WetRoughness = species.WetRoughness;
            gpu.WetDroop = species.WetDroop;
            gpu.WindResponse = species.WindResponse;
            gpu.TrampleResponse = species.TrampleResponse;
            gpu.Opacity = species.Opacity;
            gpu.BladesPerSquareMeter = species.BladesPerSquareMeter;
            for (uint32_t index = 0; index < GrassLodPointCount; ++index)
            {
                gpu.LodDistanceMeters[index] = species.Lod[index].DistanceMeters;
                gpu.LodKeepFraction[index] = species.Lod[index].KeepFraction;
            }
            data->Species.push_back(gpu);
        }
        return data;
    }
} // namespace NorvesLib::Core::Vegetation
