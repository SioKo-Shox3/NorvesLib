#include "Object/IUnknown.h"
#include "Object/IClass.h"
#include "Object/IValue.h"

namespace NorvesLib::Core
{
    UnknownImpl::UnknownImpl()
        : m_RefCount(0)
        , m_Flags(0)
        , m_VariableContainer(nullptr)
    {
    }

    UnknownImpl::~UnknownImpl()
    {
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

    std::unique_ptr<IValue> UnknownImpl::GetProperty(const Container::String propertyName) const
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

    bool UnknownImpl::SetProperty(const Container::String& propertyName, const IValue* value)
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

    void UnknownImpl::Initialize()
    {
        // VariableContainerを初期化
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

        // 初期化済みフラグを設定
        SetFlag(OF_Initialized, true);
    }

    void UnknownImpl::Finalize()
    {
        // 初期化済みフラグをクリア
        SetFlag(OF_Initialized, false);
    }

} // namespace NorvesLib::Core