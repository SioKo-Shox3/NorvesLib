#pragma once

#include <memory>
#include <type_traits>
#include "IClass.h"
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

            // 祖先テーブル（Cohenの定数時間判定）を構築する。
            // m_ParentClassが確定した後に行う必要がある。
            // 構築はstatic初期化の一度きりなので、親チェーンを線形に辿っても問題ない。
            BuildAncestorTable();

            // キャストフラグ（第1層）を蓄積する。親のフラグは既に全祖先を含むため
            // 「自分のフラグ | 親のフラグ」で全祖先ぶんのビットが揃う。
            m_CastFlags = ClassCastFlagTraits<T>::Value
                          | (m_ParentClass ? m_ParentClass->GetCastFlags() : EClassCastFlags::None);

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
            if (!cls)
            {
                return false;
            }
            const uint32_t d = cls->GetDepth();
            // 配列1アクセス・再帰なしの定数時間判定
            return d <= m_Depth && m_Ancestors[d] == cls;
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

        virtual uint32_t GetDepth() const override
        {
            return m_Depth;
        }

        virtual EClassCastFlags GetCastFlags() const override
        {
            return m_CastFlags;
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
                for (const auto &prop : T::s_PropertyRegistry().GetProperties())
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
                for (const auto &func : T::s_FunctionRegistry().GetFunctions())
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

        // 祖先テーブル（深さindex→祖先IClass*）を構築する。
        // m_Ancestors[d]はこのクラスの深さdの祖先を指し、
        // m_Ancestors[m_Depth] == this となる。
        // classIdではなくIClass*のポインタ同値で比較するため、
        // static初期化順やシリアライズに非依存。
        void BuildAncestorTable()
        {
            if (m_ParentClass)
            {
                // 親があれば深さは親+1
                m_Depth = m_ParentClass->GetDepth() + 1;
                m_Ancestors.resize(m_Depth + 1);

                // 自分自身を末尾（深さm_Depth）に格納
                m_Ancestors[m_Depth] = this;

                // 親チェーンを根まで辿り、各祖先を対応する深さindexへ格納する。
                // 各祖先のGetDepth()がその祖先の正しい深さを返すため、
                // IClassに祖先列アクセサを追加せずに全indexを埋められる。
                for (const IClass *ancestor = m_ParentClass; ancestor != nullptr; ancestor = ancestor->GetParentClass())
                {
                    m_Ancestors[ancestor->GetDepth()] = ancestor;
                }
            }
            else
            {
                // 親が無ければルート（深さ0・自分自身のみ）
                m_Depth = 0;
                m_Ancestors.resize(1);
                m_Ancestors[0] = this;
            }
        }

    private:
        Identity m_ClassName;                           // クラス名
        const IClass *m_ParentClass;                    // 親クラス
        std::unique_ptr<PropertyField> m_PropertyField; // プロパティフィールド
        std::unique_ptr<FunctionField> m_FunctionField; // 関数フィールド
        uint64_t m_ClassId;                             // クラスID
        uint32_t m_Depth = 0;                           // 継承の深さ（ルート=0）
        Container::VariableArray<const IClass *> m_Ancestors; // 祖先テーブル（index=深さ、末尾=this）
        EClassCastFlags m_CastFlags = EClassCastFlags::None;  // キャストフラグ（自分＋全祖先のOR）
    };

} // namespace NorvesLib::Core
