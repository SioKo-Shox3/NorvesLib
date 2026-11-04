#include "Save/SaveSlotStore.h"

#include "Save/SaveContainer.h"
#include "Save/SaveMigration.h"
#include <cstdio>
#include <limits>
#include <utility>
#include <Windows.h>

namespace NorvesLib::Core::Save
{
    namespace
    {
        enum class CandidateState : uint8_t { Missing, Valid, Corrupt, Unsupported, Newer, IoError };

        struct Candidate
        {
            Container::String Path;
            CandidateState State = CandidateState::Missing;
            uint64_t Counter = 0;
            bool bMigrated = false;
            Container::VariableArray<uint8_t> Payload;
            Platform::FileOperationResult Error;
        };

        Container::String MakePath(const Container::String& directory, const char* prefix, uint32_t index)
        {
            Container::String path = directory;
            if (!path.empty() && path[path.size() - 1] != '\\' && path[path.size() - 1] != '/')
            {
                path.append("\\");
            }
            char name[64]{};
            std::snprintf(name, sizeof(name), "%s_%u.sav", prefix, index);
            path.append(name);
            return path;
        }

        Platform::FileOperationResult Failure(const char* message)
        {
            return {false, Platform::FileErrorKind::IoError, 0, Container::String(message)};
        }

        Candidate ReadCandidate(const Container::String& path, Platform::IFileOperations& ops)
        {
            Candidate candidate;
            candidate.Path = path;
            Container::VariableArray<uint8_t> bytes;
            candidate.Error = Platform::ReadFileBytes(path, bytes, ops);
            if (!candidate.Error.bSuccess)
            {
                candidate.State = candidate.Error.Kind == Platform::FileErrorKind::NotFound ?
                    CandidateState::Missing : CandidateState::IoError;
                return candidate;
            }
            SaveContainerInfo info;
            Container::Span<const uint8_t> payload;
            const auto status = DecodeSaveContainer(bytes, info, payload);
            if (status == SaveContainerStatus::UnsupportedContainerVersion)
            {
                // ヘッダハッシュの検証済みなので、この版の値は破損ではない。
                uint32_t version = 0;
                for (size_t index = 0; index < 4; ++index)
                {
                    version |= static_cast<uint32_t>(bytes[4 + index]) << (index * 8);
                }
                candidate.State = version > SaveContainerVersion ? CandidateState::Newer : CandidateState::Unsupported;
                return candidate;
            }
            if (status != SaveContainerStatus::Ok)
            {
                candidate.State = CandidateState::Corrupt;
                return candidate;
            }
            const auto migration = MigrateSavePayload(payload, candidate.Payload);
            if (migration == SaveMigrationResult::RejectedNewerVersion)
            {
                candidate.State = CandidateState::Newer;
            }
            else if (migration == SaveMigrationResult::Invalid)
            {
                candidate.State = CandidateState::Corrupt;
            }
            else
            {
                candidate.State = CandidateState::Valid;
                candidate.Counter = info.WriteCounter;
                candidate.bMigrated = migration == SaveMigrationResult::Migrated;
            }
            return candidate;
        }

