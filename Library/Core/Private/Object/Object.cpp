#include "Object/Object.h"
#include "Object/IClass.h"
#include "Object/IValue.h"
#include "Object/ObjectUtility.h"

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

                // 自分自身との比較
                if (cls == this)
                {
                    return true;
                }

                // Objectはルートクラスなのでどのクラスのサブクラスでもない
                return false;
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

        private:
            Identity m_ClassName;
            uint64_t m_ClassId;
            PropertyField *m_PropertyField;
            FunctionField *m_FunctionField;
        };

        // 静的クラスインスタンス
        static ObjectClass g_ObjectClass;
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

        ObjectUtility::ApplyInitialValues(this, initializer);
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
        return &g_ObjectClass;
    }

} // namespace NorvesLib::Core
