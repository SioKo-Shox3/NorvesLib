#pragma once

#include "Platform/PlatformFile.h"

namespace NorvesLib::Core::Save
{
    inline constexpr uint32_t SaveAutosaveRingSize = 3;

    enum class SaveLoadStatus : uint8_t { Loaded, NewGame, RejectedNewerVersion, IoError };
    enum class SaveLoadSource : uint8_t { None, Primary, Backup, Autosave };

    struct SaveLoadResult
    {
        SaveLoadStatus Status = SaveLoadStatus::NewGame;
        SaveLoadSource Source = SaveLoadSource::None;
        uint32_t AutosaveIndex = 0;
        uint64_t WriteCounter = 0;
        bool bMigrated = false;
        Container::VariableArray<uint8_t> PayloadUtf8;
        Container::VariableArray<Container::String> QuarantinedPaths;
        Container::String Message;
    };

    // ディレクトリは呼び出し元が作成する。末尾の区切りは有無どちらでもよい。
    // 読めないファイルや退避の失敗は IoError とし、読み込み時には保存内容を書き直さない。
    // 同じ保存先への操作は呼び出し元で直列化する。ファイル操作の参照は本体より長く生存させる。
    class SaveSlotStore
    {
    public:
        explicit SaveSlotStore(const Container::String& directory);
        SaveSlotStore(const Container::String& directory, Platform::IFileOperations& ops);

        uint32_t CleanupTemporaries();
        // 破損した主・バックアップは保存前に退避し、主の退避後も有効なバックアップを保持する。
        Platform::FileOperationResult SaveSlot(uint32_t slot, Container::Span<const uint8_t> payloadUtf8);
        Platform::FileOperationResult SaveAutosave(Container::Span<const uint8_t> payloadUtf8, uint32_t* pOutIndex = nullptr);
        SaveLoadResult LoadSlot(uint32_t slot);

    private:
        Container::String m_Directory;
        Platform::IFileOperations& m_Operations;
    };
}