        Platform::FileOperationResult Quarantine(const Container::String& path, Container::String& outPath)
        {
            SYSTEMTIME utc{};
            GetSystemTime(&utc);
            char stamp[48]{};
            std::snprintf(stamp, sizeof(stamp), ".corrupt-%04u%02u%02uT%02u%02u%02uZ",
                utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
            Container::String base = path;
            base.append(stamp);
            uint64_t suffix = 0;
            for (;;)
            {
                Container::String destination = base;
                if (suffix != 0)
                {
                    char number[32]{};
                    std::snprintf(number, sizeof(number), "-%llu", static_cast<unsigned long long>(suffix));
                    destination.append(number);
                }
                auto result = Platform::RenameFileNoReplace(path, destination);
                if (result.bSuccess)
                {
                    outPath = std::move(destination);
                    return result;
                }
                // 存在確認だけでは競合を防げないため、改名の衝突結果で次の接尾辞へ進む。
                if (result.NativeError != ERROR_ALREADY_EXISTS && result.NativeError != ERROR_FILE_EXISTS)
                {
                    return result;
                }
                if (suffix == (std::numeric_limits<uint64_t>::max)())
                {
                    return Failure("破損ファイルの退避名を確保できません。");
                }
                ++suffix;
            }
        }

        bool CheckCandidates(Container::Span<const Candidate> candidates, SaveLoadResult& result)
        {
            // 全候補の検証が済むまで改名せず、新しい版があれば他のファイルもそのまま残す。
            for (const auto& candidate : candidates)
            {
                if (candidate.State == CandidateState::Newer)
                {
                    result.Status = SaveLoadStatus::RejectedNewerVersion;
                    result.Message = "新しい版のセーブがあるため、読み込みを中止しました。";
                    return false;
                }
            }
            for (const auto& candidate : candidates)
            {
                if (candidate.State == CandidateState::IoError)
                {
                    result.Status = SaveLoadStatus::IoError;
                    result.Message = candidate.Error.Message;
                    return false;
                }
            }
            return true;
        }

        Platform::FileOperationResult CheckWritable(Container::Span<const Candidate> candidates, uint64_t& outCounter)
        {
            outCounter = 0;
            for (const auto& candidate : candidates)
            {
                if (candidate.State == CandidateState::Newer || candidate.State == CandidateState::Unsupported)
                {
                    return Failure("未対応の版のセーブがあるため、保存を中止しました。");
                }
                if (candidate.State == CandidateState::IoError)
                {
                    return candidate.Error;
                }
                if (candidate.State == CandidateState::Valid && candidate.Counter > outCounter)
                {
                    outCounter = candidate.Counter;
                }
            }
            if (outCounter == (std::numeric_limits<uint64_t>::max)())
            {
                return Failure("セーブの書き込み番号が上限に達しました。");
            }
            ++outCounter;
            return {true, Platform::FileErrorKind::None, 0, {}};
        }
    }

    SaveSlotStore::SaveSlotStore(const Container::String& directory)
        : SaveSlotStore(directory, Platform::GetDefaultFileOperations())
    {
    }

    SaveSlotStore::SaveSlotStore(const Container::String& directory, Platform::IFileOperations& ops)
        : m_Directory(directory), m_Operations(ops)
    {
    }

    uint32_t SaveSlotStore::CleanupTemporaries()
    {
        return Platform::CleanupAtomicWriteTemporaries(m_Directory);
    }

    Platform::FileOperationResult SaveSlotStore::SaveSlot(uint32_t slot, Container::Span<const uint8_t> payloadUtf8)
    {
        const auto path = MakePath(m_Directory, "slot", slot);
        auto backup = path;
        backup.append(".bak");
        Candidate candidates[] = {ReadCandidate(path, m_Operations), ReadCandidate(backup, m_Operations)};
        uint64_t counter = 0;
        auto result = CheckWritable(candidates, counter);
        if (!result.bSuccess)
        {
            return result;
        }
        for (const auto& candidate : candidates)
        {
            if (candidate.State == CandidateState::Corrupt)
            {
                Container::String quarantined;
                result = Quarantine(candidate.Path, quarantined);
                if (!result.bSuccess)
                {
                    return result;
                }
            }
        }
        Container::VariableArray<uint8_t> bytes;
        EncodeSaveContainer(payloadUtf8, counter, bytes);
        return Platform::WriteFileAtomic(path, bytes, true, m_Operations);
    }

