#pragma once

#include "Container/PointerTypes.h"
#include <memory>
#include <cstdint>
#include <type_traits>
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    class IClass;
    class IUnknown;

    /**
     * @brief 値（変数）を抽象化するインターフェース
     * リフレクションシステムの一部として、あらゆる型の値と型情報を保持します
     */
    class IValue
    {
    public:
        /**
         * @brief 基本データ型の列挙
         */
        enum class ValueType
        {
            None,     // 未定義
            Bool,     // bool
            Int8,     // int8_t
            Int16,    // int16_t
            Int32,    // int32_t
            Int64,    // int64_t
            UInt8,    // uint8_t
            UInt16,   // uint16_t
            UInt32,   // uint32_t
            UInt64,   // uint64_t
            Float,    // float
            Double,   // double
            String,   // Container::String
            WString,  // std::wstring
            Enum,     // 列挙型
            Array,    // 配列型
            Object,   // オブジェクト型 (IUnknown系)
            Class,    // クラス型情報 (IClass系)
            Function, // 関数型
            Pointer,  // ポインタ型
            Custom    // カスタム型
        };

        virtual ~IValue() = default;

        /**
         * @brief 値の基本型種別を取得します
         * @return 値の基本型種別
         */
        virtual ValueType GetValueType() const = 0;

        /**
         * @brief 値の型情報を取得します
         * @return 型情報へのポインタ
         * @note 内蔵型の場合はnullptrを返す場合があります
         */
        virtual const IClass *GetType() const = 0;

        /**
         * @brief 値の名前を取得します
         * @return 変数名
         */
        virtual const Container::String &GetName() const = 0;

        /**
         * @brief 値へのポインタを取得します
         * @return データへの生ポインタ
         */
        virtual void *GetValuePtr() = 0;

        /**
         * @brief 値への読み取り専用ポインタを取得します
         * @return データへの読み取り専用ポインタ
         */
        virtual const void *GetValuePtr() const = 0;

        /**
         * @brief 値のサイズを取得します
         * @return バイト単位のサイズ
         */
        virtual size_t GetSize() const = 0;

        /**
         * @brief 値が有効かどうかを確認します
         * @return 有効な場合はtrue
         */
        virtual bool IsValid() const = 0;

        /**
         * @brief 値をコピーします
         * @param src コピー元データへのポインタ
         * @return コピーが成功した場合はtrue
         */
        virtual bool CopyFrom(const void *src) = 0;

        /**
         * @brief 別のIValueから値をコピーします
         * @param other コピー元のIValue
         * @return コピーが成功した場合はtrue
         */
        virtual bool CopyFrom(const IValue &other) = 0;

        /**
         * @brief 値を文字列表現に変換します
         * @return 値の文字列表現
         */
        virtual Container::String ToString() const = 0;

        /**
         * @brief 文字列から値を設定します
         * @param str 設定する文字列
         * @return 変換が成功した場合はtrue
         */
        virtual bool FromString(const Container::String &str) = 0;

        /**
         * @brief この値が与えられた型に変換可能かどうかを確認します
         * @param targetType 変換先の型情報
         * @return 変換可能な場合はtrue
         */
        virtual bool CanConvertTo(const IClass *targetType) const = 0;

        /**
         * @brief この値が指定された値型に変換可能かどうかを確認します
         * @param targetValueType 変換先の値型
         * @return 変換可能な場合はtrue
         */
        virtual bool CanConvertTo(ValueType targetValueType) const = 0;

        /**
         * @brief 新しいインスタンスを複製します
         * @return 複製されたIValue
         */
        virtual Container::TUniquePtr<IValue> Clone() const = 0;

        // 以下、型安全なアクセス用のメソッド

        /**
         * @brief bool値を取得します
         * @return bool値、変換できない場合はfalseを返します
         */
        virtual bool AsBool() const = 0;

        /**
         * @brief int32_t値を取得します
         * @return int32_t値、変換できない場合は0を返します
         */
        virtual int32_t AsInt32() const = 0;

        /**
         * @brief int64_t値を取得します
         * @return int64_t値、変換できない場合は0を返します
         */
        virtual int64_t AsInt64() const = 0;

        /**
         * @brief uint32_t値を取得します
         * @return uint32_t値、変換できない場合は0を返します
         */
        virtual uint32_t AsUInt32() const = 0;

        /**
         * @brief uint64_t値を取得します
         * @return uint64_t値、変換できない場合は0を返します
         */
        virtual uint64_t AsUInt64() const = 0;

        /**
         * @brief float値を取得します
         * @return float値、変換できない場合は0.0fを返します
         */
        virtual float AsFloat() const = 0;

        /**
         * @brief double値を取得します
         * @return double値、変換できない場合は0.0を返します
         */
        virtual double AsDouble() const = 0;

        /**
         * @brief 文字列を取得します
         * @return 文字列、変換できない場合は空文字列を返します
         */
        virtual Container::String AsString() const = 0;

        /**
         * @brief IUnknownオブジェクトを取得します
         * @return IUnknownへのポインタ、変換できない場合はnullptrを返します
         */
        virtual IUnknown *AsObject() = 0;

        /**
         * @brief IUnknownオブジェクトを読み取り専用で取得します
         * @return IUnknownへの読み取り専用ポインタ、変換できない場合はnullptrを返します
         */
        virtual const IUnknown *AsObject() const = 0;

        /**
         * @brief IClass型情報を取得します
         * @return IClassへのポインタ、変換できない場合はnullptrを返します
         */
        virtual const IClass *AsClass() const = 0;
    };

} // namespace NorvesLib::Core
