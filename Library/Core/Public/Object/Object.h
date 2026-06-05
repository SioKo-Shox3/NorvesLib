#pragma once

#include "IUnknown.h"
#include "IClass.h"
#include "TClass.h"
#include "Container/Containers.h"
#include "ReferenceCollector.h"
#include "ObjectUtility.h"

namespace NorvesLib::Core
{
    /**
     * @brief 全てのオブジェクトの基底クラス
     * UnknownImplを継承して参照カウント管理機能を実装します
     */
    class Object : public UnknownImpl
    {
    public:
        using __ThisClass = Object; // リフレクション用にクラス名を定義
        using Super = UnknownImpl;

        /**
         * @brief デフォルトコンストラクタ
         */
        Object();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit Object(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit Object(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~Object();

        /**
         * @brief オブジェクトのクラス情報を取得します
         * @return このオブジェクトのクラス情報
         */
        virtual const IClass *GetClass() const override;

        /**
         * @brief オブジェクトを初期化します
         */
        virtual void Initialize() override;

        /**
         * @brief フィールド初期化子を使用してオブジェクトを初期化します
         * @param initializer フィールド初期化子
         * @return 初期化に成功した場合はtrue
         */
        virtual bool Initialize(const FieldInitializer *initializer);

        /**
         * @brief オブジェクトの破棄前処理を行います
         */
        virtual void Finalize() override;

        /**
         * @brief ObjectHeap/GC向けに破棄予約します。
         */
        virtual void Destroy();

        /**
         * @brief 破棄予約済みかどうかを返します。
         */
        virtual bool IsPendingDestroy() const;

        /**
         * @brief GCが追跡する参照を列挙します。
         */
        virtual void AddReferencedObjects(ReferenceCollector &collector) const { (void)collector; }

        /**
         * @brief ObjectHeapがメモリを解放する直前に呼び出します。
         */
        virtual void OnDestroying() {}

        /**
         * @brief オブジェクトのクローンを作成します
         * @return クローンされたオブジェクトへのポインタ
         */
        virtual IUnknown *Clone() const override;

        /**
         * @brief フィールド初期化子を使用してオブジェクトのクローンを作成します
         * @param initializer フィールド初期化子
         * @return クローンされたオブジェクトへのポインタ
         */
        virtual IUnknown *Clone(const FieldInitializer *initializer) const override;

        /**
         * @brief オブジェクトの型を文字列として取得します
         * @return クラス名
         */
        virtual const Identity &GetTypeName() const;

        /**
         * @brief オブジェクトが指定されたクラスと互換性があるか確認します
         * @param cls 確認するクラス
         * @return クラスが互換性を持つ場合はtrue
         */
        virtual bool IsA(const IClass *cls) const;

        /**
         * @brief オブジェクトが指定されたクラスのインスタンスであるか確認します
         * @tparam T 確認するクラス型
         * @return 指定された型のインスタンスである場合はtrue
         */
        template <typename T>
        bool IsA() const
        {
            return ObjectUtility::IsA<T>(this);
        }

        /**
         * @brief オブジェクトを指定された型にキャストします
         * @tparam T キャスト先の型
         * @return キャストされたオブジェクトへのポインタ、キャスト失敗時はnullptr
         */
        template <typename T>
        T *Cast()
        {
            return ObjectUtility::CastTo<T>(this);
        }

        /**
         * @brief オブジェクトを指定された型に定数キャストします
         * @tparam T キャスト先の型
         * @return キャストされたオブジェクトへの定数ポインタ、キャスト失敗時はnullptr
         */
        template <typename T>
        const T *Cast() const
        {
            return ObjectUtility::CastTo<T>(this);
        }

        /**
         * @brief このクラスの静的クラス情報を取得します
         * @return クラス情報
         */
        static const IClass *StaticClass();
    };

} // namespace NorvesLib::Core
