// チャンクの切り方に依存しない、地形標本化と散布セルの決定的な評価。
#include "ScatterPlacement.h"
#include "TerrainMasks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    using Core::Container::AnsiString;
    using Core::Container::VariableArray;
    namespace Asset = Core::Asset;

    namespace
    {
        constexpr double Pi = 3.1415926535897932384626433832795;
        constexpr double SqrtTwo = 1.4142135623730950488016887242097;

        bool Fail(AnsiString& error, const char* message)
        {
            // 呼び出し側へ診断だけを返し、外部状態には触れない。
            error = message;
            return false;
        }

        bool IsUnit(double value)
        {
            // NaN も範囲外として扱い、密度や属性への混入を防ぐ。
            return std::isfinite(value) && value >= 0.0 && value <= 1.0;
        }

        uint64_t Mix(uint64_t value)
        {
            // splitmix64 の全単射で、入力の各ビットを決定的に拡散する。
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }

        uint64_t Next(uint64_t& state)
        {
            // 呼び出し元のセル専用状態だけを進め、共有の乱数生成器を持たない。
            state += 0x9e3779b97f4a7c15ull;
            return Mix(state);
        }

        double Unit(uint64_t value)
        {
            // 上位 53 ビットは double で正確に表せるため、丸めで 1 にならない。
            return static_cast<double>(value >> 11) * 0x1.0p-53;
        }

        uint64_t SpeciesKey(const AnsiString& name)
        {
            // 種表の番号ではなく名前のバイト列を FNV-1a64 にし、表の並べ替えに依存させない。
            uint64_t hash = Asset::CookedTextureFormatV0::Fnv1a64OffsetBasis;
            for (char value : name)
            {
                hash ^= static_cast<uint8_t>(value);
                hash *= Asset::CookedTextureFormatV0::Fnv1a64Prime;
            }
            return hash;
        }

        uint64_t CellState(uint64_t seed, uint64_t species, int64_t x, int64_t z)
        {
            // 符号付き座標を合同な uint64 として畳み、負のセルでも未定義の演算を使わない。
            uint64_t state = Mix(seed + 0x9e3779b97f4a7c15ull);
            state = Mix(state ^ species);
            state = Mix(state ^ static_cast<uint64_t>(x));
            return Mix(state ^ static_cast<uint64_t>(z));
        }

        uint8_t Byte(double value)
        {
            // 色と枯れ度の量子化は、範囲を確定してから最近整数に丸める。
            return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
        }

        struct AcceptedCell
        {
            ScatterCandidate Candidate;
            uint64_t Priority = 0;
        };

        struct PlacementContext
        {
            const ScatterPlacementParams& Params;
            const ScatterSpeciesRule& Rule;
            const ScatterTerrainSampler& Terrain;
            uint64_t Species = 0;
            double CellSize = 0.0;
            double DiskArea = 0.0;
        };

        bool EvaluateCell(const PlacementContext& context, int64_t x, int64_t z,
                          AcceptedCell& out, bool& bSaturated)
        {
            // 近傍も同じ入力で評価する。チャンク番号も走査順も受理と属性に混ぜない。
            uint64_t state = CellState(context.Params.Seed, context.Species, x, z);
            const double jitterX = Unit(Next(state));
            const double jitterZ = Unit(Next(state));
            out.Priority = Next(state);
            const double acceptance = Unit(Next(state));
            const double yaw = Unit(Next(state));
            const double scale = Unit(Next(state));
            const uint64_t rank = Next(state);
            const double brightness = Unit(Next(state));
            auto& candidate = out.Candidate;
            candidate.CellX = x;
            candidate.CellZ = z;
            candidate.X = context.Params.OriginX + (static_cast<double>(x) + jitterX) * context.CellSize;
            candidate.Z = context.Params.OriginZ + (static_cast<double>(z) + jitterZ) * context.CellSize;
            const auto& rule = context.Rule;
            if (!context.Terrain.SampleHeight(candidate.X, candidate.Z, candidate.Y) ||
                !context.Terrain.SampleNormal(candidate.X, candidate.Z, candidate.Normal))
            {
                return false;
            }
            if (rule.bHeightBandEnabled && (candidate.Y < rule.HeightMinM || candidate.Y > rule.HeightMaxM))
            {
                return false;
            }
            if (rule.bSlopeBandEnabled)
            {
                const double slope = std::atan2(std::hypot(static_cast<double>(candidate.Normal[0]), static_cast<double>(candidate.Normal[2])),
                                                static_cast<double>(candidate.Normal[1])) * (180.0 / Pi);
                if (slope < rule.SlopeMinDeg || slope > rule.SlopeMaxDeg)
                {
                    return false;
                }
            }
            float value = 0.0f;
            if (rule.bExcludeRoad &&
                (!context.Terrain.SampleLayerChannel(TerrainMaskLayerFourCC, 2, candidate.X, candidate.Z, value) ||
                 value >= rule.RoadThreshold))
            {
                return false;
            }
            if (rule.bWetnessBandEnabled &&
                (!context.Terrain.SampleLayerChannel(TerrainMaskLayerFourCC, 0, candidate.X, candidate.Z, value) ||
                 value < rule.WetnessMin || value > rule.WetnessMax))
            {
                return false;
            }
            double densityFactor = rule.DensitySource.Constant;
            if (rule.DensitySource.bUseLayer)
            {
                if (!context.Terrain.SampleLayerChannel(rule.DensitySource.FourCC, rule.DensitySource.Channel,
                                                       candidate.X, candidate.Z, value))
                {
                    return false;
                }
                densityFactor = value;
            }
            const double density = rule.BaseDensityPerM2 * densityFactor;
            if (density <= 0.0)
            {
                return false;
            }
            // Matérn II の逆変換で局所最大の損失を補う。格子ジッタでは近似であり、低占有率ほど合う。
            // rho*pi*r^2 >= 1 は有限の逆変換を持たないため、受理確率の上限 1 へ直接張り付ける。
            const double occupancy = density * context.DiskArea;
            double probability = 1.0;
            if (occupancy >= 1.0)
            {
                bSaturated = true;
            }
            else
            {
                const double lambda = -std::log1p(-occupancy) / context.DiskArea;
                probability = std::min(1.0, lambda * context.CellSize * context.CellSize);
            }
            if (acceptance >= probability)
            {
                return false;
            }
            candidate.Yaw = yaw * (2.0 * Pi);
            // 上端直前の u による演算の丸めでも、半開区間のヨーを保つ。
            candidate.Yaw = std::min(candidate.Yaw, std::nextafter(2.0 * Pi, 0.0));
            candidate.Scale = std::exp(std::lerp(std::log(rule.ScaleMin), std::log(rule.ScaleMax), scale));
            candidate.Rank = static_cast<uint8_t>(rank >> 56);
            candidate.SortKey = (static_cast<uint64_t>(candidate.Rank) << 56) | (rank & 0x00ffffffffffffffull);
            const uint8_t tint = Byte(128.0 * (1.0 + rule.BrightnessVariation * (2.0 * brightness - 1.0)));
            candidate.Tint[0] = candidate.Tint[1] = candidate.Tint[2] = tint;
            candidate.Dry = 0;
            if (rule.bUseDryGrass)
            {
                if (!context.Terrain.SampleLayerChannel(TerrainMaskLayerFourCC, 1, candidate.X, candidate.Z, value))
                {
                    return false;
                }
                candidate.Dry = Byte(static_cast<double>(value) * 255.0);
            }
            return true;
        }

        bool IsHigher(const AcceptedCell& other, const AcceptedCell& current)
        {
            // 優先度の衝突にもセル座標の辞書順を使い、常にただ一つの全順序を定める。
            if (other.Priority != current.Priority)
            {
                return other.Priority > current.Priority;
            }
            if (other.Candidate.CellX != current.Candidate.CellX)
            {
                return other.Candidate.CellX > current.Candidate.CellX;
            }
            return other.Candidate.CellZ > current.Candidate.CellZ;
        }

        bool Validate(const ScatterPlacementParams& params, const ScatterSpeciesRule& rule,
                      const ScatterTerrainSampler& terrain, double spacing, AnsiString& error)
        {
            // 不正値と必須層の欠落を走査の前に拒否し、部分的な成功を返さない。
            if (!terrain.IsInitialized())
            {
                return Fail(error, "散布の地形標本器が初期化されていません");
            }
            if (!std::isfinite(params.ChunkSizeM) || params.ChunkSizeM <= 0.0 ||
                !std::isfinite(params.OriginX) || !std::isfinite(params.OriginZ) ||
                params.OriginX != terrain.GetMinX() || params.OriginZ != terrain.GetMinZ())
            {
                return Fail(error, "散布のチャンク寸法は有限な正値、格子原点は地形の原点である必要があります");
            }
            if (rule.Name.empty() || (rule.RenderPath != Asset::ScatterRenderPath::Mega && rule.RenderPath != Asset::ScatterRenderPath::Foliage) ||
                !std::isfinite(rule.MeshRadiusM) || rule.MeshRadiusM < 0.0 ||
                !std::isfinite(rule.MinSpacingM) || rule.MinSpacingM < 0.0 ||
                !std::isfinite(spacing) || spacing <= 0.0 ||
                !std::isfinite(rule.ScaleMin) || !std::isfinite(rule.ScaleMax) || rule.ScaleMin <= 0.0 || rule.ScaleMax < rule.ScaleMin ||
                !std::isfinite(rule.BaseDensityPerM2) || rule.BaseDensityPerM2 < 0.0 ||
                !IsUnit(rule.SlopeFollow) || !IsUnit(rule.BrightnessVariation) || !std::isfinite(rule.WindStiffness) || rule.WindStiffness < 0.0)
            {
                return Fail(error, "散布の種名・描画経路・半径・間隔・拡大・密度・属性の範囲が不正です");
            }
            if ((rule.bHeightBandEnabled && (!std::isfinite(rule.HeightMinM) || !std::isfinite(rule.HeightMaxM) || rule.HeightMinM > rule.HeightMaxM)) ||
                (rule.bSlopeBandEnabled && (!std::isfinite(rule.SlopeMinDeg) || !std::isfinite(rule.SlopeMaxDeg) ||
                                           rule.SlopeMinDeg < 0.0 || rule.SlopeMaxDeg > 90.0 || rule.SlopeMinDeg > rule.SlopeMaxDeg)) ||
                (rule.bExcludeRoad && !IsUnit(rule.RoadThreshold)) ||
                (rule.bWetnessBandEnabled && (!IsUnit(rule.WetnessMin) || !IsUnit(rule.WetnessMax) || rule.WetnessMin > rule.WetnessMax)))
            {
                return Fail(error, "散布の標高・傾斜・道・湿りの条件が不正です");
            }
            if (rule.DensitySource.bUseLayer)
            {
                if (!terrain.HasLayerChannel(rule.DensitySource.FourCC, rule.DensitySource.Channel))
                {
                    return Fail(error, "散布の密度に指定した地形の層またはチャンネルがありません");
                }
            }
            else if (!IsUnit(rule.DensitySource.Constant))
            {
                return Fail(error, "散布の定数の密度倍率は 0 から 1 の有限値である必要があります");
            }
            if ((rule.bExcludeRoad && !terrain.HasLayerChannel(TerrainMaskLayerFourCC, 2)) ||
                (rule.bWetnessBandEnabled && !terrain.HasLayerChannel(TerrainMaskLayerFourCC, 0)) ||
                (rule.bUseDryGrass && !terrain.HasLayerChannel(TerrainMaskLayerFourCC, 1)))
            {
                return Fail(error, "散布の除外または枯れ度の参照に必要な MSK0 のチャンネルがありません");
            }
            return true;
        }
    } // namespace

    bool ScatterTerrainSampler::Initialize(const Asset::CookedTerrainData& terrain, AnsiString& error)
    {
        // 地形を値として保持するので、初期化元の構造体を破棄しても結果と寿命は変わらない。
        m_bInitialized = false;
        m_Layers.clear();
        m_Terrain = {};
        error.clear();
        if (terrain.CellCountX == 0 || terrain.CellCountZ == 0 || terrain.TileCells == 0 || terrain.TileCells == UINT32_MAX ||
            terrain.CellCountX % terrain.TileCells != 0 || terrain.CellCountZ % terrain.TileCells != 0 ||
            terrain.TileCountX != terrain.CellCountX / terrain.TileCells || terrain.TileCountZ != terrain.CellCountZ / terrain.TileCells ||
            static_cast<uint64_t>(terrain.TileCountX) * terrain.TileCountZ != terrain.Tiles.size() ||
            terrain.TileIndexByCoord.size() != terrain.Tiles.size() ||
            !std::isfinite(terrain.CellSizeM) || terrain.CellSizeM <= 0.0f ||
            !std::isfinite(terrain.OriginX) || !std::isfinite(terrain.OriginY) || !std::isfinite(terrain.OriginZ) ||
            !std::isfinite(terrain.HeightMinM) || !std::isfinite(terrain.HeightMaxM) || terrain.HeightMinM > terrain.HeightMaxM ||
            !std::isfinite(static_cast<float>(terrain.OriginY + terrain.HeightMinM)) ||
            !std::isfinite(static_cast<float>(terrain.OriginY + terrain.HeightMaxM)))
        {
            return Fail(error, "散布に使う地形の格子・タイル・原点・高さの写像が不正です");
        }
        m_Terrain = terrain;
        if (!std::isfinite(GetMaxX()) || !std::isfinite(GetMaxZ()) || GetMaxX() <= GetMinX() || GetMaxZ() <= GetMinZ())
        {
            return Fail(error, "散布に使う地形の世界座標の範囲が不正です");
        }
        const int32_t heightLayer = terrain.FindLayer(Asset::CookedTerrainFormatV0::HeightLayerFourCC);
        if (heightLayer < 0)
        {
            return Fail(error, "散布に使う地形に HGT0 がありません");
        }
        const size_t side = terrain.GetSamplesPerSide();
        if (side > std::numeric_limits<size_t>::max() / side / 4)
        {
            return Fail(error, "散布に使う地形のタイル標本数が大きすぎます");
        }
        for (uint32_t tz = 0; tz < terrain.TileCountZ; ++tz)
        {
            for (uint32_t tx = 0; tx < terrain.TileCountX; ++tx)
            {
                uint32_t tileIndex = 0;
                if (!terrain.FindTile(tx, tz, &tileIndex) ||
                    terrain.GetLayerBlob(tileIndex, static_cast<uint32_t>(heightLayer)).GetSize() != side * side * 2)
                {
                    return Fail(error, "散布に使う地形のタイルまたは HGT0 の標本が欠けています");
                }
            }
        }
        for (uint32_t layerIndex = 0; layerIndex < terrain.Layers.size(); ++layerIndex)
        {
            const auto& source = terrain.Layers[layerIndex];
            if (source.FourCC == Asset::CookedTerrainFormatV0::HeightLayerFourCC)
            {
                continue;
            }
            Layer layer;
            layer.FourCC = source.FourCC;
            switch (source.PixelFormat)
            {
            case Asset::CookedTexturePixelFormat::R8UNorm:
                layer.Channels = 1;
                break;
            case Asset::CookedTexturePixelFormat::RG8UNorm:
                layer.Channels = 2;
                break;
            case Asset::CookedTexturePixelFormat::RGBA8UNorm:
                layer.Channels = 4;
                break;
            default:
                return Fail(error, "散布の地形の層は非圧縮の R8・RG8・RGBA8 である必要があります");
            }
            layer.Textures.reserve(terrain.Tiles.size());
            for (uint32_t tileIndex = 0; tileIndex < terrain.Tiles.size(); ++tileIndex)
            {
                auto parsed = Asset::ParseCookedTexture(terrain.GetLayerBlob(tileIndex, layerIndex));
                if (!parsed.Succeeded() || parsed.Texture.PixelFormat != source.PixelFormat ||
                    parsed.Texture.ColorSpace != Asset::CookedTextureColorSpace::Linear || parsed.Texture.LayerCount != 1 ||
                    parsed.Texture.Width != side || parsed.Texture.Height != side ||
                    parsed.Texture.GetMipBytes(0).size() != side * side * layer.Channels)
                {
                    return Fail(error, "散布に使う地形の層をセル角格子の linear NVTEX として解析できません");
                }
                layer.Textures.push_back(std::move(parsed.Texture));
            }
            m_Layers.push_back(std::move(layer));
        }
        m_bInitialized = true;
        return true;
    }

    bool ScatterTerrainSampler::Locate(double x, double z, uint32_t& tileIndex,
                                       uint32_t& column, uint32_t& row, double& fu, double& fv) const
    {
        // 境界は正側のセル、外周だけ最後のセルを選び、高さと層のタイル選択を一致させる。
        if (!m_bInitialized || !std::isfinite(x) || !std::isfinite(z) ||
            x < GetMinX() || x > GetMaxX() || z < GetMinZ() || z > GetMaxZ())
        {
            return false;
        }
        const double gx = std::clamp((x - GetMinX()) / m_Terrain.CellSizeM, 0.0, static_cast<double>(m_Terrain.CellCountX));
        const double gz = std::clamp((z - GetMinZ()) / m_Terrain.CellSizeM, 0.0, static_cast<double>(m_Terrain.CellCountZ));
        column = static_cast<uint32_t>(std::min(std::floor(gx), static_cast<double>(m_Terrain.CellCountX - 1)));
        row = static_cast<uint32_t>(std::min(std::floor(gz), static_cast<double>(m_Terrain.CellCountZ - 1)));
        fu = gx - column;
        fv = gz - row;
        return m_Terrain.FindTile(column / m_Terrain.TileCells, row / m_Terrain.TileCells, &tileIndex) != nullptr;
    }

    bool ScatterTerrainSampler::SampleSurface(double x, double z, double& y, float (&normal)[3]) const
    {
        // LOD0 の対角線はタイル内番号ではなく全体のセル番号の偶奇で決める。
        uint32_t tileIndex = 0, column = 0, row = 0;
        double fu = 0.0, fv = 0.0;
        if (!Locate(x, z, tileIndex, column, row, fu, fv))
        {
            return false;
        }
        const uint32_t sx = column % m_Terrain.TileCells, sz = row % m_Terrain.TileCells;
        const auto at = [&](uint32_t dx, uint32_t dz)
        {
            // メッシュの頂点と同じく OriginY を加えた後 float に丸める。
            return static_cast<double>(static_cast<float>(m_Terrain.OriginY +
                m_Terrain.DecodeHeightM(m_Terrain.GetHeightSample(tileIndex, sx + dx, sz + dz))));
        };
        const double h00 = at(0, 0), h10 = at(1, 0), h01 = at(0, 1), h11 = at(1, 1);
        double base = h00, dx = 0.0, dz = 0.0;
        if (((column + row) & 1u) == 0)
        {
            if (fu >= fv)
            {
                dx = h10 - h00;
                dz = h11 - h10;
            }
            else
            {
                dx = h11 - h01;
                dz = h01 - h00;
            }
        }
        else if (fu + fv <= 1.0)
        {
            dx = h10 - h00;
            dz = h01 - h00;
        }
        else
        {
            base = h10 + h01 - h11;
            dx = h11 - h01;
            dz = h11 - h10;
        }
        y = base + dx * fu + dz * fv;
        const double nx = -dx / m_Terrain.CellSizeM, nz = -dz / m_Terrain.CellSizeM;
        const double length = std::hypot(nx, 1.0, nz);
        if (!std::isfinite(y) || !std::isfinite(length))
        {
            return false;
        }
        normal[0] = static_cast<float>(nx / length);
        normal[1] = static_cast<float>(1.0 / length);
        normal[2] = static_cast<float>(nz / length);
        return true;
    }

    bool ScatterTerrainSampler::SampleHeight(double x, double z, double& y) const
    {
        // 高さと法線を共通の面選択から求め、対角線上でも規則がずれないようにする。
        float normal[3];
        return SampleSurface(x, z, y, normal);
    }

    bool ScatterTerrainSampler::SampleNormal(double x, double z, float (&normal)[3]) const
    {
        // 同じ面の勾配から上向きの法線を求め、周囲の標本や走査順には依存しない。
        double y = 0.0;
        return SampleSurface(x, z, y, normal);
    }

    bool ScatterTerrainSampler::SampleLayerChannel(Asset::AssetPackageFourCC fourCC, uint8_t channel,
                                                   double x, double z, float& value) const
    {
        // 初期化済みの層のセル角だけを読み、共有辺を含めて双一次で連続に補間する。
        uint32_t tileIndex = 0, column = 0, row = 0;
        double fu = 0.0, fv = 0.0;
        if (!Locate(x, z, tileIndex, column, row, fu, fv))
        {
            return false;
        }
        for (const auto& layer : m_Layers)
        {
            if (layer.FourCC != fourCC || channel >= layer.Channels)
            {
                continue;
            }
            const auto pixels = layer.Textures[tileIndex].GetMipBytes(0);
            const size_t side = m_Terrain.GetSamplesPerSide();
            const size_t sx = column % m_Terrain.TileCells, sz = row % m_Terrain.TileCells;
            const auto at = [&](size_t dx, size_t dz)
            {
                // 正規化は補間の後に行い、途中で 8bit へ丸めない。
                return static_cast<double>(pixels[((sz + dz) * side + sx + dx) * layer.Channels + channel]);
            };
            const double top = std::lerp(at(0, 0), at(1, 0), fu);
            const double bottom = std::lerp(at(0, 1), at(1, 1), fu);
            value = static_cast<float>(std::clamp(std::lerp(top, bottom, fv) / 255.0, 0.0, 1.0));
            return true;
        }
        return false;
    }

    bool ScatterTerrainSampler::HasLayerChannel(Asset::AssetPackageFourCC fourCC, uint8_t channel) const
    {
        // 全タイルで検査済みのチャンネル数だけを問い合わせる。
        if (!m_bInitialized)
        {
            return false;
        }
        for (const auto& layer : m_Layers)
        {
            if (layer.FourCC == fourCC && channel < layer.Channels)
            {
                return true;
            }
        }
        return false;
    }

    bool ScatterTerrainSampler::IsInitialized() const
    {
        // 初期化が途中で失敗した場合も使用可能と見なさない。
        return m_bInitialized;
    }

    double ScatterTerrainSampler::GetMinX() const
    {
        // 地形原点は散布格子とチャンクの共通の基準である。
        return m_Terrain.OriginX;
    }

    double ScatterTerrainSampler::GetMaxX() const
    {
        // 外周の頂点までを標本化可能な範囲に含める。
        return m_Terrain.OriginX + static_cast<double>(m_Terrain.CellCountX) * m_Terrain.CellSizeM;
    }

    double ScatterTerrainSampler::GetMinZ() const
    {
        // Z も X と同じ原点規則を使う。
        return m_Terrain.OriginZ;
    }

    double ScatterTerrainSampler::GetMaxZ() const
    {
        // タイルの数でなく全体のセル数から外周を求める。
        return m_Terrain.OriginZ + static_cast<double>(m_Terrain.CellCountZ) * m_Terrain.CellSizeM;
    }

    double ResolveScatterMinSpacing(const ScatterSpeciesRule& rule)
    {
        // 個体の最大の直径を既定とし、明示した最小間隔はそのまま使う。
        return rule.MinSpacingM == 0.0 ? 2.0 * rule.MeshRadiusM * rule.ScaleMax : rule.MinSpacingM;
    }

    bool PlaceScatterChunk(const ScatterPlacementParams& params, const ScatterSpeciesRule& rule,
                           const ScatterTerrainSampler& terrain, int32_t chunkX, int32_t chunkZ,
                           VariableArray<ScatterCandidate>& out, ScatterPlacementStats* stats, AnsiString& error)
    {
        // 受理と局所最大はチャンクに依存しない。ここでは半開区間による所有と並び順だけを決める。
        out.clear();
        error.clear();
        if (stats)
        {
            *stats = {};
        }
        const double spacing = ResolveScatterMinSpacing(rule);
        if (!Validate(params, rule, terrain, spacing, error))
        {
            return false;
        }
        const double cellSize = spacing / SqrtTwo;
        const double diskArea = Pi * spacing * spacing;
        if (!std::isfinite(diskArea) || diskArea <= 0.0 || cellSize * cellSize <= 0.0)
        {
            return Fail(error, "散布の最小間隔の面積が数値の範囲外です");
        }
        // 隣同士は同じ整数境界式を使い、丸めによる隙間や重複を作らない。
        const double firstX = params.OriginX + static_cast<double>(chunkX) * params.ChunkSizeM;
        const double firstZ = params.OriginZ + static_cast<double>(chunkZ) * params.ChunkSizeM;
        const double lastX = params.OriginX + (static_cast<double>(chunkX) + 1.0) * params.ChunkSizeM;
        const double lastZ = params.OriginZ + (static_cast<double>(chunkZ) + 1.0) * params.ChunkSizeM;
        if (!std::isfinite(firstX) || !std::isfinite(firstZ) || !std::isfinite(lastX) || !std::isfinite(lastZ) ||
            lastX <= firstX || lastZ <= firstZ)
        {
            return Fail(error, "散布のチャンクの世界座標の範囲が不正です");
        }
        const double minX = std::max(firstX, terrain.GetMinX()), maxX = std::min(lastX, terrain.GetMaxX());
        const double minZ = std::max(firstZ, terrain.GetMinZ()), maxZ = std::min(lastZ, terrain.GetMaxZ());
        if (minX >= maxX || minZ >= maxZ || rule.BaseDensityPerM2 == 0.0)
        {
            return true;
        }
        const double beginX = std::floor((minX - params.OriginX) / cellSize);
        const double beginZ = std::floor((minZ - params.OriginZ) / cellSize);
        // 上辺に接するセルも調べ、最終的な半開区間の判定で所有を決める。
        const double endX = std::floor((maxX - params.OriginX) / cellSize);
        const double endZ = std::floor((maxZ - params.OriginZ) / cellSize);
        // 隣の +/-2 とループの加算に余裕を残す、int64 に安全に変換できる double の上限。
        constexpr double CellLimit = 9223372036854774784.0;
        if (!std::isfinite(beginX) || !std::isfinite(beginZ) || !std::isfinite(endX) || !std::isfinite(endZ) ||
            beginX < -CellLimit || beginZ < -CellLimit || endX > CellLimit || endZ > CellLimit)
        {
            return Fail(error, "散布のセル座標が int64 の範囲外です");
        }
        const int64_t bx = static_cast<int64_t>(beginX), bz = static_cast<int64_t>(beginZ);
        const int64_t ex = static_cast<int64_t>(endX), ez = static_cast<int64_t>(endZ);
        const uint64_t countX = static_cast<uint64_t>(ex - bx) + 1, countZ = static_cast<uint64_t>(ez - bz) + 1;
        if (countX > UINT64_MAX / countZ)
        {
            return Fail(error, "散布の調査セル数が uint64 の範囲外です");
        }
        PlacementContext context{params, rule, terrain, SpeciesKey(rule.Name), cellSize, diskArea};
        ScatterPlacementStats result;
        const double spacingSquared = spacing * spacing;
        for (int64_t z = bz; z <= ez; ++z)
        {
            for (int64_t x = bx; x <= ex; ++x)
            {
                ++result.ExaminedCells;
                AcceptedCell current;
                if (!EvaluateCell(context, x, z, current, result.bDensitySaturated))
                {
                    continue;
                }
                const auto& candidate = current.Candidate;
                if (candidate.X < firstX || candidate.X >= lastX || candidate.Z < firstZ || candidate.Z >= lastZ)
                {
                    continue;
                }
                ++result.AcceptedCount;
                bool bDominated = false;
                // 5x5 の外では少なくとも一軸のセル番号差が 3。セル間の最小距離は
                // 2*cell = sqrt(2)*r >= r なので、半径 r 未満の競合は必ず +/-2 に収まる。
                for (int64_t dz = -2; dz <= 2 && !bDominated; ++dz)
                {
                    for (int64_t dx = -2; dx <= 2; ++dx)
                    {
                        if (dx == 0 && dz == 0)
                        {
                            continue;
                        }
                        AcceptedCell other;
                        if (!EvaluateCell(context, x + dx, z + dz, other, result.bDensitySaturated) || !IsHigher(other, current))
                        {
                            continue;
                        }
                        const double deltaX = other.Candidate.X - candidate.X;
                        const double deltaZ = other.Candidate.Z - candidate.Z;
                        if (deltaX * deltaX + deltaZ * deltaZ < spacingSquared)
                        {
                            bDominated = true;
                            break;
                        }
                    }
                }
                if (bDominated)
                {
                    ++result.ThinnedCount;
                }
                else
                {
                    out.push_back(candidate);
                }
            }
        }
        std::sort(out.begin(), out.end(), [](const ScatterCandidate& a, const ScatterCandidate& b)
        {
            // Rank の接頭辞とセル座標までを比較し、走査順に依存しない出力順を確定する。
            if (a.Rank != b.Rank)
            {
                return a.Rank < b.Rank;
            }
            if (a.SortKey != b.SortKey)
            {
                return a.SortKey < b.SortKey;
            }
            if (a.CellX != b.CellX)
            {
                return a.CellX < b.CellX;
            }
            return a.CellZ < b.CellZ;
        });
        if (stats)
        {
            *stats = result;
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
