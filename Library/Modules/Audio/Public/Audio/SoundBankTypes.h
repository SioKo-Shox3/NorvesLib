#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Modules::Audio
{
    enum class SoundBankRejectReason : uint8_t
    {
        ParseError,
        UnsupportedVersion,
        UnknownKey,
        MissingField,
        InvalidValue,
        DuplicateId,
        MissingClip,
        SpatialStereoClip,
        BankNotFound,
    };

    struct SoundBankDiagnostic
    {
        uint32_t DefinitionIndex = UINT32_MAX; // バンク全体の問題なら UINT32_MAX。
        Core::Container::String DefinitionName; // 分かれば id。
        SoundBankRejectReason Reason = SoundBankRejectReason::ParseError;
        Core::Container::String Detail;
    };

    struct SoundBankLoadReport
    {
        uint32_t LoadedDefinitionCount = 0;
        uint32_t RejectedDefinitionCount = 0;
        Core::Container::VariableArray<SoundBankDiagnostic> Diagnostics;
    };
} // namespace NorvesLib::Modules::Audio
