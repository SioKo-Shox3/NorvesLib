#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstdint>

namespace NorvesLib::Core::Save
{
    inline constexpr uint32_t SavePayloadFormatVersion = 2;

    enum class SaveMigrationResult : uint8_t
    {
        UpToDate,
        Migrated,
        RejectedNewerVersion,
        Invalid
    };

    // v1: formatVersion=1、savedAt（Unix 秒）、playTime（秒）、player、world、progress。
    // v2: formatVersion=2、appVersion、savedAtUnix（Unix 秒）、playSeconds（秒）、player、world、progress。
    // savedAt と playTime は改名し、playTime が無ければ 0、appVersion が無ければ空文字列にする。
    // 他の欄は未知の欄も含めて保持する。ペイロードは BOM の無い UTF-8 JSON。
    // 絶対値が 2^53 以上の整数は文字列で保存する。移行時は丸めずに拒否する。
    // 入出力が同じ配列でもよい。失敗または新しい版の拒否時は出力を空にする。
    SaveMigrationResult MigrateSavePayload(Container::Span<const uint8_t> payloadUtf8,
                                           Container::VariableArray<uint8_t>& outPayloadUtf8,
                                           Container::String* pOutError = nullptr);
} // namespace NorvesLib::Core::Save
