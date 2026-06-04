#include "Object/IUnknown.h"
#include "Object/IClass.h"
#include "Object/ObjectUtility.h"

namespace NorvesLib::Core
{
    UnknownImpl::UnknownImpl()
        : m_RefCount(0), m_Flags(0), m_VariableContainer(nullptr), m_Outer(nullptr)
    {
        InitializeVariableContainer();
    }

    UnknownImpl::UnknownImpl(const FieldInitializer *initializer)
        : m_RefCount(0), m_Flags(0), m_VariableContainer(nullptr), m_Outer(nullptr)
    {
        InitializeVariableContainer();

        if (initializer)
        {
            // デフォルトオブジェクトにフィールド初期化子を適用
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
            }
        }
    }

    UnknownImpl::UnknownImpl(const IUnknown *sourceObject)
        : m_RefCount(0), m_Flags(0), m_VariableContainer(nullptr), m_Outer(nullptr)
    {
        // 変数コンテナを初期化
        InitializeVariableContainer();

        // ソースオブジェクトからデータをコピー
        if (sourceObject)
        {
            CopyFromObject(sourceObject);
        }
    }

    UnknownImpl::~UnknownImpl()
    {
        while (!m_Inners.empty())
        {
            IUnknown *inner = m_Inners.back();
            m_Inners.pop_back();

            if (auto *innerImpl = dynamic_cast<UnknownImpl *>(inner))
            {
                innerImpl->SetOuter(nullptr);
            }

            if (inner)
            {
                inner->Release();
            }
        }

        m_Outer = nullptr;
    }

    uint32_t UnknownImpl::AddRef() const
    {
        return ++m_RefCount;
    }

    uint32_t UnknownImpl::Release() const
    {
        uint32_t current = m_RefCount.GetValue(std::memory_order_acquire);
        while (current > 0)
        {
            const uint32_t next = current - 1;
            if (m_RefCount.CompareExchangeStrong(
                    current,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                if (next == 0 && !HasFlag(OF_DefaultObject))
                {
                    auto *mutableThis = const_cast<UnknownImpl *>(this);
                    mutableThis->SetFlag(OF_PendingDestroy, true);
                    mutableThis->Finalize();
                    delete mutableThis;
                }
                return next;
            }
        }

        return 0;
    }

    uint32_t UnknownImpl::GetRefCount() const
    {
        return m_RefCount.GetValue(std::memory_order_acquire);
    }

    bool UnknownImpl::HasFlag(uint32_t flag) const
    {
        return (m_Flags.GetValue() & flag) != 0;
    }

    void UnknownImpl::SetFlag(uint32_t flag, bool value)
    {
        if (value)
        {
            m_Flags.FetchOr(flag);
        }
        else
        {
            m_Flags.FetchAnd(~flag);
        }
    }

    void *UnknownImpl::GetPropertyValue(const Identity &propertyName)
    {
        // クラス情報を取得
        const IClass *cls = GetClass();
        if (!cls)
        {
            return nullptr;
        }

        // プロパティ情報を取得
        const ClassProperty *property = cls->GetProperty(propertyName);
        if (!property)
        {
            return nullptr;
        }

        // プロパティのデータへのポインタを取得
        return property->GetValuePtr(this);
    }

    const void *UnknownImpl::GetPropertyValue(const Identity &propertyName) const
    {
        // クラス情報を取得
        const IClass *cls = GetClass();
        if (!cls)
        {
            return nullptr;
        }

        // プロパティ情報を取得
        const ClassProperty *property = cls->GetProperty(propertyName);
        if (!property)
        {
            return nullptr;
        }

        // プロパティのデータへのポインタを取得
        return property->GetValuePtr(this);
    }

    VariableContainer *UnknownImpl::GetVariableContainer()
    {
        return m_VariableContainer.get();
    }

    const VariableContainer *UnknownImpl::GetVariableContainer() const
    {
        return m_VariableContainer.get();
    }

    const IClass *UnknownImpl::GetClass() const
    {
        // サブクラスでオーバーライドされるため、基底クラスではnullptrを返す
        return nullptr;
    }

    void UnknownImpl::Initialize()
    {
        // 初期化済みフラグを設定
        SetFlag(OF_Initialized, true);
    }

    void UnknownImpl::Finalize()
    {
        // 初期化済みフラグをクリア
        SetFlag(OF_Initialized, false);
    }

    bool UnknownImpl::IsDefaultObject() const
    {
        return HasFlag(OF_DefaultObject);
    }

    void UnknownImpl::InitializeVariableContainer()
    {
        const IClass *cls = GetClass();
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

    void UnknownImpl::CopyFromObject(const IUnknown *sourceObject)
    {
        if (!sourceObject)
        {
            return;
        }

        // VariableContainerをコピー
        const VariableContainer *srcContainer = sourceObject->GetVariableContainer();
        if (srcContainer && m_VariableContainer)
        {
            m_VariableContainer->CopyFrom(srcContainer);
        }

        // フラグをコピー (DefaultObjectフラグは除く)
        if (sourceObject->IsDefaultObject())
        {
            // デフォルトオブジェクトからコピーする場合、DefaultObjectフラグを除く
            m_Flags = sourceObject->HasFlag(OF_Initialized) ? OF_Initialized : 0;
        }
        else
        {
            // 通常オブジェクトからコピーする場合、通常のフラグをすべてコピー
            for (uint32_t flag = OF_Initialized; flag <= OF_Persistent; flag <<= 1)
            {
                if (sourceObject->HasFlag(flag))
                {
                    SetFlag(flag, true);
                }
            }
        }
    }

    // Outer/Inner関連メソッドの実装
    IUnknown *UnknownImpl::GetOuter()
    {
        return m_Outer;
    }

    const IUnknown *UnknownImpl::GetOuter() const
    {
        return m_Outer;
    }

    void UnknownImpl::SetOuter(IUnknown *outer)
    {
        m_Outer = outer;
    }

    const Container::VariableArray<IUnknown *> &UnknownImpl::GetInners() const
    {
        return m_Inners;
    }

    bool UnknownImpl::AddInner(IUnknown *inner)
    {
        if (!inner)
        {
            return false;
        }

        UnknownImpl *innerImpl = dynamic_cast<UnknownImpl *>(inner);
        if (!innerImpl || inner == this)
        {
            return false;
        }

        // すでに子リストに存在しないか確認
        for (IUnknown *existingInner : m_Inners)
        {
            if (existingInner == inner)
            {
                return false;
            }
        }

        if (innerImpl->GetOuter() != nullptr)
        {
            return false;
        }

        inner->AddRef();

        // 子リストに追加
        m_Inners.push_back(inner);

        // InnerのOuterを自身に設定
        innerImpl->SetOuter(this);
        return true;
    }

    bool UnknownImpl::RemoveInner(IUnknown *inner)
    {
        if (inner)
        {
            // 子リストから削除
            for (size_t i = 0; i < m_Inners.size(); ++i)
            {
                if (m_Inners[i] == inner)
                {
                    UnknownImpl *innerImpl = dynamic_cast<UnknownImpl *>(inner);

                    // InnerのOuterをクリア
                    if (innerImpl)
                    {
                        innerImpl->SetOuter(nullptr);
                    }

                    m_Inners.erase(m_Inners.begin() + i);
                    inner->Release();
                    return true; // 削除成功
                }
            }
        }
        return false; // 削除失敗（見つからなかった）
    }

} // namespace NorvesLib::Core
