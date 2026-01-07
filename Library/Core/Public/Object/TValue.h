#pragma once

#include "Core/Public/Container/PointerTypes.h"
#include <sstream>
#include <type_traits>
#include <limits>
#include "IValue.h"
#include "IClass.h"
#include "Container/Containers.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core
{
    // 前方宣言
    class IUnknown;

    /**
     * @brief 型からValueTypeを決定するトレイト
     */
    template <typename T>
    struct TypeToValueType
    {
        // デフォルトはカスタム型
        static constexpr IValue::ValueType value = IValue::ValueType::Custom;
    };

    // 特殊化による基本型マッピング
    template <>
    struct TypeToValueType<bool>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Bool;
    };
    template <>
    struct TypeToValueType<int8_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Int8;
    };
    template <>
    struct TypeToValueType<int16_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Int16;
    };
    template <>
    struct TypeToValueType<int32_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Int32;
    };
    template <>
    struct TypeToValueType<int64_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Int64;
    };
    template <>
    struct TypeToValueType<uint8_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::UInt8;
    };
    template <>
    struct TypeToValueType<uint16_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::UInt16;
    };
    template <>
    struct TypeToValueType<uint32_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::UInt32;
    };
    template <>
    struct TypeToValueType<uint64_t>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::UInt64;
    };
    template <>
    struct TypeToValueType<float>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Float;
    };
    template <>
    struct TypeToValueType<double>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Double;
    };
    template <>
    struct TypeToValueType<Container::String>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::String;
    };
    template <>
    struct TypeToValueType<std::wstring>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::WString;
    };

    // ポインタ型の特殊化
    template <typename T>
    struct TypeToValueType<T *>
    {
        // IUnknown派生型のポインタはオブジェクト型として扱う
        static constexpr IValue::ValueType value =
            std::is_base_of_v<IUnknown, T> ? IValue::ValueType::Object : IValue::ValueType::Pointer;
    };

    // IClass型の特殊化
    template <>
    struct TypeToValueType<IClass *>
    {
        static constexpr IValue::ValueType value = IValue::ValueType::Class;
    };

    /**
     * @brief IValueを実装する型付きの値クラス
     * @tparam T 保持する値の型
     */
    template <typename T>
    class TValue : public IValue
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         * @param name 値の名前
         */
        explicit TValue(const Container::String &name = "")
            : m_Name(name), m_Value(), m_IsValid(true)
        {
        }

        /**
         * @brief 値を指定して構築します
         * @param value 初期値
         * @param name 値の名前
         */
        TValue(const T &value, const Container::String &name = "")
            : m_Name(name), m_Value(value), m_IsValid(true)
        {
        }

        /**
         * @brief デストラクタ
         */
        virtual ~TValue() = default;

        /**
         * @brief 値の基本型種別を取得します
         * @return 値の基本型種別
         */
        virtual ValueType GetValueType() const override
        {
            return TypeToValueType<T>::value;
        }

        /**
         * @brief 値の型情報を取得します
         * @return 型情報へのポインタ
         */
        virtual const IClass *GetType() const override
        {
            // TClass<T>::GetClass()のような関連クラスで型情報を取得する
            // 実際の実装はリフレクションシステムに依存
            return nullptr; // 実際の実装はリフレクションシステムに依存します
        }

        /**
         * @brief 値の名前を取得します
         * @return 変数名
         */
        virtual const Container::String &GetName() const override
        {
            return m_Name;
        }

        /**
         * @brief 値へのポインタを取得します
         * @return データへの生ポインタ
         */
        virtual void *GetValuePtr() override
        {
            return &m_Value;
        }

        /**
         * @brief 値への読み取り専用ポインタを取得します
         * @return データへの読み取り専用ポインタ
         */
        virtual const void *GetValuePtr() const override
        {
            return &m_Value;
        }

        /**
         * @brief 値のサイズを取得します
         * @return バイト単位のサイズ
         */
        virtual size_t GetSize() const override
        {
            return sizeof(T);
        }

        /**
         * @brief 値が有効かどうかを確認します
         * @return 有効な場合はtrue
         */
        virtual bool IsValid() const override
        {
            return m_IsValid;
        }

        /**
         * @brief 値をコピーします
         * @param src コピー元データへのポインタ
         * @return コピーが成功した場合はtrue
         */
        virtual bool CopyFrom(const void *src) override
        {
            if (!src)
            {
                m_IsValid = false;
                return false;
            }

            m_Value = *static_cast<const T *>(src);
            m_IsValid = true;
            return true;
        }

        /**
         * @brief 別のIValueから値をコピーします
         * @param other コピー元のIValue
         * @return コピーが成功した場合はtrue
         */
        virtual bool CopyFrom(const IValue &other) override
        {
            // 同じ型の場合は直接コピー
            if (other.GetValueType() == GetValueType())
            {
                return CopyFrom(other.GetValuePtr());
            }

            // 型が異なる場合は変換を試みる
            if (!other.CanConvertTo(GetValueType()))
            {
                m_IsValid = false;
                return false;
            }

            // 基本型変換を試みる
            switch (GetValueType())
            {
            case ValueType::Bool:
                *static_cast<bool *>(GetValuePtr()) = other.AsBool();
                break;
            case ValueType::Int8:
            case ValueType::Int16:
            case ValueType::Int32:
                *static_cast<int32_t *>(GetValuePtr()) = other.AsInt32();
                break;
            case ValueType::Int64:
                *static_cast<int64_t *>(GetValuePtr()) = other.AsInt64();
                break;
            case ValueType::UInt8:
            case ValueType::UInt16:
            case ValueType::UInt32:
                *static_cast<uint32_t *>(GetValuePtr()) = other.AsUInt32();
                break;
            case ValueType::UInt64:
                *static_cast<uint64_t *>(GetValuePtr()) = other.AsUInt64();
                break;
            case ValueType::Float:
                *static_cast<float *>(GetValuePtr()) = other.AsFloat();
                break;
            case ValueType::Double:
                *static_cast<double *>(GetValuePtr()) = other.AsDouble();
                break;
            case ValueType::String:
                *static_cast<Container::String *>(GetValuePtr()) = other.AsString();
                break;
            case ValueType::Object:
                // オブジェクトポインタの場合
                if (auto obj = other.AsObject())
                {
                    if constexpr (std::is_pointer_v<T>)
                    {
                        using TargetType = std::remove_pointer_t<T>;
                        // 特別なケース: void*への変換
                        if constexpr (std::is_same_v<void, std::remove_const_t<TargetType>>)
                        {
                            // const_castを明示的に使用
                            *static_cast<T *>(GetValuePtr()) = const_cast<void *>(static_cast<const void *>(obj));
                            m_IsValid = true;
                            return true;
                        }
                        // IUnknown*型またはその派生型へのキャスト
                        else if constexpr (std::is_base_of_v<IUnknown, std::remove_const_t<TargetType>>)
                        {
                            if constexpr (std::is_const_v<TargetType>)
                            {
                                // const IUnknown*などへの変換は安全
                                *static_cast<T *>(GetValuePtr()) = static_cast<T>(obj);
                            }
                            else
                            {
                                // 非constへの変換は明示的にconst_castを使用
                                *static_cast<T *>(GetValuePtr()) = const_cast<T>(obj);
                            }
                            m_IsValid = true;
                            return true;
                        }
                    }
                    m_IsValid = false;
                    return false;
                }
                else
                {
                    m_IsValid = false;
                    return false;
                }
                break;
            case ValueType::Class:
                // IClass型の場合
                if (auto cls = other.AsClass())
                {
                    *static_cast<const IClass **>(GetValuePtr()) = cls;
                }
                else
                {
                    m_IsValid = false;
                    return false;
                }
                break;
            default:
                // 文字列経由で変換を試みる
                return FromString(other.ToString());
            }

            m_IsValid = true;
            return true;
        }

        /**
         * @brief 値を文字列表現に変換します
         * @return 値の文字列表現
         */
        virtual Container::String ToString() const override
        {
            if (!m_IsValid)
            {
                return "Invalid";
            }

            if constexpr (std::is_same_v<T, Container::String>)
            {
                return m_Value;
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return m_Value ? "true" : "false";
            }
            else if constexpr (std::is_pointer_v<T>)
            {
                if (m_Value == nullptr)
                {
                    return "null";
                }
                std::stringstream ss;
                ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(m_Value);
                return ss.str();
            }
            else
            {
                std::stringstream ss;
                ss << m_Value;
                return ss.str();
            }
        }

        /**
         * @brief 文字列から値を設定します
         * @param str 設定する文字列
         * @return 変換が成功した場合はtrue
         */
        virtual bool FromString(const Container::String &str) override
        {
            try
            {
                if constexpr (std::is_same_v<T, Container::String>)
                {
                    m_Value = str;
                    m_IsValid = true;
                    return true;
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    // bool型の場合、"true"/"false"や"1"/"0"を判定
                    if (str == "true" || str == "1" || str == "yes" || str == "on")
                    {
                        m_Value = true;
                        m_IsValid = true;
                        return true;
                    }
                    else if (str == "false" || str == "0" || str == "no" || str == "off")
                    {
                        m_Value = false;
                        m_IsValid = true;
                        return true;
                    }
                    m_IsValid = false;
                    return false;
                }
                else if constexpr (std::is_pointer_v<T>)
                {
                    // ポインタ型は特別な処理
                    if (str == "null" || str == "nullptr" || str == "0")
                    {
                        m_Value = nullptr;
                        m_IsValid = true;
                        return true;
                    }

                    // 16進数表記のポインタ値
                    if (str.length() > 2 && str.substr(0, 2) == "0x")
                    {
                        uintptr_t ptrValue;
                        std::stringstream ss;
                        ss << std::hex << str.substr(2);
                        ss >> ptrValue;
                        if (!ss.fail())
                        {
                            m_Value = reinterpret_cast<T>(ptrValue);
                            m_IsValid = true;
                            return true;
                        }
                    }

                    m_IsValid = false;
                    return false;
                }
                else
                {
                    // その他の型は標準的な変換を試す
                    std::stringstream ss(std::string(str.data(), str.size()));
                    ss >> m_Value;
                    m_IsValid = !ss.fail();
                    return m_IsValid;
                }
            }
            catch (...)
            {
                m_IsValid = false;
                return false;
            }
        }

        /**
         * @brief この値が与えられた型に変換可能かどうかを確認します
         * @param targetType 変換先の型情報
         * @return 変換可能な場合はtrue
         */
        virtual bool CanConvertTo(const IClass *targetType) const override
        {
            // 同じ型なら変換可能
            if (GetType() == targetType)
            {
                return true;
            }

            // 実装はリフレクションシステムに依存します
            // 基本型の場合は別途定義したCanConvertToValueTypeで判定
            return false;
        }

        /**
         * @brief この値が指定された値型に変換可能かどうかを確認します
         * @param targetValueType 変換先の値型
         * @return 変換可能な場合はtrue
         */
        virtual bool CanConvertTo(ValueType targetValueType) const override
        {
            // 同じ型なら変換可能
            if (GetValueType() == targetValueType)
            {
                return true;
            }

            // 基本型の場合の変換可否
            ValueType sourceType = GetValueType();

            // 数値型間の変換
            bool isSourceNumber =
                (sourceType == ValueType::Int8 || sourceType == ValueType::Int16 ||
                 sourceType == ValueType::Int32 || sourceType == ValueType::Int64 ||
                 sourceType == ValueType::UInt8 || sourceType == ValueType::UInt16 ||
                 sourceType == ValueType::UInt32 || sourceType == ValueType::UInt64 ||
                 sourceType == ValueType::Float || sourceType == ValueType::Double);

            bool isTargetNumber =
                (targetValueType == ValueType::Int8 || targetValueType == ValueType::Int16 ||
                 targetValueType == ValueType::Int32 || targetValueType == ValueType::Int64 ||
                 targetValueType == ValueType::UInt8 || targetValueType == ValueType::UInt16 ||
                 targetValueType == ValueType::UInt32 || targetValueType == ValueType::UInt64 ||
                 targetValueType == ValueType::Float || targetValueType == ValueType::Double);

            // 数値同士は相互変換可能
            if (isSourceNumber && isTargetNumber)
            {
                return true;
            }

            // 文字列はどんな型からでも変換可能
            if (targetValueType == ValueType::String)
            {
                return true;
            }

            // 文字列からは基本型へ変換可能
            if (sourceType == ValueType::String &&
                (isTargetNumber || targetValueType == ValueType::Bool))
            {
                return true;
            }

            // boolは数値と相互変換可能
            if ((sourceType == ValueType::Bool && isTargetNumber) ||
                (isSourceNumber && targetValueType == ValueType::Bool))
            {
                return true;
            }

            return false;
        }

        /**
         * @brief 新しいインスタンスを複製します
         * @return 複製されたIValue
         */
        virtual TUniquePtr<IValue> Clone() const override
        {
            return MakeUnique<TValue<T>>(m_Value, m_Name);
        }

        // 型変換メソッド実装

        /**
         * @brief bool値を取得します
         * @return bool値、変換できない場合はfalseを返します
         */
        virtual bool AsBool() const override
        {
            if (!m_IsValid)
            {
                return false;
            }

            if constexpr (std::is_same_v<T, bool>)
            {
                return m_Value;
            }
            else if constexpr (std::is_integral_v<T>)
            {
                return m_Value != 0;
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                return m_Value != 0.0;
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                return m_Value == "true" || m_Value == "1" || m_Value == "yes" || m_Value == "on";
            }
            else if constexpr (std::is_pointer_v<T>)
            {
                return m_Value != nullptr;
            }
            else
            {
                return false;
            }
        }

        /**
         * @brief int32_t値を取得します
         * @return int32_t値、変換できない場合は0を返します
         */
        virtual int32_t AsInt32() const override
        {
            if (!m_IsValid)
            {
                return 0;
            }

            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                // 数値型からの変換
                if constexpr (sizeof(T) <= sizeof(int32_t))
                {
                    return static_cast<int32_t>(m_Value);
                }
                else
                {
                    // 大きな整数型からの変換（オーバーフロー可能性あり）
                    return static_cast<int32_t>(m_Value);
                }
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                // 浮動小数点からの変換
                return static_cast<int32_t>(m_Value);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                // bool型からの変換
                return m_Value ? 1 : 0;
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                // 文字列からの変換
                try
                {
                    return std::stoi(std::string(m_Value.data(), m_Value.size()));
                }
                catch (...)
                {
                    return 0;
                }
            }
            else
            {
                return 0;
            }
        }

        /**
         * @brief int64_t値を取得します
         * @return int64_t値、変換できない場合は0を返します
         */
        virtual int64_t AsInt64() const override
        {
            if (!m_IsValid)
            {
                return 0;
            }

            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                // 数値型からの変換
                return static_cast<int64_t>(m_Value);
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                // 浮動小数点からの変換
                return static_cast<int64_t>(m_Value);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                // bool型からの変換
                return m_Value ? 1 : 0;
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                // 文字列からの変換
                try
                {
                    return std::stoll(std::string(m_Value.data(), m_Value.size()));
                }
                catch (...)
                {
                    return 0;
                }
            }
            else
            {
                return 0;
            }
        }

        /**
         * @brief uint32_t値を取得します
         * @return uint32_t値、変換できない場合は0を返します
         */
        virtual uint32_t AsUInt32() const override
        {
            if (!m_IsValid)
            {
                return 0;
            }

            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                // 数値型からの変換
                if constexpr (sizeof(T) <= sizeof(uint32_t))
                {
                    return static_cast<uint32_t>(m_Value);
                }
                else
                {
                    // 大きな整数型からの変換（オーバーフロー可能性あり）
                    return static_cast<uint32_t>(m_Value);
                }
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                // 浮動小数点からの変換
                return static_cast<uint32_t>(m_Value);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                // bool型からの変換
                return m_Value ? 1 : 0;
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                // 文字列からの変換
                try
                {
                    return static_cast<uint32_t>(std::stoul(std::string(m_Value.data(), m_Value.size())));
                }
                catch (...)
                {
                    return 0;
                }
            }
            else
            {
                return 0;
            }
        }

        /**
         * @brief uint64_t値を取得します
         * @return uint64_t値、変換できない場合は0を返します
         */
        virtual uint64_t AsUInt64() const override
        {
            if (!m_IsValid)
            {
                return 0;
            }

            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                // 数値型からの変換
                return static_cast<uint64_t>(m_Value);
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                // 浮動小数点からの変換
                return static_cast<uint64_t>(m_Value);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                // bool型からの変換
                return m_Value ? 1 : 0;
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                // 文字列からの変換
                try
                {
                    return std::stoull(std::string(m_Value.data(), m_Value.size()));
                }
                catch (...)
                {
                    return 0;
                }
            }
            else
            {
                return 0;
            }
        }

        /**
         * @brief float値を取得します
         * @return float値、変換できない場合は0.0fを返します
         */
        virtual float AsFloat() const override
        {
            if (!m_IsValid)
            {
                return 0.0f;
            }

            if constexpr (std::is_floating_point_v<T>)
            {
                // 浮動小数点からの変換
                return static_cast<float>(m_Value);
            }
            else if constexpr (std::is_integral_v<T>)
            {
                // 整数からの変換
                return static_cast<float>(m_Value);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                // bool型からの変換
                return m_Value ? 1.0f : 0.0f;
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                // 文字列からの変換
                try
                {
                    return std::stof(std::string(m_Value.data(), m_Value.size()));
                }
                catch (...)
                {
                    return 0.0f;
                }
            }
            else
            {
                return 0.0f;
            }
        }

        /**
         * @brief double値を取得します
         * @return double値、変換できない場合は0.0を返します
         */
        virtual double AsDouble() const override
        {
            if (!m_IsValid)
            {
                return 0.0;
            }

            if constexpr (std::is_floating_point_v<T>)
            {
                // 浮動小数点からの変換
                return static_cast<double>(m_Value);
            }
            else if constexpr (std::is_integral_v<T>)
            {
                // 整数からの変換
                return static_cast<double>(m_Value);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                // bool型からの変換
                return m_Value ? 1.0 : 0.0;
            }
            else if constexpr (std::is_same_v<T, Container::String>)
            {
                // 文字列からの変換
                try
                {
                    return std::stod(std::string(m_Value.data(), m_Value.size()));
                }
                catch (...)
                {
                    return 0.0;
                }
            }
            else
            {
                return 0.0;
            }
        }

        /**
         * @brief 文字列を取得します
         * @return 文字列、変換できない場合は空文字列を返します
         */
        virtual Container::String AsString() const override
        {
            // ToString()を利用して変換
            return ToString();
        }

        /**
         * @brief IUnknownオブジェクトを取得します
         * @return IUnknownへのポインタ、変換できない場合はnullptrを返します
         */
        virtual IUnknown *AsObject() override
        {
            if (!m_IsValid)
            {
                return nullptr;
            }

            if constexpr (std::is_pointer_v<T>)
            {
                // ポインタ型の場合、IUnknownへキャスト可能か確認
                using PointedType = std::remove_pointer_t<T>;
                using NonConstPointedType = std::remove_const_t<PointedType>;

                if constexpr (std::is_base_of_v<IUnknown, NonConstPointedType>)
                {
                    // IUnknownまたはその派生クラスへの変換可能
                    if (m_Value != nullptr)
                    {
                        return const_cast<IUnknown *>(static_cast<const IUnknown *>(m_Value));
                    }
                }
                // IClass*は直接IUnknown*に変換できないため、特別に処理
                else if constexpr (std::is_same_v<NonConstPointedType, IClass>)
                {
                    // IClassからIUnknownへの変換はサポートしない
                    return nullptr;
                }
            }
            return nullptr;
        }

        /**
         * @brief IUnknownオブジェクトを読み取り専用で取得します
         * @return IUnknownへの読み取り専用ポインタ、変換できない場合はnullptrを返します
         */
        virtual const IUnknown *AsObject() const override
        {
            if (!m_IsValid)
            {
                return nullptr;
            }

            if constexpr (std::is_pointer_v<T>)
            {
                // ポインタ型の場合、IUnknownへキャスト可能か確認
                using PointedType = std::remove_pointer_t<T>;
                using NonConstPointedType = std::remove_const_t<PointedType>;

                if constexpr (std::is_base_of_v<IUnknown, NonConstPointedType>)
                {
                    // IUnknownまたはその派生クラスへの変換可能
                    if (m_Value != nullptr)
                    {
                        return static_cast<const IUnknown *>(m_Value);
                    }
                }
                // IClass*は直接IUnknown*に変換できないため、特別に処理
                else if constexpr (std::is_same_v<NonConstPointedType, IClass>)
                {
                    // IClassからIUnknownへの変換はサポートしない
                    return nullptr;
                }
            }
            return nullptr;
        }

        /**
         * @brief IClass型情報を取得します
         * @return IClassへのポインタ、変換できない場合はnullptrを返します
         */
        virtual const IClass *AsClass() const override
        {
            if (!m_IsValid)
            {
                return nullptr;
            }

            if constexpr (std::is_same_v<T, const IClass *>)
            {
                return m_Value;
            }
            return nullptr;
        }

        /**
         * @brief 値を取得します
         * @return 保持している値
         */
        const T &Get() const
        {
            return m_Value;
        }

        /**
         * @brief 値を設定します
         * @param value 設定する値
         */
        void Set(const T &value)
        {
            m_Value = value;
            m_IsValid = true;
        }

        /**
         * @brief 値への参照を取得します
         * @return 値への参照
         */
        T &GetRef()
        {
            return m_Value;
        }

        /**
         * @brief 代入演算子
         * @param value 設定する値
         * @return this
         */
        TValue &operator=(const T &value)
        {
            Set(value);
            return *this;
        }

        /**
         * @brief 値への変換演算子
         * @return 保持している値
         */
        operator const T &() const
        {
            return m_Value;
        }

    private:
        Container::String m_Name; // 値の名前
        T m_Value;                // 実際の値
        bool m_IsValid;           // 値が有効かどうか
    };

    /**
     * @brief 指定された型のTValueを作成するヘルパー関数
     * @tparam T 値の型
     * @param value 初期値
     * @param name 値の名前（省略可能）
     * @return 作成された値オブジェクト
     */
    template <typename T>
    TUniquePtr<IValue> MakeValue(const T &value, const Container::String &name = "")
    {
        return MakeUnique<TValue<T>>(value, name);
    }

    /**
     * @brief 基本型の各種インスタンス作成用ヘルパー関数
     */
    inline TUniquePtr<IValue> MakeBoolValue(bool value, const Container::String &name = "")
    {
        return MakeValue<bool>(value, name);
    }

    inline TUniquePtr<IValue> MakeInt32Value(int32_t value, const Container::String &name = "")
    {
        return MakeValue<int32_t>(value, name);
    }

    inline TUniquePtr<IValue> MakeInt64Value(int64_t value, const Container::String &name = "")
    {
        return MakeValue<int64_t>(value, name);
    }

    inline TUniquePtr<IValue> MakeUInt32Value(uint32_t value, const Container::String &name = "")
    {
        return MakeValue<uint32_t>(value, name);
    }

    inline TUniquePtr<IValue> MakeUInt64Value(uint64_t value, const Container::String &name = "")
    {
        return MakeValue<uint64_t>(value, name);
    }

    inline TUniquePtr<IValue> MakeFloatValue(float value, const Container::String &name = "")
    {
        return MakeValue<float>(value, name);
    }

    inline TUniquePtr<IValue> MakeDoubleValue(double value, const Container::String &name = "")
    {
        return MakeValue<double>(value, name);
    }

    inline TUniquePtr<IValue> MakeStringValue(const Container::String &value, const Container::String &name = "")
    {
        return MakeValue<Container::String>(value, name);
    }

    inline TUniquePtr<IValue> MakeObjectValue(IUnknown *value, const Container::String &name = "")
    {
        return MakeValue<IUnknown *>(value, name);
    }

    inline TUniquePtr<IValue> MakeClassValue(const IClass *value, const Container::String &name = "")
    {
        return MakeValue<const IClass *>(value, name);
    }

} // namespace NorvesLib::Core