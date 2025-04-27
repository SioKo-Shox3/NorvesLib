#pragma once

#include "IClass.h"
#include "IUnknown.h"
#include "TValue.h"
#include "TClass.h"
#include "ObjectUtility.h"
#include "Container/Containers.h" // Core::Containerのすべてのコンテナ
#include <typeindex>

namespace NorvesLib::Core
{
    // リフレクションマクロ定義

    // クラス宣言時に使用するマクロ
    #define REFLECTION_CLASS(Class, ParentClass) \
        public: \
            static const TClass<Class, ParentClass>* StaticClass(); \
            virtual const IClass* GetClass() const override { return StaticClass(); } \
            virtual IUnknown* Clone() const override; \
            virtual IUnknown* Clone(const FieldInitializer* initializer) const override; \
        private: \
            using __ThisClass = Class; /* リフレクション用にクラス名を定義 */ \
            using Super = ParentClass; \
            static PropertyRegistry<Class> s_PropertyRegistry; \
            static FunctionRegistry<Class> s_FunctionRegistry; \
            friend class TClass<Class, ParentClass>;

    // クラス実装時に使用するマクロ
    #define IMPLEMENT_CLASS(Class, ParentClass) \
        PropertyRegistry<Class> Class::s_PropertyRegistry; \
        FunctionRegistry<Class> Class::s_FunctionRegistry; \
        \
        const TClass<Class, ParentClass>* Class::StaticClass() { \
            /* 親クラスの型情報をテンプレートパラメータとして利用 */ \
            static TClass<Class, ParentClass> s_ClassInfo(#Class); \
            return &s_ClassInfo; \
        } \
        \
        IUnknown* Class::Clone() const { \
            /* ObjectUtilityを使用してインスタンスを作成 */ \
            Class* newInstance = static_cast<Class*>(ObjectUtility::CreateObject(GetClass())); \
            if (newInstance) { \
                /* コンテナデータをコピー */ \
                const VariableContainer* srcContainer = GetVariableContainer(); \
                VariableContainer* dstContainer = newInstance->GetVariableContainer(); \
                if (srcContainer && dstContainer) { \
                    const void* srcData = srcContainer->GetData(); \
                    void* dstData = dstContainer->GetData(); \
                    std::memcpy(dstData, srcData, srcContainer->GetSize()); \
                } \
            } \
            return newInstance; \
        } \
        \
        IUnknown* Class::Clone(const FieldInitializer* initializer) const { \
            /* 基本クローンを作成 */ \
            Class* newInstance = static_cast<Class*>(Clone()); \
            /* 初期化子を適用 */ \
            if (newInstance && initializer) { \
                ObjectUtility::ApplyInitialValues(newInstance, initializer); \
            } \
            return newInstance; \
        }

    // プロパティ宣言用マクロ（自動登録機能付き）
    #define PROPERTY(Type, Name) \
        private: \
            Type m_PropertyValues_##Name{}; \
            static inline const TClassProperty<Type>* s_PropertyPtr_##Name = nullptr; \
            static inline void RegisterProperty_##Name() { \
                s_PropertyPtr_##Name = static_cast<const TClassProperty<Type>*>( \
                    s_PropertyRegistry.Register<Type>(Container::String(#Name), offsetof(__ThisClass, m_PropertyValues_##Name), sizeof(Type)) \
                ); \
            } \
            /* 自動登録を行うための静的変数 */ \
            static inline bool s_PropertyRegistered_##Name = []() { \
                RegisterProperty_##Name(); \
                return true; \
            }(); \
        public: \
            /* 旧APIとの互換性のためのゲッター関数 */ \
            Type& Name() { \
                return m_PropertyValues_##Name; \
            } \
            const Type& Name() const { \
                return m_PropertyValues_##Name; \
            } \
            /* プロパティ参照を取得する関数 - メンバ変数のようにアクセスできる */ \
            PropertyRef<Type> get##Name() { \
                /* s_PropertyPtr_##Name は自動登録により既に初期化されているはず */ \
                return PropertyRef<Type>(this, s_PropertyPtr_##Name); \
            } \
            ConstPropertyRef<Type> get##Name() const { \
                return ConstPropertyRef<Type>(this, s_PropertyPtr_##Name); \
            } \
            /* プロパティアクセスのための疑似メンバ変数定義 */ \
            struct Name##PropertyAccessor { \
                Type& operator()(IUnknown* obj) { \
                    return static_cast<__ThisClass*>(obj)->m_PropertyValues_##Name; \
                } \
                const Type& operator()(const IUnknown* obj) const { \
                    return static_cast<const __ThisClass*>(obj)->m_PropertyValues_##Name; \
                } \
                /* 直接メンバアクセス演算子 */ \
                PropertyRef<Type> operator->(__ThisClass* obj) { \
                    return obj->get##Name(); \
                } \
                ConstPropertyRef<Type> operator->(const __ThisClass* obj) const { \
                    return obj->get##Name(); \
                } \
            }; \
            /* クラス内部でメンバ変数のようにアクセスするためのプロパティ */ \
            static inline Name##PropertyAccessor Name{};

    // 関数宣言用マクロ (自動登録機能付き)
    #define FUNCTION(ReturnType, Name, ...) \
        private: \
            static inline void RegisterFunction_##Name() { \
                s_FunctionRegistry.Register<ReturnType>(Container::String(#Name), &__ThisClass::Name); \
            } \
            /* 自動登録を行うための静的変数 */ \
            static inline bool s_FunctionRegistered_##Name = []() { \
                RegisterFunction_##Name(); \
                return true; \
            }(); \
        public: \
            ReturnType Name(__VA_ARGS__);
        
    // プロパティを登録するためのヘルパークラス
    template<typename ClassType>
    class PropertyRegistry
    {
    public:
        // プロパティ登録関数
        template<typename PropertyType>
        const ClassProperty* Register(const Container::String& name, size_t offset, size_t size, uint32_t flags = 0)
        {
            // プロパティ情報を作成
            const IClass* propertyTypeClass = nullptr; // 通常はTClass<PropertyType>::StaticClass()などで取得
            auto property = std::make_shared<TClassProperty<PropertyType>>(name, propertyTypeClass, offset, size, flags);
            
            // 登録されたプロパティリストに追加
            m_Properties.push_back(property);
            
            // 登録したプロパティへの参照を返す
            return property.get();
        }
        
        // 登録されたプロパティのリストを取得
        const Container::VariableArray<std::shared_ptr<ClassProperty>>& GetProperties() const
        {
            return m_Properties;
        }
        
    private:
        Container::VariableArray<std::shared_ptr<ClassProperty>> m_Properties;
    };
    
    // 関数を登録するためのヘルパークラス
    template<typename ClassType>
    class FunctionRegistry
    {
    public:
        // 関数登録関数 (シンプルな例)
        template<typename ReturnType>
        void Register(const Container::String& name, ReturnType (ClassType::*func)(), uint32_t flags = 0)
        {
            // 関数情報を作成
            const IClass* returnTypeClass = nullptr; // 通常はTClass<ReturnType>::StaticClass()などで取得
            auto function = std::make_shared<ClassFunction>(name, returnTypeClass, flags);
            
            // 登録された関数リストに追加
            m_Functions.push_back(function);
        }
        
        // 登録された関数のリストを取得
        const Container::VariableArray<std::shared_ptr<ClassFunction>>& GetFunctions() const
        {
            return m_Functions;
        }
        
    private:
        Container::VariableArray<std::shared_ptr<ClassFunction>> m_Functions;
    };

} // namespace NorvesLib::Core