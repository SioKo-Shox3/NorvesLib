#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include "Container/Containers.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    class IClass;
    class IValue;
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
            : m_Size(size)
            , m_Data(nullptr)
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
        void* GetData()
        {
            return m_Data;
        }

        /**
         * @brief データへの読み取り専用ポインタを取得します
         * @return データへの読み取り専用ポインタ
         */
        const void* GetData() const
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
        void* GetAt(size_t offset)
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
        const void* GetAt(size_t offset) const
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
        bool CopyTo(size_t dstOffset, const void* src, size_t size)
        {
            if (!m_Data || !src || dstOffset + size > m_Size)
            {
                return false;
            }

            std::memcpy(m_Data + dstOffset, src, size);
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
        size_t m_Size;     // コンテナのサイズ
        uint8_t* m_Data;   // データ領域

        // コピー禁止
        VariableContainer(const VariableContainer&) = delete;
        VariableContainer& operator=(const VariableContainer&) = delete;
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
        virtual const IClass* GetClass() const = 0;

        /**
         * @brief オブジェクトを複製します
         * @return 新しいオブジェクトへのポインタ
         */
        virtual IUnknown* Clone() const = 0;

        /**
         * @brief フィールド初期化子を使用してオブジェクトを複製します
         * @param initializer フィールド初期化子
         * @return 新しいオブジェクトへのポインタ
         */
        virtual IUnknown* Clone(const FieldInitializer* initializer) const = 0;

        /**
         * @brief オブジェクトを初期化します
         */
        virtual void Initialize() = 0;

        /**
         * @brief フィールド初期化子を使用してオブジェクトを初期化します
         * @param initializer フィールド初期化子
         * @return 初期化に成功した場合はtrue
         */
        virtual bool Initialize(const FieldInitializer* initializer) = 0;

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
         * @return プロパティの値、見つからない場合はnullptr
         */
        virtual std::unique_ptr<IValue> GetProperty(const Identity& propertyName) const = 0;

        /**
         * @brief プロパティの値を設定します
         * @param propertyName プロパティ名
         * @param value 設定する値
         * @return 成功した場合はtrue
         */
        virtual bool SetProperty(const Identity& propertyName, const IValue* value) = 0;
        
        /**
         * @brief 変数コンテナを取得します
         * @return 変数コンテナへのポインタ
         */
        virtual VariableContainer* GetVariableContainer() = 0;
        
        /**
         * @brief 変数コンテナを取得します（読み取り専用）
         * @return 変数コンテナへの読み取り専用ポインタ
         */
        virtual const VariableContainer* GetVariableContainer() const = 0;
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
         */
        UnknownImpl();

        /**
         * @brief フィールド初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit UnknownImpl(const FieldInitializer* initializer);

        /**
         * @brief デストラクタ
         */
        virtual ~UnknownImpl();

        // IUnknownインターフェースの実装
        virtual uint32_t AddRef() const override;
        virtual uint32_t Release() const override;
        virtual bool HasFlag(uint32_t flag) const override;
        virtual void SetFlag(uint32_t flag, bool value) override;
        virtual std::unique_ptr<IValue> GetProperty(const Identity& propertyName) const override;
        virtual bool SetProperty(const Identity& propertyName, const IValue* value) override;
        virtual VariableContainer* GetVariableContainer() override;
        virtual const VariableContainer* GetVariableContainer() const override;
        
        virtual const IClass* GetClass() const override;
        virtual IUnknown* Clone() const override;
        virtual IUnknown* Clone(const FieldInitializer* initializer) const override;
        virtual void Initialize() override;
        virtual bool Initialize(const FieldInitializer* initializer) override;
        virtual void Finalize() override;

    protected:
        /**
         * @brief 変数コンテナを初期化します
         */
        void InitializeVariableContainer();

        /**
         * @brief フィールド初期化子を適用します
         * @param initializer フィールド初期化子
         * @return 適用された初期値の数
         */
        int ApplyFieldInitializer(const FieldInitializer* initializer);

        mutable std::atomic<uint32_t> m_RefCount;   // 参照カウント
        mutable std::atomic<uint32_t> m_Flags;      // オブジェクトフラグ
        std::unique_ptr<VariableContainer> m_VariableContainer;  // 変数コンテナ

    private:
        // コピー禁止
        UnknownImpl(const UnknownImpl&) = delete;
        UnknownImpl& operator=(const UnknownImpl&) = delete;
    };

    /**
     * @brief オブジェクトフラグ定数
     */
    enum ObjectFlags : uint32_t
    {
        OF_None             = 0,        // フラグなし
        OF_Initialized      = 1 << 0,   // 初期化済み
        OF_PendingDestroy   = 1 << 1,   // 破棄待ち
        OF_GarbageCollect   = 1 << 2,   // GC対象
        OF_Transient        = 1 << 3,   // 一時的なオブジェクト
        OF_Persistent       = 1 << 4    // 永続的なオブジェクト
    };

} // namespace NorvesLib::Core