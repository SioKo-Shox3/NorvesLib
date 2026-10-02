#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include "Thread/Atomic.h"

namespace NorvesLib::Core
{
    /**
     * @brief 単一の呼出先と登録識別を所有するデリゲート。
     * free functionは関数値、memberは同じ型のinstance/methodで比較する。
     * functorは登録ごとに識別を作り、Delegateのcopyだけが同じ識別を引き継ぐ。
     * memberのinstanceは非所有。呼出しが終わり解除するまで利用者が寿命を保つ。
     * 空同士は等しい。null instance/methodは空、move元も空になる。
     * Bind/copy代入の失敗時は以前の呼出先と識別を維持する。
     */
    template <typename RetType, typename... Args>
    class Delegate
    {
        using FunctionType = std::function<RetType(Args...)>;
        using FunctionPointer = RetType (*)(Args...);
        using NoexceptFunctionPointer = RetType (*)(Args...) noexcept;
        using MemberComparer = bool (*)(const FunctionType&, const FunctionType&);

        template <typename T, typename Method>
        struct MemberBinding
        {
            T* Instance;
            Method MethodPointer;
            RetType operator()(Args... args) const
            {
                if constexpr(std::is_void_v<RetType>)
                    (Instance->*MethodPointer)(std::forward<Args>(args)...);
                else
                    return (Instance->*MethodPointer)(std::forward<Args>(args)...);
            }
        };
        template <typename T, typename Method>
        static bool CompareMember(const FunctionType& left, const FunctionType& right)
        {
            const auto* a=left.template target<MemberBinding<T,Method>>();
            const auto* b=right.template target<MemberBinding<T,Method>>();
            return a && b && a->Instance==b->Instance && a->MethodPointer==b->MethodPointer;
        }
        static std::uint64_t NewIdentity()
        {
            // 同一signature内で再利用しない。上限到達後もwrapさせない。
            static NorvesLib::Thread::Atomic<std::uint64_t> next{1};
            auto value=next.Load(std::memory_order_relaxed);
            for(;;)
            {
                if(value==std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("Delegate registration identity exhausted");
                if(next.CompareExchangeWeak(value,value+1,std::memory_order_relaxed,std::memory_order_relaxed))
                    return value;
            }
        }
        void SetFunction(FunctionType function)
        {
            Delegate candidate;
            if(function)
            {
                // noexcept付き関数も通常の関数ポインタと同じ呼出先として扱う。
                if(const auto* noThrow=function.template target<NoexceptFunctionPointer>())
                    function=static_cast<FunctionPointer>(*noThrow);
                if(!function.template target<FunctionPointer>()) candidate.m_Identity=NewIdentity();
                candidate.m_Function.swap(function);
            }
            Swap(candidate);
        }
        void Swap(Delegate& other) noexcept
        {
            m_Function.swap(other.m_Function);
            std::swap(m_MemberComparer,other.m_MemberComparer);
            std::swap(m_Identity,other.m_Identity);
        }
        FunctionType m_Function;
        MemberComparer m_MemberComparer=nullptr;
        std::uint64_t m_Identity=0;

    public:
        Delegate() = default;
        ~Delegate() = default;
        Delegate(const Delegate&) = default;
        Delegate(Delegate&& other) noexcept { Swap(other); }
        Delegate& operator=(const Delegate& other)
        {
            if(this!=&other) { Delegate candidate(other);Swap(candidate); }
            return *this;
        }
        Delegate& operator=(Delegate&& other) noexcept
        {
            if(this!=&other) { Delegate candidate(std::move(other));Swap(candidate); }
            return *this;
        }
        Delegate(FunctionPointer function) { Bind(function); }
        Delegate(const FunctionType& function) { Bind(function); }
        template <typename F>
        requires (!std::same_as<std::decay_t<F>, Delegate<RetType, Args...>>)
        Delegate(F&& functor) { Bind(std::forward<F>(functor)); }
        template <typename T, typename Method>
        Delegate(T* instance, Method method) { Bind(instance,method); }

        void Clear() noexcept { Delegate empty;Swap(empty); }
        bool IsBound() const noexcept { return static_cast<bool>(m_Function); }
        void Bind(FunctionPointer function) { SetFunction(FunctionType(function)); }
        void Bind(const FunctionType& function) { SetFunction(function); }
        template <typename F>
        requires (!std::same_as<std::decay_t<F>, Delegate<RetType, Args...>>)
        void Bind(F&& functor) { SetFunction(FunctionType(std::forward<F>(functor))); }
        template <typename T, typename Method>
        void Bind(T* instance, Method method)
        {
            static_assert(std::is_member_function_pointer_v<Method>);
            if(!instance || !method) { Clear();return; }
            Delegate candidate;
            candidate.m_Function=MemberBinding<T,Method>{instance,method};
            candidate.m_MemberComparer=&CompareMember<T,Method>;
            Swap(candidate);
        }
        RetType Invoke(Args... args) const
        {
            if(!IsBound()) throw std::runtime_error("Delegate is not bound to a function");
            return m_Function(std::forward<Args>(args)...);
        }
        RetType operator()(Args... args) const { return Invoke(std::forward<Args>(args)...); }
        RetType InvokeIfBound(Args... args) const
        {
            if(IsBound()) return m_Function(std::forward<Args>(args)...);
            if constexpr(!std::is_void_v<RetType>) return RetType{};
        }
        bool operator==(const Delegate& other) const noexcept
        {
            if(!IsBound() || !other.IsBound()) return IsBound()==other.IsBound();
            const auto* a=m_Function.template target<FunctionPointer>();
            const auto* b=other.m_Function.template target<FunctionPointer>();
            if(a || b) return a && b && *a==*b;
            if(m_MemberComparer || other.m_MemberComparer)
                return m_MemberComparer && m_MemberComparer==other.m_MemberComparer && m_MemberComparer(m_Function,other.m_Function);
            return m_Identity!=0 && m_Identity==other.m_Identity;
        }
        bool operator!=(const Delegate& other) const noexcept { return !(*this==other); }
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
