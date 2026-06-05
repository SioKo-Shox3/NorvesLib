#pragma once

#include <cstdint>
#include "Thread/Atomic.h"
#include "Container/PointerTypes.h"
#include "Container/Containers.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    class IClass;
    class FieldInitializer;

    /**
     * @brief すべてのオブジェクトの基本インターフェース
     * ObjectHeap/GC管理対象の基本的なオブジェクト操作を提供します
     */
    class IUnknown
    {
    public:
        /**
         * @brief デストラクタ
         */
        virtual ~IUnknown() = default;

        /**
         * @brief オブジェクトのクラス情報を取得します
         * @return このオブジェクトのクラス情報へのポインタ
         */
        virtual const IClass *GetClass() const = 0;

        /**
         * @brief オブジェクトを初期化します
         */
        virtual void Initialize() = 0;

        /**
         * @brief オブジェクトの破棄前処理を行います
         */
        virtual void Finalize() = 0;

        /**
         * @brief オブジェクトがフラグを持っているか確認します
         * @param flag 確認するフラグ
         * @return フラグが設定されていればtrue
         */
        virtual bool HasFlag(uint32_t flag) const = 0;

        /**
         * @brief オブジェクトのフラグを設定します
         * @param flag 設定するフラグ
         * @param value フラグの値
         */
        virtual void SetFlag(uint32_t flag, bool value) = 0;

        /**
         * @brief プロパティの値を取得します
         * @param propertyName プロパティ名
         * @return プロパティの値をvoid*として返す
         */
        virtual void *GetPropertyValue(const Identity &propertyName) = 0;

        /**
         * @brief プロパティの値を取得します（const版）
         * @param propertyName プロパティ名
         * @return プロパティの値をconst void*として返す
         */
        virtual const void *GetPropertyValue(const Identity &propertyName) const = 0;

        /**
         * @brief 親オブジェクト（Outer）を取得します
         * @return 親オブジェクトへのポインタ、親がない場合はnullptr
         */
        virtual IUnknown *GetOuter() = 0;

        /**
         * @brief 親オブジェクト（Outer）を取得します（const版）
         * @return 親オブジェクトへの読み取り専用ポインタ、親がない場合はnullptr
         */
        virtual const IUnknown *GetOuter() const = 0;

        /**
         * @brief 子オブジェクト（Inners）のリストを取得します
         * @return 子オブジェクトのリスト
         */
        virtual const Container::VariableArray<IUnknown *> &GetInners() const = 0;

        /**
         * @brief 子オブジェクト（Inner）を追加します
         * @param inner 追加する子オブジェクト
         */
        virtual bool AddInner(IUnknown *inner) = 0;

        /**
         * @brief 子オブジェクト（Inner）を削除します
         * @param inner 削除する子オブジェクト
         * @return 削除に成功した場合はtrue
         */
        virtual bool RemoveInner(IUnknown *inner) = 0;
    };

    /**
     * @brief IUnknownの基本実装
     * フラグやOuter/Innerなどの標準実装を提供します
     */
    class UnknownImpl : public IUnknown
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        UnknownImpl();

        /**
         * @brief フィールド初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit UnknownImpl(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit UnknownImpl(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~UnknownImpl();

        // IUnknownインターフェースの実装
        virtual bool HasFlag(uint32_t flag) const override;
        virtual void SetFlag(uint32_t flag, bool value) override;
        virtual void *GetPropertyValue(const Identity &propertyName) override;
        virtual const void *GetPropertyValue(const Identity &propertyName) const override;

        virtual const IClass *GetClass() const override;
        virtual void Initialize() override;
        virtual void Finalize() override;

        // Outer/Inner関連メソッドの実装
        virtual IUnknown *GetOuter() override;
        virtual const IUnknown *GetOuter() const override;
        void SetOuter(IUnknown *outer); // SetOuterメソッドを追加（非virtual）
        virtual const Container::VariableArray<IUnknown *> &GetInners() const override;
        virtual bool AddInner(IUnknown *inner) override;
        virtual bool RemoveInner(IUnknown *inner) override;

    protected:
        /**
         * @brief 他のオブジェクトからデータをコピーします
         * @param sourceObject コピー元となるオブジェクト
         */
        void CopyFromObject(const IUnknown *sourceObject);

        mutable Thread::Atomic<uint32_t> m_Flags;      // オブジェクトフラグ
        IUnknown *m_Outer;                             // 親オブジェクト
        Container::VariableArray<IUnknown *> m_Inners; // 子オブジェクトのリスト

    private:
        // 代入は禁止
        UnknownImpl &operator=(const UnknownImpl &) = delete;
    };

    /**
     * @brief オブジェクトフラグ定数
     */
    enum ObjectFlags : uint32_t
    {
        OF_None = 0,                // フラグなし
        OF_Initialized = 1 << 0,    // 初期化済み
        OF_PendingDestroy = 1 << 1, // 破棄待ち
        OF_GarbageCollect = 1 << 2, // GC対象
        OF_Transient = 1 << 3,      // 一時的なオブジェクト
        OF_Persistent = 1 << 4, // 永続的なオブジェクト
        OF_HeapOwned = 1 << 5   // ObjectHeapがメモリを所有している
    };

} // namespace NorvesLib::Core
