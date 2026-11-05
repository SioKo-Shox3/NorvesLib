#include "Input/IInputBindingStore.h"

#include "Input/InputBindingJson.h"
#include "Save/SettingsStore.h"

namespace NorvesLib::Core::Input
{
    namespace
    {
        InputBindingStoreSaveResult SaveSettingsBindings(Save::SettingsStore& settings, const Container::String& text)
        {
            if (text.empty() || text.size() > InputBindingJson::MaximumTextBytes)
            {
                return {false, "保存する入力設定が空かサイズ上限を超えています", 0};
            }

            const Save::UserSettings previous = settings.Get();
            Save::UserSettings updated = previous;
            updated.KeyBindingsJson = text;
            settings.Set(updated);
            const auto saved = settings.Save();
            if (!saved.bSuccess)
            {
                settings.Set(previous);
                return {false, saved.Message, saved.NativeError};
            }
            return {true, {}, 0};
        }

        class SettingsInputBindingStore final : public IInputBindingStore
        {
        public:
            explicit SettingsInputBindingStore(::NorvesLib::Core::Save::SettingsStore& settings)
                : m_Settings(settings)
            {
            }

            InputBindingStoreLoadResult Load() override
            {
                const auto& text = m_Settings.Get().KeyBindingsJson;
                if (text.empty())
                {
                    return {EInputBindingStoreLoadStatus::Missing, {}, {}, 0};
                }
                if (text.size() > InputBindingJson::MaximumTextBytes)
                {
                    return {EInputBindingStoreLoadStatus::Error, {}, "入力設定がサイズ上限を超えています", 0};
                }
                return {EInputBindingStoreLoadStatus::Loaded, text, {}, 0};
            }

            InputBindingStoreSaveResult Save(const Container::String& text) override
            {
                return SaveSettingsBindings(m_Settings, text);
            }

        private:
            ::NorvesLib::Core::Save::SettingsStore& m_Settings;
        };
    }

    Container::TUniquePtr<IInputBindingStore> CreateSettingsInputBindingStore(Save::SettingsStore& settings)
    {
        return Container::MakeUnique<SettingsInputBindingStore>(settings);
    }

    LegacyInputBindingMigrationResult MigrateLegacyInputBindings(Save::SettingsStore& settings, IInputBindingStore& legacy)
    {
        if (!settings.Get().KeyBindingsJson.empty())
        {
            return {ELegacyInputBindingMigration::NotNeeded, {}};
        }

        const auto loaded = legacy.Load();
        if (loaded.Status == EInputBindingStoreLoadStatus::Missing ||
            (loaded.Status == EInputBindingStoreLoadStatus::Loaded && loaded.Text.empty()))
        {
            return {ELegacyInputBindingMigration::NoLegacy, {}};
        }
        if (loaded.Status != EInputBindingStoreLoadStatus::Loaded)
        {
            return {ELegacyInputBindingMigration::Failed, loaded.Error};
        }

        const auto saved = SaveSettingsBindings(settings, loaded.Text);
        if (!saved.Success)
        {
            return {ELegacyInputBindingMigration::Failed, saved.Error};
        }
        return {ELegacyInputBindingMigration::Migrated, {}};
    }
} // namespace NorvesLib::Core::Input
