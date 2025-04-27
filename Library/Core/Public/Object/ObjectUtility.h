#pragma once

#include <memory>
#include <type_traits>
#include "IUnknown.h"
#include "IClass.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    /**
     * @brief Objectユーティリティクラス
     * Object関連の生成・破棄のユーティリティ関数を提供します
     */
    class ObjectUtility
    {
    public:
        /**
         * @brief 指定されたクラス名のオブジェクトを作成します
         * @param className クラス名
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        static IUnknown* CreateObject(const Identity& className, IUnknown* outer = nullptr);

        /**
         * @brief 指定されたクラスIDのオブジェクトを作成します
         * @param classId クラスID
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        static IUnknown* CreateObject(uint64_t classId, IUnknown* outer = nullptr);

        /**
         * @brief 指定されたクラス型のオブジェクトを作成します
         * @param cls クラス情報
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        static IUnknown* CreateObject(const IClass* cls, IUnknown* outer = nullptr);

        /**
         * @brief フィールド初期化子を使用して指定されたクラスのオブジェクトを作成します
         * @param className クラス名
         * @param initializer フィールド初期化子
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        static IUnknown* CreateObject(const Identity& className, const FieldInitializer* initializer, IUnknown* outer = nullptr);

        /**
         * @brief フィールド初期化子を使用して指定されたクラスIDのオブジェクトを作成します
         * @param classId クラスID
         * @param initializer フィールド初期化子
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        static IUnknown* CreateObject(uint64_t classId, const FieldInitializer* initializer, IUnknown* outer = nullptr);

        /**
         * @brief フィールド初期化子を使用して指定されたクラス型のオブジェクトを作成します
         * @param cls クラス情報
         * @param initializer フィールド初期化子
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        static IUnknown* CreateObject(const IClass* cls, const FieldInitializer* initializer, IUnknown* outer = nullptr);

        /**
         * @brief オブジェクトを安全に破棄します
         * @param object 破棄するオブジェクト
         * @return 破棄に成功した場合はtrue
         */
        static bool DestroyObject(IUnknown* object);

        /**
         * @brief 指定された型のオブジェクトを作成します
         * @tparam T 作成するオブジェクトの型 (IUnknownから派生している必要があります)
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        template<typename T>
        static T* CreateTypedObject(IUnknown* outer = nullptr)
        {
            static_assert(std::is_base_of<IUnknown, T>::value, "T must derive from IUnknown");
            const IClass* cls = T::StaticClass();
            if (cls)
            {
                return static_cast<T*>(CreateObject(cls, outer));
            }
            return nullptr;
        }

        /**
         * @brief フィールド初期化子を使用して指定された型のオブジェクトを作成します
         * @tparam T 作成するオブジェクトの型 (IUnknownから派生している必要があります)
         * @param initializer フィールド初期化子
         * @param outer 生成されるオブジェクトの親オブジェクト（オーナー）
         * @return 作成されたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        template<typename T>
        static T* CreateTypedObject(const FieldInitializer* initializer, IUnknown* outer = nullptr)
        {
            static_assert(std::is_base_of<IUnknown, T>::value, "T must derive from IUnknown");
            const IClass* cls = T::StaticClass();
            if (cls)
            {
                return static_cast<T*>(CreateObject(cls, initializer, outer));
            }
            return nullptr;
        }

        /**
         * @brief オブジェクトの型を指定された型に安全にキャストします
         * @tparam T キャスト先の型 (IUnknownから派生している必要があります)
         * @param object キャストするオブジェクト
         * @return キャストされたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        template<typename T>
        static T* CastTo(IUnknown* object)
        {
            static_assert(std::is_base_of<IUnknown, T>::value, "T must derive from IUnknown");
            if (object && object->GetClass()->IsChildOf(T::StaticClass()))
            {
                return static_cast<T*>(object);
            }
            return nullptr;
        }

        /**
         * @brief オブジェクトの型を指定された型に安全にキャストします (const版)
         * @tparam T キャスト先の型 (IUnknownから派生している必要があります)
         * @param object キャストするオブジェクト
         * @return キャストされたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        template<typename T>
        static const T* CastTo(const IUnknown* object)
        {
            static_assert(std::is_base_of<IUnknown, T>::value, "T must derive from IUnknown");
            if (object && object->GetClass()->IsChildOf(T::StaticClass()))
            {
                return static_cast<const T*>(object);
            }
            return nullptr;
        }

        /**
         * @brief オブジェクトの型を指定された型に安全にトライキャストします
         * CastToと同様の機能を提供しますが、命名が異なります
         * @tparam T キャスト先の型 (IUnknownから派生している必要があります)
         * @param object キャストするオブジェクト
         * @return キャストされたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        template<typename T>
        static T* TryCast(IUnknown* object)
        {
            static_assert(std::is_base_of<IUnknown, T>::value, "T must derive from IUnknown");
            if (object && object->GetClass()->IsChildOf(T::StaticClass()))
            {
                return static_cast<T*>(object);
            }
            return nullptr;
        }

        /**
         * @brief オブジェクトの型を指定された型に安全にトライキャストします (const版)
         * @tparam T キャスト先の型 (IUnknownから派生している必要があります)
         * @param object キャストするオブジェクト
         * @return キャストされたオブジェクトへのポインタ、失敗した場合はnullptr
         */
        template<typename T>
        static const T* TryCast(const IUnknown* object)
        {
            static_assert(std::is_base_of<IUnknown, T>::value, "T must derive from IUnknown");
            if (object && object->GetClass()->IsChildOf(T::StaticClass()))
            {
                return static_cast<const T*>(object);
            }
            return nullptr;
        }

        /**
         * @brief オブジェクトが指定された型かどうかを確認します
         * @tparam T 確認する型 (IUnknownから派生している必要があります)
         * @param object 確認するオブジェクト
         * @return 指定された型の場合はtrue
         */
        template<typename T>
        static bool IsA(const IUnknown* object)
        {
            static_assert(std::is_base_of<IUnknown, T>::value, "T must derive from IUnknown");
            return object && object->GetClass()->IsChildOf(T::StaticClass());
        }

        /**
         * @brief プロパティの初期値を適用します
         * @param object 初期値を適用するオブジェクト
         * @param initializer 初期値を提供する初期化子
         * @return 適用された初期値の数
         */
        static int ApplyInitialValues(IUnknown* object, const FieldInitializer* initializer);

        /**
         * @brief プロパティにデフォルト値を適用します
         * @param object デフォルト値を適用するオブジェクト
         * @return 適用されたデフォルト値の数
         */
        static int ApplyDefaultValues(IUnknown* object);
    };

} // namespace NorvesLib::Core