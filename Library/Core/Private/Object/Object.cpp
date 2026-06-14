#include "Object/Object.h"
#include "Object/IClass.h"
#include "Object/IValue.h"
#include "Object/ObjectPropertyOps.h"

namespace NorvesLib::Core
{
    // Object クラスのための静的クラスインスタンス
    namespace
    {
        class ObjectClass : public IClass
        {
        public:
            ObjectClass()
                : m_ClassName("Object"), m_ClassId(0) // Object基本クラスには0のIDを割り当てる
                  ,
                  m_PropertyField(new PropertyField()), m_FunctionField(new FunctionField())
            {
                // 祖先テーブル（Cohenの定数時間判定）を初期化する。
                // Objectはルートクラスなので深さ0・自分自身のみ。
                m_Ancestors = {this};

                // クラスレジストリに登録
                ClassRegistry::Get().RegisterClass(this);
            }

            ~ObjectClass()
            {
                // フィールドを解放
                if (m_PropertyField)
                {
                    delete m_PropertyField;
                    m_PropertyField = nullptr;
                }

                if (m_FunctionField)
                {
                    delete m_FunctionField;
                    m_FunctionField = nullptr;
                }
            }

            // IClass インターフェース実装
            virtual const Identity &GetClassName() const override
            {
                return m_ClassName;
            }

            virtual const IClass *GetParentClass() const override
            {
                // Object は最上位クラスなので親はない
                return nullptr;
            }

            virtual bool IsChildOf(const IClass *cls) const override
            {
                if (!cls)
                {
                    return false;
                }
                const uint32_t d = cls->GetDepth();
                return d <= m_Depth && m_Ancestors[d] == cls;
            }

            virtual IUnknown *NewInstance(IUnknown *outer = nullptr) const override
            {
                (void)outer;
                return new Object();
            }

            virtual const PropertyField *GetPropertyField() const override
            {
                return m_PropertyField;
            }

            virtual const FunctionField *GetFunctionField() const override
            {
                return m_FunctionField;
            }

            virtual const ClassProperty *GetProperty(const Identity &name) const override
            {
                if (m_PropertyField)
                {
                    return m_PropertyField->GetProperty(name);
                }
                return nullptr;
            }

            virtual Container::VariableArray<const ClassProperty *> GetAllProperties() const override
            {
                if (m_PropertyField)
                {
                    return m_PropertyField->GetAllProperties();
                }
                return {};
            }

            virtual const ClassFunction *GetFunction(const Identity &name) const override
            {
                if (m_FunctionField)
                {
                    return m_FunctionField->GetFunction(name);
                }
                return nullptr;
            }

            virtual Container::VariableArray<const ClassFunction *> GetAllFunctions() const override
            {
                if (m_FunctionField)
                {
                    return m_FunctionField->GetAllFunctions();
                }
                return {};
            }

            virtual uint64_t GetClassId() const override
            {
                return m_ClassId;
            }

            virtual uint32_t GetDepth() const override
            {
                // Objectはルートクラスなので深さ0
                return 0;
            }

        private:
            Identity m_ClassName;
            uint64_t m_ClassId;
            PropertyField *m_PropertyField;
            FunctionField *m_FunctionField;
            uint32_t m_Depth = 0;                                  // 継承の深さ（ルート=0）
            Container::VariableArray<const IClass *> m_Ancestors;  // 祖先テーブル（深さ0・自分自身のみ）
        };

    }

    Object::Object()
        : UnknownImpl()
    {
    }

    Object::Object(const FieldInitializer *initializer)
        : UnknownImpl(initializer)
    {
    }

    Object::Object(const IUnknown *sourceObject)
        : UnknownImpl(sourceObject)
    {
        // UnknownImplのコンストラクタですべての必要な処理は行われる
    }

    Object::~Object()
    {
        Finalize();
    }

    const IClass *Object::GetClass() const
    {
        return StaticClass();
    }

    void Object::Initialize()
    {
        // 基本初期化を呼び出す
        UnknownImpl::Initialize();
    }

    bool Object::Initialize(const FieldInitializer *initializer)
    {
        UnknownImpl::Initialize();
        if (!initializer)
        {
            return false;
        }

        ApplyInitialValues(this, initializer);
        return true;
    }

    void Object::Finalize()
    {
        UnknownImpl::Finalize();
    }

    void Object::Destroy()
    {
        SetFlag(OF_PendingDestroy, true);
    }

    bool Object::IsPendingDestroy() const
    {
        return HasFlag(OF_PendingDestroy);
    }

    const Identity &Object::GetTypeName() const
    {
        return GetClass()->GetClassName();
    }

    bool Object::IsA(const IClass *cls) const
    {
        if (!cls)
        {
            return false;
        }

        return GetClass()->IsChildOf(cls);
    }

    const IClass *Object::StaticClass()
    {
        // ルートクラスのIClassは関数ローカルstatic（Meyersシングルトン）として保持する。
        // これにより初回アクセス時に必ず構築され、TClass群（同じくMeyersシングルトン）と
        // 同様にstatic初期化順の依存（static initialization order fiasco）を排除する。
        // 旧実装はnamespaceスコープのグローバルだったため、別TUのstatic初期化子が
        // 構築前のルートクラスへキャスト経路でアクセスし得た。
        static ObjectClass s_ObjectClass;
        return &s_ObjectClass;
    }

} // namespace NorvesLib::Core
