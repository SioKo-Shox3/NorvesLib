#pragma once

#include "IClass.h"
#include "IUnknown.h"
#include "TValue.h"
#include "TClass.h"
#include "ObjectUtility.h"
#include "Container/Containers.h" // Core::Containerのすべてのコンテナ
#include "Text/IdentityPool.h"
#include <typeindex>

namespace NorvesLib::Core
{
    // ========================================
    // TPropertyValue - プロパティ値ラッパー
    // ========================================

    /**
     * @brief プロパティ値の透過的ラッパー
     *
     * PROPERTYマクロで使用され、暗黙の型変換により
     * 通常のメンバ変数と同様に読み書きできます。
     *
     * @code
     * // ()を付けずに直接代入・読み取り可能
     * PositionX = 1.0f;
     * float x = PositionX;
     * @endcode
     */
    template <typename T>
    class TPropertyValue
    {
    public:
        TPropertyValue() : m_Value{} {}
        TPropertyValue(const T &value) : m_Value(value) {}
        TPropertyValue(T &&value) : m_Value(std::move(value)) {}

        // 暗黙の型変換（変数のように読み書き可能）
        operator T &() { return m_Value; }
        operator const T &() const { return m_Value; }

        // 代入演算子
        TPropertyValue &operator=(const T &value)
        {
            m_Value = value;
            return *this;
        }
        TPropertyValue &operator=(T &&value)
        {
            m_Value = std::move(value);
            return *this;
        }

        // メンバアクセス（構造体型プロパティ用: Handle->IsValid() 等）
        T *operator->() { return &m_Value; }
        const T *operator->() const { return &m_Value; }

        // インクリメント/デクリメント（数値型用）
        TPropertyValue &operator++()
        {
            ++m_Value;
            return *this;
        }
        T operator++(int)
        {
            T old = m_Value;
            ++m_Value;
            return old;
        }
        TPropertyValue &operator--()
        {
            --m_Value;
            return *this;
        }
        T operator--(int)
        {
            T old = m_Value;
            --m_Value;
            return old;
        }

