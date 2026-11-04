// 全体格子で湿り・地表・草密度を計算し、ペイントを適用する。
#include "TerrainMasks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "stb_image.h"

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::Container::AnsiString;
        using Core::Container::Span;
        using Core::Container::VariableArray;

        constexpr size_t MaxSplatLayers = 8;
        constexpr size_t SplatPaintGroup = 5;
        constexpr size_t PaintGroupCount = SplatPaintGroup + MaxSplatLayers;

        template <typename... Args>
        bool Fail(AnsiString& error, const char* format, Args... args)
        {
            char text[2048];
            std::snprintf(text, sizeof(text), format, args...);
            error = text;
            return false;
        }

        struct StbiPixels
        {
            stbi_uc* Data = nullptr;
            ~StbiPixels() { stbi_image_free(Data); }
        };

        struct FloodEntry
        {
            double Height;
            size_t Sequence;
            size_t Index;
        };

        bool FloodBefore(const FloodEntry& a, const FloodEntry& b)
        {
            return a.Height < b.Height || (a.Height == b.Height && a.Sequence < b.Sequence);
        }

        // 標準コンテナを持つ優先度付きキューは使わず、投入順を含む最小ヒープを作る。
        class FloodHeap
        {
        public:
            void Push(const FloodEntry& entry)
            {
                m_entries.push_back(entry);
                size_t index = m_entries.size() - 1;
                while (index > 0)
                {
                    const size_t parent = (index - 1) / 2;
                    if (!FloodBefore(m_entries[index], m_entries[parent]))
                    {
                        break;
                    }
                    std::swap(m_entries[index], m_entries[parent]);
                    index = parent;
                }
            }

            FloodEntry Pop()
            {
                const FloodEntry result = m_entries.front();
                m_entries.front() = m_entries.back();
                m_entries.pop_back();
                size_t index = 0;
                while (index < m_entries.size() / 2)
                {
                    const size_t left = index * 2 + 1;
                    const size_t right = left + 1;
                    size_t child = left;
                    if (right < m_entries.size() && FloodBefore(m_entries[right], m_entries[left]))
                    {
                        child = right;
                    }
                    if (!FloodBefore(m_entries[child], m_entries[index]))
                    {
                        break;
                    }
                    std::swap(m_entries[index], m_entries[child]);
                    index = child;
                }
                return result;
            }

            bool Empty() const { return m_entries.empty(); }

        private:
            VariableArray<FloodEntry> m_entries;
        };

        void ComputeFlowAccumulation(const TerrainHeightGrid& grid, VariableArray<double>& accumulation)
        {
            const size_t count = grid.HeightsM.size();
            constexpr size_t unvisited = std::numeric_limits<size_t>::max();
            constexpr size_t noReceiver = unvisited - 1;
            VariableArray<size_t> receiver(count, unvisited);
            VariableArray<size_t> order;
            order.reserve(count);
            FloodHeap heap;
            size_t sequence = 0;

            // 外周は z の昇順、同じ行では x の昇順で投入する。
            const auto addBoundary = [&](size_t index)
            {
                receiver[index] = noReceiver;
                heap.Push({grid.HeightsM[index], sequence++, index});
            };
            for (uint32_t x = 0; x < grid.Width; ++x)
            {
                addBoundary(x);
            }
            for (uint32_t z = 1; z < grid.Height - 1; ++z)
            {
                addBoundary(static_cast<size_t>(z) * grid.Width);
                addBoundary(static_cast<size_t>(z) * grid.Width + grid.Width - 1);
            }
            const size_t lastRow = static_cast<size_t>(grid.Height - 1) * grid.Width;
            for (uint32_t x = 0; x < grid.Width; ++x)
            {
                addBoundary(lastRow + x);
            }

            while (!heap.Empty())
            {
                const FloodEntry current = heap.Pop();
                order.push_back(current.Index);
                const uint32_t x = static_cast<uint32_t>(current.Index % grid.Width);
                const uint32_t z = static_cast<uint32_t>(current.Index / grid.Width);
                const uint32_t minX = x == 0 ? x : x - 1;
                const uint32_t maxX = x == grid.Width - 1 ? x : x + 1;
                const uint32_t minZ = z == 0 ? z : z - 1;
                const uint32_t maxZ = z == grid.Height - 1 ? z : z + 1;
                // 8 近傍は z の昇順、同じ行では x の昇順で投入する。
                for (uint32_t nz = minZ; nz <= maxZ; ++nz)
                {
                    for (uint32_t nx = minX; nx <= maxX; ++nx)
                    {
                        const size_t neighbor = static_cast<size_t>(nz) * grid.Width + nx;
                        if (receiver[neighbor] != unvisited)
                        {
                            continue;
                        }
                        receiver[neighbor] = current.Index;
                        heap.Push({std::max(grid.HeightsM[neighbor], current.Height), sequence++, neighbor});
                    }
                }
            }

            accumulation.assign(count, 1.0);
            for (size_t i = order.size(); i > 0; --i)
            {
                const size_t index = order[i - 1];
                if (receiver[index] != noReceiver)
                {
                    accumulation[receiver[index]] += accumulation[index];
                }
            }
        }

        double Ramp(double value, double start, double end)
        {
            if (value <= start)
            {
                return 0.0;
            }
            if (value >= end)
            {
                return 1.0;
            }
            const double width = end - start;
            if (std::isfinite(width))
            {
                return (value - start) / width;
            }
            return (value * 0.5 - start * 0.5) / (end * 0.5 - start * 0.5);
        }

        double HeightGradient(double low, double high, uint32_t distance, double cellSizeM)
        {
            const double difference = high - low;
            const double spacing = distance * cellSizeM;
            if (std::isfinite(difference) && std::isfinite(spacing))
            {
                return difference / spacing;
            }
            return ((high * 0.5 - low * 0.5) / cellSizeM) * (2.0 / distance);
        }

        double HeightLaplacian(double left, double right, double top, double bottom, double height, double cellSizeM)
        {
            const double numerator = right + left + bottom + top - 4.0 * height;
            const double cellSquared = cellSizeM * cellSizeM;
            if (std::isfinite(numerator) && std::isfinite(cellSquared) && cellSquared > 0.0)
            {
                return numerator / cellSquared;
            }
            if (std::isfinite(numerator))
            {
                return (numerator / cellSizeM) / cellSizeM;
            }
            const double scaled = right * 0.125 + left * 0.125 + bottom * 0.125 + top * 0.125 - height * 0.5;
            return ((scaled / cellSizeM) / cellSizeM) * 8.0;
        }

        double Band(double value, const TerrainBandRule& rule)
        {
            if (!rule.bEnabled || (value >= rule.Min && value <= rule.Max))
            {
                return 1.0;
            }
            if (rule.Soft == 0.0)
            {
                return 0.0;
            }
            const double distance = value < rule.Min ? rule.Min - value : value - rule.Max;
            return std::max(0.0, 1.0 - distance / rule.Soft);
        }

        uint64_t SplitMix64(uint64_t value)
        {
            value += 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }

        double Hash01(uint64_t xHash, uint32_t z)
        {
            return static_cast<double>(SplitMix64(xHash ^ z) >> 11) * (1.0 / 9007199254740992.0);
        }

        double ValueNoise(uint64_t prefix, uint32_t x, uint32_t z, double latticeSpacing)
        {
            const double gx = static_cast<double>(x) / latticeSpacing;
            const double gz = static_cast<double>(z) / latticeSpacing;
            const double floorX = std::floor(gx);
            const double floorZ = std::floor(gz);
            // uint32 座標は 2^32 で折り返し、範囲外の実数からの整数変換を避ける。
            const uint32_t ix = static_cast<uint32_t>(floorX < 4294967296.0 ? floorX : std::fmod(floorX, 4294967296.0));
            const uint32_t iz = static_cast<uint32_t>(floorZ < 4294967296.0 ? floorZ : std::fmod(floorZ, 4294967296.0));
            const double fx = gx - floorX;
            const double fz = gz - floorZ;
            const double sx = fx * fx * (3.0 - 2.0 * fx);
            const double sz = fz * fz * (3.0 - 2.0 * fz);
            const uint64_t left = SplitMix64(prefix ^ ix);
            const uint64_t right = SplitMix64(prefix ^ (ix + 1u));
            const double top = std::lerp(Hash01(left, iz), Hash01(right, iz), sx);
            const double bottom = std::lerp(Hash01(left, iz + 1u), Hash01(right, iz + 1u), sx);
            return std::lerp(top, bottom, sz);
        }

        uint8_t Quantize(double value)
        {
            return static_cast<uint8_t>(std::round(std::clamp(value, 0.0, 1.0) * 255.0));
        }

        double ApplyPaints(double value, size_t sample, const VariableArray<size_t>& group,
                           const TerrainRecipe& recipe, Span<const TerrainPaintImage> paints)
        {
            for (size_t paintIndex : group)
            {
                const double p = static_cast<double>(paints[paintIndex].Values[sample]) / 255.0;
                const double alpha = static_cast<double>(paints[paintIndex].Alpha[sample]) / 255.0;
                if (recipe.Paints[paintIndex].Mode == TerrainPaintMode::Override)
                {
                    value = value * (1.0 - alpha) + p * alpha;
                }
                else
                {
                    value *= (1.0 - alpha) + p * alpha;
                }
            }
            return value;
        }

        bool ValidNoise(const TerrainNoiseRule& rule)
        {
            return std::isfinite(rule.Amplitude) && rule.Amplitude >= 0.0 &&
                   std::isfinite(rule.ScaleM) && rule.ScaleM > 0.0;
        }

        bool ValidBand(const TerrainBandRule& rule)
        {
            return std::isfinite(rule.Min) && std::isfinite(rule.Max) && std::isfinite(rule.Soft) &&
                   rule.Max >= rule.Min && rule.Soft >= 0.0;
        }

        bool ValidNoiseCoordinates(const TerrainNoiseRule& noise, double cellSizeM, uint32_t maxCoordinate)
        {
            if (noise.Amplitude == 0.0)
            {
                return true;
            }
            const double spacing = noise.ScaleM / cellSizeM;
            return spacing > 0.0 && std::isfinite(static_cast<double>(maxCoordinate) / spacing);
        }

        bool ValidateRecipe(const TerrainRecipe& recipe, double cellSizeM, uint32_t maxCoordinate, AnsiString& error)
        {
            const TerrainWetnessRule& wet = recipe.Wetness;
            if (!std::isfinite(wet.FlowWeight) ||
                !std::isfinite(wet.FlowRefCells) || wet.FlowRefCells <= 0.0 ||
                !std::isfinite(wet.LowWeight) ||
                !std::isfinite(wet.LowFullM) || !std::isfinite(wet.LowZeroM) || wet.LowZeroM <= wet.LowFullM ||
                !std::isfinite(wet.ConcaveWeight) ||
                !std::isfinite(wet.ConcaveFullPerM) || wet.ConcaveFullPerM <= 0.0)
            {
                return Fail(error, "wetness の値または範囲が無効です。有限の値を指定し、基準値を正、low_zero_m を low_full_m より大きくしてください。");
            }
            if (!std::isfinite(recipe.DryGrass.Base) || !ValidNoise(recipe.DryGrass.Noise) ||
                !ValidNoiseCoordinates(recipe.DryGrass.Noise, cellSizeM, maxCoordinate))
            {
                return Fail(error, "dry_grass の値または雑音の座標が無効です。有限の値、0 以上の amplitude、正の scale_m と計算可能な格子間隔を指定してください。");
            }
            const TerrainRockRule& rock = recipe.Rock;
            if (!std::isfinite(rock.SlopeStartDeg) || !std::isfinite(rock.SlopeFullDeg) || rock.SlopeFullDeg <= rock.SlopeStartDeg)
            {
                return Fail(error, "rock の傾斜範囲が無効です。有限の値を指定し、slope_full_deg を slope_start_deg より大きくしてください。");
            }
            const TerrainGrassDensityRule& grass = recipe.GrassDensity;
            if (!std::isfinite(grass.Base) || !std::isfinite(grass.SlopeStartDeg) || !std::isfinite(grass.SlopeZeroDeg) ||
                grass.SlopeZeroDeg <= grass.SlopeStartDeg || !std::isfinite(grass.WetStart) || !std::isfinite(grass.WetZero) ||
                grass.WetZero <= grass.WetStart || !std::isfinite(grass.RoadSuppress) || grass.RoadSuppress < 0.0 ||
                !std::isfinite(grass.RockSuppress) || grass.RockSuppress < 0.0)
            {
                return Fail(error, "grass_density の値または範囲が無効です。有限の値、増加する傾斜・湿り範囲、0 以上の抑制係数を指定してください。");
            }
            for (size_t k = 0; k < recipe.SplatLayers.size(); ++k)
            {
                const TerrainSplatLayerRule& layer = recipe.SplatLayers[k];
                if (!std::isfinite(layer.Weight) || layer.Weight < 0.0 || !ValidBand(layer.SlopeDeg) ||
                    !ValidBand(layer.HeightM) || !ValidBand(layer.Wetness) || !ValidNoise(layer.Noise) ||
                    !ValidNoiseCoordinates(layer.Noise, cellSizeM, maxCoordinate))
                {
                    return Fail(error, "splat の層 %zu の値、範囲または雑音の座標が無効です。有限の値、0 以上の重み・soft・amplitude、正の scale_m と計算可能な格子間隔を指定してください。", k);
                }
            }
            return true;
        }

        void QuantizeSplat(const double* raw, size_t layerCount, uint8_t* bytes)
        {
            double sum = 0.0;
            double maxRaw = 0.0;
            for (size_t k = 0; k < layerCount; ++k)
            {
                sum += raw[k];
                maxRaw = std::max(maxRaw, raw[k]);
            }
            if (sum <= 0.0)
            {
                bytes[0] = 255;
                return;
            }
            // 有限の各層の和だけがあふれる場合も、比率を保って正規化する。
            const bool bScale = !std::isfinite(sum);
            if (bScale)
            {
                sum = 0.0;
                for (size_t k = 0; k < layerCount; ++k)
                {
                    sum += raw[k] / maxRaw;
                }
            }
            double remainder[MaxSplatLayers]{};
            unsigned used = 0;
            for (size_t k = 0; k < layerCount; ++k)
            {
                const double weight = (bScale ? raw[k] / maxRaw : raw[k]) / sum;
                const double scaled = weight * 255.0;
                const double integral = std::floor(scaled);
                bytes[k] = static_cast<uint8_t>(integral);
                used += bytes[k];
                remainder[k] = scaled - integral;
            }
            for (unsigned left = 255 - used; left > 0; --left)
            {
                size_t best = 0;
                for (size_t k = 1; k < layerCount; ++k)
                {
                    if (remainder[k] > remainder[best])
                    {
                        best = k;
                    }
                }
                ++bytes[best];
                remainder[best] = -1.0;
            }
        }
    } // namespace

    bool DecodeTerrainPaintPng(Span<const uint8_t> pngBytes, bool flipZ, TerrainPaintImage& out, AnsiString& outError)
    {
        outError.clear();
        try
        {
            constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (pngBytes.size() < sizeof(signature) || std::memcmp(pngBytes.data(), signature, sizeof(signature)) != 0)
            {
                return Fail(outError, "ペイント画像に PNG 識別子がありません。8bit PNG ファイルを指定してください。");
            }
            if (pngBytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return Fail(outError, "ペイント PNG が %zu バイトで読み込み上限を超えます。画像を小さくしてください。", pngBytes.size());
            }
            const int size = static_cast<int>(pngBytes.size());
            if (stbi_is_16_bit_from_memory(pngBytes.data(), size))
            {
                return Fail(outError, "ペイント PNG は 16bit です。8bit PNG に変換してください。");
            }
            int width = 0;
            int height = 0;
            int channels = 0;
            if (!stbi_info_from_memory(pngBytes.data(), size, &width, &height, &channels) || pngBytes.size() <= 24)
            {
                return Fail(outError, "ペイント PNG の情報を読めません。破損していない 8bit PNG を指定してください。");
            }
            if (pngBytes[24] != 8)
            {
                return Fail(outError, "ペイント PNG のビット深度は %u です。8bit PNG に変換してください。", unsigned(pngBytes[24]));
            }
            if (width < 1 || height < 1 || channels < 1 || channels > 4)
            {
                return Fail(outError, "ペイント PNG は %d×%d 標本、%d チャンネルです。正の寸法と 1..4 チャンネルの画像を指定してください。", width, height, channels);
            }
            const uint64_t count64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
            if (count64 > std::numeric_limits<size_t>::max())
            {
                return Fail(outError, "ペイント PNG の寸法 %d×%d が格納上限を超えます。画像を小さくしてください。", width, height);
            }
            StbiPixels pixels;
            pixels.Data = stbi_load_from_memory(pngBytes.data(), size, &width, &height, &channels, 0);
            if (!pixels.Data)
            {
                return Fail(outError, "ペイント PNG を復号できません。破損していない 8bit PNG を指定してください。");
            }
            TerrainPaintImage result;
            result.Width = static_cast<uint32_t>(width);
            result.Height = static_cast<uint32_t>(height);
            result.Values.resize(static_cast<size_t>(count64));
            result.Alpha.resize(static_cast<size_t>(count64));
            const int alphaChannel = channels == 2 ? 1 : (channels == 4 ? 3 : -1);
            for (uint32_t z = 0; z < result.Height; ++z)
            {
                const uint32_t sourceZ = flipZ ? result.Height - 1 - z : z;
                for (uint32_t x = 0; x < result.Width; ++x)
                {
                    const size_t target = static_cast<size_t>(z) * result.Width + x;
                    const size_t source = (static_cast<size_t>(sourceZ) * result.Width + x) * static_cast<size_t>(channels);
                    result.Values[target] = pixels.Data[source];
                    result.Alpha[target] = alphaChannel < 0 ? 255 : pixels.Data[source + static_cast<size_t>(alphaChannel)];
                }
            }
            out = std::move(result);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(outError, "ペイント PNG の展開用メモリを確保できません。画像の寸法を小さくしてください。");
        }
        catch (const std::length_error&)
        {
            return Fail(outError, "ペイント PNG の展開配列が格納上限を超えました。画像の寸法を小さくしてください。");
        }
    }

    bool ComputeTerrainMasks(const TerrainHeightGrid& grid, double cellSizeM, uint32_t seed, const TerrainRecipe& recipe,
                             Span<const TerrainPaintImage> paints, TerrainMaskFields& outFields, AnsiString& outError)
    {
        outError.clear();
        try
        {
            if (grid.Width < 2 || grid.Height < 2)
            {
                return Fail(outError, "高さ格子は %u×%u 標本です。幅・高さをそれぞれ 2 以上にしてください。", grid.Width, grid.Height);
            }
            const uint64_t count64 = static_cast<uint64_t>(grid.Width) * grid.Height;
            if (count64 > std::numeric_limits<size_t>::max() / 4 || grid.HeightsM.size() != count64)
            {
                return Fail(outError, "高さ格子 %u×%u の標本数は %zu 個か格納上限を超えています。格納可能な寸法と Width*Height 個の高さを指定してください。",
                            grid.Width, grid.Height, grid.HeightsM.size());
            }
            if (!std::isfinite(cellSizeM) || cellSizeM <= 0.0)
            {
                return Fail(outError, "CellSizeM=%.17g は無効です。有限で正の標本間隔 [m] を指定してください。", cellSizeM);
            }
            if (recipe.SplatLayers.size() > MaxSplatLayers)
            {
                return Fail(outError, "splat は %zu 層です。層数を 0..8 にしてください。", recipe.SplatLayers.size());
            }
            if (paints.size() != recipe.Paints.size())
            {
                return Fail(outError, "ペイント画像は %zu 個、recipe の指定は %zu 個です。同じ順序で同じ数の画像を渡してください。", paints.size(), recipe.Paints.size());
            }
            const size_t count = static_cast<size_t>(count64);
            for (size_t i = 0; i < count; ++i)
            {
                if (!std::isfinite(grid.HeightsM[i]))
                {
                    return Fail(outError, "高さ標本 (x=%zu, z=%zu) が非有限です。有限の高さ [m] に修正してください。", i % grid.Width, i / grid.Width);
                }
            }
            if (!ValidateRecipe(recipe, cellSizeM, std::max(grid.Width, grid.Height) - 1, outError))
            {
                return false;
            }
            VariableArray<size_t> paintGroups[PaintGroupCount];
            for (size_t i = 0; i < paints.size(); ++i)
            {
                const TerrainPaintImage& image = paints[i];
                const TerrainPaintRule& rule = recipe.Paints[i];
                if (image.Width != grid.Width || image.Height != grid.Height || image.Values.size() != count || image.Alpha.size() != count)
                {
                    return Fail(outError, "ペイント %zu は %u×%u 標本、値 %zu 個・アルファ %zu 個です。高さ格子 %u×%u と同寸法の画像を指定してください。",
                                i, image.Width, image.Height, image.Values.size(), image.Alpha.size(), grid.Width, grid.Height);
                }
                if (rule.Mode != TerrainPaintMode::Override && rule.Mode != TerrainPaintMode::Multiply)
                {
                    return Fail(outError, "ペイント %zu の mode が無効です。override または multiply を指定してください。", i);
                }
                size_t group = 0;
                switch (rule.Target)
                {
                case TerrainPaintTarget::Wetness: group = 0; break;
                case TerrainPaintTarget::DryGrass: group = 1; break;
                case TerrainPaintTarget::Road: group = 2; break;
                case TerrainPaintTarget::Rock: group = 3; break;
                case TerrainPaintTarget::GrassDensity: group = 4; break;
                case TerrainPaintTarget::Splat:
                    if (rule.SplatLayerIndex >= recipe.SplatLayers.size())
                    {
                        return Fail(outError, "ペイント %zu の splat 層 %u は層数 %zu の範囲外です。存在する層を指定してください。",
                                    i, rule.SplatLayerIndex, recipe.SplatLayers.size());
                    }
                    group = SplatPaintGroup + rule.SplatLayerIndex;
                    break;
                default:
                    return Fail(outError, "ペイント %zu の target が無効です。対応するマスクまたは splat の層を指定してください。", i);
                }
                paintGroups[group].push_back(i);
            }

            VariableArray<double> accumulation;
            ComputeFlowAccumulation(grid, accumulation);
            TerrainMaskFields result;
            result.Width = grid.Width;
            result.Height = grid.Height;
            result.MaskRgba.resize(count * 4);
            result.GrassR8.resize(count);
            const size_t layerCount = recipe.SplatLayers.size();
            if (layerCount > 0)
            {
                result.Splat0Rgba.resize(count * 4);
            }
            if (layerCount > 4)
            {
                result.Splat1Rgba.resize(count * 4);
            }
            result.SplatLayerNames.reserve(layerCount);
            for (const TerrainSplatLayerRule& layer : recipe.SplatLayers)
            {
                result.SplatLayerNames.push_back(layer.Name);
            }
            const double flowDenominator = std::log1p(recipe.Wetness.FlowRefCells);
            const double drySpacing = recipe.DryGrass.Noise.ScaleM / cellSizeM;
            const uint64_t seedHash = SplitMix64(seed);
            const uint64_t dryPrefix = SplitMix64(seedHash ^ 1u);
            double splatSpacing[MaxSplatLayers]{};
            uint64_t splatPrefix[MaxSplatLayers]{};
            for (size_t k = 0; k < layerCount; ++k)
            {
                splatSpacing[k] = recipe.SplatLayers[k].Noise.ScaleM / cellSizeM;
                splatPrefix[k] = SplitMix64(seedHash ^ static_cast<uint32_t>(16 + k));
            }
            constexpr double radiansToDegrees = 57.2957795130823208768;
            for (uint32_t z = 0; z < grid.Height; ++z)
            {
                const uint32_t prevZ = z == 0 ? z : z - 1;
                const uint32_t nextZ = z == grid.Height - 1 ? z : z + 1;
                for (uint32_t x = 0; x < grid.Width; ++x)
                {
                    const uint32_t prevX = x == 0 ? x : x - 1;
                    const uint32_t nextX = x == grid.Width - 1 ? x : x + 1;
                    const size_t index = static_cast<size_t>(z) * grid.Width + x;
                    const double h = grid.HeightsM[index];
                    const double left = grid.HeightsM[static_cast<size_t>(z) * grid.Width + prevX];
                    const double right = grid.HeightsM[static_cast<size_t>(z) * grid.Width + nextX];
                    const double top = grid.HeightsM[static_cast<size_t>(prevZ) * grid.Width + x];
                    const double bottom = grid.HeightsM[static_cast<size_t>(nextZ) * grid.Width + x];
                    const double dhdx = HeightGradient(left, right, nextX - prevX, cellSizeM);
                    const double dhdz = HeightGradient(top, bottom, nextZ - prevZ, cellSizeM);
                    const double slope = std::atan(std::sqrt(dhdx * dhdx + dhdz * dhdz)) * radiansToDegrees;
                    const double flow = std::clamp(std::log1p(accumulation[index]) / flowDenominator, 0.0, 1.0);
                    const double low = 1.0 - Ramp(h, recipe.Wetness.LowFullM, recipe.Wetness.LowZeroM);
                    const double lap = HeightLaplacian(left, right, top, bottom, h, cellSizeM);
                    const double concave = std::clamp(lap / recipe.Wetness.ConcaveFullPerM, 0.0, 1.0);
                    double wet = std::clamp(recipe.Wetness.FlowWeight * flow + recipe.Wetness.LowWeight * low +
                                            recipe.Wetness.ConcaveWeight * concave, 0.0, 1.0);
                    double dry = recipe.DryGrass.Base;
                    if (recipe.DryGrass.Noise.Amplitude != 0.0)
                    {
                        dry += recipe.DryGrass.Noise.Amplitude * (2.0 * ValueNoise(dryPrefix, x, z, drySpacing) - 1.0);
                    }
                    dry = std::clamp(dry, 0.0, 1.0);
                    double road = 0.0;
                    double rock = Ramp(slope, recipe.Rock.SlopeStartDeg, recipe.Rock.SlopeFullDeg);
                    wet = ApplyPaints(wet, index, paintGroups[0], recipe, paints);
                    dry = ApplyPaints(dry, index, paintGroups[1], recipe, paints);
                    road = ApplyPaints(road, index, paintGroups[2], recipe, paints);
                    rock = ApplyPaints(rock, index, paintGroups[3], recipe, paints);
                    if (!std::isfinite(slope) || !std::isfinite(wet) || !std::isfinite(dry) || !std::isfinite(road) || !std::isfinite(rock))
                    {
                        return Fail(outError, "標本 (x=%u, z=%u) のマスクを有限値で計算できません。高さ・標本間隔・recipe の値の範囲を見直してください。", x, z);
                    }
                    result.MaskRgba[index * 4] = Quantize(wet);
                    result.MaskRgba[index * 4 + 1] = Quantize(dry);
                    result.MaskRgba[index * 4 + 2] = Quantize(road);
                    result.MaskRgba[index * 4 + 3] = Quantize(rock);

                    if (layerCount > 0)
                    {
                        double raw[MaxSplatLayers]{};
                        for (size_t k = 0; k < layerCount; ++k)
                        {
                            const TerrainSplatLayerRule& layer = recipe.SplatLayers[k];
                            double noiseFactor = 1.0;
                            if (layer.Noise.Amplitude != 0.0)
                            {
                                noiseFactor = std::max(0.0, 1.0 + layer.Noise.Amplitude *
                                    (2.0 * ValueNoise(splatPrefix[k], x, z, splatSpacing[k]) - 1.0));
                            }
                            raw[k] = layer.Weight * Band(slope, layer.SlopeDeg) * Band(h, layer.HeightM) * Band(wet, layer.Wetness) * noiseFactor;
                            raw[k] = ApplyPaints(raw[k], index, paintGroups[SplatPaintGroup + k], recipe, paints);
                            if (!std::isfinite(raw[k]))
                            {
                                return Fail(outError, "標本 (x=%u, z=%u) の splat 層 %zu を有限値で計算できません。層の重み・雑音・ペイントの値を見直してください。", x, z, k);
                            }
                        }
                        uint8_t quantized[MaxSplatLayers]{};
                        QuantizeSplat(raw, layerCount, quantized);
                        for (size_t k = 0; k < 4; ++k)
                        {
                            result.Splat0Rgba[index * 4 + k] = quantized[k];
                        }
                        if (layerCount > 4)
                        {
                            for (size_t k = 0; k < 4; ++k)
                            {
                                result.Splat1Rgba[index * 4 + k] = quantized[k + 4];
                            }
                        }
                    }

                    const TerrainGrassDensityRule& rule = recipe.GrassDensity;
                    double grass = std::clamp(rule.Base * (1.0 - Ramp(slope, rule.SlopeStartDeg, rule.SlopeZeroDeg)) *
                        (1.0 - Ramp(wet, rule.WetStart, rule.WetZero)) * (1.0 - rule.RoadSuppress * road) *
                        (1.0 - rule.RockSuppress * rock), 0.0, 1.0);
                    grass = ApplyPaints(grass, index, paintGroups[4], recipe, paints);
                    if (!std::isfinite(grass))
                    {
                        return Fail(outError, "標本 (x=%u, z=%u) の草密度を有限値で計算できません。grass_density とペイントの値を見直してください。", x, z);
                    }
                    result.GrassR8[index] = Quantize(grass);
                }
            }
            outFields = std::move(result);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(outError, "地形マスク %u×%u の計算用メモリを確保できません。入力寸法を小さくしてください。", grid.Width, grid.Height);
        }
        catch (const std::length_error&)
        {
            return Fail(outError, "地形マスク %u×%u の計算用配列が格納上限を超えました。入力寸法を小さくしてください。", grid.Width, grid.Height);
        }
    }
} // namespace NorvesLib::Tools::AssetCook
