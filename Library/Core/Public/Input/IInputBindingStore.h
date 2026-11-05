#pragma once
#include "Container/PointerTypes.h"
#include "Container/String.h"
#include <cstdint>

namespace NorvesLib::Core::Save { class SettingsStore; }

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
    // 作業ディレクトリの古い保存先。古い場所からの写しを読み取るために使う。
    // 生成時の作業ディレクトリに固定し、非対応プラットフォームでは nullptr を返す。
    Container::TUniquePtr<IInputBindingStore> CreateWorkingDirectoryInputBindingStore();

    // settings.json の keyBindings を読み書きする。settings は戻り値より長く生存させる。GameThread 専用。
    Container::TUniquePtr<IInputBindingStore> CreateSettingsInputBindingStore(Save::SettingsStore& settings);

    enum class ELegacyInputBindingMigration : uint8_t { NotNeeded, NoLegacy, Migrated, Failed };
    struct LegacyInputBindingMigrationResult
    {
        ELegacyInputBindingMigration Status = ELegacyInputBindingMigration::Failed;
        Container::String Error;
    };
    // settings に keyBindings がなく、legacy が読めたときだけ写して保存する。古いファイルは消さない。
    LegacyInputBindingMigrationResult MigrateLegacyInputBindings(Save::SettingsStore& settings, IInputBindingStore& legacy);
}
