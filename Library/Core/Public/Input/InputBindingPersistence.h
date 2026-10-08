#pragma once
#include "Input/IInputBindingStore.h"
#include "Input/InputBindingJson.h"

namespace NorvesLib::Core::Input
{
    enum class EInputBindingUserStatus : uint8_t { NotRequested, Missing, Applied, Invalid, ReadError };
    struct InputBindingConfiguration
    {
        bool DefaultsValid = false;
        EInputBindingUserStatus UserStatus = EInputBindingUserStatus::NotRequested;
        InputBindingSet Defaults;
        InputBindingSet Current;
        InputBindingJsonReport DefaultReport;
        InputBindingJsonReport UserReport;
        Container::String StoreError;
        uint32_t StoreNativeError = 0;
    };
    // 不正な既定ではuserを読まない。userだけの失敗はCurrentを既定に保つ。
    InputBindingConfiguration LoadInputBindingConfiguration(const Container::String& defaultJson,IInputBindingStore* store = nullptr);
    // 明示呼出しの時だけuser差分を保存する。ロードに伴う自動書き戻しはしない。
    InputBindingStoreSaveResult SaveInputBindingOverrides(IInputBindingStore& store,
        const InputBindingSet& defaults,const InputBindingSet& current);
}
