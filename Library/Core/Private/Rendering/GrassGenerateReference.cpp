#include "Rendering/GrassGenerateReference.h"

#include "Rendering/MegaGeometryPass.h"
#include "Math/MatrixUtils.h"
#include <algorithm>
#include <cassert>
#include <cmath>

// 判定の手前で積と和を融合させない。シェーダー側も対応する式へ precise を付ける。
#if defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#endif

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float Lerp(float a, float b, float fraction)
        {
            return a + (b - a) * fraction;
        }

        bool ViewClearanceIsActive(const ViewClearanceGpuData& c)
        {
            return c.Control[1] != 0.0f && c.Control[0] > 0.0f;
        }

        float ViewClearanceSmoothStep(float start, float end, float value)
        {
            if (end <= start)
            {
                return value < start ? 0.0f : 1.0f;
            }
            const float x = GrassSaturate((value - start) / (end - start));
            return (x * x) * (3.0f - 2.0f * x);
        }

        float ViewClearanceVisibility(const ViewClearanceGpuData& c, const Math::Vector3& p)
        {
            if (!ViewClearanceIsActive(c))
            {
                return 1.0f;
            }

            const float dx = p.x - c.CameraRadius[0];
            const float dy = p.y - c.CameraRadius[1];
            const float dz = p.z - c.CameraRadius[2];
            const float ax = c.TargetRadius[0] - c.CameraRadius[0];
            const float ay = c.TargetRadius[1] - c.CameraRadius[1];
            const float az = c.TargetRadius[2] - c.CameraRadius[2];
            const float lengthSquared = (ax * ax + ay * ay) + az * az;
            float clearance = 1.0f;
            if (lengthSquared > 1.0e-12f)
            {
                const float t = ((dx * ax + dy * ay) + dz * az) / lengthSquared;
                if (t >= 0.0f && t <= 1.0f)
                {
                    const float radius = c.CameraRadius[3] + (c.TargetRadius[3] - c.CameraRadius[3]) * t;
                    if (radius > 0.0f)
                    {
                        const float offsetX = dx - t * ax;
                        const float offsetY = dy - t * ay;
                        const float offsetZ = dz - t * az;
                        const float distance = std::sqrt((offsetX * offsetX + offsetY * offsetY) + offsetZ * offsetZ);
                        const float featherEnd = radius * (1.0f + c.Shape[0]);
                        clearance = ViewClearanceSmoothStep(radius, featherEnd, distance);
                    }
                }
            }
            const float cameraDistance = std::sqrt((dx * dx + dy * dy) + dz * dz);
            const float nearFade = ViewClearanceSmoothStep(c.Shape[1], c.Shape[2], cameraDistance);
            const float visibility = std::min(clearance, nearFade);
            return 1.0f - c.Control[0] * (1.0f - visibility);
        }

        // シェーダーと同じ中央値を使い、除算と平方根を使わず境界を判定する。
        bool ViewClearanceKeep(const ViewClearanceGpuData& c, const Math::Vector3& p, float u, float g)
        {
            if (!ViewClearanceIsActive(c))
            {
                return true;
            }
            const float dx = p.x - c.CameraRadius[0];
            const float dy = p.y - c.CameraRadius[1];
            const float dz = p.z - c.CameraRadius[2];
            const float dist2 = (dx * dx + dy * dy) + dz * dz;
            const float nearT = c.Shape[1] + (c.Shape[2] - c.Shape[1]) * u;
            const float nearT2 = nearT * nearT;
            const bool bNearPass = dist2 >= nearT2;
            const float ax = c.TargetRadius[0] - c.CameraRadius[0];
            const float ay = c.TargetRadius[1] - c.CameraRadius[1];
            const float az = c.TargetRadius[2] - c.CameraRadius[2];
            const float L2 = (ax * ax + ay * ay) + az * az;
            const float D = (dx * ax + dy * ay) + dz * az;
            bool bClearPass = true;
            if (L2 > 1.0e-12f && D >= 0.0f && D <= L2)
            {
                const float rL = c.CameraRadius[3] * L2 + (c.TargetRadius[3] - c.CameraRadius[3]) * D;
                if (rL > 0.0f)
                {
                    const float perp = dist2 * L2 - D * D;
                    const float lhs = perp * L2;
                    const float k = rL * (1.0f + c.Shape[0] * u);
                    const float k2 = k * k;
                    bClearPass = lhs >= k2;
                }
            }
            if (bNearPass && bClearPass)
            {
                return true;
            }
            return !(g < c.Control[0]);
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

        const GrassFieldChunkRenderData* FindChunk(const GrassFieldRenderData& field, int32_t cx, int32_t cz)
        {
            // 描画データは ChunkZ、ChunkX の昇順。
            size_t first = 0;
            size_t last = field.Chunks.size();
            while (first < last)
            {
                const size_t middle = first + ((last - first) >> 1);
                const auto& chunk = field.Chunks[middle];
                if (chunk.ChunkZ < cz || (chunk.ChunkZ == cz && chunk.ChunkX < cx))
                {
                    first = middle + 1;
                }
                else
                {
                    last = middle;
                }
            }
            if (first < field.Chunks.size() && field.Chunks[first].ChunkX == cx && field.Chunks[first].ChunkZ == cz)
            {
                return &field.Chunks[first];
            }
            return nullptr;
        }

        const uint8_t* ReadPixel(const uint8_t* pixels, int32_t x, int32_t z, uint32_t resolution)
        {
            // GrassField::ReadPixel と同じく行が +Z、行内が +X。隣のチャンクは読まない。
            const uint64_t offset = (static_cast<uint64_t>(ClampPixel(z, resolution)) * resolution +
                                     ClampPixel(x, resolution)) * 4;
            return pixels + offset;
        }

        void SampleLayerA(const uint8_t* pixels, uint32_t resolution, float invPixelWidth,
                          float localX, float localZ, float& red, float& green, uint32_t& speciesId)
        {
            // GrassField::SampleLayer と同じ画素中心。共有する逆数を掛け、シェーダーは precise を付ける。
            const float u = localX * invPixelWidth - 0.5f;
            const float v = localZ * invPixelWidth - 0.5f;
            const int32_t i = static_cast<int32_t>(std::floor(u));
            const int32_t j = static_cast<int32_t>(std::floor(v));
            const float fractionX = u - static_cast<float>(i);
            const float fractionZ = v - static_cast<float>(j);
            const uint8_t* p00 = ReadPixel(pixels, i, j, resolution);
            const uint8_t* p10 = ReadPixel(pixels, i + 1, j, resolution);
            const uint8_t* p01 = ReadPixel(pixels, i, j + 1, resolution);
            const uint8_t* p11 = ReadPixel(pixels, i + 1, j + 1, resolution);
            float channels[2];
            for (uint32_t channel = 0; channel < 2; ++channel)
            {
                const float row0 = Lerp(static_cast<float>(p00[channel]), static_cast<float>(p10[channel]), fractionX);
                const float row1 = Lerp(static_cast<float>(p01[channel]), static_cast<float>(p11[channel]), fractionX);
                channels[channel] = Lerp(row0, row1, fractionZ);
            }
            red = channels[0];
            green = channels[1];

            // GrassField::Sample と同じ最近傍。A は補間せず、画素中心間の中点で次の画素へ進む。
            const int32_t nearestX = static_cast<int32_t>(std::floor(localX * invPixelWidth));
            const int32_t nearestZ = static_cast<int32_t>(std::floor(localZ * invPixelWidth));
            speciesId = ReadPixel(pixels, nearestX, nearestZ, resolution)[3] & 63u;
        }

        float ComputeKeepFraction(float distanceSquared, const GrassGenerateConstants& constants)
        {
            if (distanceSquared > constants.BandMaxDistanceSquared[2] || distanceSquared > constants.KeepDistanceSquared[3])
            {
                return 0.0f;
            }
            if (distanceSquared <= constants.KeepDistanceSquared[0])
            {
                return constants.KeepFractions[0];
            }
            for (uint32_t knot = 1; knot < 4; ++knot)
            {
                if (distanceSquared <= constants.KeepDistanceSquared[knot])
                {
                    const float fraction = (distanceSquared - constants.KeepDistanceSquared[knot - 1]) *
                        constants.KeepInvSpanSquared[knot - 1];
                    // 二乗距離の折れ線。シェーダーは precise を付け、mix に置き換えない。
                    return Lerp(constants.KeepFractions[knot - 1], constants.KeepFractions[knot], fraction);
                }
            }
            return constants.KeepFractions[3];
        }

        float ComputeFade(float distance, const GrassGenerateConstants& constants)
        {
            const float end = constants.BandMaxDistanceMeters[2];
            const float start = constants.TileChunkFadeFacing[2] * end;
            const float t = GrassSaturate((distance - start) * constants.SamplingAndFacing[3]);
            // GLSL の smoothstep と同じ式を明記し、量子化まで演算順を保つ。
            const float smooth = (t * t) * (3.0f - 2.0f * t);
            return 1.0f - smooth;
        }
    } // namespace

    GrassGenerateConstants BuildGrassGenerateConstants(const GrassGenerateSettings& settings,
        const Math::Vector3& cameraPos, const Math::Vector4 frustumPlanes[6], uint32_t layerARes,
        uint32_t layerBRes, float chunkSize, const MegaHiZContract* hizOrNull)
    {
        GrassGenerateConstants constants;
        constants.CameraPosition[0] = cameraPos.x;
        constants.CameraPosition[1] = cameraPos.y;
        constants.CameraPosition[2] = cameraPos.z;
        for (uint32_t plane = 0; plane < 6; ++plane)
        {
            constants.FrustumPlanes[plane][0] = frustumPlanes[plane].x;
            constants.FrustumPlanes[plane][1] = frustumPlanes[plane].y;
            constants.FrustumPlanes[plane][2] = frustumPlanes[plane].z;
            constants.FrustumPlanes[plane][3] = frustumPlanes[plane].w;
        }
        uint32_t base = 0;
        uint32_t firstIndex = 0;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            constants.BandSpacingMeters[lod] = settings.BandSpacingMeters[lod];
            constants.BandMaxDistanceMeters[lod] = settings.BandMaxDistanceMeters[lod];
            constants.BandMaxDistanceSquared[lod] = settings.BandMaxDistanceMeters[lod] * settings.BandMaxDistanceMeters[lod];
            constants.BandCellsPerAxis[lod] = static_cast<uint32_t>(std::round(settings.TileSizeMeters / settings.BandSpacingMeters[lod]));
            const float spacingRatio = settings.BandSpacingMeters[lod] / settings.BandSpacingMeters[0];
            constants.BandKeepScale[lod] = spacingRatio * spacingRatio;
            constants.LodCapacity[lod] = settings.LodCapacity[lod];
            constants.LodBase[lod] = base;
            constants.LodIndexCount[lod] = settings.LodSegments[lod] * 6u;
            constants.LodFirstIndex[lod] = firstIndex;
            base += settings.LodCapacity[lod];
            firstIndex += constants.LodIndexCount[lod];
        }
        for (uint32_t knot = 0; knot < 4; ++knot)
        {
            constants.KeepDistanceSquared[knot] = settings.KeepDistancesMeters[knot] * settings.KeepDistancesMeters[knot];
            constants.KeepFractions[knot] = settings.KeepFractions[knot];
        }
        for (uint32_t span = 0; span < 3; ++span)
        {
            constants.KeepInvSpanSquared[span] = 1.0f /
                (constants.KeepDistanceSquared[span + 1] - constants.KeepDistanceSquared[span]);
        }
        constants.TileChunkFadeFacing[0] = settings.TileSizeMeters;
        constants.TileChunkFadeFacing[1] = chunkSize;
        constants.TileChunkFadeFacing[2] = settings.FadeStartFraction;
        constants.TileChunkFadeFacing[3] = settings.FacingCullThreshold;
        constants.TilesPerChunk[0] = static_cast<uint32_t>(std::round(chunkSize / settings.TileSizeMeters));
        constants.LayerResolutions[0] = layerARes;
        constants.LayerResolutions[1] = layerBRes;
        constants.SamplingAndFacing[0] = static_cast<float>(layerARes) / chunkSize;
        constants.SamplingAndFacing[1] = settings.BandSpacingMeters[0] * settings.BandSpacingMeters[0];
        constants.SamplingAndFacing[2] = settings.FacingCullThreshold * settings.FacingCullThreshold;
        const float fadeEnd = settings.BandMaxDistanceMeters[2];
        const float fadeStart = settings.FadeStartFraction * fadeEnd;
        constants.SamplingAndFacing[3] = 1.0f / (fadeEnd - fadeStart);
        constants.BladeQuantMaxMeters[0] = settings.HeightQuantMaxMeters;
        constants.BladeQuantMaxMeters[1] = settings.WidthQuantMaxMeters;

        float shaderMatrix[16];
        Math::MatrixUtils::TransposeToShaderData(hizOrNull != nullptr ? hizOrNull->ViewProjection :
                                                Math::Matrix4x4::Identity, shaderMatrix);
        for (uint32_t column = 0; column < 4; ++column)
        {
            for (uint32_t row = 0; row < 4; ++row)
            {
                constants.HiZViewProjection[column][row] = shaderMatrix[column * 4 + row];
            }
        }
        if (hizOrNull != nullptr && hizOrNull->bValid && hizOrNull->DepthWidth > 0 && hizOrNull->DepthHeight > 0)
        {
            constants.Flags[0] = kGrassGenerateFlagHiZ;
            // cluster_cull と同じく元の深度寸法を渡す。mip0 はその半分（切り上げ）。
            constants.HiZDimensions[0] = hizOrNull->DepthWidth;
            constants.HiZDimensions[1] = hizOrNull->DepthHeight;
            constants.HiZDimensions[2] = HiZPyramid::ComputeMipCount(
                HiZPyramid::ComputeBaseSize(hizOrNull->DepthWidth),
                HiZPyramid::ComputeBaseSize(hizOrNull->DepthHeight));
        }
        return constants;
    }

    void SetGrassViewClearance(GrassGenerateConstants& constants, const ViewClearanceParameters& parameters)
    {
        constants.ViewClearance = MakeViewClearanceGpuData(parameters, parameters.bAffectGrass);
    }

    GrassGenerateCounts RunGrassGenerateReference(const GrassFieldRenderData& field,
        const GrassGenerateConstants& constants, const GrassTileInfo* tiles, uint32_t tileCount,
        Container::VariableArray<GrassBladeRecord>* outRecords)
    {
        assert((constants.Flags[0] & kGrassGenerateFlagHiZ) == 0);
        GrassGenerateCounts counts;
        if (tiles == nullptr || field.LayerAResolution == 0 || field.Species.empty())
        {
            return counts;
        }
        const float chunkSize = constants.TileChunkFadeFacing[1];
        const uint32_t resolution = constants.LayerResolutions[0];
        assert(resolution == field.LayerAResolution && chunkSize == field.ChunkSizeMeters);
        const float invPixelWidth = constants.SamplingAndFacing[0];
        const float baseCellArea = constants.SamplingAndFacing[1];

        for (uint32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            const GrassTileInfo& tile = tiles[tileIndex];
            const uint32_t band = tile.Band;
            if (band >= kGrassLodCount)
            {
                continue;
            }
            const float spacing = constants.BandSpacingMeters[band];
            const uint32_t n = constants.BandCellsPerAxis[band];
            counts.Candidates += static_cast<uint64_t>(n) * n;
            const int32_t cx = GrassFloorDiv(tile.TileX, constants.TilesPerChunk[0]);
            const int32_t cz = GrassFloorDiv(tile.TileZ, constants.TilesPerChunk[0]);
            const GrassFieldChunkRenderData* pChunk = FindChunk(field, cx, cz);
            if (pChunk == nullptr)
            {
                continue;
            }
            const uint64_t pixelEnd = static_cast<uint64_t>(pChunk->LayerAByteOffset) +
                static_cast<uint64_t>(resolution) * resolution * 4;
            if (pixelEnd > field.LayerAPixels.size())
            {
                continue;
            }
            const uint8_t* pixels = field.LayerAPixels.data() + pChunk->LayerAByteOffset;
            const float chunkOriginX = static_cast<float>(cx) * chunkSize;
            const float chunkOriginZ = static_cast<float>(cz) * chunkSize;
            for (uint32_t j = 0; j < n; ++j)
            {
                for (uint32_t i = 0; i < n; ++i)
                {
                    // uint の乗算で負の座標も 2 の補数のまま写し、符号付きの桁あふれを避ける。
                    const uint32_t gx = static_cast<uint32_t>(tile.TileX) * n + i;
                    const uint32_t gz = static_cast<uint32_t>(tile.TileZ) * n + j;
                    const uint32_t saltBase = band * 8u;
                    const float jx = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltJitterX));
                    const float jz = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltJitterZ));
                    const float x = tile.OriginX + (static_cast<float>(i) + jx) * spacing;
                    const float z = tile.OriginZ + (static_cast<float>(j) + jz) * spacing;
                    const float y = tile.GroundY;

                    // 手順 2。シェーダーは precise を付け、積和を融合させず左から足す。
                    const float dx = x - constants.CameraPosition[0];
                    const float dy = y - constants.CameraPosition[1];
                    const float dz = z - constants.CameraPosition[2];
                    const float distanceSquared = (dx * dx + dy * dy) + dz * dz;
                    if (distanceSquared > constants.BandMaxDistanceSquared[2])
                    {
                        continue;
                    }
                    const float keep = ComputeKeepFraction(distanceSquared, constants);
                    const float scaledKeep = keep * constants.BandKeepScale[band];
                    const float keepBand = scaledKeep < 1.0f ? scaledKeep : 1.0f;

                    float red;
                    float green;
                    uint32_t speciesId;
                    // GLSL へ写すため差と積も float で行う。画素の中心と番地は GrassField と同じ。
                    SampleLayerA(pixels, resolution, invPixelWidth, x - chunkOriginX, z - chunkOriginZ,
                                 red, green, speciesId);
                    if (speciesId >= field.Species.size())
                    {
                        speciesId = 0;
                    }
                    const GrassSpeciesGpuData& species = field.Species[speciesId];
                    const float speciesDensity = species.BladesPerSquareMeter * baseCellArea;
                    const float densityScale = speciesDensity < 1.0f ? speciesDensity : 1.0f;
                    const float density = (red * kGrassInv255) * densityScale;
                    const float heightScale = green * kGrassInv255;
                    const float hash2 = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltKeep));
                    // 手順 4。厳密な > の比較を保つ。シェーダーは precise を付ける。
                    const float keepProbability = density * keepBand;
                    if (!(keepProbability > hash2))
                    {
                        continue;
                    }

                    const float heightRandom = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltHeight));
                    const float height = Lerp(species.HeightMinMeters, species.HeightMaxMeters, heightRandom) * heightScale;
                    if (height <= 0.0f)
                    {
                        continue;
                    }
                    // 手順 6。シェーダーは precise を付け、dot に置き換えず同じ順で評価する。
                    const float halfHeight = height * 0.5f;
                    const float centerY = y + halfHeight;
                    const Math::Vector3 clearancePoint(x, centerY, z);
                    if (ViewClearanceIsActive(constants.ViewClearance))
                    {
                        const float c0 = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltClearance0));
                        const float c1 = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltClearance1));
                        const float c2 = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltClearance2));
                        const float u = std::max(std::min(c0, c1), std::min(std::max(c0, c1), c2));
                        const float g = GrassHashToUnit(GrassHash(gz, gx, saltBase + kGrassHashSaltClearance0));
                        if (!ViewClearanceKeep(constants.ViewClearance, clearancePoint, u, g))
                        {
                            continue;
                        }
                    }
                    const float radius = halfHeight + constants.BladeQuantMaxMeters[1];
                    bool bOutside = false;
                    for (uint32_t plane = 0; plane < 6; ++plane)
                    {
                        const float* p = constants.FrustumPlanes[plane];
                        const float planeDistance = ((p[0] * x + p[1] * centerY) + p[2] * z) + p[3];
                        if (planeDistance < -radius)
                        {
                            bOutside = true;
                            break;
                        }
                    }
                    if (bOutside)
                    {
                        continue;
                    }

                    const float tx = -dx;
                    const float tz = -dz;
                    const float horizontalDistanceSquared = tx * tx + tz * tz;
                    if (horizontalDistanceSquared >= 1.0e-8f)
                    {
                        const float facingRandom = GrassHashToUnit(GrassHash(gx, gz, saltBase + kGrassHashSaltFacing));
                        const float u = 2.0f * facingRandom - 1.0f;
                        const float uSquared = u * u;
                        const float normalX = 1.0f - uSquared;
                        const float normalZ = 2.0f * u;
                        const float a = normalX * tx + normalZ * tz;
                        const float normalLength = 1.0f + uSquared;
                        const float normalLengthSquared = normalLength * normalLength;
                        const float facingLimit = (constants.SamplingAndFacing[2] * normalLengthSquared) *
                            horizontalDistanceSquared;
                        // 手順 7。正規化せず二乗で比べる。シェーダーは precise を付ける。
                        if (a * a < facingLimit)
                        {
                            continue;
                        }
                    }

                    // HZB の判定は GPU だけ。カウンタは記録の容量に達しても増やす。
                    const uint32_t recordIndex = counts.PerLod[band]++;
                    if (outRecords != nullptr && recordIndex < constants.LodCapacity[band])
                    {
                        // 判定をすべて終えてから記録の値を作る。近似の誤差をカウンタへ戻さない。
                        const float safeKeep = keep > 1.0e-6f ? keep : 1.0e-6f;
                        const float width = species.BladeWidthMeters * (1.0f / std::sqrt(safeKeep));
                        const float fade = ComputeFade(std::sqrt(distanceSquared), constants);
                        GrassBladeRecord record;
                        record.PositionX = x;
                        record.PositionY = y;
                        record.PositionZ = z;
                        float recordHeight = height;
                        if (ViewClearanceIsActive(constants.ViewClearance))
                        {
                            const float visibility = ViewClearanceVisibility(constants.ViewClearance, clearancePoint);
                            recordHeight = height * Lerp(constants.ViewClearance.Shape[3], 1.0f, visibility);
                        }
                        record.Packed = PackGrassBlade(recordHeight, width, speciesId, fade,
                            constants.BladeQuantMaxMeters[0], constants.BladeQuantMaxMeters[1]);
                        outRecords->push_back(record);
                    }
                }
            }
        }
        return counts;
    }
} // namespace NorvesLib::Core::Rendering

#if defined(_MSC_VER)
#pragma float_control(pop)
#endif
