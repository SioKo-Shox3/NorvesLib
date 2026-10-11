#include "GameModes/MegaBananas/BananaLayout.h"

#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Quaternion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;

namespace Game::GameModes
{

    namespace
    {
        // 敷き詰め: 互いに kPoissonRadius 以上離れた点（ポアソン円盤）に置く。バナナは長さ約 0.2 m・反りで横にも張り出し、
        // 中心から 0.125 m ほどの円に収まるので、この間隔なら向きと大きさをばらしても重ならない。升目の並びは出ない
        constexpr float kPoissonRadius = 0.25f;
        // 矩形の縁から内側へ、この距離をかけて密度を 0 から 1 へ上げる（縁が直線に見えないようにする）
        constexpr float kFieldTaperMeters = 3.5f;
        // 縁の位置をゆらす幅と、そのゆらぎの空間の周期（縁が 1 本の直線に見えないようにする）
        constexpr float kFieldEdgeWobbleMeters = 5.0f;
        constexpr float kFieldEdgeNoiseCellMeters = 6.0f;
        // 敷き詰めの矩形の奥行きに対する幅の比
        constexpr float kFieldAspect = 2.2f;
        // 本数から矩形の面積を見積もるときの 1 平方メートルあたりの本数（足りなければ広げて作り直す）
        constexpr float kFieldDensityGuess = 9.0f;
        constexpr int kFieldMaxRetries = 16;
        // 敷き詰めの手前の端の z（カメラの後ろまで敷き、手前の縁の間引きを画面に出さない）
        constexpr float kFieldNearZ = -16.0f;
        // 山（塚）: 中心から正規分布で落とし、置いたバナナの形の上に載せる。物理は使わず、高さの格子へ
        // 「バナナの頂点の最大の高さ」を書き込み、次のバナナは自分の下面が格子に初めて触れる高さに置く
        constexpr uint32_t kPileBananas = 260;
        constexpr int kPileCount = 3;
        // 山の中心（床の x・z）。塚の裾の半径は約 0.85 m で、互いに重ならない間隔
        constexpr float kPileCenters[kPileCount][2] = {{-2.3f, 2.5f}, {0.2f, 3.5f}, {2.7f, 2.3f}};
        // 落とす位置の標準偏差と、落とす位置の最大半径（標準偏差の倍数）
        constexpr float kPileSigmaMeters = 0.30f;
        constexpr float kPileMaxSigmas = 2.4f;
        // 1 本ごとに試す落とし位置の数。最も低く収まる位置を採る（くぼみが埋まり、裾が広がりすぎない程度）
        constexpr int kPileCandidates = 3;
        // 高さの格子: 山の中心から片側 kPileGridHalfMeters の範囲を kPileGridCellMeters 刻み
        constexpr float kPileGridHalfMeters = 1.4f;
        constexpr float kPileGridCellMeters = 0.005f;
        // 山の周りに敷き詰めを空ける半径（塚の裾 + 余白）
        constexpr float kPileExcludeRadius = 1.0f;
        // 山に使う本数の上限（全体の本数の 1/4 まで）
        constexpr uint32_t kMaxPileShareDivisor = 4;
        // 形の高さを調べるために使うバナナの頂点の数の上限（間引いて使う）。落とし位置の比較は粗い点（速い）で、
        // 置く高さの確定と格子への書き込みは密な点（細い茎の先も拾う）で行う
        constexpr uint32_t kMaxSamplePoints = 6000;
        constexpr uint32_t kMaxDensePoints = 24000;
        // 床へわずかに沈める深さ（接地の隙間を隠す）。山の上段も同じ量だけ下の形へ沈める
        constexpr float kSinkMeters = 0.002f;
        // 向き・大きさのばらつき
        constexpr float kScaleMin = 0.92f;
        constexpr float kScaleMax = 1.08f;
        constexpr float kPi = 3.14159265358979323846f;

