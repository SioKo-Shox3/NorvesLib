#pragma once

/**
 * @file EcsTypes.h
 * @brief ECS の実体の識別子と、列（コンポーネントの型）の記述
 *
 * EcsEntity は「表の番号 + 世代」の 64 bit。解放した番号は世代を進めてから再利用するので、
 * 古い識別子は IsAlive や取得で拒まれる。世代は 1 から始まり、全ビット 0 の識別子は常に null を表す。
 *
 * 列の型は trivially copyable に限る（移動と入れ替えは memcpy）。所有や参照カウントを持つ値は
 * 列に置かず Object に残す。列の型の番号は TypeRegistry::GetTypeId<T>() の結果を使う。
 *
 * 列の型は、EcsWorld の初期化とシステムの登録のとき（GameThread、並列が始まる前）に
 * EcsRegisterColumn<T>() で登録して番号を固定する。TypeRegistry は同期なしで書き換わるので、
 * 並列の中（EcsParallelScope の間）では引かない。未登録の型を並列の中で使うと、開発ビルドで assert する。
 */

#include "Object/RuntimeSchema.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Ecs
{
    /// 実体の識別子。Index は実体の表の番号、Generation は再利用のたびに進む世代（0 は null）。
    struct EcsEntity
    {
        uint32_t Index = 0;
        uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsNull() const
        {
            return Generation == 0;
        }

        [[nodiscard]] constexpr bool operator==(const EcsEntity &other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }

        [[nodiscard]] constexpr bool operator!=(const EcsEntity &other) const
        {
            return !(*this == other);
        }
    };

    inline constexpr EcsEntity kNullEcsEntity{};

    class EcsWorld;

    /// プロパティが値を探す行。World が null、または Entity が null なら「行が無い」（値は Object の予備の値）。
    struct EcsRowRef
    {
        EcsWorld *World = nullptr;
        EcsEntity Entity;

        [[nodiscard]] bool IsBound() const
        {
            return World != nullptr && !Entity.IsNull();
        }
    };

    /// Object を持つ実体の行 → Object の弱い参照（強い参照ではない）。Object が行を切る前に null にするので、
    /// 生きた行では常に Object を指す。Generation は行の EcsEntity の世代（行の作り直しとの取り違えを見分ける）。
    struct EcsObjectRef
    {
        void *Object = nullptr;
        uint32_t Generation = 0;
        uint32_t Reserved = 0;
    };

    /// Object を持つ実体の階層。World 直下（根）の Parent は null で、根どうしの兄弟の連結は持たない。
    /// 子の並びは Outer/Inner の並び（Entity だけを取り出した順）と同じ。Depth は根が 0。
    struct EcsHierarchy
    {
        EcsEntity Parent;
        EcsEntity FirstChild;
        EcsEntity LastChild;
        EcsEntity NextSibling;
        EcsEntity PrevSibling;
        uint32_t Depth = 0;
        uint32_t Reserved = 0;
    };

    /// アーキタイプの番号（EcsWorld が割り当てる）
    using EcsArchetypeId = uint32_t;
    inline constexpr EcsArchetypeId kInvalidEcsArchetype = 0xFFFFFFFFu;

    /// 列 1 本の記述。TypeId は TypeRegistry の番号、Size / Align は値 1 つの大きさと配置。
    struct EcsColumnInfo
    {
        TypeId Type = 0;
        uint32_t Size = 0;
        uint32_t Align = 1;
    };

    namespace Detail
    {
        /// 並列の区間に入る / 出る（深さを数える）。EcsParallelScope を使う。
        void EcsEnterParallel();
        void EcsLeaveParallel();
        [[nodiscard]] bool EcsIsParallelActive();

        /// ECS_PROPERTY の読み書きの前に通す共通の検査。並列の中なら報告して false を返す（開発ビルドだけ。
        /// ハンドラが無ければ abort する。NDEBUG に依存しない）。false のときは呼び出し側が列にも予備にも触らない。
        [[nodiscard]] bool EcsCheckSerialAccess();

        /// 並列の中で未登録の列の型が使われたときの報告。開発ビルドでは assert する（ハンドラがあればそれを呼ぶ）。
        void EcsReportUnregisteredColumn();

        /// 型 T の列を EcsRegisterColumn で登録済みか
        template <typename T>
        inline std::atomic<bool> g_EcsColumnRegistered{false};
    }

    /// 並列の区間を表す。この間は TypeRegistry を書き換える登録をしてはならない。
    class EcsParallelScope
    {
    public:
        EcsParallelScope()
        {
            Detail::EcsEnterParallel();
        }

        ~EcsParallelScope()
        {
            Detail::EcsLeaveParallel();
        }

        EcsParallelScope(const EcsParallelScope &) = delete;
        EcsParallelScope &operator=(const EcsParallelScope &) = delete;
    };

    // ---- ECS_PROPERTY の読み書き口（GameThread かつ並列の外だけ。並列の中で呼ぶと開発ビルドでは報告し、失敗を返す） ----

    /// 行の列 column の [offset, offset + size) を out へ写す。行が無い・生きていない・その列を持たない・範囲が列の外なら false。
    [[nodiscard]] bool EcsReadColumnPart(const EcsRowRef &row, TypeId column, uint32_t offset, uint32_t size, void *out);

    /// 行の列 column の [offset, offset + size) を書き、変更として記録して版を付ける。失敗の条件は EcsReadColumnPart と同じ。
    bool EcsWriteColumnPart(const EcsRowRef &row, TypeId column, uint32_t offset, uint32_t size, const void *value);

    /// 行が列 column を持つか（持てばプロパティの値は列にある）
    [[nodiscard]] bool EcsRowHasColumn(const EcsRowRef &row, TypeId column);

    /// 行の列 column の [offset, ...) へのポインタ（読み取り専用。構造変更をまたいで持たない）。無ければ nullptr。
    [[nodiscard]] const void *EcsGetColumnPartPtr(const EcsRowRef &row, TypeId column, uint32_t offset, uint32_t size);

    using EcsUnregisteredColumnHandler = void (*)();

    /// 未登録の報告を assert の代わりに受けるハンドラを差し替える（テスト用）。前のハンドラを返す。
    EcsUnregisteredColumnHandler EcsSetUnregisteredColumnHandler(EcsUnregisteredColumnHandler handler);

    using EcsParallelAccessHandler = void (*)();

    /// 並列の中での ECS_PROPERTY の読み書きの報告を abort の代わりに受けるハンドラを差し替える（テスト用）。前のハンドラを返す。
    EcsParallelAccessHandler EcsSetParallelAccessHandler(EcsParallelAccessHandler handler);

    /// 並列の中で ECS_PROPERTY の読み書きが試みられた回数
    [[nodiscard]] uint32_t EcsGetParallelAccessReportCount();

    /// 並列の中で未登録の列の型が使われた回数
    [[nodiscard]] uint32_t EcsGetUnregisteredColumnReportCount();

    /**
     * @brief 型 T を列の型として登録し、記述を返す（GameThread、並列が始まる前に呼ぶ）
     *
     * 登録した型の番号は固定される。T は trivially copyable でなければならない。
     * 並列の中で未登録の T に使うと報告し、型の番号が InvalidSchemaId の記述を返す（アーキタイプには使えない）。
     */
    template <typename T>
    [[nodiscard]] EcsColumnInfo EcsRegisterColumn()
    {
        static_assert(std::is_trivially_copyable_v<T>, "ECS の列の型は trivially copyable でなければならない");
        static_assert(std::is_trivially_destructible_v<T>, "ECS の列の型は自明に破棄できなければならない");
        static_assert(sizeof(T) > 0 && alignof(T) <= 64, "ECS の列の型の配置は 64 byte 以下");
        const uint32_t size = static_cast<uint32_t>(sizeof(T));
        const uint32_t align = static_cast<uint32_t>(alignof(T));
        std::atomic<bool> &registered = Detail::g_EcsColumnRegistered<T>;
        if (!registered.load(std::memory_order_acquire))
        {
            if (Detail::EcsIsParallelActive())
            {
                Detail::EcsReportUnregisteredColumn();
                return EcsColumnInfo{InvalidSchemaId, size, align};
            }
            registered.store(true, std::memory_order_release);
        }
        // 型ごとの番号は最初の登録で覚える
        static const TypeId s_Type = TypeRegistry::Get().GetTypeId<T>();
        return EcsColumnInfo{s_Type, size, align};
    }

    /// 型 T の列の記述を返す。登録済みでなければ、並列の外では登録して返す（並列の中は EcsRegisterColumn と同じ）。
    template <typename T>
    [[nodiscard]] EcsColumnInfo EcsColumnInfoOf()
    {
        return EcsRegisterColumn<T>();
    }

}
