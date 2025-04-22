#pragma once

#include "IUnknown.h"
#include "IClass.h"
#include "TClass.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    /**
     * @brief 全てのオブジェクトの基底クラス
     * UnknownImplを継承して参照カウント管理機能を実装します
     */
    class Object : public UnknownImpl
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        Object();
        
        /**
         * @brief デストラクタ
         */
        virtual ~Object();
        
        /**
         * @brief オブジェクトのクラス情報を取得します
         * @return このオブジェクトのクラス情報
         */
        virtual const IClass* GetClass() const override;
        
        /**
         * @brief オブジェクトを複製します
         * @return 新しいオブジェクトへのポインタ
         */
        virtual IUnknown* Clone() const override;
        
        /**
         * @brief オブジェクトを初期化します
         */
        virtual void Initialize() override;
        
        /**
         * @brief オブジェクトの破棄前処理を行います
         */
        virtual void Finalize() override;
        
        /**
         * @brief オブジェクトの型を文字列として取得します
         * @return クラス名
         */
        virtual const Identity& GetTypeName() const;
        
        /**
         * @brief オブジェクトが指定されたクラスと互換性があるか確認します
         * @param cls 確認するクラス
         * @return クラスが互換性を持つ場合はtrue
         */
        virtual bool IsA(const IClass* cls) const;
        
        /**
         * @brief オブジェクトが指定されたクラスのインスタンスであるか確認します
         * @tparam T 確認するクラス型
         * @return 指定された型のインスタンスである場合はtrue
         */
        template<typename T>
        bool IsA() const
        {
            return IsA(&TClass<T>::GetInstance());
        }
        
        /**
         * @brief オブジェクトを指定された型にキャストします
         * @tparam T キャスト先の型
         * @return キャストされたオブジェクトへのポインタ、キャスト失敗時はnullptr
         */
        template<typename T>
        T* Cast()
        {
            return IsA<T>() ? static_cast<T*>(this) : nullptr;
        }
        
        /**
         * @brief オブジェクトを指定された型に定数キャストします
         * @tparam T キャスト先の型
         * @return キャストされたオブジェクトへの定数ポインタ、キャスト失敗時はnullptr
         */
        template<typename T>
        const T* Cast() const
        {
            return IsA<T>() ? static_cast<const T*>(this) : nullptr;
        }
    };

} // namespace NorvesLib::Core