        // 整数のハッシュ（配置の乱数。番号だけで決まり、実行ごとに同じ）
        uint32_t HashIndex(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        float UnitFloat(uint32_t hash)
        {
            return static_cast<float>(hash & 0xFFFFFFu) / 16777216.0f;
        }

        /// 床に横たえたバナナの回転。長手（ローカル Y）を水平にし、反りの面も水平にして、太さ（ローカル X）を縦にする
        Math::Quaternion MakeBananaRotation(float yaw)
        {
            const Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
            const Math::Vector3 zAxis(0.0f, 0.0f, 1.0f);
            const Math::Quaternion roll(zAxis, 0.5f * kPi);
            const Math::Quaternion turn(yAxis, yaw);
            // 描画側は行ベクトル規約なので、先に倒して（roll）から水平に回す（turn）には roll * turn と書く。
            // 逆順だと長手が全個体で (1,0,0) に固定され、乱数の角度が反りを上下へ回して床へ食い込む
            return roll * turn;
        }

        /// バナナのローカルの点を、倍率と向きを掛けたワールドの差分（位置は含まない）へ変える。
        /// 描画側は行ベクトル規約で、回転は共役（逆回転）として点に作用する
        void OrientSamples(const VariableArray<Math::Vector3> &local, float yaw, float scale, VariableArray<Math::Vector3> &out)
        {
            const Math::Quaternion rotation = MakeBananaRotation(yaw);
            const Math::Quaternion inverse(-rotation.x, -rotation.y, -rotation.z, rotation.w);
            // 点ごとに四元数を掛けると遅いので、基底を 1 回回して 3×3 の列にする
            const Math::Vector3 columnX = inverse * Math::Vector3(scale, 0.0f, 0.0f);
            const Math::Vector3 columnY = inverse * Math::Vector3(0.0f, scale, 0.0f);
            const Math::Vector3 columnZ = inverse * Math::Vector3(0.0f, 0.0f, scale);
            out.resize(local.size());
            for (size_t index = 0; index < local.size(); ++index)
            {
                const Math::Vector3 &point = local[index];
                out[index] = Math::Vector3(columnX.x * point.x + columnY.x * point.y + columnZ.x * point.z,
                                           columnX.y * point.x + columnY.y * point.y + columnZ.y * point.z,
                                           columnX.z * point.x + columnY.z * point.y + columnZ.z * point.z);
            }
        }

        /// 1 本のバナナを散布の入力へ足す。位置はバナナの原点（中心）の世界座標（散布の所有者は原点に置く）
        void AddBanana(Container::VariableArray<Component::MegaScatterInstance> &instances, float x, float y, float z, float yaw,
                       float scale)
        {
            Component::MegaScatterInstance instance;
            instance.Position = Math::Vector3(x, y, z);
            instance.Rotation = MakeBananaRotation(yaw);
            instance.Scale = Math::Vector3(scale, scale, scale);
            instances.push_back(instance);
        }

        /// 疑似乱数。番号だけで決まり、実行ごとに同じ並びになる
        struct PlacementRandom
        {
            uint32_t Counter = 0;

            float Next()
            {
                return UnitFloat(HashIndex(Counter++));
            }
        };

        /// 2 次元の値ノイズ（0〜1。格子点の乱数を滑らかに補間する）
        float ValueNoise(float x, float z, float cellMeters)
        {
            const float gridX = x / cellMeters;
            const float gridZ = z / cellMeters;
            const float floorX = std::floor(gridX);
            const float floorZ = std::floor(gridZ);
            const float fractionX = gridX - floorX;
            const float fractionZ = gridZ - floorZ;
            const auto lattice = [](float ix, float iz)
            {
                return UnitFloat(HashIndex(static_cast<uint32_t>(static_cast<int32_t>(ix)) * 73856093u ^
                                           static_cast<uint32_t>(static_cast<int32_t>(iz)) * 19349663u ^ 0x9e3779b9u));
            };
            const float smoothX = fractionX * fractionX * (3.0f - 2.0f * fractionX);
            const float smoothZ = fractionZ * fractionZ * (3.0f - 2.0f * fractionZ);
            const float near0 = lattice(floorX, floorZ) * (1.0f - smoothX) + lattice(floorX + 1.0f, floorZ) * smoothX;
            const float near1 =
                lattice(floorX, floorZ + 1.0f) * (1.0f - smoothX) + lattice(floorX + 1.0f, floorZ + 1.0f) * smoothX;
            return near0 * (1.0f - smoothZ) + near1 * smoothZ;
        }

        struct FieldPoint
        {
            float X = 0.0f;
            float Z = 0.0f;
        };

        /// 矩形の中に、互いに radius 以上離れた点を敷く（Bridson 法のポアソン円盤）
        void GeneratePoissonDisk(float minX, float minZ, float width, float depth, float radius, PlacementRandom &random,
                                 VariableArray<FieldPoint> &outPoints)
        {
            outPoints.clear();
            const float cell = radius / 1.41421356f;
            const int gridWidth = static_cast<int>(std::ceil(width / cell)) + 1;
            const int gridDepth = static_cast<int>(std::ceil(depth / cell)) + 1;
            VariableArray<int32_t> grid(static_cast<size_t>(gridWidth) * static_cast<size_t>(gridDepth), -1);
            VariableArray<uint32_t> active;

            const auto isFree = [&](float x, float z)
            {
                const int cellX = static_cast<int>((x - minX) / cell);
                const int cellZ = static_cast<int>((z - minZ) / cell);
                for (int dz = -2; dz <= 2; ++dz)
                {
                    const int gz = cellZ + dz;
                    if (gz < 0 || gz >= gridDepth)
                    {
                        continue;
                    }
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const int gx = cellX + dx;
                        if (gx < 0 || gx >= gridWidth)
                        {
                            continue;
                        }
                        const int32_t other = grid[static_cast<size_t>(gz) * static_cast<size_t>(gridWidth) + static_cast<size_t>(gx)];
                        if (other < 0)
                        {
                            continue;
                        }
                        const float diffX = outPoints[static_cast<size_t>(other)].X - x;
                        const float diffZ = outPoints[static_cast<size_t>(other)].Z - z;
                        if (diffX * diffX + diffZ * diffZ < radius * radius)
                        {
                            return false;
                        }
                    }
                }
                return true;
            };
            const auto insert = [&](float x, float z)
            {
                const uint32_t index = static_cast<uint32_t>(outPoints.size());
                outPoints.push_back(FieldPoint{x, z});
                const int cellX = static_cast<int>((x - minX) / cell);
                const int cellZ = static_cast<int>((z - minZ) / cell);
                grid[static_cast<size_t>(cellZ) * static_cast<size_t>(gridWidth) + static_cast<size_t>(cellX)] =
                    static_cast<int32_t>(index);
                active.push_back(index);
            };

            insert(minX + width * random.Next(), minZ + depth * random.Next());
            while (!active.empty())
            {
                const size_t pick = std::min(active.size() - 1, static_cast<size_t>(random.Next() * static_cast<float>(active.size())));
                const FieldPoint origin = outPoints[active[pick]];
                bool bFound = false;
                for (int attempt = 0; attempt < 24 && !bFound; ++attempt)
                {
                    const float angle = 2.0f * kPi * random.Next();
                    const float distance = radius * (1.0f + random.Next());
                    const float x = origin.X + std::cos(angle) * distance;
                    const float z = origin.Z + std::sin(angle) * distance;
                    if (x < minX || x >= minX + width || z < minZ || z >= minZ + depth || !isFree(x, z))
                    {
                        continue;
                    }
                    insert(x, z);
                    bFound = true;
                }
                if (!bFound)
                {
                    active[pick] = active.back();
                    active.pop_back();
                }
            }
        }

