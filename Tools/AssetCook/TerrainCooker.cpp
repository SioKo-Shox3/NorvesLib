// 地形格子を全体で量子化し、法線・LOD 誤差付きの NVTRN v0 を生成する。
#include "TerrainCooker.h"

#include "TextureCooker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::Container::AnsiString;
        using Core::Container::Span;
        using Core::Container::VariableArray;
        using Core::Asset::CookedTerrainWriteStatus;

        template <typename... Args>
        bool Fail(AnsiString& error, const char* format, Args... args)
        {
            char text[2048];
            std::snprintf(text, sizeof(text), format, args...);
            error = text;
            return false;
        }

        const char* WriteStatusName(CookedTerrainWriteStatus status)
        {
            switch (status)
            {
            case CookedTerrainWriteStatus::Success: return "Success";
            case CookedTerrainWriteStatus::InvalidDimensions: return "InvalidDimensions";
            case CookedTerrainWriteStatus::InvalidLodCount: return "InvalidLodCount";
            case CookedTerrainWriteStatus::InvalidLayerCount: return "InvalidLayerCount";
            case CookedTerrainWriteStatus::InvalidLayer: return "InvalidLayer";
            case CookedTerrainWriteStatus::DuplicateLayerFourCC: return "DuplicateLayerFourCC";
            case CookedTerrainWriteStatus::TileCountMismatch: return "TileCountMismatch";
            case CookedTerrainWriteStatus::TileIndexOutOfRange: return "TileIndexOutOfRange";
            case CookedTerrainWriteStatus::DuplicateTile: return "DuplicateTile";
            case CookedTerrainWriteStatus::InvalidTileBounds: return "InvalidTileBounds";
            case CookedTerrainWriteStatus::LayerBytesCountMismatch: return "LayerBytesCountMismatch";
            case CookedTerrainWriteStatus::HeightSizeMismatch: return "HeightSizeMismatch";
            case CookedTerrainWriteStatus::EmbeddedTextureInvalid: return "EmbeddedTextureInvalid";
            case CookedTerrainWriteStatus::IntegerOverflow: return "IntegerOverflow";
            }
            return "Unknown";
        }

        struct NormalRg
        {
            uint8_t R = 0;
            uint8_t G = 0;
        };

        struct TileBuffers
        {
            VariableArray<uint8_t> HeightBytes;
            VariableArray<uint8_t> NormalBytes;
            VariableArray<uint8_t> MaskBytes;
            VariableArray<uint8_t> Splat0Bytes;
            VariableArray<uint8_t> Splat1Bytes;
            VariableArray<uint8_t> GrassBytes;
            VariableArray<uint8_t> PuddleBytes;
        };

        uint8_t EncodeNormalComponent(double value)
        {
            return static_cast<uint8_t>(std::clamp(std::round((value * 0.5 + 0.5) * 255.0), 0.0, 255.0));
        }

        float ComputeLodError(const VariableArray<float>& heights, uint32_t tileCells, uint32_t stride)
        {
            const size_t side = static_cast<size_t>(tileCells) + 1;
            double maxError = 0.0;
            for (uint32_t z = 0; z < tileCells; z += stride)
            {
                for (uint32_t x = 0; x < tileCells; x += stride)
                {
                    const double h00 = heights[static_cast<size_t>(z) * side + x];
                    const double h10 = heights[static_cast<size_t>(z) * side + x + stride];
                    const double h01 = heights[static_cast<size_t>(z + stride) * side + x];
                    const double h11 = heights[static_cast<size_t>(z + stride) * side + x + stride];
                    // 各粗いセルの境界を含む全標本で、両対角の三角形補間と復元済み LOD0 高さの差を求める。
                    // 対角 A は u>=w、対角 B は u+w<=1 で三角形を選び、両方の絶対誤差の最大を採る。
                    // 描画時の対角の向きによらない保守的な誤差を、タイル全体で double により集計する。
                    for (uint32_t j = 0; j <= stride; ++j)
                    {
                        const double w = static_cast<double>(j) / stride;
                        for (uint32_t i = 0; i <= stride; ++i)
                        {
                            const double u = static_cast<double>(i) / stride;
                            const double diagonalA = u >= w
                                ? h00 + u * (h10 - h00) + w * (h11 - h10)
                                : h00 + w * (h01 - h00) + u * (h11 - h01);
                            const double diagonalB = u + w <= 1.0
                                ? h00 + u * (h10 - h00) + w * (h01 - h00)
                                : h11 + (1.0 - u) * (h01 - h11) + (1.0 - w) * (h10 - h11);
                            const double height = heights[static_cast<size_t>(z + j) * side + x + i];
                            maxError = std::max(maxError, std::max(std::abs(height - diagonalA), std::abs(height - diagonalB)));
                        }
                    }
                }
            }
            return static_cast<float>(maxError);
        }
    } // namespace

    bool BuildCookedTerrain(const TerrainHeightGrid& grid, const TerrainBuildSettings& settings,
                            TerrainBuildResult& outResult, AnsiString& outError)
    {
        using namespace Core::Asset;
        namespace Format = CookedTerrainFormatV0;
        outError.clear();
        const uint32_t tileCells = settings.TileCells;
        const double cell = settings.CellSizeM;
        const auto* masks = settings.Masks;
        if (tileCells < 8 || tileCells > 1024 || (tileCells & (tileCells - 1)) != 0)
        {
            return Fail(outError, "TileCells=%u は無効です。8..1024 の 2 の冪にしてください。", tileCells);
        }
        if (!std::isfinite(cell) || cell <= 0.0)
        {
            return Fail(outError, "CellSizeM=%.17g は無効です。有限で正の標本間隔 [m] にしてください。", cell);
        }
        if (grid.Width <= tileCells || grid.Height <= tileCells ||
            (grid.Width - 1) % tileCells != 0 || (grid.Height - 1) % tileCells != 0)
        {
            return Fail(outError, "高さ格子は %u×%u 標本、TileCells=%u です。幅・高さをそれぞれ TileCells*N+1（N>=1）にしてください。",
                        grid.Width, grid.Height, tileCells);
        }
        const uint64_t sampleCount64 = static_cast<uint64_t>(grid.Width) * grid.Height;
        if (sampleCount64 > std::numeric_limits<size_t>::max() || grid.HeightsM.size() != sampleCount64)
        {
            return Fail(outError, "高さ格子 %u×%u の標本数は %zu 個です。HeightsM に Width*Height=%llu 個を格納してください。",
                        grid.Width, grid.Height, grid.HeightsM.size(), static_cast<unsigned long long>(sampleCount64));
        }
        if (masks != nullptr)
        {
            if (masks->Width != grid.Width || masks->Height != grid.Height)
            {
                return Fail(outError, "マスク格子 %u×%u は高さ格子 %u×%u と一致しません。寸法を揃えてください。",
                            masks->Width, masks->Height, grid.Width, grid.Height);
            }
            if (sampleCount64 > std::numeric_limits<size_t>::max() / 4)
            {
                return Fail(outError, "マスク格子 %u×%u の RGBA8 配列が格納上限を超えます。入力寸法を小さくしてください。",
                            grid.Width, grid.Height);
            }
            const size_t rgbaBytes = static_cast<size_t>(sampleCount64) * 4;
            if (masks->MaskRgba.size() != rgbaBytes || masks->GrassR8.size() != sampleCount64 ||
                (!masks->Splat0Rgba.empty() && masks->Splat0Rgba.size() != rgbaBytes) ||
                (!masks->Splat1Rgba.empty() && masks->Splat1Rgba.size() != rgbaBytes))
            {
                return Fail(outError, "マスク配列の標本数が高さ格子と一致しません。MaskRgba・Splat は標本数の4倍、GrassR8 は標本数のバイト数にしてください。");
            }
            if (!masks->PuddleR8.empty() && masks->PuddleR8.size() != sampleCount64)
            {
                return Fail(outError, "凹み度配列の標本数が高さ格子と一致しません。PuddleR8 は空にするか、標本数と同じバイト数にしてください。");
            }
            const size_t layerCount = masks->SplatLayerNames.size();
            if (layerCount > 8 || (layerCount > 0) != !masks->Splat0Rgba.empty() ||
                (layerCount > 4) != !masks->Splat1Rgba.empty())
            {
                return Fail(outError, "スプラット層名と配列が矛盾しています。層名は0..8個とし、1層以上なら SPL0、5層以上なら SPL1 を格納してください。");
            }
        }
        double minD = grid.HeightsM[0];
        double maxD = minD;
        for (size_t index = 0; index < grid.HeightsM.size(); ++index)
        {
            const double height = grid.HeightsM[index];
            if (!std::isfinite(height))
            {
                return Fail(outError, "標本 (x=%zu, z=%zu) の高さ %.17g m が非有限です。有限の高さに修正してください。",
                            index % grid.Width, index / grid.Width, height);
            }
            minD = std::min(minD, height);
            maxD = std::max(maxD, height);
        }
        const float heightMin = static_cast<float>(minD);
        const float heightMax = static_cast<float>(maxD);
        if (!std::isfinite(heightMin) || !std::isfinite(heightMax) || heightMax < heightMin)
        {
            return Fail(outError, "高さの範囲 [%.17g, %.17g] m を float に格納できません。有限の float に収まる高さに修正してください。", minD, maxD);
        }
        const float cellFloat = static_cast<float>(cell);
        const float extentX = static_cast<float>(static_cast<double>(grid.Width - 1) * cell);
        const float extentZ = static_cast<float>(static_cast<double>(grid.Height - 1) * cell);
        if (!std::isfinite(cellFloat) || cellFloat <= 0.0f || !std::isfinite(extentX) || !std::isfinite(extentZ))
        {
            return Fail(outError, "CellSizeM=%.17g、セル数=%u×%u の寸法を有限の float に格納できません。正の float に収まる標本間隔と地形寸法にしてください。",
                        cell, grid.Width - 1, grid.Height - 1);
        }

        try
        {
            const size_t sampleCount = static_cast<size_t>(sampleCount64);
            const double lo = heightMin;
            const double hi = heightMax;
            const double range = hi - lo;
            VariableArray<uint16_t> quantized(sampleCount);
            VariableArray<NormalRg> normals(sampleCount);
            // 全体で一度だけ量子化と差分を行い、共有辺でも同じ標本値と法線を使う。
            for (uint32_t z = 0; z < grid.Height; ++z)
            {
                const uint32_t prevZ = z == 0 ? z : z - 1;
                const uint32_t nextZ = z == grid.Height - 1 ? z : z + 1;
                for (uint32_t x = 0; x < grid.Width; ++x)
                {
                    const size_t index = static_cast<size_t>(z) * grid.Width + x;
                    quantized[index] = range > 0.0
                        ? static_cast<uint16_t>(std::clamp(std::round((grid.HeightsM[index] - lo) / range * 65535.0), 0.0, 65535.0))
                        : 0;
                    const uint32_t prevX = x == 0 ? x : x - 1;
                    const uint32_t nextX = x == grid.Width - 1 ? x : x + 1;
                    const double dhdx = (grid.HeightsM[static_cast<size_t>(z) * grid.Width + nextX] -
                                         grid.HeightsM[static_cast<size_t>(z) * grid.Width + prevX]) /
                                        ((nextX - prevX) * cell);
                    const double dhdz = (grid.HeightsM[static_cast<size_t>(nextZ) * grid.Width + x] -
                                         grid.HeightsM[static_cast<size_t>(prevZ) * grid.Width + x]) /
                                        ((nextZ - prevZ) * cell);
                    const double length = std::sqrt(dhdx * dhdx + 1.0 + dhdz * dhdz);
                    normals[index] = {EncodeNormalComponent(-dhdx / length), EncodeNormalComponent(-dhdz / length)};
                }
            }

            const uint32_t tileCountX = (grid.Width - 1) / tileCells;
            const uint32_t tileCountZ = (grid.Height - 1) / tileCells;
            const size_t tileCount = static_cast<size_t>(tileCountX) * tileCountZ;
            const uint32_t side = tileCells + 1;
            const size_t tileSamples = static_cast<size_t>(side) * side;
            VariableArray<TileBuffers> buffers(tileCount);
            VariableArray<float> tileHeights(tileSamples);
            VariableArray<uint8_t> rgba(tileSamples * 4);
            VariableArray<uint8_t> maskRgba(masks != nullptr ? tileSamples * 4 : 0);
            VariableArray<uint8_t> splat0Rgba(masks != nullptr && !masks->Splat0Rgba.empty() ? tileSamples * 4 : 0);
            VariableArray<uint8_t> splat1Rgba(masks != nullptr && !masks->Splat1Rgba.empty() ? tileSamples * 4 : 0);
            VariableArray<uint8_t> grassRgba(masks != nullptr ? tileSamples * 4 : 0);
            VariableArray<uint8_t> puddleRgba(masks != nullptr && !masks->PuddleR8.empty() ? tileSamples * 4 : 0);
            CookedTerrainWriteDesc desc;
            desc.CellCountX = grid.Width - 1;
            desc.CellCountZ = grid.Height - 1;
            desc.TileCells = tileCells;
            desc.LodCount = 4;
            desc.CellSizeM = cellFloat;
            desc.HeightMinM = heightMin;
            desc.HeightMaxM = heightMax;
            desc.Layers.resize(2);
            desc.Layers[0].FourCC = Format::HeightLayerFourCC;
            desc.Layers[0].Encoding = Format::LayerEncodingRawHeight16;
            desc.Layers[0].PixelFormat = CookedTexturePixelFormat::R16UNorm;
            desc.Layers[0].ColorSpace = CookedTextureColorSpace::Linear;
            desc.Layers[0].Semantic = TerrainHeightSemantic;
            desc.Layers[1].FourCC = TerrainNormalLayerFourCC;
            desc.Layers[1].Encoding = Format::LayerEncodingEmbeddedNvtex;
            desc.Layers[1].PixelFormat = CookedTexturePixelFormat::RG8UNorm;
            desc.Layers[1].ColorSpace = CookedTextureColorSpace::Linear;
            desc.Layers[1].Semantic = TerrainNormalSemantic;
            if (masks != nullptr)
            {
                const auto appendLayer = [&](AssetPackageFourCC fourCC, CookedTexturePixelFormat pixelFormat,
                                             const AnsiString& semantic)
                {
                    CookedTerrainLayerDesc layer;
                    layer.FourCC = fourCC;
                    layer.Encoding = Format::LayerEncodingEmbeddedNvtex;
                    layer.PixelFormat = pixelFormat;
                    layer.ColorSpace = CookedTextureColorSpace::Linear;
                    layer.Semantic = semantic;
                    desc.Layers.push_back(std::move(layer));
                };
                const auto appendSplatLayer = [&](size_t firstLayer, AssetPackageFourCC fourCC)
                {
                    AnsiString semantic = TerrainSplatSemanticPrefix;
                    const size_t endLayer = std::min(firstLayer + 4, masks->SplatLayerNames.size());
                    for (size_t index = firstLayer; index < endLayer; ++index)
                    {
                        if (index != firstLayer)
                        {
                            semantic.push_back(',');
                        }
                        const auto& name = masks->SplatLayerNames[index];
                        if (semantic.size() > Format::MaxSemanticBytes || name.size() > Format::MaxSemanticBytes - semantic.size())
                        {
                            return Fail(outError, "スプラットの意味文字列が255文字を超えます。層名を短くしてください。");
                        }
                        semantic.append(name);
                    }
                    appendLayer(fourCC, CookedTexturePixelFormat::RGBA8UNorm, semantic);
                    return true;
                };
                appendLayer(TerrainMaskLayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, TerrainMaskSemantic);
                if (!masks->Splat0Rgba.empty() && !appendSplatLayer(0, TerrainSplat0LayerFourCC))
                {
                    return false;
                }
                if (!masks->Splat1Rgba.empty() && !appendSplatLayer(4, TerrainSplat1LayerFourCC))
                {
                    return false;
                }
                appendLayer(TerrainGrassLayerFourCC, CookedTexturePixelFormat::R8UNorm, TerrainGrassSemantic);
                if (!masks->PuddleR8.empty())
                {
                    appendLayer(TerrainPuddleLayerFourCC, CookedTexturePixelFormat::R8UNorm, Core::Asset::CookedTerrainFormatV0::PuddleSemantic);
                }
            }
            desc.Tiles.resize(tileCount);
            for (uint32_t tz = 0; tz < tileCountZ; ++tz)
            {
                for (uint32_t tx = 0; tx < tileCountX; ++tx)
                {
                    const size_t tileIndex = static_cast<size_t>(tz) * tileCountX + tx;
                    auto& tile = desc.Tiles[tileIndex];
                    auto& data = buffers[tileIndex];
                    tile.TileX = tx;
                    tile.TileZ = tz;
                    tile.HeightMinM = std::numeric_limits<float>::max();
                    tile.HeightMaxM = std::numeric_limits<float>::lowest();
                    data.HeightBytes.resize(tileSamples * 2);
                    for (uint32_t sz = 0; sz < side; ++sz)
                    {
                        for (uint32_t sx = 0; sx < side; ++sx)
                        {
                            const size_t globalIndex = static_cast<size_t>(tz * tileCells + sz) * grid.Width + tx * tileCells + sx;
                            const size_t localIndex = static_cast<size_t>(sz) * side + sx;
                            const uint16_t value = quantized[globalIndex];
                            data.HeightBytes[localIndex * 2] = static_cast<uint8_t>(value & 0xffu);
                            data.HeightBytes[localIndex * 2 + 1] = static_cast<uint8_t>(value >> 8);
                            // DecodeHeightM と同じ double の補間式を float に丸める。
                            const double fraction = static_cast<double>(value) / 65535.0;
                            const float hq = static_cast<float>(lo * (1.0 - fraction) + hi * fraction);
                            tileHeights[localIndex] = hq;
                            tile.HeightMinM = std::min(tile.HeightMinM, hq);
                            tile.HeightMaxM = std::max(tile.HeightMaxM, hq);
                            rgba[localIndex * 4] = normals[globalIndex].R;
                            rgba[localIndex * 4 + 1] = normals[globalIndex].G;
                            rgba[localIndex * 4 + 2] = 0;
                            rgba[localIndex * 4 + 3] = 255;
                            if (masks != nullptr)
                            {
                                for (size_t channel = 0; channel < 4; ++channel)
                                {
                                    maskRgba[localIndex * 4 + channel] = masks->MaskRgba[globalIndex * 4 + channel];
                                    if (!splat0Rgba.empty())
                                    {
                                        splat0Rgba[localIndex * 4 + channel] = masks->Splat0Rgba[globalIndex * 4 + channel];
                                    }
                                    if (!splat1Rgba.empty())
                                    {
                                        splat1Rgba[localIndex * 4 + channel] = masks->Splat1Rgba[globalIndex * 4 + channel];
                                    }
                                }
                                grassRgba[localIndex * 4] = masks->GrassR8[globalIndex];
                                grassRgba[localIndex * 4 + 1] = 0;
                                grassRgba[localIndex * 4 + 2] = 0;
                                grassRgba[localIndex * 4 + 3] = 255;
                                if (!puddleRgba.empty())
                                {
                                    puddleRgba[localIndex * 4] = masks->PuddleR8[globalIndex];
                                    puddleRgba[localIndex * 4 + 1] = 0;
                                    puddleRgba[localIndex * 4 + 2] = 0;
                                    puddleRgba[localIndex * 4 + 3] = 255;
                                }
                            }
                        }
                    }
                    tile.AabbMin[0] = static_cast<float>(static_cast<double>(tx) * tileCells * cell);
                    tile.AabbMin[1] = tile.HeightMinM;
                    tile.AabbMin[2] = static_cast<float>(static_cast<double>(tz) * tileCells * cell);
                    tile.AabbMax[0] = static_cast<float>(static_cast<double>(tx + 1) * tileCells * cell);
                    tile.AabbMax[1] = tile.HeightMaxM;
                    tile.AabbMax[2] = static_cast<float>(static_cast<double>(tz + 1) * tileCells * cell);
                    for (uint32_t lod = 1; lod < desc.LodCount; ++lod)
                    {
                        tile.LodMaxErrorM[lod] = ComputeLodError(tileHeights, tileCells, 1u << lod);
                    }
                    // RGBA8 の R・G を線形 RG8 に焼く。ミップは領域平均で、法線の再正規化は行わない。
                    TextureCookResult normalTexture;
                    AnsiString textureError;
                    if (!CookRgba8ToNvtex(Span<const uint8_t>(rgba.data(), rgba.size()), side, side,
                                          "nvtex.v0.rg8.linear", normalTexture, textureError))
                    {
                        return Fail(outError, "タイル (%u, %u) の NRM0 を生成できません: %s。標本数と RGBA8 入力を確認してください。",
                                    tx, tz, textureError.c_str());
                    }
                    data.NormalBytes = std::move(normalTexture.NvtexBytes);
                    tile.LayerBytes.push_back(Span<const uint8_t>(data.HeightBytes.data(), data.HeightBytes.size()));
                    tile.LayerBytes.push_back(Span<const uint8_t>(data.NormalBytes.data(), data.NormalBytes.size()));
                    if (masks != nullptr)
                    {
                        // buffers は全タイル分を確保済みで、各層の所有先を動かさず Span の参照先を保つ。
                        const auto cookLayer = [&](const VariableArray<uint8_t>& pixels, const char* format,
                                                   const char* layerName, VariableArray<uint8_t>& bytes)
                        {
                            TextureCookResult texture;
                            if (!CookRgba8ToNvtex(Span<const uint8_t>(pixels.data(), pixels.size()), side, side,
                                                  format, texture, textureError))
                            {
                                return Fail(outError, "タイル (%u, %u) の %s を生成できません: %s。標本数と RGBA8 入力を確認してください。",
                                            tx, tz, layerName, textureError.c_str());
                            }
                            bytes = std::move(texture.NvtexBytes);
                            tile.LayerBytes.push_back(Span<const uint8_t>(bytes.data(), bytes.size()));
                            return true;
                        };
                        if (!cookLayer(maskRgba, "nvtex.v0.rgba8.linear", "MSK0", data.MaskBytes) ||
                            (!splat0Rgba.empty() && !cookLayer(splat0Rgba, "nvtex.v0.rgba8.linear", "SPL0", data.Splat0Bytes)) ||
                            (!splat1Rgba.empty() && !cookLayer(splat1Rgba, "nvtex.v0.rgba8.linear", "SPL1", data.Splat1Bytes)) ||
                            !cookLayer(grassRgba, "nvtex.v0.r8.linear", "GRS0", data.GrassBytes) ||
                            (!puddleRgba.empty() && !cookLayer(puddleRgba, "nvtex.v0.r8.linear", "PDL0", data.PuddleBytes)))
                        {
                            return false;
                        }
                    }
                }
            }

            TerrainBuildResult result;
            const auto status = WriteCookedTerrain(desc, result.NvtrnBytes);
            if (status != CookedTerrainWriteStatus::Success)
            {
                return Fail(outError, "NVTRN v0 の書き出しに失敗しました（%s）。地形の寸法・タイル境界・レイヤの値と格納範囲を確認してください。",
                            WriteStatusName(status));
            }
            result.TileCountX = tileCountX;
            result.TileCountZ = tileCountZ;
            result.LodCount = desc.LodCount;
            result.HeightMinM = heightMin;
            result.HeightMaxM = heightMax;
            outResult = std::move(result);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(outError, "高さ格子 %u×%u の cook 用メモリを確保できません。入力寸法を小さくしてください。", grid.Width, grid.Height);
        }
        catch (const std::length_error&)
        {
            return Fail(outError, "高さ格子 %u×%u の cook 用配列が格納上限を超えました。入力寸法を小さくしてください。", grid.Width, grid.Height);
        }
    }
} // namespace NorvesLib::Tools::AssetCook
