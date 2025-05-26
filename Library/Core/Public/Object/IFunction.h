#pragma once

#include "Container/PointerTypes.h"
#include "IUnknown.h"
#include "IValue.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    class IClass;

    /**
     * @brief 関数を抽象化するインターフェース
     * リフレクションシステムの一部として、関数のシグネチャと呼び出し機能を提供します
     */
    class IFunction
    {
    public:
        virtual ~IFunction() = default;

        /**
         * @brief 関数名を取得します
         * @return 関数名
         */
        virtual const Container::String& GetName() const = 0;

        /**
         * @brief 返り値の型情報を取得します
         * @return 返り値の型情報、void型の場合はnullptr
         */
        virtual const IClass* GetReturnType() const = 0;

        /**
         * @brief パラメータの数を取得します
         * @return パラメータの数
         */
        virtual size_t GetParameterCount() const = 0;

        /**
         * @brief パラメータの型情報を取得します
         * @param index パラメータのインデックス
         * @return 型情報へのポインタ、インデックスが無効な場合はnullptr
         */
        virtual const IClass* GetParameterType(size_t index) const = 0;

        /**
         * @brief すべてのパラメータの型情報を取得します
         * @return パラメータの型情報配列
         */
        virtual Container::VariableArray<const IClass*> GetParameterTypes() const = 0;

        /**
         * @brief パラメータ名を取得します
         * @param index パラメータのインデックス
         * @return パラメータ名、インデックスが無効な場合は空文字列
         */
        virtual Container::String GetParameterName(size_t index) const = 0;

        /**
         * @brief 関数を呼び出します
         * @param instance 関数を呼び出すオブジェクトインスタンス
         * @param params パラメータ配列
         * @return 関数の戻り値、void型の場合はnullptr
         */
        virtual Container::TUniquePtr<IValue> Invoke(IUnknown* instance, const Container::VariableArray<IValue*>& params) const = 0;

        /**
         * @brief 関数が指定されたパラメータで呼び出し可能か確認します
         * @param params チェックするパラメータ配列
         * @return 呼び出し可能な場合はtrue
         */
        virtual bool CanInvoke(const Container::VariableArray<IValue*>& params) const = 0;

        /**
         * @brief 関数シグネチャを文字列形式で取得します
         * @return 関数シグネチャの文字列表現
         */
        virtual Container::String GetSignature() const = 0;

        /**
         * @brief 関数が静的かどうかを確認します
         * @return 静的関数の場合はtrue
         */
        virtual bool IsStatic() const = 0;

        /**
         * @brief 関数が仮想関数かどうかを確認します
         * @return 仮想関数の場合はtrue
         */
        virtual bool IsVirtual() const = 0;

        /**
         * @brief 関数がプロパティアクセサかどうかを確認します
         * @return プロパティアクセサの場合はtrue
         */
        virtual bool IsPropertyAccessor() const = 0;
    };

} // namespace NorvesLib::Core