#pragma once

#include <functional>
#include <memory>
#include <stdexcept>

namespace NorvesLib::Core
{

    /**
     * @brief 単一の関数を登録して呼び出すためのデリゲートクラス
     * @tparam RetType 戻り値の型
     * @tparam Args 引数の型...
     */
    template <typename RetType, typename... Args>
    class Delegate
    {
    private:
        // 内部で関数を保持するための型
        using FunctionType = std::function<RetType(Args...)>;

        // 関数ポインタ
        FunctionType m_Function;

    public:
        /**
         * @brief デフォルトコンストラクタ - 空のデリゲートを作成
         */
        Delegate() = default;

        /**
         * @brief 関数ポインタからデリゲートを作成
         * @param function 登録する関数ポインタ
         */
        Delegate(RetType (*function)(Args...))
            : m_Function(function)
        {
        }

        /**
         * @brief std::functionからデリゲートを作成
         * @param function 登録するstd::function
         */
        Delegate(const FunctionType &function)
            : m_Function(function)
        {
        }

        /**
         * @brief 関数オブジェクトからデリゲートを作成
         * @tparam F 関数オブジェクトの型
         * @param functor 登録する関数オブジェクト
         */
        template <typename F>
        Delegate(F &&functor)
            : m_Function(std::forward<F>(functor))
        {
        }

        /**
         * @brief メソッドとオブジェクトからデリゲートを作成
         * @tparam T オブジェクトの型
         * @tparam Method メソッドの型
         * @param instance オブジェクトのポインタ
         * @param method メソッドのポインタ
         */
        template <typename T, typename Method>
        Delegate(T *instance, Method method)
        {
            m_Function = [instance, method](Args... args) -> RetType
            {
                return (instance->*method)(std::forward<Args>(args)...);
            };
        }

        /**
         * @brief デリゲートをクリア
         */
        void Clear()
        {
            m_Function = nullptr;
        }

        /**
         * @brief デリゲートが有効か（関数が登録されているか）を確認
         * @return デリゲートが有効な場合true
         */
        bool IsBound() const
        {
            return static_cast<bool>(m_Function);
        }

        /**
         * @brief 関数ポインタをデリゲートに設定
         * @param function 設定する関数ポインタ
         */
        void Bind(RetType (*function)(Args...))
        {
            m_Function = function;
        }

        /**
         * @brief std::functionをデリゲートに設定
         * @param function 設定するstd::function
         */
        void Bind(const FunctionType &function)
        {
            m_Function = function;
        }

        /**
         * @brief 関数オブジェクトをデリゲートに設定
         * @tparam F 関数オブジェクトの型
         * @param functor 設定する関数オブジェクト
         */
        template <typename F>
        void Bind(F &&functor)
        {
            m_Function = std::forward<F>(functor);
        }

        /**
         * @brief メソッドとオブジェクトをデリゲートに設定
         * @tparam T オブジェクトの型
         * @tparam Method メソッドの型
         * @param instance オブジェクトのポインタ
         * @param method メソッドのポインタ
         */
        template <typename T, typename Method>
        void Bind(T *instance, Method method)
        {
            m_Function = [instance, method](Args... args) -> RetType
            {
                return (instance->*method)(std::forward<Args>(args)...);
            };
        }

        /**
         * @brief デリゲートを実行
         * @param args 関数に渡す引数
         * @return 関数の戻り値
         */
        RetType Invoke(Args... args) const
        {
            if (!IsBound())
            {
                throw std::runtime_error("Delegate is not bound to a function");
            }
            return m_Function(std::forward<Args>(args)...);
        }

        /**
         * @brief デリゲートを関数呼び出し演算子で実行
         * @param args 関数に渡す引数
         * @return 関数の戻り値
         */
        RetType operator()(Args... args) const
        {
            return Invoke(std::forward<Args>(args)...);
        }

        /**
         * @brief デリゲートが有効であれば実行し、無効であれば何もしない
         * @param args 関数に渡す引数
         * @return デリゲートが有効である場合は関数の戻り値、無効ならデフォルト値
         */
        RetType InvokeIfBound(Args... args) const
        {
            if (IsBound())
            {
                return m_Function(std::forward<Args>(args)...);
            }
            // デフォルト値を返す（クラス型ならnullptr、数値型なら0、bool型ならfalseなど）
            return RetType{};
        }

        /**
         * @brief 比較演算子のオーバーロード
         */
        bool operator==(const Delegate &other) const
        {
            // std::functionの比較は難しいため、ポインタとtarget_typeの比較で代用
            return (m_Function.template target<RetType (*)(Args...)>() ==
                    other.m_Function.template target<RetType (*)(Args...)>()) &&
                   (m_Function.target_type() == other.m_Function.target_type());
        }

        bool operator!=(const Delegate &other) const
        {
            return !(*this == other);
        }
    };

    // 戻り値がvoidの場合の特殊化
    template <typename... Args>
    class Delegate<void, Args...>
    {
    private:
        using FunctionType = std::function<void(Args...)>;
        FunctionType m_Function;

    public:
        Delegate() = default;

        Delegate(void (*function)(Args...))
            : m_Function(function)
        {
        }

        Delegate(const FunctionType &function)
            : m_Function(function)
        {
        }

        template <typename F>
        Delegate(F &&functor)
            : m_Function(std::forward<F>(functor))
        {
        }

        template <typename T, typename Method>
        Delegate(T *instance, Method method)
        {
            m_Function = [instance, method](Args... args)
            {
                (instance->*method)(std::forward<Args>(args)...);
            };
        }

        void Clear()
        {
            m_Function = nullptr;
        }

        bool IsBound() const
        {
            return static_cast<bool>(m_Function);
        }

        void Bind(void (*function)(Args...))
        {
            m_Function = function;
        }

        void Bind(const FunctionType &function)
        {
            m_Function = function;
        }

        template <typename F>
        void Bind(F &&functor)
        {
            m_Function = std::forward<F>(functor);
        }

        template <typename T, typename Method>
        void Bind(T *instance, Method method)
        {
            m_Function = [instance, method](Args... args)
            {
                (instance->*method)(std::forward<Args>(args)...);
            };
        }

        void Invoke(Args... args) const
        {
            if (!IsBound())
            {
                throw std::runtime_error("Delegate is not bound to a function");
            }
            m_Function(std::forward<Args>(args)...);
        }

        void operator()(Args... args) const
        {
            Invoke(std::forward<Args>(args)...);
        }

        void InvokeIfBound(Args... args) const
        {
            if (IsBound())
            {
                m_Function(std::forward<Args>(args)...);
            }
        }

        bool operator==(const Delegate &other) const
        {
            return (m_Function.template target<void (*)(Args...)>() ==
                    other.m_Function.template target<void (*)(Args...)>()) &&
                   (m_Function.target_type() == other.m_Function.target_type());
        }

        bool operator!=(const Delegate &other) const
        {
            return !(*this == other);
        }
    };

    // デリゲート作成のヘルパー関数
    template <typename RetType, typename... Args>
    Delegate<RetType, Args...> MakeDelegate(RetType (*function)(Args...))
    {
        return Delegate<RetType, Args...>(function);
    }

    template <typename T, typename RetType, typename... Args>
    Delegate<RetType, Args...> MakeDelegate(T *instance, RetType (T::*method)(Args...))
    {
        return Delegate<RetType, Args...>(instance, method);
    }

    template <typename T, typename RetType, typename... Args>
    Delegate<RetType, Args...> MakeDelegate(T *instance, RetType (T::*method)(Args...) const)
    {
        return Delegate<RetType, Args...>(instance, method);
    }

} // namespace NorvesLib::Core