    Platform::FileOperationResult SaveSlotStore::SaveAutosave(Container::Span<const uint8_t> payloadUtf8, uint32_t* pOutIndex)
    {
        Candidate candidates[SaveAutosaveRingSize];
        for (uint32_t index = 0; index < SaveAutosaveRingSize; ++index)
        {
            candidates[index] = ReadCandidate(MakePath(m_Directory, "autosave", index), m_Operations);
        }
        uint64_t counter = 0;
        auto result = CheckWritable(candidates, counter);
        if (!result.bSuccess)
        {
            return result;
        }
        uint32_t selected = SaveAutosaveRingSize;
        for (const auto state : {CandidateState::Missing, CandidateState::Corrupt, CandidateState::Valid})
        {
            for (uint32_t index = 0; index < SaveAutosaveRingSize; ++index)
            {
                if (candidates[index].State == state &&
                    (selected == SaveAutosaveRingSize || candidates[index].Counter < candidates[selected].Counter))
                {
                    selected = index;
                }
            }
            if (selected != SaveAutosaveRingSize)
            {
                break;
            }
        }
        if (candidates[selected].State == CandidateState::Corrupt)
        {
            Container::String quarantined;
            result = Quarantine(candidates[selected].Path, quarantined);
            if (!result.bSuccess)
            {
                return result;
            }
        }
        Container::VariableArray<uint8_t> bytes;
        EncodeSaveContainer(payloadUtf8, counter, bytes);
        result = Platform::WriteFileAtomic(candidates[selected].Path, bytes, false, m_Operations);
        if (result.bSuccess && pOutIndex)
        {
            *pOutIndex = selected;
        }
        return result;
    }

    SaveLoadResult SaveSlotStore::LoadSlot(uint32_t slot)
    {
        const auto path = MakePath(m_Directory, "slot", slot);
        auto backup = path;
        backup.append(".bak");
        Container::VariableArray<Candidate> candidates;
        candidates.push_back(ReadCandidate(path, m_Operations));
        candidates.push_back(ReadCandidate(backup, m_Operations));
        SaveLoadResult result;
        if (!CheckCandidates(candidates, result))
        {
            return result;
        }
        size_t selected = candidates.size();
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            if (candidates[index].State == CandidateState::Valid &&
                (selected == candidates.size() || candidates[index].Counter > candidates[selected].Counter))
            {
                selected = index;
            }
        }
        if (selected == candidates.size())
        {
            for (uint32_t index = 0; index < SaveAutosaveRingSize; ++index)
            {
                candidates.push_back(ReadCandidate(MakePath(m_Directory, "autosave", index), m_Operations));
            }
            if (!CheckCandidates(candidates, result))
            {
                return result;
            }
            selected = candidates.size();
            for (size_t index = 2; index < candidates.size(); ++index)
            {
                if (candidates[index].State == CandidateState::Valid &&
                    (selected == candidates.size() || candidates[index].Counter > candidates[selected].Counter))
                {
                    selected = index;
                }
            }
        }
        for (const auto& candidate : candidates)
        {
            if (candidate.State == CandidateState::Corrupt)
            {
                Container::String quarantined;
                const auto renamed = Quarantine(candidate.Path, quarantined);
                if (!renamed.bSuccess)
                {
                    result.Status = SaveLoadStatus::IoError;
                    result.Message = renamed.Message;
                    return result;
                }
                result.QuarantinedPaths.push_back(std::move(quarantined));
            }
        }
        if (selected != candidates.size())
        {
            auto& candidate = candidates[selected];
            result.Status = SaveLoadStatus::Loaded;
            result.Source = selected == 0 ? SaveLoadSource::Primary :
                (selected == 1 ? SaveLoadSource::Backup : SaveLoadSource::Autosave);
            result.AutosaveIndex = selected >= 2 ? static_cast<uint32_t>(selected - 2) : 0;
            result.WriteCounter = candidate.Counter;
            result.bMigrated = candidate.bMigrated;
            result.PayloadUtf8 = std::move(candidate.Payload);
            result.Message = "セーブを読み込みました。";
        }
        else
        {
            result.Message = "読み込めるセーブがないため、新規ゲームを開始します。";
        }
        return result;
    }
}
