#include "Object/IUnknown.h"
#include "Object/IClass.h"
#include "Object/ObjectUtility.h"

namespace NorvesLib::Core
{
    UnknownImpl::UnknownImpl()
        : m_RefCount(0), m_Flags(OF_DefaultObject) // デフォルトオブジェクトフラグを設定
          ,
          m_VariableContainer(nullptr), m_Outer(nullptr)
    {
        InitializeVariableContainer();
    }

    UnknownImpl::UnknownImpl(const FieldInitializer *initializer)
        : m_RefCount(0), m_Flags(OF_DefaultObject) // デフォルトオブジェクトフラグを設定
          ,
          m_VariableContainer(nullptr), m_Outer(nullptr)
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
        : m_RefCount(0), m_Flags(0) // 通常オブジェクトとして初期化
          ,
          m_VariableContainer(nullptr), m_Outer(nullptr)
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
        // VariableContainerはスマートポインタで自動的に解放されます
    }

    uint32_t UnknownImpl::AddRef() const
    {
        return ++m_RefCount;
    }

    uint32_t UnknownImpl::Release() const
    {
        uint32_t count = --m_RefCount;
        if (count == 0 && !HasFlag(OF_DefaultObject)) // デフォルトオブジェクトは自動削除しない
        {
            delete this;
        }
        return count;
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

    void UnknownImpl::AddInner(IUnknown *inner)
    {
        if (inner)
        {
            // すでに子リストに存在しないか確認
            for (IUnknown *existingInner : m_Inners)
            {
                if (existingInner == inner)
                {
                    return; // すでに存在する場合は何もしない
                }
            }

            // 子リストに追加
            m_Inners.push_back(inner);
        }
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
                    m_Inners.erase(m_Inners.begin() + i);
                    return true; // 削除成功
                }
            }
        }
        return false; // 削除失敗（見つからなかった）
    }

} // namespace NorvesLib::Core
