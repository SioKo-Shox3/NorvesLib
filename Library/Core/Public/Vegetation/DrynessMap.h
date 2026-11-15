#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Rendering/WorldFieldTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core
{
    class JsonWriter;
    class JsonValue;
} // namespace NorvesLib::Core

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

    /** @brief 枯れ度マップの復元に失敗した理由。 */
    enum class DrynessRestoreError : uint8_t
    {
        None,
        NotObject,
        UnsupportedVersion,
        MissingField,
        WrongType,
        UnknownShape,
        NonFinite,
        NonPositiveRadius,
        OutOfRange,
        DuplicateId,
        InvalidId
    };

    /** @brief 機械照合用のエラー名を返す。 */
    const char* GetDrynessRestoreErrorName(DrynessRestoreError error);

    /** @brief 復元の結果と、エラーの原因となった源の添字。 */
    struct DrynessRestoreResult
    {
        DrynessRestoreError Error = DrynessRestoreError::None;
        size_t SourceIndex = SIZE_MAX; // 源に由来しないエラーでは SIZE_MAX。
        bool Succeeded() const { return Error == DrynessRestoreError::None; }
    };

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

    /** @brief 鍵ごとに保存する源の値。実行時の SourceId は含めない。 */
    struct DrynessKeyedState
    {
        Container::String Key;
        float CurrentLevel = 0;
        float TargetLevel = 0;
        float RemainingSeconds = 0;
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
     * 登録時に鍵を結び、源を除くと値を保持し、同じ鍵が結ばれると値を移す。
     * 読み込んだ値は次の有効な Tick まで保留し、結ばれなければ未知の鍵として捨てる。
     * セーブには CaptureKeyedStates / RestoreKeyedStates を使い、SourceId は保存しない。
     * GameThread 専用であり、排他制御は行わない。
     */
    class DrynessMap
    {
    public:
        /** @brief GPU の UBO の上限と同じ定数を使う。 */
        static constexpr uint32_t DefaultGpuSourceLimit = Rendering::WorldFieldDrynessMaxSources;

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

        /**
         * @brief 源に鍵を結ぶ。空または内部に終端文字を含む鍵は警告して false。
         * 不在 ID、結合済みの源、手放されていない源と重複する鍵は false（重複は警告）。
         * 手放された源または保持から現在値・目標値・残り秒数を移し、遷移秒数を残り秒数にする。
         * 古い源と保持は消し、移す前の新しい源の現在値を復元時の既定値として覚える。
         */
        bool BindKey(DrynessSourceId id, const Container::String& key);

        /**
         * @brief 所有者が源を手放す。不在 ID は false。
         * 鍵を結んで遷移中なら到達まで源を残し、それ以外は Remove と同様に除く。
         */
        bool Release(DrynessSourceId id);

        /** @brief 鍵を結んだ生きた源（手放した源を含む）の ID を返す。不在なら無効 ID。 */
        DrynessSourceId FindByKey(const Container::String& key) const;

        /** @brief 鍵で見つかる源に SetTarget を行う。不在なら false。 */
        bool SetTargetByKey(const Container::String& key, float level, float seconds);

        /**
         * @brief out を空にし、鍵ごとに生きた源の現在値・目標値・残り秒数、または保持の値を写す。
         * 保留中の保持も含み、鍵の昇順（String の比較順）に並べる。
         */
        void CaptureKeyedStates(Container::VariableArray<DrynessKeyedState>& out) const;

        /**
         * @brief 鍵ごとの値を置き換える。鍵は空でなく内部に終端文字を含まないものに限る。
         * 現在値・目標値は有限の [0, 1]、残り秒数は有限の 0 以上とし、不正な項目は警告して飛ばす。
         * 重複する鍵は警告して最初の有効な項目を使い、他の項目は読み続ける。
         * 手放した源と生きた源の無い鍵を消し、残る源は入力の値へ、入力に無ければ既定値へ戻す。
         * 入力の残りは次の有効な Tick まで保留し、結ばれれば移し、結ばれなければ警告して捨てる。
         * 値を移す際は遷移秒数を残り秒数とし、既定値へ戻す際は目標も同じ値で秒数は 0 とする。
         */
        void RestoreKeyedStates(Container::Span<const DrynessKeyedState> states);

        /** @brief ID が一致する源を除き、鍵の値を保持して残りの順序を保つ。不在 ID なら false。 */
        bool Remove(DrynessSourceId id);

        /** @brief すべての源と鍵の表を除く。発行済み ID と GPU 上限はリセットしない。 */
        void Clear();

        /**
         * @brief 有限かつ 0 以上の秒数だけ各源を進める。不正な秒数では何もしない。
         * 残り秒数以上なら現在値を目標値に合わせ、残りを 0 にする。
         * それ以外では現在値に (目標値 - 現在値) * (経過秒数 / 残り秒数) を足し、
         * 残り秒数から経過秒数を引く。これにより一定の速さで指定秒数に到達する。
         * 到達した手放された源を除いて値を保持し、結ばれなかった保留値は警告して捨てる。
         */
        void Tick(float deltaSeconds);

        /** @brief 指定したワールド XZ で各源の寄与の最大値を求める。非有限の点では 0 を返す。 */
        float Sample(float x, float z) const;

        /** @brief key が null なら値のオブジェクトだけを書き、それ以外なら指定したキーで書く。 */
        void Serialize(JsonWriter& writer, const char* key = nullptr) const;

        /** @brief 全検証の成功時だけ源と次の ID を置き換え、鍵の表を空にする。失敗時は何も変えず、GPU 上限は保持する。 */
        DrynessRestoreResult Restore(const JsonValue& value);

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
        /** @brief 生きた源との結合、登録時の既定値、未結合時の保持と読み込み保留を管理する。 */
        struct KeyEntry
        {
            Container::String Key;
            DrynessSourceId LiveId = InvalidDrynessSourceId;
            float DefaultLevel = 0;
            DrynessKeyedState Held;
            bool bHasHeld = false;
            bool bReleased = false;
            bool bPendingRestore = false;
        };

        Container::VariableArray<DrynessSource> m_Sources;
        Container::VariableArray<KeyEntry> m_Keys;
        DrynessSourceId m_NextId = 1;
        uint32_t m_GpuSourceLimit = DefaultGpuSourceLimit;
    };
} // namespace NorvesLib::Core::Vegetation
