#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include "IFunction.h"
#include "IClass.h"
#include "TValue.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    /**
     * @brief 関数特性を抽出するためのヘルパークラス
     */
    namespace FunctionTraits
    {
        // プライマリテンプレート（具体的な実装は特殊化で行う）
        template<typename T>
        struct FunctionTraits;

        // 通常の関数ポインタ
        template<typename R, typename... Args>
        struct FunctionTraits<R(*)(Args...)>
        {
            using ReturnType = R;
            using ClassType = void; // 非メンバ関数
            static constexpr bool IsStatic = true;
            static constexpr bool IsVirtual = false;
            static constexpr size_t ArgCount = sizeof...(Args);

            template<size_t i>
            struct Arg
            {
                using Type = typename std::tuple_element<i, std::tuple<Args...>>::type;
            };
        };

        // メンバ関数ポインタ
        template<typename R, typename C, typename... Args>
        struct FunctionTraits<R(C::*)(Args...)>
        {
            using ReturnType = R;
            using ClassType = C;
            static constexpr bool IsStatic = false;
            static constexpr bool IsVirtual = false; // 実際には検出できない
            static constexpr size_t ArgCount = sizeof...(Args);

            template<size_t i>
            struct Arg
            {
                using Type = typename std::tuple_element<i, std::tuple<Args...>>::type;
            };
        };

        // const メンバ関数ポインタ
        template<typename R, typename C, typename... Args>
        struct FunctionTraits<R(C::*)(Args...) const>
        {
            using ReturnType = R;
            using ClassType = C;
            static constexpr bool IsStatic = false;
            static constexpr bool IsVirtual = false; // 実際には検出できない
            static constexpr size_t ArgCount = sizeof...(Args);

            template<size_t i>
            struct Arg
            {
                using Type = typename std::tuple_element<i, std::tuple<Args...>>::type;
            };
        };

        // std::function
        template<typename R, typename... Args>
        struct FunctionTraits<std::function<R(Args...)>>
        {
            using ReturnType = R;
            using ClassType = void; // 非メンバ関数
            static constexpr bool IsStatic = true;
            static constexpr bool IsVirtual = false;
            static constexpr size_t ArgCount = sizeof...(Args);

            template<size_t i>
            struct Arg
            {
                using Type = typename std::tuple_element<i, std::tuple<Args...>>::type;
            };
        };
    }

    /**
     * @brief IFunctionを実装する型付き関数クラス
     * @tparam FuncType 関数の型
     */
    template<typename FuncType>
    class TFunction : public IFunction
    {
    private:
        using Traits = FunctionTraits::FunctionTraits<FuncType>;
        using RetType = typename Traits::ReturnType;

    public:
        /**
         * @brief コンストラクタ
         * @param func 関数ポインタまたは関数オブジェクト
         * @param name 関数名
         * @param isVirtual 仮想関数かどうか
         * @param isPropertyAccessor プロパティアクセサかどうか
         */
        TFunction(FuncType func, const Container::String& name, bool isVirtual = false, bool isPropertyAccessor = false)
            : m_Function(func)
            , m_Name(name)
            , m_IsVirtual(isVirtual || Traits::IsVirtual)
            , m_IsPropertyAccessor(isPropertyAccessor)
        {
            // パラメータ名のデフォルト設定
            for (size_t i = 0; i < Traits::ArgCount; ++i)
            {
                m_ParameterNames.push_back("arg" + std::to_string(i));
            }
        }

        /**
         * @brief デストラクタ
         */
        virtual ~TFunction() = default;

        /**
         * @brief パラメータ名を設定します
         * @param index パラメータのインデックス
         * @param name 設定するパラメータ名
         * @return 成功した場合はtrue
         */
        bool SetParameterName(size_t index, const Container::String& name)
        {
            if (index >= Traits::ArgCount)
            {
                return false;
            }
            m_ParameterNames[index] = name;
            return true;
        }

        /**
         * @brief 関数名を取得します
         * @return 関数名
         */
        virtual const Container::String& GetName() const override
        {
            return m_Name;
        }

        /**
         * @brief 返り値の型情報を取得します
         * @return 返り値の型情報、void型の場合はnullptr
         */
        virtual const IClass* GetReturnType() const override
        {
            // 実際の実装はリフレクションシステムに依存
            return nullptr; // TypeToClass<RetType>::GetClass() のような関数を使用
        }

        /**
         * @brief パラメータの数を取得します
         * @return パラメータの数
         */
        virtual size_t GetParameterCount() const override
        {
            return Traits::ArgCount;
        }

        /**
         * @brief パラメータの型情報を取得します
         * @param index パラメータのインデックス
         * @return 型情報へのポインタ、インデックスが無効な場合はnullptr
         */
        virtual const IClass* GetParameterType(size_t index) const override
        {
            if (index >= Traits::ArgCount)
            {
                return nullptr;
            }
            // 実際の実装はリフレクションシステムに依存
            return nullptr; // TypeToClass<typename Traits::template Arg<index>::Type>::GetClass() のような関数を使用
        }

        /**
         * @brief すべてのパラメータの型情報を取得します
         * @return パラメータの型情報配列
         */
        virtual Container::VariableArray<const IClass*> GetParameterTypes() const override
        {
            Container::VariableArray<const IClass*> types;
            for (size_t i = 0; i < Traits::ArgCount; ++i)
            {
                types.push_back(GetParameterType(i));
            }
            return types;
        }

        /**
         * @brief パラメータ名を取得します
         * @param index パラメータのインデックス
         * @return パラメータ名、インデックスが無効な場合は空文字列
         */
        virtual Container::String GetParameterName(size_t index) const override
        {
            if (index >= m_ParameterNames.size())
            {
                return "";
            }
            return m_ParameterNames[index];
        }

        /**
         * @brief 関数を呼び出します
         * @param instance 関数を呼び出すオブジェクトインスタンス
         * @param params パラメータ配列
         * @return 関数の戻り値、void型の場合はnullptr
         */
        virtual std::unique_ptr<IValue> Invoke(IUnknown* instance, const Container::VariableArray<IValue*>& params) const override
        {
            // 実際の実装はかなり複雑になるため、ここではスタブ実装のみ提供
            // 実際には型チェックと適切なキャスト、パラメータ展開が必要
            
            // void型の場合、nullptrを返す
            if constexpr (std::is_void_v<RetType>)
            {
                return nullptr;
            }
            else
            {
                // 戻り値のデフォルト値を返す（実際の実装ではない）
                return std::make_unique<TValue<RetType>>(RetType());
            }
        }

        /**
         * @brief 関数が指定されたパラメータで呼び出し可能か確認します
         * @param params チェックするパラメータ配列
         * @return 呼び出し可能な場合はtrue
         */
        virtual bool CanInvoke(const Container::VariableArray<IValue*>& params) const override
        {
            // パラメータ数のチェック
            if (params.size() != Traits::ArgCount)
            {
                return false;
            }

            // パラメータ型のチェック（実際の実装はリフレクションシステムに依存）
            for (size_t i = 0; i < params.size(); ++i)
            {
                const IClass* paramType = GetParameterType(i);
                if (!params[i]->CanConvertTo(paramType))
                {
                    return false;
                }
            }

            return true;
        }

        /**
         * @brief 関数シグネチャを文字列形式で取得します
         * @return 関数シグネチャの文字列表現
         */
        virtual Container::String GetSignature() const override
        {
            Container::String signature;
            
            // 返り値の型名を追加（実際の実装はリフレクションシステムに依存）
            const IClass* returnType = GetReturnType();
            signature += returnType ? returnType->GetClassName() : "void";
            
            signature += " " + m_Name + "(";
            
            // パラメータリストを追加
            for (size_t i = 0; i < GetParameterCount(); ++i)
            {
                if (i > 0)
                {
                    signature += ", ";
                }
                
                const IClass* paramType = GetParameterType(i);
                signature += paramType ? paramType->GetClassName() : "unknown";
                signature += " " + GetParameterName(i);
            }
            
            signature += ")";
            return signature;
        }

        /**
         * @brief 関数が静的かどうかを確認します
         * @return 静的関数の場合はtrue
         */
        virtual bool IsStatic() const override
        {
            return Traits::IsStatic;
        }

        /**
         * @brief 関数が仮想関数かどうかを確認します
         * @return 仮想関数の場合はtrue
         */
        virtual bool IsVirtual() const override
        {
            return m_IsVirtual;
        }

        /**
         * @brief 関数がプロパティアクセサかどうかを確認します
         * @return プロパティアクセサの場合はtrue
         */
        virtual bool IsPropertyAccessor() const override
        {
            return m_IsPropertyAccessor;
        }

        /**
         * @brief 関数オブジェクトを取得します
         * @return 関数オブジェクト
         */
        FuncType GetFunctionObject() const
        {
            return m_Function;
        }

    private:
        FuncType m_Function;                 // 関数オブジェクト
        Container::String m_Name;                  // 関数名
        Container::VariableArray<Container::String> m_ParameterNames; // パラメータ名のリスト
        bool m_IsVirtual;                    // 仮想関数かどうか
        bool m_IsPropertyAccessor;           // プロパティアクセサかどうか
    };

} // namespace NorvesLib::Core