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
                  m_PropertyField(new PropertyField()), m_FunctionField(new FunctionField()), m_DefaultObject(nullptr)
            {
                // クラスレジストリに登録
                ClassRegistry::Get().RegisterClass(this);

                // デフォルトオブジェクトを作成
                m_DefaultObject = new Object();
                m_DefaultObject->SetFlag(OF_DefaultObject, true);

                // FieldInitializerを使用してデフォルト値を設定
                FieldInitializer initializer;
                // ここでObjectクラスのデフォルト値をsetup
                // 例: initializer.SetInitialValue<int>("PropertyName", defaultValue);

                // デフォルトオブジェクトを初期化
                m_DefaultObject->Initialize();
            }

            ~ObjectClass()
            {
                // デフォルトオブジェクトを解放
                if (m_DefaultObject)
                {
                    delete m_DefaultObject;
                    m_DefaultObject = nullptr;
                }

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

            virtual const IUnknown *GetDefaultObject() const override
            {
                return m_DefaultObject;
            }

            virtual IUnknown *NewInstance(IUnknown *outer = nullptr) const override
            {
                // デフォルトオブジェクトをコピーして新しいインスタンスを作成
                return new Object(m_DefaultObject);
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

            virtual size_t GetVariableContainerSize() const override
            {
                if (m_PropertyField)
                {
                    return m_PropertyField->GetTotalSize();
                }
                return 0;
            }

            virtual void InitializeVariableContainer(void *container) const override
            {
                // 基本的な初期化を行う（すべて0に設定）
                if (container)
                {
                    std::memset(container, 0, GetVariableContainerSize());
                }
            }

        private:
            Identity m_ClassName;
            uint64_t m_ClassId;
            PropertyField *m_PropertyField;
            FunctionField *m_FunctionField;
            Object *m_DefaultObject;
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
        // 基本初期化
        UnknownImpl::Initialize();

        if (initializer)
        {
            const IClass *cls = GetClass();
            if (cls)
            {
                const Container::VariableArray<const ClassProperty *> properties = cls->GetAllProperties();

                // 各プロパティに初期値を適用
                for (const ClassProperty *prop : properties)
                {
                    if (prop)
                    {
                        prop->ApplyInitialValue(this, initializer);
                    }
                }
                return true;
            }
        }

        return false;
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

    IUnknown *Object::Clone() const
    {
        // ObjectはREFLECTION_CLASSを使用しないので、手動でクローンを実装
        Object *newInstance = new Object();
        if (newInstance)
        {
            // コンテナデータをコピー
            const VariableContainer *srcContainer = GetVariableContainer();
            VariableContainer *dstContainer = newInstance->GetVariableContainer();
            if (srcContainer && dstContainer)
            {
                const void *srcData = srcContainer->GetData();
                void *dstData = dstContainer->GetData();
                if (srcData && dstData && srcContainer->GetSize() > 0)
                {
                    std::memcpy(dstData, srcData, srcContainer->GetSize());
                }
            }
        }
        return newInstance;
    }

    IUnknown *Object::Clone(const FieldInitializer *initializer) const
    {
        Object *newInstance = static_cast<Object *>(Clone());
        if (newInstance && initializer)
        {
            ObjectUtility::ApplyInitialValues(newInstance, initializer);
        }
        return newInstance;
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