    private:
        T m_Value;
    };

// リフレクションマクロ定義

// クラス宣言時に使用するマクロ
#define REFLECTION_CLASS(Class, ParentClass)                                     \
public:                                                                          \
    static const TClass<Class, ParentClass> *StaticClass();                      \
    virtual const IClass *GetClass() const override { return StaticClass(); }    \
    virtual IUnknown *Clone() const override;                                    \
    virtual IUnknown *Clone(const FieldInitializer *initializer) const override; \
                                                                                 \
private:                                                                         \
    using __ThisClass = Class; /* リフレクション用にクラス名を定義 */            \
    using Super = ParentClass;                                                   \
    static inline PropertyRegistry<Class> s_PropertyRegistry;                    \
    static inline FunctionRegistry<Class> s_FunctionRegistry;                    \
    friend class TClass<Class, ParentClass>;

// クラス実装時に使用するマクロ
#define IMPLEMENT_CLASS(Class, ParentClass)                                                 \
    const TClass<Class, ParentClass> *Class::StaticClass()                                  \
    {                                                                                       \
        /* 親クラスの型情報をテンプレートパラメータとして利用 */                            \
        static TClass<Class, ParentClass> s_ClassInfo(#Class);                              \
        return &s_ClassInfo;                                                                \
    }                                                                                       \
                                                                                            \
    IUnknown *Class::Clone() const                                                          \
    {                                                                                       \
        /* ObjectUtilityを使用してインスタンスを作成 */                                     \
        Class *newInstance = static_cast<Class *>(ObjectUtility::CreateObject(GetClass())); \
        if (newInstance)                                                                    \
        {                                                                                   \
            /* コンテナデータをコピー */                                                    \
            const VariableContainer *srcContainer = GetVariableContainer();                 \
            VariableContainer *dstContainer = newInstance->GetVariableContainer();          \
            if (srcContainer && dstContainer)                                               \
            {                                                                               \
                const void *srcData = srcContainer->GetData();                              \
                void *dstData = dstContainer->GetData();                                    \
                std::memcpy(dstData, srcData, srcContainer->GetSize());                     \
            }                                                                               \
        }                                                                                   \
        return newInstance;                                                                 \
    }                                                                                       \
                                                                                            \
    IUnknown *Class::Clone(const FieldInitializer *initializer) const                       \
    {                                                                                       \
        /* 基本クローンを作成 */                                                            \
        Class *newInstance = static_cast<Class *>(Clone());                                 \
        /* 初期化子を適用 */                                                                \
        if (newInstance && initializer)                                                     \
        {                                                                                   \
            ObjectUtility::ApplyInitialValues(newInstance, initializer);                    \
        }                                                                                   \
        return newInstance;                                                                 \
    }

// プロパティ宣言用マクロ（自動登録機能付き）
// TPropertyValue<Type>を使用し、()なしで変数のようにアクセス可能
#define PROPERTY(Type, Name)                                                                                         \
private:                                                                                                             \
    static inline const TClassProperty<Type> *s_PropertyPtr_##Name = nullptr;                                        \
    static inline void RegisterProperty_##Name()                                                                     \
    {                                                                                                                \
        s_PropertyPtr_##Name = static_cast<const TClassProperty<Type> *>(                                            \
            s_PropertyRegistry.Register<Type>(Container::String(#Name), offsetof(__ThisClass, Name), sizeof(Type))); \
    }                                                                                                                \
    /* 自動登録を行うための静的変数 */                                                                               \
    static inline bool s_PropertyRegistered_##Name = []() { \
                RegisterProperty_##Name(); \
                return true; }();                                                     \
                                                                                                                     \
public:                                                                                                              \
    /* プロパティ参照を取得する関数 */                                                                               \
    PropertyRef<Type> get##Name()                                                                                    \
    {                                                                                                                \
        return PropertyRef<Type>(this, s_PropertyPtr_##Name);                                                        \
    }                                                                                                                \
    ConstPropertyRef<Type> get##Name() const                                                                         \
    {                                                                                                                \
        return ConstPropertyRef<Type>(this, s_PropertyPtr_##Name);                                                   \
    }                                                                                                                \
    /* リフレクション経由のプロパティアクセス用 */                                                                   \
    struct Name##PropertyAccessor                                                                                    \
    {                                                                                                                \
        Type &operator()(IUnknown *obj)                                                                              \
        {                                                                                                            \
            return static_cast<__ThisClass *>(obj)->Name;                                                            \
        }                                                                                                            \
        const Type &operator()(const IUnknown *obj) const                                                            \
        {                                                                                                            \
            return static_cast<const __ThisClass *>(obj)->Name;                                                      \
        }                                                                                                            \
    };                                                                                                               \
                                                                                                                     \
protected:                                                                                                           \
    TPropertyValue<Type> Name;

// 関数宣言用マクロ (自動登録機能付き)
#define FUNCTION(ReturnType, Name, ...)                                                        \
private:                                                                                       \
    static inline void RegisterFunction_##Name()                                               \
    {                                                                                          \
        s_FunctionRegistry.Register<ReturnType>(Container::String(#Name), &__ThisClass::Name); \
    }                                                                                          \
    /* 自動登録を行うための静的変数 */                                                         \
    static inline bool s_FunctionRegistered_##Name = []() { \
                RegisterFunction_##Name(); \
                return true; }();                               \
                                                                                               \
public:                                                                                        \
    ReturnType Name(__VA_ARGS__);

    // プロパティを登録するためのヘルパークラス
    template <typename ClassType>
    class PropertyRegistry
    {
    public:
        // プロパティ登録関数
        template <typename PropertyType>
        const ClassProperty *Register(const Container::String &name, size_t offset, size_t size, uint32_t flags = 0)
        {
            // プロパティ情報を作成
            const IClass *propertyTypeClass = nullptr; // 通常はTClass<PropertyType>::StaticClass()などで取得
            auto property = std::make_shared<TClassProperty<PropertyType>>(Identity(name), propertyTypeClass, offset, size, flags);

            // 登録されたプロパティリストに追加
            m_Properties.push_back(property);

            // 登録したプロパティへの参照を返す
            return property.get();
        }

        // 登録されたプロパティのリストを取得
        const Container::VariableArray<std::shared_ptr<ClassProperty>> &GetProperties() const
        {
            return m_Properties;
        }

    private:
        Container::VariableArray<std::shared_ptr<ClassProperty>> m_Properties;
    };

    // 関数を登録するためのヘルパークラス
    template <typename ClassType>
    class FunctionRegistry
    {
    public:
        // 関数登録関数 (シンプルな例)
        template <typename ReturnType>
        void Register(const Container::String &name, ReturnType (ClassType::*func)(), uint32_t flags = 0)
        {
            // 関数情報を作成
            const IClass *returnTypeClass = nullptr; // 通常はTClass<ReturnType>::StaticClass()などで取得
            auto function = std::make_shared<ClassFunction>(name, returnTypeClass, flags);

            // 登録された関数リストに追加
            m_Functions.push_back(function);
        }

        // 登録された関数のリストを取得
        const Container::VariableArray<std::shared_ptr<ClassFunction>> &GetFunctions() const
        {
            return m_Functions;
        }

    private:
        Container::VariableArray<std::shared_ptr<ClassFunction>> m_Functions;
    };

} // namespace NorvesLib::Core
