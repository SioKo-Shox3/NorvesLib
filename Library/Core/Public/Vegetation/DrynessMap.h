#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Vegetation
{
    /** @brief 枯れ度の源の形状。 */
    enum class DrynessShape : uint8_t
    {
        Circle = 0,
        Ellipse = 1
    };

    using DrynessSourceId = uint32_t;
    inline constexpr DrynessSourceId InvalidDrynessSourceId = 0;

    /** @brief ワールド XZ 上の解析的な枯れ度の源。長さは m、角度はラジアン。 */
    struct DrynessSource
    {
        DrynessSourceId Id = InvalidDrynessSourceId;
        DrynessShape Shape = DrynessShape::Circle;
        float CenterX = 0, CenterZ = 0;  // ワールドの XZ 座標。
        float RadiusA = 0, RadiusB = 0;  // 内側の局所 X・Z 半径。円は A == B。
        float Yaw = 0;                   // 局所 X 軸をワールド XZ 上で回す角度。
        float Falloff = 0;               // 外縁から 0 になるまでの幅。0 は硬い縁。
        float TargetLevel = 0;           // 目標値 [0, 1]。
        float CurrentLevel = 0;          // 現在値 [0, 1]。
        float TransitionSeconds = 0;     // 最後に設定した遷移秒数。
        float RemainingSeconds = 0;      // 目標到達までの残り秒数。
    };

    /**
     * @brief 円と楕円の源を正本として保持する枯れ度マップ。
     *
     * 半径 A と B は値が 1 となる内側の局所 X・Z 半径で、円は A == B、Yaw == 0。
     * Yaw 回転後の局所 X 軸はワールド XZ 上で (cos(Yaw), sin(Yaw)) を向く。
     * 中心から点への差を dx = x - CenterX、dz = z - CenterZ とすると、
     * lx = dx * cos(Yaw) + dz * sin(Yaw)、lz = -dx * sin(Yaw) + dz * cos(Yaw)。
     * dist = sqrt(lx * lx + lz * lz)、rho = sqrt((lx / A)^2 + (lz / B)^2) とする。
     * rho <= 1 では減衰値は 1。それ以外では e = dist * (1 - 1 / rho) とし、
     * Falloff == 0 なら減衰値は 0、そうでなければ t = clamp(e / Falloff, 0, 1)、
     * 減衰値は 1 - t * t * (3 - 2 * t)。寄与は CurrentLevel と減衰値の積。
     * Sample はすべての寄与の最大値を返し、源がなければ 0 を返す。
     */
    class DrynessMap
    {
    public:
        static constexpr uint32_t DefaultGpuSourceLimit = 64;

        /**
         * @brief 円形の源を追加する。中心・半径・幅・値は有限、半径は正、
         * 幅は 0 以上、値は [0, 1]。不正または ID 枯渇時は何も追加せず無効 ID を返す。
         * 値は現在値と目標値の両方に設定し、遷移時間は 0 とする。
         */
        DrynessSourceId AddCircle(float centerX, float centerZ, float radius, float falloff, float level);

        /**
         * @brief 楕円形の源を追加する。中心・半径・向き・幅・値は有限、
         * 半径は正、幅は 0 以上、値は [0, 1]。不正または ID 枯渇時は何も追加せず無効 ID を返す。
         * 値は現在値と目標値の両方に設定し、遷移時間は 0 とする。
         * ID は 1 から増やし再利用しない。次の ID が UINT32_MAX に達したら追加できない。
         */
        DrynessSourceId AddEllipse(float centerX, float centerZ, float radiusA, float radiusB, float yaw,
                                   float falloff, float level);

        /**
         * @brief 既存の源の目標値と遷移秒数を設定する。値は有限の [0, 1]、秒数は有限かつ 0 以上。
         * TransitionSeconds と RemainingSeconds を設定した秒数にする。
         * 0 秒なら現在値も直ちに目標値にする。不正な入力または不在 ID なら変更せず false。
         */
        bool SetTarget(DrynessSourceId id, float level, float seconds);

        /** @brief 有限の中心を既存の源に設定する。不正な入力または不在 ID なら変更せず false。 */
        bool SetSourceCenter(DrynessSourceId id, float centerX, float centerZ);

        /** @brief ID が一致する源を除き、残りの順序を保つ。不在 ID なら false。 */
        bool Remove(DrynessSourceId id);

        /** @brief すべての源を除く。発行済み ID と GPU 上限はリセットしない。 */
        void Clear();

        /**
         * @brief 有限かつ 0 以上の秒数だけ各源を進める。不正な秒数では何もしない。
         * 残り秒数以上なら現在値を目標値に合わせ、残りを 0 にする。
         * それ以外では現在値に (目標値 - 現在値) * (経過秒数 / 残り秒数) を足し、
         * 残り秒数から経過秒数を引く。これにより一定の速さで指定秒数に到達する。
         */
        void Tick(float deltaSeconds);

        /** @brief 指定したワールド XZ で各源の寄与の最大値を求める。非有限の点では 0 を返す。 */
        float Sample(float x, float z) const;

        /** @brief ID が一致する源を借用し、不在なら null を返す。配列の変更で参照は無効になり得る。 */
        const DrynessSource* Find(DrynessSourceId id) const;

        /** @brief 追加順の源を借用する。配列の変更でビューは無効になり得る。 */
        Container::Span<const DrynessSource> GetSources() const;

        /** @brief 保持している源の数を返す。 */
        size_t GetSourceCount() const;

        /** @brief GPU 向けの源の上限を設定する。0 なら選別結果は空。 */
        void SetGpuSourceLimit(uint32_t limit);

        /** @brief GPU 向けの源の上限を返す。 */
        uint32_t GetGpuSourceLimit() const;

        /** @brief out を空にし、焦点からの中心距離の二乗順、同距離なら ID の昇順に上限まで写す。 */
        void CollectGpuSources(float focusX, float focusZ, Container::VariableArray<DrynessSource>& out) const;

    private:
        Container::VariableArray<DrynessSource> m_Sources;
        DrynessSourceId m_NextId = 1;
        uint32_t m_GpuSourceLimit = DefaultGpuSourceLimit;
    };
} // namespace NorvesLib::Core::Vegetation