        /// 敷き詰める fieldCount 個の位置を作る。山の周りは空け、矩形の縁は密度を落とし、足りなければ矩形を広げて作り直す
        void BuildFieldPoints(uint32_t fieldCount, const IBananaFieldMask *pMask, PlacementRandom &random,
                              VariableArray<FieldPoint> &outPoints)
        {
            float area = static_cast<float>(fieldCount) / kFieldDensityGuess + 15.0f;
            VariableArray<FieldPoint> candidates;
            for (int retry = 0; retry <= kFieldMaxRetries; ++retry)
            {
                const float width = std::sqrt(area * kFieldAspect);
                const float depth = area / width;
                const float minX = -0.5f * width;
                GeneratePoissonDisk(minX, kFieldNearZ, width, depth, kPoissonRadius, random, candidates);

                outPoints.clear();
                for (const FieldPoint &point : candidates)
                {
                    bool bUnderPile = false;
                    for (int pile = 0; pile < kPileCount; ++pile)
                    {
                        const float diffX = point.X - kPileCenters[pile][0];
                        const float diffZ = point.Z - kPileCenters[pile][1];
                        if (diffX * diffX + diffZ * diffZ < kPileExcludeRadius * kPileExcludeRadius)
                        {
                            bUnderPile = true;
                            break;
                        }
                    }
                    if (bUnderPile)
                    {
                        continue;
                    }
                    // マスクが塞いだ位置（mega-stress の Entity の足元）は空ける
                    if (pMask != nullptr && pMask->IsBlocked(point.X, point.Z))
                    {
                        continue;
                    }
                    // 縁から内側へ向けて、確率で間引く（滑らかなステップ）
                    const float edge = std::min(std::min(point.X - minX, minX + width - point.X),
                                                std::min(point.Z - kFieldNearZ, kFieldNearZ + depth - point.Z));
                    // 縁の位置を低周波のノイズでゆらし、境目が 1 本の直線に見えないようにする
                    const float wobble = kFieldEdgeWobbleMeters * ValueNoise(point.X, point.Z, kFieldEdgeNoiseCellMeters);
                    const float t = std::clamp((edge - wobble) / kFieldTaperMeters, 0.0f, 1.0f);
                    if (random.Next() < t * t * (3.0f - 2.0f * t))
                    {
                        outPoints.push_back(point);
                    }
                }
                if (outPoints.size() >= fieldCount)
                {
                    break;
                }
                const float shortage = static_cast<float>(fieldCount) / static_cast<float>(std::max<size_t>(1, outPoints.size()));
                area *= std::max(1.03f, shortage * 1.04f);
            }

            // 余りは乱数でシャッフルして先頭から使い、残りを捨てる（密度が一様なまま本数を合わせる）
            for (size_t index = outPoints.size(); index > 1; --index)
            {
                const size_t other = std::min(index - 1, static_cast<size_t>(random.Next() * static_cast<float>(index)));
                std::swap(outPoints[index - 1], outPoints[other]);
            }
            if (outPoints.size() > fieldCount)
            {
                outPoints.resize(fieldCount);
            }
        }

