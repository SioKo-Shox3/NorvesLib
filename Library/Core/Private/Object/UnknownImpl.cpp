#include "Object/IUnknown.h"
#include "Object/IClass.h"
#include "Object/IValue.h"
#include "Object/ObjectUtility.h"

namespace NorvesLib::Core
{
    UnknownImpl::UnknownImpl()
        : m_RefCount(0)
        , m_Flags(0)
        , m_VariableContainer(nullptr)
    {
        InitializeVariableContainer();
    }

    UnknownImpl::UnknownImpl(const FieldInitializer* initializer)
        : m_RefCount(0)
        , m_Flags(0)
        , m_VariableContainer(nullptr)
    {
        InitializeVariableContainer();
        ApplyFieldInitializer(initializer);
    }

    UnknownImpl::~UnknownImpl()
    {
        // VariableContainerはスマートポインタで自動的に解放されます
    }

    uint32_t UnknownImpl::AddRef() const
    {
        return ++m_RefCount;
    }

    uint32_t UnknownImpl::Release() const
    {
        uint32_t count = --m_RefCount;
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    bool UnknownImpl::HasFlag(uint32_t flag) const
    {
        return (m_Flags & flag) != 0;
    }

    void UnknownImpl::SetFlag(uint32_t flag, bool value)
    {
        if (value)
        {
            m_Flags |= flag;
        }
        else
        {
            m_Flags &= ~flag;
        }
    }

    std::unique_ptr<IValue> UnknownImpl::GetProperty(const Identity& propertyName) const
    {
        // クラス情報を取得
        const IClass* cls = GetClass();
        if (!cls)
        {
            return nullptr;
        }

        // プロパティ情報を取得
        const ClassProperty* property = cls->GetProperty(propertyName);
        if (!property)
        {
            return nullptr;
        }

        // プロパティの値を取得
        return property->GetValue(this);
    }

    bool UnknownImpl::SetProperty(const Identity& propertyName, const IValue* value)
    {
        if (!value)
        {
            return false;
        }

        // クラス情報を取得
        const IClass* cls = GetClass();
        if (!cls)
        {
            return false;
        }

        // プロパティ情報を取得
        const ClassProperty* property = cls->GetProperty(propertyName);
        if (!property)
        {
            return false;
        }

        // プロパティに値を設定
        return property->SetValue(this, value);
    }

    VariableContainer* UnknownImpl::GetVariableContainer()
    {
        return m_VariableContainer.get();
    }

    const VariableContainer* UnknownImpl::GetVariableContainer() const
    {
        return m_VariableContainer.get();
    }

    const IClass* UnknownImpl::GetClass() const
    {
        // サブクラスでオーバーライドされるため、基底クラスではnullptrを返す
        return nullptr;
    }

    IUnknown* UnknownImpl::Clone() const
    {
        // サブクラスでオーバーライドされるため、基底クラスではnullptrを返す
        return nullptr;
    }

    IUnknown* UnknownImpl::Clone(const FieldInitializer* initializer) const
    {
        // サブクラスでオーバーライドされるため、基底クラスではnullptrを返す
        return nullptr;
    }

    void UnknownImpl::Initialize()
    {
        // 初期化済みフラグを設定
        SetFlag(OF_Initialized, true);
    }

    bool UnknownImpl::Initialize(const FieldInitializer* initializer)
    {
        // 基本的な初期化を行う
        Initialize();

        // フィールド初期化子を適用
        return ApplyFieldInitializer(initializer) > 0;
    }

    void UnknownImpl::Finalize()
    {
        // 初期化済みフラグをクリア
        SetFlag(OF_Initialized, false);
    }

    void UnknownImpl::InitializeVariableContainer()
    {
        const IClass* cls = GetClass();
        if (cls)
        {
            size_t containerSize = cls->GetVariableContainerSize();
            if (containerSize > 0)
            {
                m_VariableContainer = std::make_unique<VariableContainer>(containerSize);
                cls->InitializeVariableContainer(m_VariableContainer->GetData());
            }
        }
    }

    int UnknownImpl::ApplyFieldInitializer(const FieldInitializer* initializer)
    {
        if (initializer)
        {
            return ObjectUtility::ApplyInitialValues(this, initializer);
        }
        return 0;
    }

} // namespace NorvesLib::Core