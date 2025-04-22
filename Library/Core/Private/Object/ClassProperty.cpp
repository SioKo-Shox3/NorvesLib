#include "Object/IClass.h"
#include "Object/IUnknown.h"
#include "Object/IValue.h"
#include "Object/TValue.h"

namespace NorvesLib::Core
{
    /**
     * @brief 変数の値を取得します
     * @param instance オブジェクトインスタンス
     * @return 変数の値
     */
    std::unique_ptr<IValue> ClassProperty::GetValue(const IUnknown* instance) const
    {
        if (!instance)
        {
            return nullptr;
        }

        // インスタンスからVariableContainerを取得
        const VariableContainer* container = instance->GetVariableContainer();
        if (!container)
        {
            return nullptr;
        }

        // オフセット位置のデータを取得
        const void* data = container->GetAt(m_Offset);
        if (!data)
        {
            return nullptr;
        }

        // 型情報に基づいて適切な型のTValueを作成
        std::unique_ptr<IValue> value = nullptr;
        
        // 実際にはここで型情報を使って適切なTValue<T>を作成する必要がある
        // この例では基本的なデータ型を扱う方法を示す

        // TODO: 型情報を使って適切なTValueを作成するロジックを実装
        // 以下は簡略化した例
        if (m_Size == sizeof(bool))
        {
            bool val = *static_cast<const bool*>(data);
            value = MakeBoolValue(val, m_Name);
        }
        else if (m_Size == sizeof(int32_t))
        {
            int32_t val = *static_cast<const int32_t*>(data);
            value = MakeInt32Value(val, m_Name);
        }
        else if (m_Size == sizeof(float))
        {
            float val = *static_cast<const float*>(data);
            value = MakeFloatValue(val, m_Name);
        }
        else if (m_Size == sizeof(double))
        {
            double val = *static_cast<const double*>(data);
            value = MakeDoubleValue(val, m_Name);
        }
        else if (m_Size == sizeof(void*))
        {
            // ポインタ型の場合
            void* val = *static_cast<void* const*>(data);
            
            // ポインタがIUnknown型かどうか確認して処理
            // TODO: ここでは簡略化のために単純なポインタとして扱う
            value = MakeValue<void*>(val, m_Name);
        }
        else
        {
            // サイズがわからない場合はバイナリデータとして扱う
            // 実際にはもっと複雑な型情報処理が必要
        }

        return value;
    }

    /**
     * @brief 変数の値を設定します
     * @param instance オブジェクトインスタンス
     * @param value 設定する値
     * @return 成功した場合はtrue
     */
    bool ClassProperty::SetValue(IUnknown* instance, const IValue* value) const
    {
        if (!instance || !value)
        {
            return false;
        }

        // インスタンスからVariableContainerを取得
        VariableContainer* container = instance->GetVariableContainer();
        if (!container)
        {
            return false;
        }

        // 値の型が対象の型と一致するか、変換可能か確認
        if (!value->CanConvertTo(m_Type))
        {
            // 型の不一致で変換不可能
            return false;
        }

        // コンテナ内の位置に値をコピー
        void* dst = container->GetAt(m_Offset);
        if (!dst)
        {
            return false;
        }

        // 型情報に基づいて適切な方法でデータをコピー
        // TODO: 型情報を使って適切な変換とコピーを行うロジックを実装
        // 以下は簡略化した例
        if (m_Size == sizeof(bool))
        {
            bool val = value->AsBool();
            return container->CopyTo(m_Offset, &val, sizeof(bool));
        }
        else if (m_Size == sizeof(int32_t))
        {
            int32_t val = value->AsInt32();
            return container->CopyTo(m_Offset, &val, sizeof(int32_t));
        }
        else if (m_Size == sizeof(float))
        {
            float val = value->AsFloat();
            return container->CopyTo(m_Offset, &val, sizeof(float));
        }
        else if (m_Size == sizeof(double))
        {
            double val = value->AsDouble();
            return container->CopyTo(m_Offset, &val, sizeof(double));
        }
        else if (m_Size == sizeof(void*))
        {
            // ポインタ型の場合
            if (value->GetValueType() == IValue::ValueType::Object)
            {
                // IUnknown型のオブジェクトポインタ
                void* val = const_cast<IUnknown*>(value->AsObject());
                return container->CopyTo(m_Offset, &val, sizeof(void*));
            }
            else
            {
                // その他のポインタ型
                const void* srcData = value->GetValuePtr();
                return container->CopyTo(m_Offset, &srcData, sizeof(void*));
            }
        }
        else
        {
            // 直接データをコピー（単純な型の場合）
            return container->CopyTo(m_Offset, value->GetValuePtr(), std::min(m_Size, value->GetSize()));
        }
    }

} // namespace NorvesLib::Core