        /// 山の範囲の高さの格子。置いたバナナの頂点の最大の高さを書き込み、床は 0
        struct PileHeightField
        {
            float OriginX = 0.0f;
            float OriginZ = 0.0f;
            int Size = 0;
            VariableArray<float> Heights;

            PileHeightField(float centerX, float centerZ)
            {
                Size = static_cast<int>(2.0f * kPileGridHalfMeters / kPileGridCellMeters);
                OriginX = centerX - kPileGridHalfMeters;
                OriginZ = centerZ - kPileGridHalfMeters;
                Heights.assign(static_cast<size_t>(Size) * static_cast<size_t>(Size), 0.0f);
            }

            /// 範囲の中の格子の番号を返せたら true
            bool CellIndex(float x, float z, size_t &outIndex) const
            {
                const int cellX = static_cast<int>(std::floor((x - OriginX) / kPileGridCellMeters));
                const int cellZ = static_cast<int>(std::floor((z - OriginZ) / kPileGridCellMeters));
                if (cellX < 0 || cellX >= Size || cellZ < 0 || cellZ >= Size)
                {
                    return false;
                }
                outIndex = static_cast<size_t>(cellZ) * static_cast<size_t>(Size) + static_cast<size_t>(cellX);
                return true;
            }

            /// 範囲外は 0（床）
            float Sample(float x, float z) const
            {
                size_t index = 0;
                return CellIndex(x, z, index) ? Heights[index] : 0.0f;
            }

