#pragma once

#include <memory>
#include <type_traits>
#include "IClass.h"
#include "ObjectUtility.h"
#include "Container/Containers.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    // 前方宣言
    class Object; // Object前方宣言を追加

    template <typename ClassType>
    class PropertyRegistry;

    template <typename ClassType>
    class FunctionRegistry;

    /**
     * @brief IClassの実装テンプレート
     * 具体的なクラス型に対するリフレクション情報を提供します
     */
    template <typename T, typename Parent = void>
    class TClass : public IClass
    {
    public:
        TClass(const Container::String &className, const IClass *parentClass = nullptr)
            : m_ClassName(Identity(className)), m_ParentClass(parentClass), m_PropertyField(std::make_unique<PropertyField>()), m_FunctionField(std::make_unique<FunctionField>()), m_ClassId(GenerateClassId())
        {
            // 親クラスがテンプレート引数で指定されている場合
            if constexpr (!std::is_same_v<Parent, void>)
            {
                m_ParentClass = Parent::StaticClass();
            }

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
        virtual const Identity &GetClassName() const override { return m_ClassName; }
        virtual const IClass *GetParentClass() const override { return m_ParentClass; }

        virtual bool IsChildOf(const IClass *cls) const override
        {
            if (this == cls)
                return true;
            if (m_ParentClass)
                return m_ParentClass->IsChildOf(cls);
            return false;
        }

        virtual const PropertyField *GetPropertyField() const override
        {
            return m_PropertyField.get();
        }

        virtual const FunctionField *GetFunctionField() const override
        {
            return m_FunctionField.get();
        }

        virtual const ClassProperty *GetProperty(const Identity &name) const override
        {
            return m_PropertyField->GetProperty(name);
        }

        virtual Container::VariableArray<const ClassProperty *> GetAllProperties() const override
        {
            return m_PropertyField->GetAllProperties();
        }

        virtual const ClassFunction *GetFunction(const Identity &name) const override
        {
            return m_FunctionField->GetFunction(name);
        }

        virtual Container::VariableArray<const ClassFunction *> GetAllFunctions() const override
        {
            return m_FunctionField->GetAllFunctions();
        }

        virtual uint64_t GetClassId() const override
        {
            return m_ClassId;
        }

        // シングルトンインスタンス取得（Parent指定版とそうでない版を統一）
        static TClass<T, Parent> &GetInstance()
        {
            if constexpr (std::is_same_v<Parent, void>)
            {
                static TClass<T, Parent> instance("#Undefined", nullptr);
                return instance;
            }
            else
            {
                static TClass<T, Parent> instance("#Undefined");
                return instance;
            }
        }

        virtual IUnknown *NewInstance([[maybe_unused]] IUnknown *outer = nullptr) const override
        {
            try
            {
                return new T();
            }
            catch ([[maybe_unused]] const std::exception &e)
            {
                // 例外が発生した場合はnullptrを返す
                return nullptr;
            }
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
                for (const auto &prop : T::s_PropertyRegistry.GetProperties())
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
                for (const auto &func : T::s_FunctionRegistry.GetFunctions())
                {
                    m_FunctionField->AddFunction(func);
                }
            }
        }

        void InheritFromParent()
        {
            const PropertyField *parentPropertyField = m_ParentClass->GetPropertyField();
            if (parentPropertyField)
            {
                for (const auto &prop : parentPropertyField->GetAllProperties())
                {
                    // 親クラスのプロパティをそのまま登録（共有）
                    // 注意: 親クラスのプロパティへの参照を保持
                    m_PropertyField->AddInheritedProperty(prop);
                }
            }

            const FunctionField *parentFunctionField = m_ParentClass->GetFunctionField();
            if (parentFunctionField)
            {
                for (const auto &func : parentFunctionField->GetAllFunctions())
                {
                    // 親クラスの関数をそのまま登録（共有）
                    m_FunctionField->AddInheritedFunction(func);
                }
            }
        }

        // クラスIDを生成するヘルパー関数
        static uint64_t GenerateClassId()
        {
            return ClassRegistry::Get().AllocateClassId();
        }

    private:
        Identity m_ClassName;                           // クラス名
        const IClass *m_ParentClass;                    // 親クラス
        std::unique_ptr<PropertyField> m_PropertyField; // プロパティフィールド
        std::unique_ptr<FunctionField> m_FunctionField; // 関数フィールド
        uint64_t m_ClassId;                             // クラスID
    };

} // namespace NorvesLib::Core
