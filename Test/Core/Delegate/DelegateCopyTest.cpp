#include "Delegate/Delegate.h"
#include "Delegate/MulticastDelegate.h"

#include <cassert>
#include <iostream>
#include <type_traits>

namespace
{
    using ReturnDelegate = NorvesLib::Core::Delegate<int, int>;
    using VoidDelegate = NorvesLib::Core::Delegate<void, int>;

    static_assert(std::is_copy_constructible_v<ReturnDelegate>);
    static_assert(std::is_move_constructible_v<ReturnDelegate>);
    static_assert(std::is_copy_assignable_v<ReturnDelegate>);
    static_assert(std::is_move_assignable_v<ReturnDelegate>);
    static_assert(std::is_nothrow_move_constructible_v<ReturnDelegate>);
    static_assert(std::is_nothrow_move_assignable_v<ReturnDelegate>);

    static_assert(std::is_copy_constructible_v<VoidDelegate>);
    static_assert(std::is_move_constructible_v<VoidDelegate>);
    static_assert(std::is_copy_assignable_v<VoidDelegate>);
    static_assert(std::is_move_assignable_v<VoidDelegate>);
    static_assert(std::is_nothrow_move_constructible_v<VoidDelegate>);
    static_assert(std::is_nothrow_move_assignable_v<VoidDelegate>);

    int Increment(int value)
    {
        return value + 1;
    }

    struct MemberTarget
    {
        int Total = 0;

        void Add(int value)
        {
            Total += value;
        }
    };

    void TestReturnDelegateCopyConstruction()
    {
        int invocationCount = 0;
        ReturnDelegate source([&invocationCount](int value)
        {
            ++invocationCount;
            return value + 1;
        });
        ReturnDelegate copy(source);

        assert(source.IsBound());
        assert(copy.IsBound());
        assert(source.Invoke(1) == 2);
        assert(copy.Invoke(2) == 3);
        assert(invocationCount == 2);

        source.Clear();
        assert(!source.IsBound());
        assert(copy.IsBound());
        assert(copy.Invoke(3) == 4);
        assert(invocationCount == 3);
    }

    void TestVoidDelegateCopyConstruction()
    {
        int invocationCount = 0;
        VoidDelegate source([&invocationCount](int value)
        {
            invocationCount += value;
        });
        VoidDelegate copy(source);

        assert(source.IsBound());
        assert(copy.IsBound());
        source.Invoke(1);
        copy.Invoke(2);
        assert(invocationCount == 3);

        source.Clear();
        assert(!source.IsBound());
        assert(copy.IsBound());
        copy.Invoke(3);
        assert(invocationCount == 6);
    }

    void TestReturnDelegateCopyAssignment()
    {
        int invocationCount = 0;
        ReturnDelegate source([&invocationCount](int value)
        {
            ++invocationCount;
            return value * 2;
        });
        ReturnDelegate copy;
        copy = source;

        assert(source.IsBound());
        assert(copy.IsBound());
        assert(source.Invoke(2) == 4);
        assert(copy.Invoke(3) == 6);
        assert(invocationCount == 2);

        source.Clear();
        assert(!source.IsBound());
        assert(copy.IsBound());
        assert(copy.Invoke(4) == 8);
        assert(invocationCount == 3);
    }

    void TestVoidDelegateCopyAssignment()
    {
        int invocationCount = 0;
        VoidDelegate source([&invocationCount](int value)
        {
            invocationCount += value;
        });
        VoidDelegate copy;
        copy = source;

        assert(source.IsBound());
        assert(copy.IsBound());
        source.Invoke(1);
        copy.Invoke(2);
        assert(invocationCount == 3);

        source.Clear();
        assert(!source.IsBound());
        assert(copy.IsBound());
        copy.Invoke(3);
        assert(invocationCount == 6);
    }

    void TestMulticastDelegateAddsNonConstLvalue()
    {
        int invocationCount = 0;
        VoidDelegate delegate([&invocationCount](int value)
        {
            invocationCount += value;
        });
        NorvesLib::Core::MulticastDelegate<int> multicast;

        multicast.Add(delegate);

        assert(multicast.GetSize() == 1);
        multicast.Broadcast(1);
        assert(invocationCount == 1);
    }

    void TestExistingDelegateConstruction()
    {
        ReturnDelegate lambda([](int value)
        {
            return value + 1;
        });
        ReturnDelegate functionPointer(&Increment);
        MemberTarget target;
        VoidDelegate member(&target, &MemberTarget::Add);

        assert(lambda.Invoke(1) == 2);
        assert(functionPointer.Invoke(2) == 3);
        member.Invoke(4);
        assert(target.Total == 4);
    }
} // namespace

int main()
{
    std::cout << "DelegateCopyTest start\n";
    TestReturnDelegateCopyConstruction();
    TestVoidDelegateCopyConstruction();
    TestReturnDelegateCopyAssignment();
    TestVoidDelegateCopyAssignment();
    TestMulticastDelegateAddsNonConstLvalue();
    TestExistingDelegateConstruction();
    std::cout << "DelegateCopyTest passed\n";
    return 0;
}