            void Raise(float x, float z, float height)
            {
                size_t index = 0;
                if (CellIndex(x, z, index) && height > Heights[index])
                {
                    Heights[index] = height;
                }
            }
        };

        /// 山を 1 つ作る。中心から正規分布で落とし、下の形に初めて触れる高さに置く
        void PlacePile(Container::VariableArray<Component::MegaScatterInstance> &instances, const BananaShape &shape,
                       int pileIndex, float centerX, float centerZ, uint32_t count, PlacementRandom &random)
        {
            PileHeightField field(centerX, centerZ);
            VariableArray<Math::Vector3> oriented;
            float peakHeight = 0.0f;
            for (uint32_t n = 0; n < count; ++n)
            {
                float bestRequired = std::numeric_limits<float>::max();
                float bestX = 0.0f;
                float bestZ = 0.0f;
                float bestYaw = 0.0f;
                float bestScale = 1.0f;
                for (int candidate = 0; candidate < kPileCandidates; ++candidate)
                {
                    const float radius = std::min(kPileSigmaMeters * std::sqrt(-2.0f * std::log(std::max(random.Next(), 1.0e-6f))),
                                                  kPileSigmaMeters * kPileMaxSigmas);
                    const float angle = 2.0f * kPi * random.Next();
                    const float x = centerX + std::cos(angle) * radius;
                    const float z = centerZ + std::sin(angle) * radius;
                    const float yaw = random.Next() * 2.0f * kPi;
                    const float scale = kScaleMin + (kScaleMax - kScaleMin) * random.Next();
                    OrientSamples(shape.SamplePoints, yaw, scale, oriented);
                    // 自分の最下面が、下の形（と床）のどの点にも食い込まない最小の原点の高さ
                    float required = 0.0f;
                    for (const Math::Vector3 &offset : oriented)
                    {
                        required = std::max(required, field.Sample(x + offset.x, z + offset.z) - offset.y);
                    }
                    if (required < bestRequired)
                    {
                        bestRequired = required;
                        bestX = x;
                        bestZ = z;
                        bestYaw = yaw;
                        bestScale = scale;
                    }
                }

                // 選んだ位置の高さを、密な点で確かめ直す（粗い点が拾わなかった下の形の尖りへ食い込まない）
                OrientSamples(shape.DensePoints, bestYaw, bestScale, oriented);
                for (const Math::Vector3 &offset : oriented)
                {
                    bestRequired = std::max(bestRequired, field.Sample(bestX + offset.x, bestZ + offset.z) - offset.y);
                }
                const float y = bestRequired - kSinkMeters;
                for (const Math::Vector3 &offset : oriented)
                {
                    field.Raise(bestX + offset.x, bestZ + offset.z, y + offset.y);
                    peakHeight = std::max(peakHeight, y + offset.y);
                }
                AddBanana(instances, bestX, y, bestZ, bestYaw, bestScale);
#if NORVES_BUILD_DEVELOPMENT
                // 置いた姿勢（外部の検算が頂点から接触の隙間を数えるための記録）
                LOG_INFO_F("MEGA_BANANAS_PILE_ITEM pile=%d index=%u x=%.5f y=%.5f z=%.5f yaw=%.6f scale=%.6f", pileIndex, n,
                           static_cast<double>(bestX), static_cast<double>(y), static_cast<double>(bestZ),
                           static_cast<double>(bestYaw), static_cast<double>(bestScale));
#endif
            }
            LOG_INFO_F("MEGA_BANANAS_PILE_SHAPE pile=%d count=%u peak_height=%.4f", pileIndex, count,
                       static_cast<double>(peakHeight));
        }

    } // namespace

