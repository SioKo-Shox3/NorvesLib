#pragma once

#include <memory>
#include <type_traits>
#include "IClass.h"
#include "Container/Containers.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    // 前方宣言
    template<typename ClassType>
    class PropertyRegistry;

    template<typename ClassType>
    class FunctionRegistry;

    /**
     * @brief IClassの実装テンプレート
     * 具体的なクラス型に対するリフレクション情報を提供します
     */
    template<typename T>
    class TClass : public IClass
    {
    public:
        TClass(const Container::String& className, const IClass* parentClass)
            : m_ClassName(Identity(className))
            , m_ParentClass(parentClass)
            , m_PropertyField(std::make_unique<PropertyField>())
            , m_FunctionField(std::make_unique<FunctionField>())
            , m_ClassId(GenerateClassId())
        {
            // 親クラスからプロパティと関数を継承
            if (m_ParentClass)
            {
                InheritFromParent();
            }
            
            // このクラス独自のプロパティを登録
            RegisterClassProperties();
            
            // このクラス独自の関数を登録
            RegisterClassFunctions();
            
            // クラスをレジストリに登録
            ClassRegistry::Get().RegisterClass(this);
        }
        
        virtual ~TClass() = default;
        
        // IClassインターフェースの実装
        virtual const Identity& GetClassName() const override { return m_ClassName; }
        virtual const IClass* GetParentClass() const override { return m_ParentClass; }
        
        virtual bool IsChildOf(const IClass* cls) const override
        {
            if (this == cls) return true;
            if (m_ParentClass) return m_ParentClass->IsChildOf(cls);
            return false;
        }
        
        virtual const IUnknown* GetDefaultObject() const override
        {
            if (!m_DefaultObject)
            {
                m_DefaultObject.reset(new T());
                m_DefaultObject->Initialize();
            }
            return m_DefaultObject.get();
        }
        
        virtual IUnknown* CreateInstance() const override
        {
            T* instance = new T();
            instance->Initialize();
            return instance;
        }
        
        virtual const PropertyField* GetPropertyField() const override
        {
            return m_PropertyField.get();
        }
        
        virtual const FunctionField* GetFunctionField() const override
        {
            return m_FunctionField.get();
        }
        
        virtual const ClassProperty* GetProperty(const Identity& name) const override
        {
            return m_PropertyField->GetProperty(name);
        }
        
        virtual Container::VariableArray<const ClassProperty*> GetAllProperties() const override
        {
            return m_PropertyField->GetAllProperties();
        }
        
        virtual const ClassFunction* GetFunction(const Identity& name) const override
        {
            return m_FunctionField->GetFunction(name);
        }
        
        virtual Container::VariableArray<const ClassFunction*> GetAllFunctions() const override
        {
            return m_FunctionField->GetAllFunctions();
        }
        
        virtual uint64_t GetClassId() const override
        {
            return m_ClassId;
        }
        
        virtual size_t GetVariableContainerSize() const override
        {
            return m_PropertyField->GetTotalSize();
        }
        
        virtual void InitializeVariableContainer(void* container) const override
        {
            // 単純な初期化（クラスの詳細に応じて、より複雑な初期化が必要かもしれない）
            if (container)
            {
                std::memset(container, 0, GetVariableContainerSize());
                
                // 必要に応じて、個別のプロパティのデフォルト値を設定する
                // この例では単純に0で初期化
            }
        }

        // シングルトンインスタンス取得
        static TClass<T>& GetInstance()
        {
            static TClass<T> instance("#Undefined", nullptr);
            return instance;
        }
        
        // クラス情報の初期化
        void Initialize(const Container::String& className, T* defaultObject)
        {
            m_ClassName = Identity(className);
            m_DefaultObject.reset(defaultObject);
        }

    private:
        void RegisterClassProperties()
        {
            if constexpr (std::is_same_v<T, Object>)
            {
                // Objectクラスは特殊ケースで、PropertyRegistryを使わない
                // 基底クラスなので必要に応じてここでプロパティを直接追加
            }
            else
            {
                // 登録されたすべてのプロパティをフィールドに追加
                for (const auto& prop : T::s_PropertyRegistry.GetProperties())
                {
                    m_PropertyField->AddProperty(prop);
                }
            }
        }
        
        void RegisterClassFunctions()
        {
            if constexpr (std::is_same_v<T, Object>)
            {
                // Objectクラスは特殊ケースで、FunctionRegistryを使わない
                // 必要に応じて関数を直接追加
            }
            else
            {
                // 登録されたすべての関数をフィールドに追加
                for (const auto& func : T::s_FunctionRegistry.GetFunctions())
                {
                    m_FunctionField->AddFunction(func);
                }
            }
        }
        
        void InheritFromParent()
        {
            const PropertyField* parentPropertyField = m_ParentClass->GetPropertyField();
            if (parentPropertyField)
            {
                for (const auto& prop : parentPropertyField->GetAllProperties())
                {
                    // 親クラスのプロパティをコピーして登録
                    auto copiedProp = std::make_shared<ClassProperty>(
                        prop->GetName(), prop->GetType(), prop->GetOffset(), prop->GetSize(), prop->GetFlags());
                    m_PropertyField->AddProperty(copiedProp);
                }
            }
            
            const FunctionField* parentFunctionField = m_ParentClass->GetFunctionField();
            if (parentFunctionField)
            {
                for (const auto& func : parentFunctionField->GetAllFunctions())
                {
                    // 実際の実装ではオーバーライドされた関数を考慮する必要がある
                    // この例では単純に親の関数をコピー
                    // TODO: 関数オーバーライドの処理を実装
                }
            }
        }
        
        // クラスIDを生成するヘルパー関数
        static uint64_t GenerateClassId()
        {
            static uint64_t nextId = 1;
            return nextId++;
        }
        
    private:
        Identity m_ClassName;                  // クラス名
        const IClass* m_ParentClass;                  // 親クラス
        std::unique_ptr<PropertyField> m_PropertyField;  // プロパティフィールド
        std::unique_ptr<FunctionField> m_FunctionField;  // 関数フィールド
        mutable std::unique_ptr<T> m_DefaultObject;   // デフォルトオブジェクト
        uint64_t m_ClassId;                           // クラスID
    };

    /**
     * @brief 親クラスを指定できるTClassの特殊化
     * REFLECT_CLASS_PARENTマクロで使用されます
     */
    template<typename T, typename Parent>
    class TClass<T, Parent> : public IClass
    {
    public:
        TClass(const Container::String& className)
            : m_ClassName(Identity(className))
            , m_ParentClass(&Parent::StaticClass())
            , m_PropertyField(std::make_unique<PropertyField>())
            , m_FunctionField(std::make_unique<FunctionField>())
            , m_ClassId(GenerateClassId())
        {
            // 親クラスからプロパティと関数を継承
            InheritFromParent();
            
            // このクラス独自のプロパティを登録
            RegisterClassProperties();
            
            // このクラス独自の関数を登録
            RegisterClassFunctions();
            
            // クラスをレジストリに登録
            ClassRegistry::Get().RegisterClass(this);
        }
        
        virtual ~TClass() = default;
        
        // IClassインターフェースの実装
        virtual const Identity& GetClassName() const override { return m_ClassName; }
        virtual const IClass* GetParentClass() const override { return m_ParentClass; }
        
        virtual bool IsChildOf(const IClass* cls) const override
        {
            if (this == cls) return true;
            if (m_ParentClass) return m_ParentClass->IsChildOf(cls);
            return false;
        }
        
        // シングルトンインスタンス取得
        static TClass<T, Parent>& GetInstance()
        {
            static TClass<T, Parent> instance("#Undefined");
            return instance;
        }
        
        // クラス情報の初期化
        void Initialize(const Container::String& className, T* defaultObject)
        {
            m_ClassName = Identity(className);
            m_DefaultObject.reset(defaultObject);
        }

        virtual const IUnknown* GetDefaultObject() const override
        {
            if (!m_DefaultObject)
            {
                m_DefaultObject.reset(new T());
                m_DefaultObject->Initialize();
            }
            return m_DefaultObject.get();
        }
        
        virtual IUnknown* CreateInstance() const override
        {
            T* instance = new T();
            instance->Initialize();
            return instance;
        }
        
        virtual const PropertyField* GetPropertyField() const override
        {
            return m_PropertyField.get();
        }
        
        virtual const FunctionField* GetFunctionField() const override
        {
            return m_FunctionField.get();
        }
        
        virtual const ClassProperty* GetProperty(const Identity& name) const override
        {
            return m_PropertyField->GetProperty(name);
        }
        
        virtual Container::VariableArray<const ClassProperty*> GetAllProperties() const override
        {
            return m_PropertyField->GetAllProperties();
        }
        
        virtual const ClassFunction* GetFunction(const Identity& name) const override
        {
            return m_FunctionField->GetFunction(name);
        }
        
        virtual Container::VariableArray<const ClassFunction*> GetAllFunctions() const override
        {
            return m_FunctionField->GetAllFunctions();
        }
        
        virtual uint64_t GetClassId() const override
        {
            return m_ClassId;
        }
        
        virtual size_t GetVariableContainerSize() const override
        {
            return m_PropertyField->GetTotalSize();
        }
        
        virtual void InitializeVariableContainer(void* container) const override
        {
            // 単純な初期化（クラスの詳細に応じて、より複雑な初期化が必要かもしれない）
            if (container)
            {
                std::memset(container, 0, GetVariableContainerSize());
                
                // 必要に応じて、個別のプロパティのデフォルト値を設定する
                // この例では単純に0で初期化
            }
        }

    private:
        void RegisterClassProperties()
        {
            // 登録されたすべてのプロパティをフィールドに追加
            for (const auto& prop : T::s_PropertyRegistry.GetProperties())
            {
                m_PropertyField->AddProperty(prop);
            }
        }
        
        void RegisterClassFunctions()
        {
            // 登録されたすべての関数をフィールドに追加
            for (const auto& func : T::s_FunctionRegistry.GetFunctions())
            {
                m_FunctionField->AddFunction(func);
            }
        }
        
        void InheritFromParent()
        {
            const PropertyField* parentPropertyField = m_ParentClass->GetPropertyField();
            if (parentPropertyField)
            {
                for (const auto& prop : parentPropertyField->GetAllProperties())
                {
                    // 親クラスのプロパティをコピーして登録
                    auto copiedProp = std::make_shared<ClassProperty>(
                        prop->GetName(), prop->GetType(), prop->GetOffset(), prop->GetSize(), prop->GetFlags());
                    m_PropertyField->AddProperty(copiedProp);
                }
            }
            
            const FunctionField* parentFunctionField = m_ParentClass->GetFunctionField();
            if (parentFunctionField)
            {
                for (const auto& func : parentFunctionField->GetAllFunctions())
                {
                    // 実際の実装ではオーバーライドされた関数を考慮する必要がある
                    // この例では単純に親の関数をコピー
                    // TODO: 関数オーバーライドの処理を実装
                    auto copiedFunc = std::make_shared<ClassFunction>(
                        func->GetName(), func->GetReturnType(), func->GetFlags());
                    m_FunctionField->AddFunction(copiedFunc);
                }
            }
        }
        
        // クラスIDを生成するヘルパー関数
        static uint64_t GenerateClassId()
        {
            static uint64_t nextId = 1;
            return nextId++;
        }
        
    private:
        Identity m_ClassName;                  // クラス名
        const IClass* m_ParentClass;                  // 親クラス
        std::unique_ptr<PropertyField> m_PropertyField;  // プロパティフィールド
        std::unique_ptr<FunctionField> m_FunctionField;  // 関数フィールド
        mutable std::unique_ptr<T> m_DefaultObject;   // デフォルトオブジェクト
        uint64_t m_ClassId;                           // クラスID
    };

} // namespace NorvesLib::Core