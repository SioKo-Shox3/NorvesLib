#include "Input/InputBindingPersistence.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace
{
    const char* Defaults=R"({"schema":"bindings.v1","contexts":[{"id":"Gameplay","actions":[
      {"id":"Jump","type":"button","bindings":[{"source":"key","code":"Space"}]}]}]})";
    class FakeStore final : public IInputBindingStore
    {
    public:
        int Loads=0,Saves=0;
        bool SaveFails=false;
        InputBindingStoreLoadResult Next;
        Container::String Saved;
        InputBindingStoreLoadResult Load() override { ++Loads;return Next; }
        InputBindingStoreSaveResult Save(const Container::String& text) override
        {
            ++Saves;InputBindingStoreSaveResult result;
            if(SaveFails) { result.NativeError=99;return result; }
            Saved=text;result.Success=true;return result;
        }
    };
    Container::String Canonical(const InputBindingSet& value)
    {
        Container::String text;assert(InputBindingJson::WriteDefaults(value,text));return text;
    }
}
int main()
{
    FakeStore store;
    auto invalid=LoadInputBindingConfiguration("broken",&store);
    assert(!invalid.DefaultsValid && store.Loads==0 && store.Saves==0);
    assert(!invalid.DefaultReport.Error.empty() && invalid.Current.GetContexts().empty());
    auto defaults=LoadInputBindingConfiguration(Defaults);
    assert(defaults.DefaultsValid && defaults.UserStatus==EInputBindingUserStatus::NotRequested);
    const auto canonical=Canonical(defaults.Defaults);
    assert(Canonical(defaults.Current)==canonical);
    store.Next.Status=EInputBindingStoreLoadStatus::Missing;
    auto missing=LoadInputBindingConfiguration(Defaults,&store);
    assert(missing.DefaultsValid && missing.UserStatus==EInputBindingUserStatus::Missing);
    assert(Canonical(missing.Current)==canonical && store.Saves==0);
    store.Next.Status=EInputBindingStoreLoadStatus::Error;store.Next.NativeError=5;
    auto denied=LoadInputBindingConfiguration(Defaults,&store);
    assert(denied.UserStatus==EInputBindingUserStatus::ReadError && denied.StoreNativeError==5 && !denied.StoreError.empty());
    assert(Canonical(denied.Current)==canonical);
    store.Next.Status=static_cast<EInputBindingStoreLoadStatus>(255);
    auto unknown=LoadInputBindingConfiguration(Defaults,&store);
    assert(unknown.UserStatus==EInputBindingUserStatus::ReadError && Canonical(unknown.Current)==canonical);
    store.Next.Status=EInputBindingStoreLoadStatus::Loaded;store.Next.Text="broken";
    auto corrupt=LoadInputBindingConfiguration(Defaults,&store);
    assert(corrupt.UserStatus==EInputBindingUserStatus::Invalid && !corrupt.UserReport.Error.empty());
    assert(Canonical(corrupt.Current)==canonical && store.Saves==0);
    store.Next.Text.clear();
    assert(LoadInputBindingConfiguration(Defaults,&store).UserStatus==EInputBindingUserStatus::Invalid);
    store.Next.Text=R"({"schema":"bindings.v1","contexts":[{"id":"Gameplay","actions":[{"id":"Jump","bindings":[]}]}]})";
    auto user=LoadInputBindingConfiguration(Defaults,&store);
    assert(user.UserStatus==EInputBindingUserStatus::Applied);
    assert(user.Current.FindAction("Gameplay"_id,"Jump"_id)->Bindings.empty());
    assert(!user.Defaults.FindAction("Gameplay"_id,"Jump"_id)->Bindings.empty());
    assert(store.Saves==0);
    auto saved=SaveInputBindingOverrides(store,user.Defaults,user.Current);
    assert(saved.Success && store.Saves==1 && !store.Saved.empty());
    store.Next.Text=store.Saved;
    auto roundtrip=LoadInputBindingConfiguration(Defaults,&store);
    assert(roundtrip.UserStatus==EInputBindingUserStatus::Applied && Canonical(roundtrip.Current)==Canonical(user.Current));
    const auto previous=store.Saved;store.SaveFails=true;
    auto failed=SaveInputBindingOverrides(store,defaults.Defaults,defaults.Current);
    assert(!failed.Success && failed.NativeError==99 && !failed.Error.empty() && store.Saved==previous);
    InputBindingSet changedStructure;
    const int calls=store.Saves;
    auto rejected=SaveInputBindingOverrides(store,defaults.Defaults,changedStructure);
    assert(!rejected.Success && !rejected.Error.empty() && store.Saves==calls);
    std::cout << "InputBindingPersistenceTest passed\n";
    return 0;
}
