#include "Object/IUnknown.h"
#include "Object/IClass.h"

namespace NorvesLib::Core
{
    UnknownImpl::UnknownImpl()
        : m_Flags(0), m_Outer(nullptr)
    {
    }

    UnknownImpl::UnknownImpl(const FieldInitializer *initializer)
        : m_Flags(0), m_Outer(nullptr)
    {
        (void)initializer;
    }

    UnknownImpl::UnknownImpl(const IUnknown *sourceObject)
        : m_Flags(0), m_Outer(nullptr)
    {
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

            if (inner->HasFlag(OF_HeapOwned))
            {
                inner->SetFlag(OF_PendingDestroy, true);
            }
            else
            {
                inner->Finalize();
                delete inner;
            }
        }

        m_Outer = nullptr;
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

    void UnknownImpl::CopyFromObject(const IUnknown *sourceObject)
    {
        if (!sourceObject)
        {
            return;
        }

        for (uint32_t flag = OF_Initialized; flag <= OF_Persistent; flag <<= 1)
        {
            if (sourceObject->HasFlag(flag))
            {
                SetFlag(flag, true);
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
                    return true; // 削除成功
                }
            }
        }
        return false; // 削除失敗（見つからなかった）
    }

} // namespace NorvesLib::Core
