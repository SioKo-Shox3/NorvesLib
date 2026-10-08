#include "Input/InputBindingPersistence.h"
#include <utility>

namespace NorvesLib::Core::Input
{
    InputBindingConfiguration LoadInputBindingConfiguration(const Container::String& defaultJson,IInputBindingStore* store)
    {
        InputBindingConfiguration result;
        result.DefaultsValid=InputBindingJson::ParseDefaults(defaultJson,result.Defaults,&result.DefaultReport);
        if(!result.DefaultsValid) return result;
        result.Current=result.Defaults;
        if(!store) return result;
        auto loaded=store->Load();
        switch(loaded.Status)
        {
        case EInputBindingStoreLoadStatus::Missing:
            result.UserStatus=EInputBindingUserStatus::Missing;
            break;
        case EInputBindingStoreLoadStatus::Loaded:
            result.UserStatus=InputBindingJson::ApplyOverrides(result.Defaults,loaded.Text,result.Current,&result.UserReport)
                ? EInputBindingUserStatus::Applied : EInputBindingUserStatus::Invalid;
            break;
        default:
            result.UserStatus=EInputBindingUserStatus::ReadError;
            result.StoreError=loaded.Error.empty()?Container::String("入力設定を読み込めません"):std::move(loaded.Error);
            result.StoreNativeError=loaded.NativeError;
            break;
        }
        return result;
    }
    InputBindingStoreSaveResult SaveInputBindingOverrides(IInputBindingStore& store,
        const InputBindingSet& defaults,const InputBindingSet& current)
    {
        Container::String json;
        if(!InputBindingJson::WriteOverrides(defaults,current,json))
        {
            InputBindingStoreSaveResult result;result.Error="入力設定の差分を書き出せません";return result;
        }
        auto result=store.Save(json);
        if(!result.Success && result.Error.empty()) result.Error="入力設定を保存できません";
        return result;
    }
}
