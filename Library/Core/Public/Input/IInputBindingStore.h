#pragma once
#include "Container/PointerTypes.h"
#include "Container/String.h"
#include <cstdint>

namespace NorvesLib::Core::Input
{
    enum class EInputBindingStoreLoadStatus : uint8_t { Loaded, Missing, Error };
    struct InputBindingStoreLoadResult
    {
        EInputBindingStoreLoadStatus Status = EInputBindingStoreLoadStatus::Error;
        Container::String Text;
        Container::String Error;
        uint32_t NativeError = 0;
    };
    struct InputBindingStoreSaveResult
    {
        bool Success = false;
        Container::String Error;
        uint32_t NativeError = 0;
    };
    // JSON textの保存先境界。結果は値返しで、出力引数同士のaliasを作らない。
    // GameThread専用。validation/IO失敗は結果、allocation例外は伝播する。
    class IInputBindingStore
    {
    public:
        virtual ~IInputBindingStore() = default;
        virtual InputBindingStoreLoadResult Load() = 0;
        virtual InputBindingStoreSaveResult Save(const Container::String& text) = 0;
    };
    // Windows暫定adapter。生成時のworking directoryへInputBindings.jsonを固定する。
    // 非対応platformはnullptr。GR76のuser-data保存先へはこの境界だけ差し替える。
    Container::TUniquePtr<IInputBindingStore> CreateWorkingDirectoryInputBindingStore();
}
