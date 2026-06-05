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
// リフレクションマクロ定義

// クラス宣言時に使用するマクロ
#define REFLECTION_CLASS(Class, ParentClass)                                     \
public:                                                                          \
    static const NorvesLib::Core::TClass<Class, ParentClass> *StaticClass();     \
    virtual const NorvesLib::Core::IClass *GetClass() const override { return StaticClass(); } \
                                                                                 \
private:                                                                         \
    using __ThisClass = Class; /* リフレクション用にクラス名を定義 */            \
    using Super = ParentClass;                                                   \
    static inline NorvesLib::Core::PropertyRegistry<Class> s_PropertyRegistry;   \
    static inline NorvesLib::Core::FunctionRegistry<Class> s_FunctionRegistry;   \
    friend class NorvesLib::Core::TClass<Class, ParentClass>;

// クラス実装時に使用するマクロ
#define IMPLEMENT_CLASS(Class, ParentClass)                                      \
    const NorvesLib::Core::TClass<Class, ParentClass> *Class::StaticClass()      \
    {                                                                            \
        static NorvesLib::Core::TClass<Class, ParentClass> s_ClassInfo(#Class);  \
        return &s_ClassInfo;                                                     \
    }

// プロパティ宣言用マクロ（自動登録機能付き）
// TPropertyValue<Type>を使用し、()なしで変数のようにアクセス可能
#define PROPERTY(Type, Name)                                                                                         \
private:                                                                                                             \
    static inline const NorvesLib::Core::TClassProperty<Type> *s_PropertyPtr_##Name = nullptr;                       \
    static inline void RegisterProperty_##Name()                                                                     \
    {                                                                                                                \
        s_PropertyPtr_##Name = static_cast<const NorvesLib::Core::TClassProperty<Type> *>(                           \
            s_PropertyRegistry.Register<Type>(NorvesLib::Core::Container::String(#Name), 0, sizeof(Type), 0,         \
                [](NorvesLib::Core::IUnknown *obj) -> Type &                                                        \
                {                                                                                                    \
                    return static_cast<__ThisClass *>(obj)->Name.Get();                                              \
                },                                                                                                   \
                [](const NorvesLib::Core::IUnknown *obj) -> const Type &                                             \
                {                                                                                                    \
                    return static_cast<const __ThisClass *>(obj)->Name.Get();                                        \
                }));                                                                                                 \
    }                                                                                                                \
    /* 自動登録を行うための静的変数 */                                                                               \
    static inline bool s_PropertyRegistered_##Name = []() { \
                RegisterProperty_##Name(); \
                return true; }();                                                     \
                                                                                                                     \
public:                                                                                                              \
    /* プロパティ参照を取得する関数 */                                                                               \
    NorvesLib::Core::PropertyRef<Type> get##Name()                                                                   \
    {                                                                                                                \
        return NorvesLib::Core::PropertyRef<Type>(this, s_PropertyPtr_##Name);                                       \
    }                                                                                                                \
    NorvesLib::Core::ConstPropertyRef<Type> get##Name() const                                                        \
    {                                                                                                                \
        return NorvesLib::Core::ConstPropertyRef<Type>(this, s_PropertyPtr_##Name);                                  \
    }                                                                                                                \
    /* リフレクション経由のプロパティアクセス用 */                                                                   \
    struct Name##PropertyAccessor                                                                                    \
    {                                                                                                                \
        Type &operator()(NorvesLib::Core::IUnknown *obj)                                                             \
        {                                                                                                            \
            return static_cast<__ThisClass *>(obj)->Name;                                                            \
        }                                                                                                            \
        const Type &operator()(const NorvesLib::Core::IUnknown *obj) const                                           \
        {                                                                                                            \
            return static_cast<const __ThisClass *>(obj)->Name;                                                      \
        }                                                                                                            \
    };                                                                                                               \
                                                                                                                     \
protected:                                                                                                           \
    NorvesLib::Core::TPropertyValue<Type> Name;

// 関数宣言用マクロ (自動登録機能付き)
#define FUNCTION(ReturnType, Name, ...)                                                        \
private:                                                                                       \
    static inline void RegisterFunction_##Name()                                               \
    {                                                                                          \
        s_FunctionRegistry.Register<ReturnType>(NorvesLib::Core::Container::String(#Name), &__ThisClass::Name); \
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
        const ClassProperty *Register(const Container::String &name, size_t offset, size_t size, uint32_t flags = 0,
                                      typename TClassProperty<PropertyType>::MutableGetter mutableGetter = nullptr,
                                      typename TClassProperty<PropertyType>::ConstGetter constGetter = nullptr)
        {
            // プロパティ情報を作成
            const IClass *propertyTypeClass = nullptr; // 通常はTClass<PropertyType>::StaticClass()などで取得
            auto property = std::make_shared<TClassProperty<PropertyType>>(
                Identity(name), propertyTypeClass, offset, size, flags, mutableGetter, constGetter);

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
            auto function = std::make_shared<TClassFunction<ClassType, ReturnType>>(Identity(name), func, returnTypeClass, flags);

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
