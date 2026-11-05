// ストリーミングのフレームごとと常駐量の予算を定義する。
#pragma once

#include <cstdint>

namespace NorvesLib::Core::Streaming
{
    // 既定値は仮の値。
    struct StreamingBudget
    {
        double PerFrameCommitMs = 2.0;               // 1 フレームのコミットに使ってよい時間。
        uint64_t PerFrameCommitBytes = 32ull << 20; // 1 フレームにコミットしてよいバイト。
        uint32_t MaxResidentCells = 64;              // Unloaded・Failed 以外のセルの上限。
        uint64_t MaxCpuBytes = 1ull << 30;
        uint64_t MaxGpuBytes = 1ull << 30;
    };
} // namespace NorvesLib::Core::Streaming