    void BuildBananaShape(const Asset::CookedMeshData &mesh, BananaShape &outShape)
    {
        outShape = BananaShape{};
        // 床に横たえたときの高さを、頂点の太さの範囲から求める
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        for (const Asset::CookedMeshVertex &vertex : mesh.Vertices)
        {
            minX = std::min(minX, vertex.Position.X);
            maxX = std::max(maxX, vertex.Position.X);
        }
        outShape.ThicknessMin = minX;
        outShape.ThicknessMax = maxX;
        {
            const size_t vertexCount = mesh.Vertices.size();
            const size_t coarseStride = std::max<size_t>(1, vertexCount / kMaxSamplePoints);
            const size_t denseStride = std::max<size_t>(1, vertexCount / kMaxDensePoints);
            for (size_t index = 0; index < vertexCount; ++index)
            {
                const Asset::CookedMeshVertex &vertex = mesh.Vertices[index];
                const Math::Vector3 point(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
                if (index % coarseStride == 0)
                {
                    outShape.SamplePoints.push_back(point);
                }
                if (index % denseStride == 0)
                {
                    outShape.DensePoints.push_back(point);
                }
            }
        }
    }

    uint32_t PlaceBananas(Container::VariableArray<Component::MegaScatterInstance> &instances, const BananaShape &shape,
                          uint32_t count, const IBananaFieldMask *pMask, uint32_t &outPileCount)
    {
        PlacementRandom random;
        uint32_t placed = 0;
        const uint32_t pileBudget = std::min(kPileBananas * static_cast<uint32_t>(kPileCount), count / kMaxPileShareDivisor);
        if (shape.SamplePoints.empty() || shape.DensePoints.empty())
        {
            outPileCount = 0;
            return 0;
        }

        for (int pile = 0; pile < kPileCount; ++pile)
        {
            const uint32_t pileCount =
                pileBudget / kPileCount + (static_cast<uint32_t>(pile) < pileBudget % kPileCount ? 1u : 0u);
            PlacePile(instances, shape, pile, kPileCenters[pile][0], kPileCenters[pile][1], pileCount, random);
            placed += pileCount;
        }
        outPileCount = placed;

        // 敷き詰め: 床に横たえる。太さの向きが縦になるので、中心の高さ = 最下点が床に接する高さ（拡大後）
        const uint32_t fieldCount = count - placed;
        if (fieldCount > 0)
        {
            VariableArray<FieldPoint> points;
            BuildFieldPoints(fieldCount, pMask, random, points);
            const float halfThickness = 0.5f * (shape.ThicknessMax - shape.ThicknessMin);
            const float centerOffset = 0.5f * (shape.ThicknessMax + shape.ThicknessMin);
            for (const FieldPoint &point : points)
            {
                const float yaw = random.Next() * 2.0f * kPi;
                const float scale = kScaleMin + (kScaleMax - kScaleMin) * random.Next();
                const float y = (halfThickness + centerOffset) * scale - kSinkMeters;
                AddBanana(instances, point.X, y, point.Z, yaw, scale);
                ++placed;
            }
            if (points.size() < fieldCount)
            {
                LOG_WARNING_F("mega-bananas: 敷き詰めの位置が足りませんでした（要求 %u・実際 %zu）", fieldCount, points.size());
            }
        }
        return placed;
    }

} // namespace Game::GameModes
