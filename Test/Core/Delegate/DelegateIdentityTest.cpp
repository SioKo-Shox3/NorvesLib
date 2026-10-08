#include "Delegate/Delegate.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <stdexcept>
using NorvesLib::Core::Delegate;
namespace
{
    int AddOne(int v) { return v+1; }
    int AddTwo(int v) { return v+2; }
    void Ignore(int) {}
    void IgnoreOther(int) {}
    void IgnoreNoexcept(int) noexcept {}
    struct Target
    {
        mutable int Total=0;
        int Add(int n) { Total+=n;return Total; }
        int Sub(int n) { Total-=n;return Total; }
        int ConstAdd(int n) const { Total+=n;return Total; }
        void AddVoid(int n) { Total+=n; }
        void SubVoid(int n) { Total-=n; }
    };
    struct ThrowCopy
    {
        bool* Throw;
        explicit ThrowCopy(bool& flag):Throw(&flag) {}
        ThrowCopy(const ThrowCopy& other):Throw(other.Throw)
        {
            if(*Throw) throw std::runtime_error("copy failure");
        }
        int operator()(int n) const { return n+9; }
    };
}
int main()
{
    using D=Delegate<int,int>;
    using V=Delegate<void,int>;
    static_assert(std::is_nothrow_move_constructible_v<D> && std::is_nothrow_move_assignable_v<D>);
    static_assert(std::is_nothrow_move_constructible_v<V> && std::is_nothrow_move_assignable_v<V>);
    D empty,none(static_cast<int(*)(int)>(nullptr));
    assert(empty==none && !none.IsBound() && none.InvokeIfBound(1)==0);
    bool threw=false;try { empty.Invoke(1); } catch(const std::runtime_error&) { threw=true; }assert(threw);
    V voidEmpty;voidEmpty.InvokeIfBound(2);
    D a(AddOne),b(AddOne),c(AddTwo),copy(a);
    assert(a==b && a==copy && a!=c && a!=empty && a(1)==2);
    std::function<int(int)> wrapped(AddOne);
    D fromWrapped(wrapped);assert(fromWrapped==a);
    V va(Ignore),vb(Ignore),vc(IgnoreOther);assert(va==vb && va!=vc);
    V vn(IgnoreNoexcept),vnNormal(static_cast<void(*)(int)>(&IgnoreNoexcept));
    std::function<void(int)> wrappedNoexcept(&IgnoreNoexcept);V vnWrapped(wrappedNoexcept);
    assert(vn==vnNormal && vn==vnWrapped);
    Target first,second;
    D ma(&first,&Target::Add),same(&first,&Target::Add),differentObject(&second,&Target::Add),differentMethod(&first,&Target::Sub);
    assert(ma==same && ma!=differentObject && ma!=differentMethod && ma!=a);
    ma(4);assert(first.Total==4 && second.Total==0);
    V discardResult(&second,&Target::Add),discardSame(&second,&Target::Add);
    assert(discardResult==discardSame);discardResult(5);assert(second.Total==5);
    discardResult.Bind(&second,&Target::Sub);discardResult(2);assert(second.Total==3);
    const Target constTarget;
    D ca(&constTarget,&Target::ConstAdd),cb(&constTarget,&Target::ConstAdd);
    assert(ca==cb && ca(3)==3);
    V vm(&first,&Target::AddVoid),vmSame(&first,&Target::AddVoid),vmObject(&second,&Target::AddVoid),vmMethod(&first,&Target::SubVoid);
    assert(vm==vmSame && vm!=vmObject && vm!=vmMethod);
    D nullObject(static_cast<Target*>(nullptr),&Target::Add);
    D nullMethod(&first,static_cast<int(Target::*)(int)>(nullptr));
    assert(!nullObject.IsBound() && !nullMethod.IsBound() && nullObject==empty);
    auto make=[](int captured) { return [captured](int n) { return n+captured; }; };
    auto callable=make(3);
    D fa(callable),fb(callable),fc(make(7)),handle(fa);
    assert(fa!=fb && fa!=fc && fa==handle && fa(2)==5);
    fa.Bind(callable);assert(fa!=handle);fa=handle;assert(fa==handle);
    std::function<int(int)> opaque(callable);
    D oa(opaque),ob(opaque);assert(oa!=ob);
    D moved(std::move(fa));assert(moved==handle && !fa.IsBound());
    fa=std::move(moved);assert(fa==handle && !moved.IsBound());
    D* self=&fa;fa=*self;assert(fa==handle);fa=std::move(*self);assert(fa==handle);
    fa.Clear();assert(fa==empty && handle.IsBound());
    auto voidCallable=[&first](int n) { first.Total+=n; };
    V vf(voidCallable),vg(voidCallable),vh(vf);assert(vf!=vg && vf==vh);
    V vmove(std::move(vf));assert(!vf.IsBound() && vmove==vh);
    vf=std::move(vmove);assert(!vmove.IsBound() && vf==vh);
    vf.Bind(Ignore);assert(vf==va && vf!=vh);
    // copy/Bindの失敗でもcallableと識別は必ず以前の組のまま残る。
    bool fail=false;ThrowCopy throwing(fail);D source(throwing),destination(AddTwo),saved(destination);
    fail=true;threw=false;
    try { destination=source; } catch(const std::runtime_error&) { threw=true; }
    assert(threw && destination==saved && destination(1)==3);
    threw=false;try { destination.Bind(throwing); } catch(const std::runtime_error&) { threw=true; }
    assert(threw && destination==saved && destination(1)==3);
    fail=false;destination=source;assert(destination==source && destination(1)==10);
    // callbackの内部状態が変わっても、copy handleによる解除識別を保つ。
    D mutableFunctor([count=0](int) mutable { return ++count; }),mutableHandle(mutableFunctor);
    assert(mutableFunctor(0)==1 && mutableFunctor(0)==2 && mutableFunctor==mutableHandle);
    std::cout << "DelegateIdentityTest passed\n";
    return 0;
}
