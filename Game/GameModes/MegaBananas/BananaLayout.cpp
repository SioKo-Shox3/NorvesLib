#include "GameModes/MegaBananas/BananaLayout.h"

#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Quaternion.h"

#include <algorithm>
#include <chrono>
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
        // ---- 海（中心からの距離で密度を落とす円盤） ----
        // 円盤の中心（床の x・z）。手前の山のあたりに置き、密度の高い所がカメラの前に来るようにする
        constexpr float kSeaCenterX = 0.2f;
        constexpr float kSeaCenterZ = 2.8f;
        // 密度の形（半径に対する比）: 近の平らな所 → 中へ移る区間 → 中の平らな所 → 遠へ移る区間 → 遠の平らな所 → 縁で 0。
        // 移る区間は密度の対数を直線でつなぎ、境に段（輪の帯）を作らない
        constexpr float kSeaNearEnd = 0.075f;
        constexpr float kSeaMidStart = 0.11f;
        constexpr float kSeaMidEnd = 0.20f;
        constexpr float kSeaFarStart = 0.26f;
        // 縁: 縁の位置を半径の ±kSeaEdgeWobble だけ低周波のノイズでゆらし、その内側 kSeaEdgeTaper の幅で密度を 0 へ落とす
        // （縁が 1 本の円に見えないようにする）。ノイズの空間の周期も半径に対する比
        constexpr float kSeaEdgeWobble = 0.04f;
        constexpr float kSeaEdgeTaper = 0.10f;
        constexpr float kSeaEdgeNoiseCell = 0.08f;
        // 区画の一辺[m]と、区画の本数を見積もる積分の片側の分割数
        constexpr float kSeaTileMeters = 16.0f;
        constexpr int kSeaTileIntegrationSteps = 16;
        // 区画の密度の上限を標本の最大から決めるときの余裕（標本の間隔 1 m で密度が変わる割合より大きく）
        constexpr float kSeaTileDensityMargin = 1.25f;
        // 半径を本数から決める積分の分割数
        constexpr int kSeaRadialIntegrationSteps = 4096;
        // 1 本ごとに比べる位置の数（床に置ける位置が見つかればそこに決め、無ければ最も低く載る位置）と、位置を引く回数の上限
        constexpr int kSeaCandidates = 4;
        constexpr int kSeaMaxDraws = 512;
        // 芯に沿って区切る球の数（球の間隔が球の半径より短く、つなぎ目のくぼみが 2 mm 未満になる数）
        constexpr uint32_t kSpineSphereCount = 12;
        // 床に置いたときより、この高さ以上持ち上げたものを「載せた」と数える
        constexpr float kStackedLiftMeters = 0.001f;
        // 予算を確かめる間隔（本数）
        constexpr uint32_t kSeaTimeCheckInterval = 64;
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
        // ---- 大きな塚（100 万本級のマスクなしの配置だけ。山と同じ落とし込みで、海の中に積む） ----
        // 塚を置く最少の本数と、塚に使う本数（全体の 1/kMoundShareDivisor、上限 kMoundMaxBananas）
        constexpr uint32_t kMoundMinCount = 500000;
        constexpr uint32_t kMoundShareDivisor = 10;
        constexpr uint32_t kMoundMaxBananas = 150000;
        constexpr int kMoundCount = 4;
        // 塚の中心（床の x・z）。horizon の視点（(0, 1.7, -15) から +Z）の 30〜75 m 先に、画面の上で重ならないよう左右へ散らす。
        // 番号 0 が検算用の塚
        constexpr float kMoundCenters[kMoundCount][2] = {{-7.0f, 14.0f}, {8.0f, 22.0f}, {-6.0f, 60.0f}, {3.0f, 45.0f}};
        // 検算用の塚の本数（1 本ずつの姿勢をログへ出すのはこの塚だけ）
        constexpr uint32_t kCheckMoundBananas = 20000;
        // 塚の高さの格子の升[m]（外部の検算の升と同じ。升は世界の原点にそろえる）
        constexpr float kMoundCellMeters = 0.005f;
        // 塚の姿勢を丸める段数（向き・大きさ）。段ごとの足跡を使い回す
        constexpr int kMoundYawSteps = 128;
        constexpr int kMoundScaleSteps = 8;
        // 1 本ごとに比べる候補の数と、候補を引く落とした位置の周りの半径[m]（バナナの長さほど。局所のくぼみと尖りをならす）
        constexpr int kMoundCandidates = 8;
        constexpr float kMoundLocalRadius = 0.3f;
        // 塚の落とす位置の標準偏差を、山と相似な値から広げる倍率（大きな塚は詰まりが粗く高くなるので、斜面を 40° 前後に寝かせる）
        constexpr float kMoundSigmaScale = 1.3f;
        // 予算を確かめる間隔（本数）
        constexpr uint32_t kMoundTimeCheckInterval = 256;
        // 浮いていると数える隙間[m]
        constexpr float kFloatingGapMeters = 0.005f;
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

        /// バナナのローカルの点を、倍率と向きを掛けたワールドの差分（位置は含まない）へ変える 3×3 の列。
        /// 描画側は行ベクトル規約で、回転は共役（逆回転）として点に作用する
        struct OrientBasis
        {
            Math::Vector3 ColumnX;
            Math::Vector3 ColumnY;
            Math::Vector3 ColumnZ;

            OrientBasis(const Math::Quaternion &rotation, float scale)
            {
                const Math::Quaternion inverse(-rotation.x, -rotation.y, -rotation.z, rotation.w);
                // 点ごとに四元数を掛けると遅いので、基底を 1 回回して 3×3 の列にする
                ColumnX = inverse * Math::Vector3(scale, 0.0f, 0.0f);
                ColumnY = inverse * Math::Vector3(0.0f, scale, 0.0f);
                ColumnZ = inverse * Math::Vector3(0.0f, 0.0f, scale);
            }

            Math::Vector3 Apply(const Math::Vector3 &point) const
            {
                return Math::Vector3(ColumnX.x * point.x + ColumnY.x * point.y + ColumnZ.x * point.z,
                                     ColumnX.y * point.x + ColumnY.y * point.y + ColumnZ.y * point.z,
                                     ColumnX.z * point.x + ColumnY.z * point.y + ColumnZ.z * point.z);
            }
        };

        void OrientSamples(const VariableArray<Math::Vector3> &local, float yaw, float scale, VariableArray<Math::Vector3> &out)
        {
            const OrientBasis basis(MakeBananaRotation(yaw), scale);
            out.resize(local.size());
            for (size_t index = 0; index < local.size(); ++index)
            {
                out[index] = basis.Apply(local[index]);
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

        float SmoothStep01(float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        /// 密度の形（半径の比 u での、縁を除いた密度[本/m²]）。u について単調に減る
        float SeaProfileDensity(float u)
        {
            const auto logLerp = [](float from, float to, float t) { return from * std::pow(to / from, t); };
            if (u <= kSeaNearEnd)
            {
                return kSeaDensityNear;
            }
            if (u < kSeaMidStart)
            {
                return logLerp(kSeaDensityNear, kSeaDensityMid, (u - kSeaNearEnd) / (kSeaMidStart - kSeaNearEnd));
            }
            if (u <= kSeaMidEnd)
            {
                return kSeaDensityMid;
            }
            if (u < kSeaFarStart)
            {
                return logLerp(kSeaDensityMid, kSeaDensityFar, (u - kSeaMidEnd) / (kSeaFarStart - kSeaMidEnd));
            }
            return kSeaDensityFar;
        }

        /// 縁で密度を 0 へ落とす係数（0〜1）。縁の位置は床の x・z の低周波のノイズでゆらす
        float SeaEdgeFactor(float u, float x, float z, float radius)
        {
            const float edge = 1.0f + kSeaEdgeWobble * (2.0f * ValueNoise(x, z, kSeaEdgeNoiseCell * radius) - 1.0f);
            return SmoothStep01((edge - u) / kSeaEdgeTaper);
        }

        /// 山の周りの、海を置かない円の中なら true
        bool IsUnderPile(float x, float z)
        {
            for (int pile = 0; pile < kPileCount; ++pile)
            {
                const float diffX = x - kPileCenters[pile][0];
                const float diffZ = z - kPileCenters[pile][1];
                if (diffX * diffX + diffZ * diffZ < kPileExcludeRadius * kPileExcludeRadius)
                {
                    return true;
                }
            }
            return false;
        }

        /// 大きな塚 1 つ（中心・本数・落とす位置の標準偏差・落とす位置の最大半径・海を置かない半径）
        struct MoundSite
        {
            float CenterX = 0.0f;
            float CenterZ = 0.0f;
            uint32_t Count = 0;
            float Sigma = 0.0f;
            float MaxRadius = 0.0f;
            float ExcludeRadius = 0.0f;
        };

        /// 床の x・z での海の密度[本/m²]（縁と山・塚の周りを含む）
        float SeaDensityAt(float x, float z, float radius, const VariableArray<MoundSite> &mounds)
        {
            if (IsUnderPile(x, z))
            {
                return 0.0f;
            }
            for (const MoundSite &mound : mounds)
            {
                const float diffX = x - mound.CenterX;
                const float diffZ = z - mound.CenterZ;
                if (diffX * diffX + diffZ * diffZ < mound.ExcludeRadius * mound.ExcludeRadius)
                {
                    return 0.0f;
                }
            }
            const float diffX = x - kSeaCenterX;
            const float diffZ = z - kSeaCenterZ;
            const float u = std::sqrt(diffX * diffX + diffZ * diffZ) / radius;
            return SeaProfileDensity(u) * SeaEdgeFactor(u, x, z, radius);
        }

        /// 半径 1 の円盤に置ける本数（密度の形を面積で積分した値。縁のゆらぎは平均で 0 として除く）。
        /// 半径 R の円盤の本数はこの値の R² 倍
        double SeaUnitDiskCount()
        {
            double total = 0.0;
            const double step = (1.0 + kSeaEdgeWobble) / kSeaRadialIntegrationSteps;
            for (int index = 0; index < kSeaRadialIntegrationSteps; ++index)
            {
                const double u = (static_cast<double>(index) + 0.5) * step;
                const double density =
                    SeaProfileDensity(static_cast<float>(u)) * SmoothStep01(static_cast<float>((1.0 - u) / kSeaEdgeTaper));
                total += density * 2.0 * static_cast<double>(kPi) * u * step;
            }
            return total;
        }

        /// 山に使う本数（3 つの塚の合計）
        uint32_t PileBudget(uint32_t count)
        {
            return std::min(kPileBananas * static_cast<uint32_t>(kPileCount), count / kMaxPileShareDivisor);
        }

        /// 大きな塚に使う本数（全部の塚の合計。マスクを渡した配置と、本数が少ないときは 0）
        uint32_t MoundBudget(uint32_t count, bool bMasked)
        {
            if (bMasked || count < kMoundMinCount)
            {
                return 0;
            }
            return std::min(count / kMoundShareDivisor, kMoundMaxBananas);
        }

        /// 落とす位置の標準偏差。260 本で kPileSigmaMeters の山と相似な形になるよう、本数の立方根に比例させる
        float PileSigmaFor(uint32_t count)
        {
            return kPileSigmaMeters * std::cbrt(static_cast<float>(count) / static_cast<float>(kPileBananas));
        }

        /// 芯に沿った球の 1 つ（ワールド座標）
        struct SpineSphere
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
            float Radius = 0.0f;
        };

        /// 近傍の格子に入れた、置いたバナナ 1 本（球は SpineSpheres の FirstSphere から kSpineSphereCount 個）
        struct SpineRecord
        {
            float X = 0.0f;
            float Z = 0.0f;
            float Bound = 0.0f;
            uint32_t FirstSphere = 0;
        };

        /// 塚の足跡の升 1 つ（原点の升からの升の差と、その升に入る頂点の最低・最高の高さ。原点からの差[m]）
        struct FootprintCell
        {
            int16_t DX = 0;
            int16_t DZ = 0;
            float Bottom = 0.0f;
            float Top = 0.0f;
        };

        /// 丸めた姿勢 1 つの足跡（初めて使うときに作る）
        struct MoundFootprint
        {
            bool bBuilt = false;
            VariableArray<FootprintCell> Cells;
        };

    } // namespace

    struct BananaFieldBuilder::State
    {
        /// 海の区画（処理の順に並べる）
        struct Tile
        {
            int GridX = 0;
            int GridZ = 0;
            float MinX = 0.0f;
            float MinZ = 0.0f;
            /// 区画に置く本数（密度の積分から決めた分）
            uint32_t Quota = 0;
            /// 区画の中での密度の上限（位置を密度に比例して引く棄却法に使う）と、置けなかったときに使う締めた上限
            float MaxDensity = 0.0f;
            float TightMaxDensity = 0.0f;
            /// 置いた入力の範囲
            uint32_t First = 0;
            uint32_t Count = 0;
            bool bDone = false;
        };

        const BananaShape *pShape = nullptr;
        const IBananaFieldMask *pMask = nullptr;
        VariableArray<Component::MegaScatterInstance> Instances;
        BananaLayoutStats Stats;
        bool bDone = true;

        VariableArray<Tile> Tiles;
        /// 区画の格子（一辺 GridSize）の番号 → Tiles の番号（円盤に掛からない区画は -1）
        VariableArray<int32_t> TileAt;
        int GridSize = 0;
        float GridMinX = 0.0f;
        float GridMinZ = 0.0f;
        uint32_t CurrentTile = 0;

        // ---- 処理中の区画 ----
        bool bTileOpen = false;
        uint32_t TileTarget = 0;
        uint32_t TilePlaced = 0;
        /// 置けずに次の区画へ回す本数
        uint32_t Carry = 0;
        /// 最後に残った本数を、処理済みの区画へ戻して置いている
        bool bBackfillTile = false;
        PlacementRandom TileRandom;

        // ---- 処理中の区画と、その周り 1 升の近傍の格子（升の一辺は 2 本の包む半径の和の最大） ----
        float CellMeters = 0.0f;
        int CellsPerSide = 0;
        float CellOriginX = 0.0f;
        float CellOriginZ = 0.0f;
        VariableArray<int32_t> CellHead;
        VariableArray<int32_t> RecordNext;
        VariableArray<SpineRecord> Records;
        VariableArray<SpineSphere> Spheres;
        /// 候補の向きの球（位置を含まない差分と半径）の作業用
        VariableArray<SpineSphere> CandidateSpine;
        VariableArray<SpineSphere> BestSpine;

        // ---- 大きな塚（海の区画より先に、塚の順に積む） ----
        VariableArray<MoundSite> Mounds;
        uint32_t CurrentMound = 0;
        bool bMoundOpen = false;
        uint32_t MoundPlaced = 0;
        PlacementRandom MoundRandom;
        /// 処理中の塚の高さの格子（世界の升の番号 FieldMinX・FieldMinZ から一辺 FieldSize。床は 0）
        int FieldMinX = 0;
        int FieldMinZ = 0;
        int FieldSize = 0;
        VariableArray<float> FieldHeights;
        /// 処理中の塚の内訳（隙間は各バナナの最小の隙間の、塚の中の最大・最小）
        float MoundPeak = 0.0f;
        float MoundMaxGap = 0.0f;
        float MoundMinGap = 0.0f;
        uint32_t MoundFloating = 0;
        uint32_t MoundStacked = 0;
        /// 丸めた姿勢ごとの足跡（向きの段 × 大きさの段）と、足跡の局所の箱の片側の升の数
        VariableArray<MoundFootprint> Footprints;
        int FootprintHalfCells = 0;
        VariableArray<float> FootprintBottom;
        VariableArray<float> FootprintTop;
    };

    namespace
    {
        using BuilderState = BananaFieldBuilder::State;

        /// 向きと倍率を掛けた芯の球（位置を含まない差分と半径）を作る
        void OrientSpine(const BananaShape &shape, const Math::Quaternion &rotation, float scale,
                         VariableArray<SpineSphere> &out)
        {
            const OrientBasis basis(rotation, scale);
            out.resize(shape.SpineCenters.size());
            for (size_t index = 0; index < shape.SpineCenters.size(); ++index)
            {
                const Math::Vector3 offset = basis.Apply(shape.SpineCenters[index]);
                out[index] = SpineSphere{offset.x, offset.y, offset.z, shape.SpineRadii[index] * scale};
            }
        }

        /// 床の x・z の升の番号（格子の外なら false）
        bool CellCoord(const BuilderState &state, float x, float z, int &outX, int &outZ)
        {
            outX = static_cast<int>(std::floor((x - state.CellOriginX) / state.CellMeters));
            outZ = static_cast<int>(std::floor((z - state.CellOriginZ) / state.CellMeters));
            return outX >= 0 && outX < state.CellsPerSide && outZ >= 0 && outZ < state.CellsPerSide;
        }

        /// 置いたバナナを近傍の格子へ入れる（格子の外なら入れない。区画の中の候補と重なりうるのは格子の中のものだけ）
        void AddRecord(BuilderState &state, const Math::Vector3 &position, const VariableArray<SpineSphere> &orientedSpine,
                       float scale)
        {
            int cellX = 0;
            int cellZ = 0;
            if (!CellCoord(state, position.x, position.z, cellX, cellZ))
            {
                return;
            }
            SpineRecord record;
            record.X = position.x;
            record.Z = position.z;
            record.Bound = state.pShape->SpineBoundRadius * scale;
            record.FirstSphere = static_cast<uint32_t>(state.Spheres.size());
            for (const SpineSphere &sphere : orientedSpine)
            {
                state.Spheres.push_back(
                    SpineSphere{position.x + sphere.X, position.y + sphere.Y, position.z + sphere.Z, sphere.Radius});
            }
            const size_t cell = static_cast<size_t>(cellZ) * static_cast<size_t>(state.CellsPerSide) + static_cast<size_t>(cellX);
            state.RecordNext.push_back(state.CellHead[cell]);
            state.CellHead[cell] = static_cast<int32_t>(state.Records.size());
            state.Records.push_back(record);
        }

        /// 候補の位置に置くときの原点の高さ。床に置く高さと、近傍のバナナの球に初めて触れる高さの大きい方
        float RequiredHeight(const BuilderState &state, float x, float z, const VariableArray<SpineSphere> &orientedSpine,
                             float bound, float floorY)
        {
            float required = floorY;
            int cellX = 0;
            int cellZ = 0;
            if (!CellCoord(state, x, z, cellX, cellZ))
            {
                return required;
            }
            for (int cz = std::max(0, cellZ - 1); cz <= std::min(state.CellsPerSide - 1, cellZ + 1); ++cz)
            {
                for (int cx = std::max(0, cellX - 1); cx <= std::min(state.CellsPerSide - 1, cellX + 1); ++cx)
                {
                    int32_t recordIndex =
                        state.CellHead[static_cast<size_t>(cz) * static_cast<size_t>(state.CellsPerSide) + static_cast<size_t>(cx)];
                    for (; recordIndex >= 0; recordIndex = state.RecordNext[static_cast<size_t>(recordIndex)])
                    {
                        const SpineRecord &record = state.Records[static_cast<size_t>(recordIndex)];
                        const float centerX = record.X - x;
                        const float centerZ = record.Z - z;
                        const float reach = record.Bound + bound;
                        if (centerX * centerX + centerZ * centerZ >= reach * reach)
                        {
                            continue;
                        }
                        // どのバナナも同じ形なので、球の数は自分の球の数と同じ
                        const SpineSphere *other = &state.Spheres[record.FirstSphere];
                        const size_t sphereCount = orientedSpine.size();
                        for (const SpineSphere &mine : orientedSpine)
                        {
                            const float mineX = x + mine.X;
                            const float mineZ = z + mine.Z;
                            for (size_t index = 0; index < sphereCount; ++index)
                            {
                                const float diffX = other[index].X - mineX;
                                const float diffZ = other[index].Z - mineZ;
                                const float sum = other[index].Radius + mine.Radius;
                                const float distanceSq = diffX * diffX + diffZ * diffZ;
                                if (distanceSq < sum * sum)
                                {
                                    // 2 つの球が接する高さ（球の中心どうしの距離が半径の和）
                                    const float touch = other[index].Y + std::sqrt(sum * sum - distanceSq) - mine.Y - kSinkMeters;
                                    required = std::max(required, touch);
                                }
                            }
                        }
                    }
                }
            }
            return required;
        }

        /// 区画を開く: 置く本数を決め、乱数を区画の番号で始め、近傍の格子へ周りの済んだ区画のバナナを入れる
        void OpenTile(BuilderState &state)
        {
            BuilderState::Tile &tile = state.Tiles[state.CurrentTile];
            state.TileTarget = tile.Quota + state.Carry;
            state.Carry = 0;
            state.TilePlaced = 0;
            const uint32_t gridIndex =
                static_cast<uint32_t>(tile.GridZ) * static_cast<uint32_t>(state.GridSize) + static_cast<uint32_t>(tile.GridX);
            state.TileRandom.Counter = HashIndex(gridIndex * 0x9E3779B1u + 0x2545F491u);
            if (state.bBackfillTile)
            {
                // 同じ区画に最初に置いた位置の並びを繰り返さない。
                state.TileRandom.Counter = HashIndex(state.TileRandom.Counter ^ 0xA511E9B3u);
            }
            tile.First = static_cast<uint32_t>(state.Instances.size());
            tile.Count = 0;

            state.CellsPerSide = static_cast<int>(std::ceil(kSeaTileMeters / state.CellMeters)) + 2;
            state.CellOriginX = tile.MinX - state.CellMeters;
            state.CellOriginZ = tile.MinZ - state.CellMeters;
            state.CellHead.assign(static_cast<size_t>(state.CellsPerSide) * static_cast<size_t>(state.CellsPerSide), -1);
            state.RecordNext.clear();
            state.Records.clear();
            state.Spheres.clear();

            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int gridX = tile.GridX + dx;
                    const int gridZ = tile.GridZ + dz;
                    if ((dx == 0 && dz == 0 && !state.bBackfillTile) ||
                        gridX < 0 || gridX >= state.GridSize || gridZ < 0 || gridZ >= state.GridSize)
                    {
                        continue;
                    }
                    const int32_t neighbor =
                        state.TileAt[static_cast<size_t>(gridZ) * static_cast<size_t>(state.GridSize) + static_cast<size_t>(gridX)];
                    if (neighbor < 0 || !state.Tiles[static_cast<size_t>(neighbor)].bDone)
                    {
                        continue;
                    }
                    const BuilderState::Tile &other = state.Tiles[static_cast<size_t>(neighbor)];
                    for (uint32_t index = other.First; index < other.First + other.Count; ++index)
                    {
                        const Component::MegaScatterInstance &instance = state.Instances[index];
                        int cellX = 0;
                        int cellZ = 0;
                        if (!CellCoord(state, instance.Position.x, instance.Position.z, cellX, cellZ))
                        {
                            continue;
                        }
                        OrientSpine(*state.pShape, instance.Rotation, instance.Scale.x, state.CandidateSpine);
                        AddRecord(state, instance.Position, state.CandidateSpine, instance.Scale.x);
                    }
                }
            }
        }

        void CloseTile(BuilderState &state)
        {
            BuilderState::Tile &tile = state.Tiles[state.CurrentTile];
            tile.Count = static_cast<uint32_t>(state.Instances.size()) - tile.First;
            tile.bDone = true;
            if (state.TilePlaced < state.TileTarget)
            {
                state.Carry += state.TileTarget - state.TilePlaced;
            }
            ++state.CurrentTile;
        }

        /// 区画に 1 本置く。密度に比例して位置を引き、床に置ける位置か、無ければ最も低く載る位置を採る。
        /// 位置を引く回数の上限まで、置ける位置が 1 つも無ければ false
        bool PlaceSeaBanana(BuilderState &state)
        {
            const BuilderState::Tile &tile = state.Tiles[state.CurrentTile];
            const BananaShape &shape = *state.pShape;
            PlacementRandom &random = state.TileRandom;
            const float radius = state.Stats.Radius;

            int candidates = 0;
            float bestY = std::numeric_limits<float>::max();
            float bestFloorY = 0.0f;
            float bestX = 0.0f;
            float bestZ = 0.0f;
            float bestYaw = 0.0f;
            float bestScale = 1.0f;
            // 位置が 1 つも無いときだけ上限を締め、マスクがある場合は空いた升にも絞って再び引く。
            // 通常の配置とマスクなしの乱数の並びは変えない。
            uint32_t openCells[kSeaTileIntegrationSteps * kSeaTileIntegrationSteps];
            uint32_t openCellCount = 0;
            float maskedMaxDensity = 0.0f;
            const float sampleStep = kSeaTileMeters / static_cast<float>(kSeaTileIntegrationSteps);
            for (int draw = 0; draw < 3 * kSeaMaxDraws && candidates < kSeaCandidates; ++draw)
            {
                if (draw >= kSeaMaxDraws && candidates > 0)
                {
                    break;
                }
                if (draw == 2 * kSeaMaxDraws)
                {
                    if (state.pMask == nullptr)
                    {
                        break;
                    }
                    for (int sz = 0; sz < kSeaTileIntegrationSteps; ++sz)
                    {
                        for (int sx = 0; sx < kSeaTileIntegrationSteps; ++sx)
                        {
                            const float x = tile.MinX + (static_cast<float>(sx) + 0.5f) * sampleStep;
                            const float z = tile.MinZ + (static_cast<float>(sz) + 0.5f) * sampleStep;
                            const float density = SeaDensityAt(x, z, radius, state.Mounds);
                            if (density > 0.0f && !state.pMask->IsBlocked(x, z))
                            {
                                openCells[openCellCount++] = static_cast<uint32_t>(sz * kSeaTileIntegrationSteps + sx);
                                maskedMaxDensity = std::max(maskedMaxDensity, density);
                            }
                        }
                    }
                    if (openCellCount == 0)
                    {
                        break;
                    }
                    maskedMaxDensity = std::min(tile.TightMaxDensity, maskedMaxDensity * kSeaTileDensityMargin);
                }
                const bool bMaskedRetry = draw >= 2 * kSeaMaxDraws;
                const float maxDensity = bMaskedRetry ? maskedMaxDensity : (draw < kSeaMaxDraws ? tile.MaxDensity : tile.TightMaxDensity);
                float x = 0.0f;
                float z = 0.0f;
                const bool bUseCellCenter = bMaskedRetry && draw == 3 * kSeaMaxDraws - 1;
                if (bMaskedRetry)
                {
                    const uint32_t cell = openCells[std::min(static_cast<uint32_t>(random.Next() * openCellCount), openCellCount - 1)];
                    const float offsetX = random.Next();
                    const float offsetZ = random.Next();
                    x = tile.MinX + (static_cast<float>(cell % kSeaTileIntegrationSteps) + (bUseCellCenter ? 0.5f : offsetX)) * sampleStep;
                    z = tile.MinZ + (static_cast<float>(cell / kSeaTileIntegrationSteps) + (bUseCellCenter ? 0.5f : offsetZ)) * sampleStep;
                }
                else
                {
                    x = tile.MinX + kSeaTileMeters * random.Next();
                    z = tile.MinZ + kSeaTileMeters * random.Next();
                }
                // 最後の候補は密度が正で空いている升の中心なので、密度による棄却は行わない。
                const float densityDraw = random.Next();
                const float density = SeaDensityAt(x, z, radius, state.Mounds);
                if (density <= 0.0f || (!bUseCellCenter && densityDraw * maxDensity >= density))
                {
                    continue;
                }
                if (state.pMask != nullptr && state.pMask->IsBlocked(x, z))
                {
                    continue;
                }
                ++candidates;
                const float yaw = random.Next() * 2.0f * kPi;
                const float scale = kScaleMin + (kScaleMax - kScaleMin) * random.Next();
                // 床に横たえる: 太さの向きが縦になるので、中心の高さ = 最下点が床に接する高さ（拡大後）
                const float floorY = shape.ThicknessMax * scale - kSinkMeters;
                OrientSpine(shape, MakeBananaRotation(yaw), scale, state.CandidateSpine);
                const float y =
                    RequiredHeight(state, x, z, state.CandidateSpine, shape.SpineBoundRadius * scale, floorY);
                if (y < bestY)
                {
                    bestY = y;
                    bestFloorY = floorY;
                    bestX = x;
                    bestZ = z;
                    bestYaw = yaw;
                    bestScale = scale;
                    state.BestSpine = state.CandidateSpine;
                }
                if (y <= floorY)
                {
                    break;
                }
            }
            if (candidates == 0)
            {
                return false;
            }

            AddBanana(state.Instances, bestX, bestY, bestZ, bestYaw, bestScale);
            AddRecord(state, state.Instances.back().Position, state.BestSpine, bestScale);

            BananaLayoutStats &stats = state.Stats;
            const float diffX = bestX - stats.CenterX;
            const float diffZ = bestZ - stats.CenterZ;
            const float distance = std::sqrt(diffX * diffX + diffZ * diffZ);
            if (distance < stats.NearRadius)
            {
                ++stats.NearCount;
            }
            else if (distance < stats.MidRadius)
            {
                ++stats.MidCount;
            }
            else
            {
                ++stats.FarCount;
            }
            const float lift = bestY - bestFloorY;
            if (lift > kStackedLiftMeters)
            {
                ++stats.StackedCount;
            }
            stats.MaxLift = std::max(stats.MaxLift, lift);
            return true;
        }

        /// 最後に残った本数を、密度が正でマスクも空いている升を持つ処理済みの区画へ戻す。
        /// 入力の連続した範囲は元の区画と分け、追加分を末尾の区画として記録する。
        bool ReopenTileForCarry(BuilderState &state)
        {
            const float sampleStep = kSeaTileMeters / static_cast<float>(kSeaTileIntegrationSteps);
            for (size_t index = state.Tiles.size(); index > 0; --index)
            {
                const BuilderState::Tile &tile = state.Tiles[index - 1];
                for (int sz = 0; sz < kSeaTileIntegrationSteps; ++sz)
                {
                    for (int sx = 0; sx < kSeaTileIntegrationSteps; ++sx)
                    {
                        const float x = tile.MinX + (static_cast<float>(sx) + 0.5f) * sampleStep;
                        const float z = tile.MinZ + (static_cast<float>(sz) + 0.5f) * sampleStep;
                        if (SeaDensityAt(x, z, state.Stats.Radius, state.Mounds) <= 0.0f || state.pMask->IsBlocked(x, z))
                        {
                            continue;
                        }
                        BuilderState::Tile retry = tile;
                        retry.Quota = 0;
                        retry.First = 0;
                        retry.Count = 0;
                        retry.bDone = false;
                        state.Tiles.push_back(retry);
                        state.bBackfillTile = true;
                        return true;
                    }
                }
            }
            return false;
        }

        /// 円盤に掛かる区画を作り、中心に近い順に並べ、密度の積分に比例して本数を配る（合計がちょうど fieldCount）
        void BuildSeaTiles(BuilderState &state, uint32_t fieldCount)
        {
            BananaLayoutStats &stats = state.Stats;
            const float radius = stats.Radius;
            const float extent = radius * (1.0f + kSeaEdgeWobble);
            state.GridSize = std::max(1, static_cast<int>(std::ceil(2.0f * extent / kSeaTileMeters)));
            state.GridMinX = stats.CenterX - 0.5f * kSeaTileMeters * static_cast<float>(state.GridSize);
            state.GridMinZ = stats.CenterZ - 0.5f * kSeaTileMeters * static_cast<float>(state.GridSize);

            struct TileOrder
            {
                float Distance = 0.0f;
                uint32_t Index = 0;
            };
            VariableArray<BuilderState::Tile> candidates;
            VariableArray<double> integrals;
            VariableArray<TileOrder> order;
            const float sampleStep = kSeaTileMeters / static_cast<float>(kSeaTileIntegrationSteps);
            for (int gridZ = 0; gridZ < state.GridSize; ++gridZ)
            {
                for (int gridX = 0; gridX < state.GridSize; ++gridX)
                {
                    BuilderState::Tile tile;
                    tile.GridX = gridX;
                    tile.GridZ = gridZ;
                    tile.MinX = state.GridMinX + kSeaTileMeters * static_cast<float>(gridX);
                    tile.MinZ = state.GridMinZ + kSeaTileMeters * static_cast<float>(gridZ);
                    // 区画の中で中心に最も近い点（密度の上限はそこで決まる）
                    const float nearX = std::clamp(stats.CenterX, tile.MinX, tile.MinX + kSeaTileMeters) - stats.CenterX;
                    const float nearZ = std::clamp(stats.CenterZ, tile.MinZ, tile.MinZ + kSeaTileMeters) - stats.CenterZ;
                    const float nearest = std::sqrt(nearX * nearX + nearZ * nearZ);
                    if (nearest >= extent)
                    {
                        continue;
                    }
                    double integral = 0.0;
                    float sampledMax = 0.0f;
                    for (int sz = 0; sz < kSeaTileIntegrationSteps; ++sz)
                    {
                        for (int sx = 0; sx < kSeaTileIntegrationSteps; ++sx)
                        {
                            const float x = tile.MinX + (static_cast<float>(sx) + 0.5f) * sampleStep;
                            const float z = tile.MinZ + (static_cast<float>(sz) + 0.5f) * sampleStep;
                            const float density = SeaDensityAt(x, z, radius, state.Mounds);
                            integral += static_cast<double>(density);
                            sampledMax = std::max(sampledMax, density);
                        }
                    }
                    if (integral <= 0.0)
                    {
                        continue;
                    }
                    tile.MaxDensity = SeaProfileDensity(nearest / radius);
                    // 縁で細った区画は上の上限では採択がまれで、引く回数の上限までに置けないことがある。そのときだけ使う、
                    // 標本の最大に余裕を掛けた締めた上限
                    tile.TightMaxDensity = std::min(tile.MaxDensity, sampledMax * kSeaTileDensityMargin);
                    const float centerX = tile.MinX + 0.5f * kSeaTileMeters - stats.CenterX;
                    const float centerZ = tile.MinZ + 0.5f * kSeaTileMeters - stats.CenterZ;
                    order.push_back(TileOrder{std::sqrt(centerX * centerX + centerZ * centerZ),
                                              static_cast<uint32_t>(candidates.size())});
                    candidates.push_back(tile);
                    integrals.push_back(integral);
                }
            }
            // 中心に近い順（同じ距離は格子の番号の順）。区画の数は円盤の面積に比例する（バナナの本数ではなく区画の数の並べ替え）
            std::sort(order.begin(), order.end(),
                      [](const TileOrder &a, const TileOrder &b)
                      { return a.Distance < b.Distance || (a.Distance == b.Distance && a.Index < b.Index); });

            double total = 0.0;
            for (const double integral : integrals)
            {
                total += integral;
            }
            state.Tiles.clear();
            state.Tiles.reserve(order.size());
            state.TileAt.assign(static_cast<size_t>(state.GridSize) * static_cast<size_t>(state.GridSize), -1);
            double cumulative = 0.0;
            uint64_t assigned = 0;
            for (const TileOrder &entry : order)
            {
                cumulative += integrals[entry.Index];
                const uint64_t upTo =
                    total > 0.0 ? static_cast<uint64_t>(std::floor(static_cast<double>(fieldCount) * cumulative / total + 0.5)) : 0;
                BuilderState::Tile tile = candidates[entry.Index];
                tile.Quota = static_cast<uint32_t>(std::min<uint64_t>(upTo, fieldCount) - std::min<uint64_t>(assigned, upTo));
                assigned = std::max(assigned, std::min<uint64_t>(upTo, fieldCount));
                state.TileAt[static_cast<size_t>(tile.GridZ) * static_cast<size_t>(state.GridSize) + static_cast<size_t>(tile.GridX)] =
                    static_cast<int32_t>(state.Tiles.size());
                state.Tiles.push_back(tile);
            }
            // 丸めの残り（積分が 0 の区画しか無いときなど）は最後の区画へ回す
            if (!state.Tiles.empty() && assigned < fieldCount)
            {
                state.Tiles.back().Quota += static_cast<uint32_t>(fieldCount - assigned);
            }
            stats.TileCount = static_cast<uint32_t>(state.Tiles.size());
        }

    } // namespace

    namespace
    {

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
        /// @param bLogItems 1 本ずつの姿勢をログへ出す（塚を積むときは検算用の塚だけが出すので false）
        void PlacePile(Container::VariableArray<Component::MegaScatterInstance> &instances, const BananaShape &shape,
                       int pileIndex, float centerX, float centerZ, uint32_t count, PlacementRandom &random, bool bLogItems)
        {
            PileHeightField field(centerX, centerZ);
            VariableArray<Math::Vector3> oriented;
            float peakHeight = 0.0f;
            float maxGap = std::numeric_limits<float>::lowest();
            float minGap = std::numeric_limits<float>::max();
            uint32_t floating = 0;
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
                // 接触の隙間（自分の点と、書き込む前の下の形の高さの差の最小）
                float gap = std::numeric_limits<float>::max();
                for (const Math::Vector3 &offset : oriented)
                {
                    gap = std::min(gap, y + offset.y - field.Sample(bestX + offset.x, bestZ + offset.z));
                }
                maxGap = std::max(maxGap, gap);
                minGap = std::min(minGap, gap);
                floating += gap > kFloatingGapMeters ? 1u : 0u;
                for (const Math::Vector3 &offset : oriented)
                {
                    field.Raise(bestX + offset.x, bestZ + offset.z, y + offset.y);
                    peakHeight = std::max(peakHeight, y + offset.y);
                }
                AddBanana(instances, bestX, y, bestZ, bestYaw, bestScale);
#if NORVES_BUILD_DEVELOPMENT
                if (bLogItems)
                {
                    // 置いた姿勢（外部の検算が頂点から接触の隙間を数えるための記録）
                    LOG_INFO_F("MEGA_BANANAS_PILE_ITEM pile=%d index=%u x=%.5f y=%.5f z=%.5f yaw=%.6f scale=%.6f", pileIndex, n,
                               static_cast<double>(bestX), static_cast<double>(y), static_cast<double>(bestZ),
                               static_cast<double>(bestYaw), static_cast<double>(bestScale));
                }
#endif
            }
            LOG_INFO_F("MEGA_BANANAS_PILE_SHAPE pile=%d count=%u peak_height=%.4f", pileIndex, count,
                       static_cast<double>(peakHeight));
            if (count > 0)
            {
                LOG_INFO_F("MEGA_BANANAS_MOUND_GAP pile=%d kind=pile count=%u peak_height=%.4f max_gap_mm=%.3f min_gap_mm=%.3f "
                           "floating=%u logged=%d",
                           pileIndex, count, static_cast<double>(peakHeight), static_cast<double>(maxGap) * 1000.0,
                           static_cast<double>(minGap) * 1000.0, floating, bLogItems ? 1 : 0);
            }
        }

        // ---- 大きな塚 ----

        float MoundYaw(int yawStep)
        {
            return static_cast<float>(yawStep) * (2.0f * kPi / static_cast<float>(kMoundYawSteps));
        }

        float MoundScale(int scaleStep)
        {
            return kScaleMin + (kScaleMax - kScaleMin) * (static_cast<float>(scaleStep) + 0.5f) / static_cast<float>(kMoundScaleSteps);
        }

        /// 丸めた姿勢の足跡を返す（初めてなら全頂点から作る）。原点を升の中心に置いたとき、各頂点が入る升の差を数える
        const MoundFootprint &GetFootprint(BuilderState &state, int yawStep, int scaleStep)
        {
            MoundFootprint &footprint = state.Footprints[static_cast<size_t>(yawStep) * kMoundScaleSteps + static_cast<size_t>(scaleStep)];
            if (footprint.bBuilt)
            {
                return footprint;
            }
            const int half = state.FootprintHalfCells;
            const int side = 2 * half + 1;
            state.FootprintBottom.assign(static_cast<size_t>(side) * static_cast<size_t>(side), std::numeric_limits<float>::max());
            state.FootprintTop.assign(static_cast<size_t>(side) * static_cast<size_t>(side), std::numeric_limits<float>::lowest());
            const OrientBasis basis(MakeBananaRotation(MoundYaw(yawStep)), MoundScale(scaleStep));
            for (const Math::Vector3 &point : state.pShape->FullPoints)
            {
                const Math::Vector3 offset = basis.Apply(point);
                // 原点は升の中心（升の番号 + 0.5）にあるので、頂点の升は原点の升 + floor(0.5 + 差 / 升)
                const int dx = static_cast<int>(std::floor(0.5f + offset.x / kMoundCellMeters));
                const int dz = static_cast<int>(std::floor(0.5f + offset.z / kMoundCellMeters));
                if (dx < -half || dx > half || dz < -half || dz > half)
                {
                    continue;
                }
                const size_t index = static_cast<size_t>(dz + half) * static_cast<size_t>(side) + static_cast<size_t>(dx + half);
                state.FootprintBottom[index] = std::min(state.FootprintBottom[index], offset.y);
                state.FootprintTop[index] = std::max(state.FootprintTop[index], offset.y);
            }
            for (int dz = -half; dz <= half; ++dz)
            {
                for (int dx = -half; dx <= half; ++dx)
                {
                    const size_t index = static_cast<size_t>(dz + half) * static_cast<size_t>(side) + static_cast<size_t>(dx + half);
                    if (state.FootprintBottom[index] <= state.FootprintTop[index])
                    {
                        footprint.Cells.push_back(FootprintCell{static_cast<int16_t>(dx), static_cast<int16_t>(dz),
                                                                state.FootprintBottom[index], state.FootprintTop[index]});
                    }
                }
            }
            footprint.bBuilt = true;
            return footprint;
        }

        /// 処理中の塚の格子の高さ（格子の外は床の 0）
        float MoundFieldAt(const BuilderState &state, int cellX, int cellZ)
        {
            const int localX = cellX - state.FieldMinX;
            const int localZ = cellZ - state.FieldMinZ;
            if (localX < 0 || localX >= state.FieldSize || localZ < 0 || localZ >= state.FieldSize)
            {
                return 0.0f;
            }
            return state.FieldHeights[static_cast<size_t>(localZ) * static_cast<size_t>(state.FieldSize) + static_cast<size_t>(localX)];
        }

        /// 塚の格子を用意する（落とす位置の最大半径 + 候補の半径 + バナナの包む半径を覆う）
        void OpenMound(BuilderState &state)
        {
            const MoundSite &mound = state.Mounds[state.CurrentMound];
            const float half = mound.MaxRadius + kMoundLocalRadius + static_cast<float>(state.FootprintHalfCells + 1) * kMoundCellMeters;
            state.FieldMinX = static_cast<int>(std::floor((mound.CenterX - half) / kMoundCellMeters));
            state.FieldMinZ = static_cast<int>(std::floor((mound.CenterZ - half) / kMoundCellMeters));
            state.FieldSize = static_cast<int>(std::ceil(2.0f * half / kMoundCellMeters)) + 2;
            state.FieldHeights.assign(static_cast<size_t>(state.FieldSize) * static_cast<size_t>(state.FieldSize), 0.0f);
            state.MoundPlaced = 0;
            state.MoundPeak = 0.0f;
            state.MoundMaxGap = std::numeric_limits<float>::lowest();
            state.MoundMinGap = std::numeric_limits<float>::max();
            state.MoundFloating = 0;
            state.MoundStacked = 0;
            // 乱数は塚の番号で始める（山・海の乱数と独立）
            state.MoundRandom.Counter = HashIndex(state.CurrentMound * 0x85EBCA77u + 0x27D4EB2Fu);
        }

        void CloseMound(BuilderState &state)
        {
            const MoundSite &mound = state.Mounds[state.CurrentMound];
            LOG_INFO_F("MEGA_BANANAS_MOUND_GAP pile=%u kind=mound count=%u center_x=%.1f center_z=%.1f sigma=%.3f "
                       "peak_height=%.4f stacked=%u max_gap_mm=%.3f min_gap_mm=%.3f floating=%u logged=%d",
                       static_cast<uint32_t>(kPileCount) + state.CurrentMound, state.MoundPlaced,
                       static_cast<double>(mound.CenterX), static_cast<double>(mound.CenterZ), static_cast<double>(mound.Sigma),
                       static_cast<double>(state.MoundPeak), state.MoundStacked, static_cast<double>(state.MoundMaxGap) * 1000.0,
                       static_cast<double>(state.MoundMinGap) * 1000.0, state.MoundFloating, state.CurrentMound == 0 ? 1 : 0);
            ++state.CurrentMound;
            state.bMoundOpen = false;
        }

        /// 塚に 1 本置く。中心から正規分布で落とす位置を 1 つ引き（最大半径の外は引き直す）、その近くの候補のうち最も低く収まる
        /// 位置に、下の形へ初めて触れる高さで置く。候補を近くに限るので、局所のくぼみは埋まり、全体の形は落とす位置の分布
        /// （正規分布の塚）に従う（候補を塚全体から引くと、低い所から均一に埋まって頂の平らな円柱になる）。
        /// 位置は升の中心へ、向き・大きさは段へ丸める（足跡が全頂点の高さを升ごとにそのまま持つ）
        void PlaceMoundBanana(BuilderState &state)
        {
            const MoundSite &mound = state.Mounds[state.CurrentMound];
            PlacementRandom &random = state.MoundRandom;
            float dropRadius = mound.MaxRadius;
            for (int draw = 0; draw < 64; ++draw)
            {
                dropRadius = mound.Sigma * std::sqrt(-2.0f * std::log(std::max(random.Next(), 1.0e-6f)));
                if (dropRadius <= mound.MaxRadius)
                {
                    break;
                }
            }
            dropRadius = std::min(dropRadius, mound.MaxRadius);
            const float dropAngle = 2.0f * kPi * random.Next();
            const float dropX = mound.CenterX + std::cos(dropAngle) * dropRadius;
            const float dropZ = mound.CenterZ + std::sin(dropAngle) * dropRadius;
            float bestRequired = std::numeric_limits<float>::max();
            int bestCellX = 0;
            int bestCellZ = 0;
            int bestYaw = 0;
            int bestScale = 0;
            for (int candidate = 0; candidate < kMoundCandidates; ++candidate)
            {
                // 最初の候補は落とした位置、残りは落とした位置の周り kMoundLocalRadius の円の中
                const float localRadius = candidate == 0 ? 0.0f : kMoundLocalRadius * std::sqrt(random.Next());
                const float localAngle = 2.0f * kPi * random.Next();
                const int cellX = static_cast<int>(std::floor((dropX + std::cos(localAngle) * localRadius) / kMoundCellMeters));
                const int cellZ = static_cast<int>(std::floor((dropZ + std::sin(localAngle) * localRadius) / kMoundCellMeters));
                const int yawStep = std::min(static_cast<int>(random.Next() * kMoundYawSteps), kMoundYawSteps - 1);
                const int scaleStep = std::min(static_cast<int>(random.Next() * kMoundScaleSteps), kMoundScaleSteps - 1);
                const MoundFootprint &footprint = GetFootprint(state, yawStep, scaleStep);
                // 自分の最下面が、下の形（と床）のどの升にも食い込まない最小の原点の高さ
                float required = 0.0f;
                for (const FootprintCell &cell : footprint.Cells)
                {
                    required = std::max(required, MoundFieldAt(state, cellX + cell.DX, cellZ + cell.DZ) - cell.Bottom);
                }
                if (required < bestRequired)
                {
                    bestRequired = required;
                    bestCellX = cellX;
                    bestCellZ = cellZ;
                    bestYaw = yawStep;
                    bestScale = scaleStep;
                }
            }

            const MoundFootprint &footprint = GetFootprint(state, bestYaw, bestScale);
            const float y = bestRequired - kSinkMeters;
            float gap = std::numeric_limits<float>::max();
            float support = 0.0f;
            for (const FootprintCell &cell : footprint.Cells)
            {
                const float below = MoundFieldAt(state, bestCellX + cell.DX, bestCellZ + cell.DZ);
                const float difference = y + cell.Bottom - below;
                if (difference < gap)
                {
                    gap = difference;
                    support = below;
                }
            }
            for (const FootprintCell &cell : footprint.Cells)
            {
                const int localX = bestCellX + cell.DX - state.FieldMinX;
                const int localZ = bestCellZ + cell.DZ - state.FieldMinZ;
                if (localX < 0 || localX >= state.FieldSize || localZ < 0 || localZ >= state.FieldSize)
                {
                    continue;
                }
                float &height =
                    state.FieldHeights[static_cast<size_t>(localZ) * static_cast<size_t>(state.FieldSize) + static_cast<size_t>(localX)];
                height = std::max(height, y + cell.Top);
                state.MoundPeak = std::max(state.MoundPeak, height);
            }
            state.MoundMaxGap = std::max(state.MoundMaxGap, gap);
            state.MoundMinGap = std::min(state.MoundMinGap, gap);
            state.MoundFloating += gap > kFloatingGapMeters ? 1u : 0u;
            state.MoundStacked += support > 0.01f ? 1u : 0u;

            // 原点は升の中心
            const double x = (static_cast<double>(bestCellX) + 0.5) * static_cast<double>(kMoundCellMeters);
            const double z = (static_cast<double>(bestCellZ) + 0.5) * static_cast<double>(kMoundCellMeters);
            const float yaw = MoundYaw(bestYaw);
            const float scale = MoundScale(bestScale);
            AddBanana(state.Instances, static_cast<float>(x), y, static_cast<float>(z), yaw, scale);
#if NORVES_BUILD_DEVELOPMENT
            if (state.CurrentMound == 0)
            {
                // 検算用の塚だけ、置いた姿勢を記録する（外部の検算が頂点から接触の隙間を数える）
                LOG_INFO_F("MEGA_BANANAS_PILE_ITEM pile=%u index=%u x=%.5f y=%.5f z=%.5f yaw=%.6f scale=%.6f",
                           static_cast<uint32_t>(kPileCount) + state.CurrentMound, state.MoundPlaced, x,
                           static_cast<double>(y), z, static_cast<double>(yaw), static_cast<double>(scale));
            }
#endif
            ++state.MoundPlaced;
            ++state.Stats.MoundCount;
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
            outShape.FullPoints.reserve(vertexCount);
            for (size_t index = 0; index < vertexCount; ++index)
            {
                const Asset::CookedMeshVertex &vertex = mesh.Vertices[index];
                const Math::Vector3 point(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
                outShape.FullPoints.push_back(point);
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
        // 芯の球: 長手（ローカル Y）を kSpineSphereCount 個に区切り、区切りごとの頂点の箱の中心から最も遠い頂点までを半径にする
        // （頂点を全部包むので、球どうしが離れていればメッシュも重ならない）
        {
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            for (const Asset::CookedMeshVertex &vertex : mesh.Vertices)
            {
                minY = std::min(minY, vertex.Position.Y);
                maxY = std::max(maxY, vertex.Position.Y);
            }
            const float length = std::max(maxY - minY, 1.0e-6f);
            const auto binOf = [&](float y)
            {
                const int bin = static_cast<int>((y - minY) / length * static_cast<float>(kSpineSphereCount));
                return static_cast<size_t>(std::clamp(bin, 0, static_cast<int>(kSpineSphereCount) - 1));
            };
            VariableArray<Math::Vector3> boxMin(kSpineSphereCount, Math::Vector3(std::numeric_limits<float>::max(),
                                                                                 std::numeric_limits<float>::max(),
                                                                                 std::numeric_limits<float>::max()));
            VariableArray<Math::Vector3> boxMax(kSpineSphereCount, Math::Vector3(std::numeric_limits<float>::lowest(),
                                                                                 std::numeric_limits<float>::lowest(),
                                                                                 std::numeric_limits<float>::lowest()));
            for (const Asset::CookedMeshVertex &vertex : mesh.Vertices)
            {
                const size_t bin = binOf(vertex.Position.Y);
                boxMin[bin] = Math::Vector3(std::min(boxMin[bin].x, vertex.Position.X), std::min(boxMin[bin].y, vertex.Position.Y),
                                            std::min(boxMin[bin].z, vertex.Position.Z));
                boxMax[bin] = Math::Vector3(std::max(boxMax[bin].x, vertex.Position.X), std::max(boxMax[bin].y, vertex.Position.Y),
                                            std::max(boxMax[bin].z, vertex.Position.Z));
            }
            VariableArray<float> radiusSq(kSpineSphereCount, 0.0f);
            for (const Asset::CookedMeshVertex &vertex : mesh.Vertices)
            {
                const size_t bin = binOf(vertex.Position.Y);
                const float diffX = vertex.Position.X - 0.5f * (boxMin[bin].x + boxMax[bin].x);
                const float diffY = vertex.Position.Y - 0.5f * (boxMin[bin].y + boxMax[bin].y);
                const float diffZ = vertex.Position.Z - 0.5f * (boxMin[bin].z + boxMax[bin].z);
                radiusSq[bin] = std::max(radiusSq[bin], diffX * diffX + diffY * diffY + diffZ * diffZ);
            }
            for (size_t bin = 0; bin < kSpineSphereCount; ++bin)
            {
                if (boxMin[bin].x > boxMax[bin].x)
                {
                    // 頂点の無い区切り（無いはずだが、あれば球を作らない）
                    continue;
                }
                const Math::Vector3 center(0.5f * (boxMin[bin].x + boxMax[bin].x), 0.5f * (boxMin[bin].y + boxMax[bin].y),
                                           0.5f * (boxMin[bin].z + boxMax[bin].z));
                const float radius = std::sqrt(radiusSq[bin]);
                outShape.SpineCenters.push_back(center);
                outShape.SpineRadii.push_back(radius);
                const float reach = std::sqrt(center.x * center.x + center.y * center.y + center.z * center.z) + radius;
                outShape.SpineBoundRadius = std::max(outShape.SpineBoundRadius, reach);
            }
        }
    }

    BananaFieldBuilder::BananaFieldBuilder() : m_pState(MakeUnique<State>()) {}

    BananaFieldBuilder::~BananaFieldBuilder() = default;

    float ComputeSeaRadius(uint32_t count)
    {
        const uint32_t fieldCount = count - PileBudget(count) - MoundBudget(count, false);
        return static_cast<float>(std::sqrt(static_cast<double>(fieldCount) / SeaUnitDiskCount()));
    }

    float SeaFullDensityEnd()
    {
        // 縁がいちばん内側へゆれた所から、密度を 0 へ落とし始める所まで
        return 1.0f - kSeaEdgeWobble - kSeaEdgeTaper;
    }

    float SeaOuterEdge()
    {
        return 1.0f + kSeaEdgeWobble;
    }

    void BananaFieldBuilder::Begin(const BananaShape &shape, uint32_t count, const IBananaFieldMask *pMask)
    {
        m_pState = MakeUnique<State>();
        State &state = *m_pState;
        state.pShape = &shape;
        state.pMask = pMask;
        state.Stats.Requested = count;
        state.Stats.CenterX = kSeaCenterX;
        state.Stats.CenterZ = kSeaCenterZ;
        if (shape.SamplePoints.empty() || shape.DensePoints.empty() || shape.SpineCenters.empty())
        {
            state.Stats.Deficit = count;
            return;
        }
        const auto startTime = std::chrono::steady_clock::now();
        state.Instances.reserve(count);

        // 大きな塚の場所と本数（積むのは Step。海はこの周りを空ける）。番号 0 の検算用の塚は kCheckMoundBananas 本、残りを等分する
        const uint32_t moundBudget = shape.FullPoints.empty() ? 0u : MoundBudget(count, pMask != nullptr);
        if (moundBudget > 0)
        {
            const uint32_t checkCount = std::min(kCheckMoundBananas, moundBudget / kMoundCount);
            const uint32_t restCount = moundBudget - checkCount;
            const uint32_t others = static_cast<uint32_t>(kMoundCount - 1);
            for (int mound = 0; mound < kMoundCount; ++mound)
            {
                MoundSite site;
                site.CenterX = kMoundCenters[mound][0];
                site.CenterZ = kMoundCenters[mound][1];
                const uint32_t other = static_cast<uint32_t>(mound - 1);
                site.Count = mound == 0 ? checkCount : restCount / others + (other < restCount % others ? 1u : 0u);
                site.Sigma = PileSigmaFor(site.Count) * kMoundSigmaScale;
                site.MaxRadius = site.Sigma * kPileMaxSigmas;
                // 裾のバナナ（中心は最大半径 + 候補の半径まで）と海のバナナが重ならない半径
                site.ExcludeRadius = site.MaxRadius + kMoundLocalRadius + 2.0f * shape.SpineBoundRadius * kScaleMax;
                state.Mounds.push_back(site);
            }
            state.Stats.MoundSiteCount = static_cast<uint32_t>(kMoundCount);
            state.FootprintHalfCells = static_cast<int>(std::ceil(shape.SpineBoundRadius * kScaleMax / kMoundCellMeters)) + 1;
            state.Footprints.resize(static_cast<size_t>(kMoundYawSteps) * kMoundScaleSteps);
        }

        // 山（乱数は番号 0 から。山の配置は海の形によらず前と同じ）。塚を積むときは、1 本ずつの記録を検算用の塚だけに絞る
        PlacementRandom random;
        const uint32_t pileBudget = PileBudget(count);
        for (int pile = 0; pile < kPileCount; ++pile)
        {
            const uint32_t pileCount =
                pileBudget / kPileCount + (static_cast<uint32_t>(pile) < pileBudget % kPileCount ? 1u : 0u);
            PlacePile(state.Instances, shape, pile, kPileCenters[pile][0], kPileCenters[pile][1], pileCount, random,
                      state.Mounds.empty());
        }
        state.Stats.PileCount = static_cast<uint32_t>(state.Instances.size());

        // 海: 本数から半径を決め（密度の形は本数によらない）、区画へ本数を配る
        const uint32_t fieldCount = count - state.Stats.PileCount - moundBudget;
        if (fieldCount > 0)
        {
            state.Stats.Radius = static_cast<float>(std::sqrt(static_cast<double>(fieldCount) / SeaUnitDiskCount()));
            state.Stats.NearRadius = state.Stats.Radius * 0.5f * (kSeaNearEnd + kSeaMidStart);
            state.Stats.MidRadius = state.Stats.Radius * 0.5f * (kSeaMidEnd + kSeaFarStart);
            // 升の一辺: 2 本の包む半径の和の最大（隣の升の外のバナナとは重なりえない）
            state.CellMeters = 2.0f * shape.SpineBoundRadius * kScaleMax;
            BuildSeaTiles(state, fieldCount);
        }
        state.bDone = state.Tiles.empty() && state.Mounds.empty();
        state.Stats.BuildMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
        if (state.bDone)
        {
            state.Stats.Placed = static_cast<uint32_t>(state.Instances.size());
            state.Stats.Deficit = count - state.Stats.Placed;
        }
    }

    bool BananaFieldBuilder::Step(double budgetSeconds)
    {
        State &state = *m_pState;
        if (state.bDone)
        {
            return true;
        }
        const auto startTime = std::chrono::steady_clock::now();
        const auto elapsedSeconds = [&startTime]()
        { return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count(); };
        const auto finishStep = [&state, &startTime]()
        {
            state.Stats.BuildMilliseconds +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
            ++state.Stats.StepCount;
        };

        // 塚を先に積む（入力では山の直後に塚の順で並ぶ）
        while (state.CurrentMound < state.Mounds.size())
        {
            if (!state.bMoundOpen)
            {
                OpenMound(state);
                state.bMoundOpen = true;
            }
            const uint32_t target = state.Mounds[state.CurrentMound].Count;
            while (state.MoundPlaced < target)
            {
                PlaceMoundBanana(state);
                if (budgetSeconds > 0.0 && state.MoundPlaced % kMoundTimeCheckInterval == 0 && elapsedSeconds() >= budgetSeconds)
                {
                    finishStep();
                    return false;
                }
            }
            CloseMound(state);
            if (state.CurrentMound == state.Mounds.size())
            {
                // 作業用の格子と足跡を返す
                state.FieldHeights = VariableArray<float>();
                state.Footprints = VariableArray<MoundFootprint>();
                state.FootprintBottom = VariableArray<float>();
                state.FootprintTop = VariableArray<float>();
            }
            if (budgetSeconds > 0.0 && elapsedSeconds() >= budgetSeconds)
            {
                finishStep();
                return false;
            }
        }

        while (state.CurrentTile < state.Tiles.size())
        {
            if (!state.bTileOpen)
            {
                OpenTile(state);
                state.bTileOpen = true;
            }
            while (state.TilePlaced < state.TileTarget)
            {
                if (!PlaceSeaBanana(state))
                {
                    // 位置がほぼ全部塞がれた区画。残りは次の区画へ回す
                    break;
                }
                ++state.TilePlaced;
                if (budgetSeconds > 0.0 && state.TilePlaced % kSeaTimeCheckInterval == 0 && elapsedSeconds() >= budgetSeconds)
                {
                    finishStep();
                    return false;
                }
            }
            CloseTile(state);
            state.bTileOpen = false;
            if (state.CurrentTile == state.Tiles.size() && state.pMask != nullptr && state.Carry > 0 && !state.bBackfillTile)
            {
                ReopenTileForCarry(state);
            }
            if (budgetSeconds > 0.0 && state.CurrentTile < state.Tiles.size() && elapsedSeconds() >= budgetSeconds)
            {
                finishStep();
                return false;
            }
        }

        finishStep();
        state.bDone = true;
        state.Stats.Placed = static_cast<uint32_t>(state.Instances.size());
        state.Stats.Deficit = state.Stats.Requested - state.Stats.Placed;
        // 作業用の格子を返す（入力と区画の範囲は SplitBatches のために残す）
        state.CellHead = VariableArray<int32_t>();
        state.RecordNext = VariableArray<int32_t>();
        state.Records = VariableArray<SpineRecord>();
        state.Spheres = VariableArray<SpineSphere>();
        if (state.Stats.Deficit > 0)
        {
            LOG_WARNING_F("mega-bananas: 敷き詰めの位置が足りませんでした（要求 %u・実際 %u）", state.Stats.Requested,
                          state.Stats.Placed);
        }
        return true;
    }

    bool BananaFieldBuilder::IsDone() const
    {
        return m_pState->bDone;
    }

    const VariableArray<Component::MegaScatterInstance> &BananaFieldBuilder::GetInstances() const
    {
        return m_pState->Instances;
    }

    VariableArray<Component::MegaScatterInstance> &BananaFieldBuilder::GetInstances()
    {
        return m_pState->Instances;
    }

    void BananaFieldBuilder::SplitBatches(uint32_t maxPerBatch, VariableArray<uint32_t> &outBatchStarts) const
    {
        const State &state = *m_pState;
        outBatchStarts.clear();
        const uint32_t total = static_cast<uint32_t>(state.Instances.size());
        if (total == 0)
        {
            outBatchStarts.push_back(0);
            return;
        }
        // 山と塚の全部を 1 つ目の単位、区画を 1 つずつの単位として、単位の境で区切る
        uint32_t batchStart = 0;
        uint32_t batchSize = state.Stats.PileCount + state.Stats.MoundCount;
        for (const State::Tile &tile : state.Tiles)
        {
            if (tile.Count == 0)
            {
                continue;
            }
            if (batchSize > 0 && batchSize + tile.Count > maxPerBatch)
            {
                outBatchStarts.push_back(batchStart);
                batchStart = tile.First;
                batchSize = 0;
            }
            batchSize += tile.Count;
        }
        outBatchStarts.push_back(batchStart);
        outBatchStarts.push_back(total);
    }

    const BananaLayoutStats &BananaFieldBuilder::GetStats() const
    {
        return m_pState->Stats;
    }

    uint32_t PlaceBananas(Container::VariableArray<Component::MegaScatterInstance> &instances, const BananaShape &shape,
                          uint32_t count, const IBananaFieldMask *pMask, uint32_t &outPileCount)
    {
        BananaFieldBuilder builder;
        builder.Begin(shape, count, pMask);
        builder.Step(0.0);
        const VariableArray<Component::MegaScatterInstance> &placed = builder.GetInstances();
        instances.reserve(instances.size() + placed.size());
        for (const Component::MegaScatterInstance &instance : placed)
        {
            instances.push_back(instance);
        }
        outPileCount = builder.GetStats().PileCount;
        return static_cast<uint32_t>(placed.size());
    }

} // namespace Game::GameModes
