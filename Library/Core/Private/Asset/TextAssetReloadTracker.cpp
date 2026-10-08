#include "Asset/TextAssetReloadTracker.h"
#include <cmath>
namespace NorvesLib::Core::Asset
{
    bool TextAssetReloadTracker::SetPath(Container::AnsiStringView path)
    {
        if (path.empty())
            return false;
        for (char ch : path)
            if (ch == 0)
                return false;
        const auto normalized = AssetPath::Normalize(path);
        if (!normalized.IsValid() || normalized.IsAbsolute() || !normalized.HasLogicalPath())
            return false;
        m_Path = normalized.GetLogicalPath();
        Reset();
        return true;
    }
    void TextAssetReloadTracker::Reset()
    {
        m_Elapsed = 0;
        m_bFirst = true;
        m_bObserved = false;
        m_LastHash = 0;
        m_LastSize = 0;
        m_LastReadStatus = AssetReadStatus::InvalidRequest;
    }
    TextAssetPollResult TextAssetReloadTracker::Poll(double elapsedSeconds, AssetBlob& out)
    {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0 || m_Path.empty())
            return TextAssetPollResult::InvalidArgument;
        // 非常に大きいdtでも加算overflowを避け、余りだけ保持する。
        constexpr double interval = .25;
        const bool due = m_bFirst || elapsedSeconds >= interval || m_Elapsed + elapsedSeconds >= interval;
        m_Elapsed = m_bFirst ? 0 : std::fmod(m_Elapsed + std::fmod(elapsedSeconds, interval), interval);
        if (!due)
            return TextAssetPollResult::NotDue;
        m_bFirst = false;
        AssetReadRequest request;
        request.InputPath = m_Path;
        request.bAllowAbsolutePath = false;
        request.MaxReadBytes = MaximumBytes;
        auto read = m_Reader.Read(request);
        m_LastReadStatus = read.Status;
        if (!read.Succeeded())
            return TextAssetPollResult::ReadFailed;
        uint64_t hash = 14695981039346656037ull;
        for (uint8_t value : read.Blob.GetSpan())
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        if (m_bObserved && m_LastHash == hash && m_LastSize == read.Blob.GetSize())
            return TextAssetPollResult::Unchanged;
        m_LastHash = hash;
        m_LastSize = read.Blob.GetSize();
        m_bObserved = true;
        out = read.Blob;
        return TextAssetPollResult::Changed;
    }
} // namespace NorvesLib::Core::Asset
