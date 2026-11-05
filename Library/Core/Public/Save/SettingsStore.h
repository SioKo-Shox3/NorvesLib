#pragma once

#include "Delegate/MulticastDelegate.h"
#include "Platform/PlatformFile.h"
#include <cstdint>

namespace NorvesLib::Core::Save
{
    inline constexpr uint32_t SettingsFormatVersion = 1;

    struct UserSettings
    {
        float MasterVolume = 1.0f;
        float EffectVolume = 1.0f;
        float AmbienceVolume = 1.0f;
        // 0 は未指定で、起動時の既定に従う。
        uint32_t ResolutionWidth = 0;
        uint32_t ResolutionHeight = 0;
        bool bFullscreen = false;
        Container::String Language;
        // 入力の上書きを表す JSON オブジェクト。空は未保存。
        Container::String KeyBindingsJson;

        bool operator==(const UserSettings&) const = default;
    };

    enum class SettingsLoadStatus : uint8_t { Loaded, LoadedFromBackup, Defaults, RejectedNewerVersion, IoError };

    struct SettingsLoadResult
    {
        SettingsLoadStatus Status = SettingsLoadStatus::Defaults;
        Container::String Message;
    };

    // GameThread 専用。保存先の親ディレクトリは呼び出し元で作成する。
    // ファイル操作の参照はストアより長く生存させる。
    class SettingsStore
    {
    public:
        explicit SettingsStore(const Container::String& filePath);
        SettingsStore(const Container::String& filePath, Platform::IFileOperations& ops);

        SettingsLoadResult Load();
        Platform::FileOperationResult Save();
        const UserSettings& Get() const { return m_Value; }
        // 音量を正規化し、有効なキー割当は整形の差を除いた JSON に揃える。
        bool Set(const UserSettings& value);

        // Add/Remove には同じ関数・オブジェクトとメソッド・Delegate を渡す。
        // 通知中の購読変更は次の通知から反映される。
        MulticastDelegate<const UserSettings&>& OnValueChanged() { return m_OnValueChanged; }

    private:
        Container::String m_FilePath;
        Platform::IFileOperations& m_Operations;
        UserSettings m_Value;
        MulticastDelegate<const UserSettings&> m_OnValueChanged;
        bool m_bSaveBlocked = false;
    };
} // namespace NorvesLib::Core::Save
