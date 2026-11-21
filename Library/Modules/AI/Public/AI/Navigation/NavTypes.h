#pragma once

#include "Container/String.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Modules::AI
{
    /// XZ 平面上の格子の原点、セルの大きさ、タイルの一辺のセル数。
    struct NavGridDesc
    {
        float OriginX = 0.0f;
        float OriginZ = 0.0f;
        float CellSize = 1.0f;
        uint32_t TileCells = 64;

        /// セルの大きさが有限の正数で、一辺のセル数が 1～1024 なら有効。
        bool IsValid() const
        {
            return std::isfinite(CellSize) && CellSize > 0.0f && TileCells >= 1 && TileCells <= 1024;
        }
    };

    /// 格子上のタイル座標。タイルは Z、X の順に昇順で並ぶ。
    struct NavTileCoord
    {
        int32_t X = 0;
        int32_t Z = 0;

        /// X と Z がともに等しければ同じタイル座標とする。
        bool operator==(const NavTileCoord&) const = default;

        /// Z を上位 32 ビット、X を下位 32 ビットに格納する。
        uint64_t ToKey() const
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(Z)) << 32) | static_cast<uint32_t>(X);
        }
    };

    /// 格子全体を基準にしたセル座標。
    struct NavCellCoord
    {
        int32_t X = 0;
        int32_t Z = 0;

        /// X と Z がともに等しければ同じセル座標とする。
        bool operator==(const NavCellCoord&) const = default;

        /// Z を上位 32 ビット、X を下位 32 ビットに格納する。
        uint64_t ToKey() const
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(Z)) << 32) | static_cast<uint32_t>(X);
        }
    };

    /// セルの障害物、水深区分、傾斜を表すビットフラグ。
    enum class ENavCellFlags : uint8_t
    {
        None = 0,
        StaticBlocked = 1u << 0,
        WaterShallow = 1u << 1,
        WaterDeep = 1u << 2,
        Cliff = 1u << 3,
        SteepSlope = 1u << 4
    };

    /// 静的障害物、深い水、崖、急斜面のいずれかで通行を阻む。
    inline constexpr uint8_t kNavBlockingFlags =
        static_cast<uint8_t>(ENavCellFlags::StaticBlocked) |
        static_cast<uint8_t>(ENavCellFlags::WaterDeep) |
        static_cast<uint8_t>(ENavCellFlags::Cliff) |
        static_cast<uint8_t>(ENavCellFlags::SteepSlope);

    /// 指定したフラグのビットが立っているかを返す。
    inline bool HasNavFlag(uint8_t flags, ENavCellFlags flag)
    {
        return (flags & static_cast<uint8_t>(flag)) != 0;
    }

    /// 縦横に 1 セル進むときの基本コスト。
    inline constexpr uint32_t kNavCostOrthogonal = 128;
    /// 斜めに 1 セル進むときの基本コスト。
    inline constexpr uint32_t kNavCostDiagonal = 181;
    /// 通行不能を表すコスト。
    inline constexpr uint32_t kNavCostImpassable = 0xFFFFFFFFu;
    /// セル幅に相当するクリアランスの量子化段数。
    inline constexpr uint32_t kNavClearanceStepsPerCell = 8;

    /// ClearanceQ で表せる上限（255）を超える半径のしきい値。どのセルも満たさない。
    inline constexpr uint16_t kNavClearanceThresholdUnreachable = 256;

    /// 半径をセル幅の 1/8 単位で切り上げ、255 を超えたら 256 を返す。半径やセル幅が非有限または 0 以下なら 0。
    inline uint16_t NavClearanceThresholdQ(float radius, float cellSize)
    {
        if (!std::isfinite(radius) || radius <= 0.0f || !std::isfinite(cellSize) || cellSize <= 0.0f)
        {
            return 0;
        }
        const double steps = static_cast<double>(radius) * kNavClearanceStepsPerCell / static_cast<double>(cellSize);
        if (steps > 255.0)
        {
            return kNavClearanceThresholdUnreachable;
        }
        return static_cast<uint16_t>(std::ceil(steps));
    }

    /// クリアランスをセル幅の 1/8 単位で切り捨てる。下限は 0、上限は 255 で、NaN は 0。
    inline uint8_t QuantizeNavClearance(float clearanceCells)
    {
        if (!(clearanceCells > 0.0f))
        {
            return 0;
        }
        const double steps = static_cast<double>(clearanceCells) * kNavClearanceStepsPerCell;
        if (steps >= 255.0)
        {
            return 255;
        }
        return static_cast<uint8_t>(steps);
    }

    /// 傾斜角を 0～90 度に収め、0～255 の整数に四捨五入する。NaN は 0。
    inline uint8_t QuantizeNavSlopeDegrees(float degrees)
    {
        if (!(degrees > 0.0f))
        {
            return 0;
        }
        if (degrees >= 90.0f)
        {
            return 255;
        }
        return static_cast<uint8_t>(std::floor(static_cast<double>(degrees) * 255.0 / 90.0 + 0.5));
    }

    /// 量子化した傾斜を 0～90 度の角度に戻す。
    inline float NavSlopeQToDegrees(uint8_t slopeQ)
    {
        return static_cast<float>(slopeQ) * (90.0f / 255.0f);
    }

    /// 通行を阻むフラグがなく、クリアランスがしきい値以上なら通れる。
    inline bool IsNavCellPassable(uint8_t flags, uint8_t clearanceQ, uint16_t thresholdQ)
    {
        return (flags & kNavBlockingFlags) == 0 && clearanceQ >= thresholdQ;
    }

    /// 経路の問い合わせに使うエージェントの半径と地形別の追加コスト。
    struct NavAgentProfile
    {
        Core::Container::String Name;
        float Radius = 0.5f;                  // 問い合わせに使う半径（m）
        uint16_t ShallowWaterExtraPer128 = 0;  // 浅い水に入るときの基本コスト 128 あたりの追加量
        uint16_t GrassExtraPer128 = 0;         // 草の密度が 255 のときの基本コスト 128 あたりの追加量
    };

    /// 浅い水の追加量と草の密度に比例する追加量を足し、基本コスト 128 あたりの値を返す。
    inline uint32_t NavCellExtraPer128(const NavAgentProfile& profile, uint8_t flags, uint8_t grassQ)
    {
        const uint32_t water = HasNavFlag(flags, ENavCellFlags::WaterShallow) ? profile.ShallowWaterExtraPer128 : 0u;
        const uint32_t grass = static_cast<uint32_t>(grassQ) * profile.GrassExtraPer128 / 255u;
        return water + grass;
    }

    /// 一歩の基本コストに地形分の割増を切り捨てて加え、通行不能を表す値を上限とする。
    inline uint32_t NavStepCost(bool bDiagonal, uint32_t extraPer128)
    {
        const uint32_t base = bDiagonal ? kNavCostDiagonal : kNavCostOrthogonal;
        const uint64_t cost = base + static_cast<uint64_t>(base) * extraPer128 / kNavCostOrthogonal;
        return cost >= kNavCostImpassable ? kNavCostImpassable : static_cast<uint32_t>(cost);
    }

    /// 経路を添字と世代番号で識別するハンドル。
    struct PathHandle
    {
        uint32_t Index = 0;
        uint32_t Generation = 0;

        /// 世代番号が 0 以外なら有効。
        bool IsValid() const
        {
            return Generation != 0;
        }

        /// 添字と世代番号がともに等しければ同じハンドルとする。
        bool operator==(const PathHandle&) const = default;
    };

    /// 経路探索の進行状態と結果。
    enum class ENavPathStatus : uint8_t
    {
        Invalid = 0,
        Pending,
        Succeeded,
        Partial,
        Failed,
        Cancelled
    };

    /// タイルの読み込み結果。
    enum class ENavTileResult : uint8_t
    {
        Ok = 0,
        InvalidTile,
        Mismatch
    };
} // namespace NorvesLib::Modules::AI
