#pragma once

#include <cstdint>
#include "Thread/Public/Atomic.h"
#include <memory>
#include "Container/Containers.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    class IClass;
    class FieldInitializer;

    /**
     * @brief VariableContainer クラス
     * オブジェクトのメンバ変数データを格納する連続メモリ領域を管理します
     */
    class VariableContainer
    {
    public:
        /**
         * @brief コンストラクタ
         * @param size コンテナのサイズ（バイト単位）
         */
        explicit VariableContainer(size_t size)
            : m_Size(size), m_Data(nullptr)
        {
            if (size > 0)
            {
                m_Data = new uint8_t[size]();
            }
        }

        /**
         * @brief デストラクタ
         */
        ~VariableContainer()
        {
            if (m_Data)
            {
                delete[] m_Data;
                m_Data = nullptr;
            }
        }

        /**
         * @brief データへのポインタを取得します
         * @return データへのポインタ
         */
        void *GetData()
        {
            return m_Data;
        }

        /**
         * @brief データへの読み取り専用ポインタを取得します
         * @return データへの読み取り専用ポインタ
         */
        const void *GetData() const
        {
            return m_Data;
        }

        /**
         * @brief サイズを取得します
         * @return コンテナのバイトサイズ
         */
        size_t GetSize() const
        {
            return m_Size;
        }

        /**
         * @brief オフセット位置のデータへのポインタを取得します
         * @param offset コンテナ先頭からのオフセット
         * @return オフセット位置のデータへのポインタ
         */
        void *GetAt(size_t offset)
        {
            if (offset < m_Size && m_Data)
            {
                return m_Data + offset;
            }
            return nullptr;
        }

        /**
         * @brief オフセット位置のデータへの読み取り専用ポインタを取得します
         * @param offset コンテナ先頭からのオフセット
         * @return オフセット位置のデータへの読み取り専用ポインタ
         */
        const void *GetAt(size_t offset) const
        {
            if (offset < m_Size && m_Data)
            {
                return m_Data + offset;
            }
            return nullptr;
        }

        /**
         * @brief メモリをコピーします
         * @param dstOffset コピー先オフセット
         * @param src コピー元データ
         * @param size コピーするバイト数
         * @return コピーが成功した場合はtrue
         */
        bool CopyTo(size_t dstOffset, const void *src, size_t size)
        {
            if (!m_Data || !src || dstOffset + size > m_Size)
            {
                return false;
            }

            std::memcpy(m_Data + dstOffset, src, size);
            return true;
        }

        /**
         * @brief コンテナ全体を別のコンテナからコピーします
         * @param source コピー元コンテナ
         * @return コピーが成功した場合はtrue
         */
        bool CopyFrom(const VariableContainer *source)
        {
            if (!m_Data || !source || !source->m_Data || m_Size != source->m_Size)
            {
                return false;
            }

            std::memcpy(m_Data, source->m_Data, m_Size);
            return true;
        }

        /**
         * @brief コンテナ内のメモリをコピーします
         * @param dstOffset コピー先オフセット
         * @param srcOffset コピー元オフセット
         * @param size コピーするバイト数
         * @return コピーが成功した場合はtrue
         */
        bool CopyWithin(size_t dstOffset, size_t srcOffset, size_t size)
        {
            if (!m_Data || dstOffset + size > m_Size || srcOffset + size > m_Size)
            {
                return false;
            }

            std::memmove(m_Data + dstOffset, m_Data + srcOffset, size);
            return true;
        }

        /**
         * @brief メモリをクリアします
         * @param offset クリア開始位置
         * @param size クリアするバイト数
         * @return クリアが成功した場合はtrue
         */
        bool Clear(size_t offset = 0, size_t size = 0)
        {
            if (!m_Data)
            {
                return false;
            }

            if (size == 0)
            {
                size = m_Size - offset;
            }

            if (offset + size > m_Size)
            {
                return false;
            }

            std::memset(m_Data + offset, 0, size);
            return true;
        }

    private:
        size_t m_Size;   // コンテナのサイズ
        uint8_t *m_Data; // データ領域

        // コピー禁止
        VariableContainer(const VariableContainer &) = delete;
        VariableContainer &operator=(const VariableContainer &) = delete;
    };

    /**
     * @brief すべてのオブジェクトの基本インターフェース
     * 参照カウント管理と基本的なオブジェクト操作を提供します
     */
    class IUnknown
    {
    public:
        /**
         * @brief デストラクタ
         */
        virtual ~IUnknown() = default;

        /**
         * @brief 参照カウントをインクリメントします
         * @return 増加後の参照カウント
         */
        virtual uint32_t AddRef() const = 0;

        /**
         * @brief 参照カウントをデクリメントします
         * オブジェクトの参照カウントが0になると自動的に削除されます
         * @return 減少後の参照カウント
         */
        virtual uint32_t Release() const = 0;

        /**
         * @brief オブジェクトのクラス情報を取得します
         * @return このオブジェクトのクラス情報へのポインタ
         */
        virtual const IClass *GetClass() const = 0;

        /**
         * @brief オブジェクトを初期化します
         * デフォルトオブジェクトの値を適用します
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
         * @brief 変数コンテナを取得します
         * @return 変数コンテナへのポインタ
         */
        virtual VariableContainer *GetVariableContainer() = 0;

        /**
         * @brief 変数コンテナを取得します（読み取り専用）
         * @return 変数コンテナへの読み取り専用ポインタ
         */
        virtual const VariableContainer *GetVariableContainer() const = 0;

        /**
         * @brief このオブジェクトがデフォルトオブジェクトかどうかを返します
         * @return デフォルトオブジェクトの場合はtrue
         */
        virtual bool IsDefaultObject() const = 0;

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
        virtual void AddInner(IUnknown *inner) = 0;

        /**
         * @brief 子オブジェクト（Inner）を削除します
         * @param inner 削除する子オブジェクト
         * @return 削除に成功した場合はtrue
         */
        virtual bool RemoveInner(IUnknown *inner) = 0;
    };

    /**
     * @brief IUnknownの基本実装
     * 参照カウンタやフラグなどの標準実装を提供します
     */
    class UnknownImpl : public IUnknown
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         * このコンストラクタはデフォルトオブジェクトの作成時に使用されます
         */
        UnknownImpl();

        /**
         * @brief フィールド初期化子を使用したコンストラクタ
         * このコンストラクタはデフォルトオブジェクトの初期化時にのみ使用されます
         * @param initializer フィールド初期化子
         */
        explicit UnknownImpl(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * このコンストラクタはオブジェクトのクローン作成にも使用されます
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit UnknownImpl(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~UnknownImpl();

        // IUnknownインターフェースの実装
        virtual uint32_t AddRef() const override;
        virtual uint32_t Release() const override;
        virtual bool HasFlag(uint32_t flag) const override;
        virtual void SetFlag(uint32_t flag, bool value) override;
        virtual void *GetPropertyValue(const Identity &propertyName) override;
        virtual const void *GetPropertyValue(const Identity &propertyName) const override;
        virtual VariableContainer *GetVariableContainer() override;
        virtual const VariableContainer *GetVariableContainer() const override;

        virtual const IClass *GetClass() const override;
        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual bool IsDefaultObject() const override;

        // Outer/Inner関連メソッドの実装
        virtual IUnknown *GetOuter() override;
        virtual const IUnknown *GetOuter() const override;
        void SetOuter(IUnknown *outer); // SetOuterメソッドを追加（非virtual）
        virtual const Container::VariableArray<IUnknown *> &GetInners() const override;
        virtual void AddInner(IUnknown *inner) override;
        virtual bool RemoveInner(IUnknown *inner) override;

    protected:
        /**
         * @brief 変数コンテナを初期化します
         */
        void InitializeVariableContainer();

        /**
         * @brief 他のオブジェクトからデータをコピーします
         * @param sourceObject コピー元となるオブジェクト
         */
        void CopyFromObject(const IUnknown *sourceObject);

        mutable Thread::Atomic<uint32_t> m_RefCount;            // 参照カウント
        mutable Thread::Atomic<uint32_t> m_Flags;               // オブジェクトフラグ
        std::unique_ptr<VariableContainer> m_VariableContainer; // 変数コンテナ
        IUnknown *m_Outer;                                      // 親オブジェクト
        Container::VariableArray<IUnknown *> m_Inners;          // 子オブジェクトのリスト

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
        OF_Persistent = 1 << 4,     // 永続的なオブジェクト
        OF_DefaultObject = 1 << 5   // デフォルトオブジェクト
    };

} // namespace NorvesLib::Core