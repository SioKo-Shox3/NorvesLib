#include "Scripting/ScriptSourceTracker.h"

#include "Asset/AssetPath.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace NorvesLib::Core::Scripting
{
    namespace
    {
        ScriptContentHash HashBytes(Container::AnsiStringView bytes) noexcept
        {
            constexpr ScriptContentHash offsetBasis = 14695981039346656037ull;
            constexpr ScriptContentHash prime = 1099511628211ull;
            ScriptContentHash hash = offsetBasis;
            for (size_t index = 0; index < bytes.size(); ++index)
            {
                hash ^= static_cast<unsigned char>(bytes[index]);
                hash *= prime;
            }
            return hash;
        }

        bool HasRejectedSegment(Container::StringView path) noexcept
        {
            size_t segmentStart = 0;
            for (size_t index = 0; index <= path.size(); ++index)
            {
                if (index != path.size() && path[index] != '/' && path[index] != '\\')
                {
                    continue;
                }

                const size_t segmentLength = index - segmentStart;
                if (segmentLength == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.')
                {
                    return true;
                }
                segmentStart = index + 1;
            }
            return false;
        }

        bool ResolveScriptPath(
            Container::StringView requestedPath,
            Container::String& outLogicalPath,
            std::filesystem::path& outPhysicalPath)
        {
            outLogicalPath.clear();
            outPhysicalPath.clear();
            if (requestedPath.empty() || HasRejectedSegment(requestedPath) ||
                requestedPath[0] == '/' || requestedPath[0] == '\\' ||
                (requestedPath.size() >= 2 &&
                    ((requestedPath[0] >= 'A' && requestedPath[0] <= 'Z') ||
                        (requestedPath[0] >= 'a' && requestedPath[0] <= 'z')) &&
                    requestedPath[1] == ':'))
            {
                return false;
            }

            Container::String requestedPathText;
            requestedPathText.append(requestedPath.data(), requestedPath.size());
            const Asset::AssetPath normalized = Asset::AssetPath::Normalize(
                requestedPathText.c_str(), NORVES_ASSET_DIR);
            if (!normalized.IsValid() || normalized.IsAbsolute() || !normalized.HasLogicalPath())
            {
                return false;
            }

            std::error_code error;
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(
                std::filesystem::path(NORVES_ASSET_DIR), error);
            if (error)
            {
                return false;
            }

            const std::filesystem::path canonicalCandidate = std::filesystem::weakly_canonical(
                canonicalRoot / normalized.GetLogicalPath().c_str(), error);
            if (error)
            {
                return false;
            }

            std::filesystem::path::const_iterator rootIt = canonicalRoot.begin();
            std::filesystem::path::const_iterator candidateIt = canonicalCandidate.begin();
            while (rootIt != canonicalRoot.end())
            {
                if (candidateIt == canonicalCandidate.end() || *rootIt != *candidateIt)
                {
                    return false;
                }
                ++rootIt;
                ++candidateIt;
            }

            outLogicalPath = Container::String(normalized.GetLogicalPath().c_str());
            outPhysicalPath = canonicalCandidate;
            return true;
        }

        bool ReadBinaryFile(const std::filesystem::path& path, Container::AnsiString& outBytes)
        {
            outBytes.clear();
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error)
            {
                return false;
            }

            const uintmax_t fileSize = std::filesystem::file_size(path, error);
            if (error || fileSize > static_cast<uintmax_t>(SIZE_MAX))
            {
                return false;
            }

            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                return false;
            }

            uintmax_t remainingBytes = fileSize;
            char buffer[4096];
            while (remainingBytes > 0)
            {
                const size_t chunkSize = remainingBytes < sizeof(buffer)
                    ? static_cast<size_t>(remainingBytes)
                    : sizeof(buffer);
                stream.read(buffer, static_cast<std::streamsize>(chunkSize));
                if (stream.gcount() != static_cast<std::streamsize>(chunkSize))
                {
                    outBytes.clear();
                    return false;
                }
                outBytes.append(buffer, chunkSize);
                remainingBytes -= chunkSize;
            }
            return stream.good() || stream.eof();
        }
    }

    void ScriptSourcePollBatch::Reset()
    {
        Sources.clear();
        Changes.clear();
        Fingerprint = 0;
    }

    void ScriptSourceTracker::Reset()
    {
        m_Bindings.clear();
        m_AccumulatedSeconds = 0.0f;
        m_LastRejectedFingerprint = 0;
        m_bHasRejectedFingerprint = false;
    }

    EScriptSourceReadResult ScriptSourceTracker::ReadSource(
        Container::StringView requestedPath,
        ScriptSourceSnapshot& outSnapshot) const
    {
        outSnapshot = {};
        outSnapshot.RequestedPath.append(requestedPath.data(), requestedPath.size());
        std::filesystem::path physicalPath;
        if (!ResolveScriptPath(requestedPath, outSnapshot.LogicalPath, physicalPath))
        {
            outSnapshot.Result = EScriptSourceReadResult::InvalidPath;
            return outSnapshot.Result;
        }

        if (!ReadBinaryFile(physicalPath, outSnapshot.Bytes))
        {
            outSnapshot.Result = EScriptSourceReadResult::ReadFailed;
            return outSnapshot.Result;
        }

        outSnapshot.ContentHash = HashBytes(Container::AnsiStringView(outSnapshot.Bytes.data(), outSnapshot.Bytes.size()));
        outSnapshot.Result = EScriptSourceReadResult::Success;
        return outSnapshot.Result;
    }

    EScriptSourcePollResult ScriptSourceTracker::Poll(
        float deltaSeconds,
        Container::Span<const ScriptSourceBindingView> bindings,
        ScriptSourcePollBatch& outBatch)
    {
        outBatch.Reset();
        if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f)
        {
            m_AccumulatedSeconds += deltaSeconds;
        }
        if (m_AccumulatedSeconds < PollIntervalSeconds)
        {
            return EScriptSourcePollResult::NotDue;
        }
        m_AccumulatedSeconds = std::fmod(m_AccumulatedSeconds, PollIntervalSeconds);

        constexpr ScriptContentHash offsetBasis = 14695981039346656037ull;
        constexpr ScriptContentHash prime = 1099511628211ull;
        ScriptContentHash fingerprint = offsetBasis;
        const auto hashByte = [&fingerprint](unsigned char value)
        {
            fingerprint ^= value;
            fingerprint *= prime;
        };
        const auto hashUint32 = [&hashByte](uint32_t value)
        {
            for (size_t index = 0; index < sizeof(value); ++index)
            {
                hashByte(static_cast<unsigned char>(value >> (index * 8)));
            }
        };
        const auto hashUint64 = [&hashByte](uint64_t value)
        {
            for (size_t index = 0; index < sizeof(value); ++index)
            {
                hashByte(static_cast<unsigned char>(value >> (index * 8)));
            }
        };
        const auto hashString = [&hashByte, &hashUint64](Container::StringView value)
        {
            hashUint64(static_cast<uint64_t>(value.size()));
            for (size_t index = 0; index < value.size(); ++index)
            {
                hashByte(static_cast<unsigned char>(value[index]));
            }
        };

        for (const ScriptSourceBindingView& binding : bindings)
        {
            uint32_t sourceIndex = ~uint32_t{0};
            for (uint32_t index = 0; index < static_cast<uint32_t>(outBatch.Sources.size()); ++index)
            {
                const ScriptSourceSnapshot& source = outBatch.Sources[index];
                if (source.RequestedPath.size() == binding.ScriptPath.size() &&
                    std::memcmp(source.RequestedPath.data(), binding.ScriptPath.data(), binding.ScriptPath.size()) == 0)
                {
                    sourceIndex = index;
                    break;
                }
            }
            if (sourceIndex == ~uint32_t{0})
            {
                ScriptSourceSnapshot source;
                ReadSource(binding.ScriptPath, source);
                outBatch.Sources.push_back(std::move(source));
                sourceIndex = static_cast<uint32_t>(outBatch.Sources.size() - 1);
            }

            const ScriptSourceSnapshot& source = outBatch.Sources[sourceIndex];
            const BindingRecord* record = nullptr;
            for (const BindingRecord& candidate : m_Bindings)
            {
                if (candidate.Approved.SlotIndex == binding.SlotIndex &&
                    candidate.Approved.Generation == binding.Generation)
                {
                    record = &candidate;
                    break;
                }
            }

            const Container::StringView approvedPropertyPath = record != nullptr
                ? Container::StringView(record->Approved.ApprovedPropertyPath.data(),
                    record->Approved.ApprovedPropertyPath.size())
                : Container::StringView();
            const Container::StringView approvedClassName = record != nullptr
                ? Container::StringView(record->Approved.ApprovedClassName.data(),
                    record->Approved.ApprovedClassName.size())
                : Container::StringView();
            const bool bDirty = record == nullptr ||
                approvedPropertyPath != binding.ScriptPath ||
                approvedClassName != binding.ScriptClassName ||
                record->Approved.ApprovedLogicalPath != source.LogicalPath ||
                record->Approved.ApprovedContentHash != source.ContentHash ||
                source.Result != EScriptSourceReadResult::Success;
            if (!bDirty)
            {
                continue;
            }

            ScriptSourceChange change;
            change.SlotIndex = binding.SlotIndex;
            change.Generation = binding.Generation;
            change.SourceIndex = sourceIndex;
            change.ScriptClassName.append(binding.ScriptClassName.data(), binding.ScriptClassName.size());
            outBatch.Changes.push_back(std::move(change));

            hashUint32(binding.SlotIndex);
            hashUint32(binding.Generation);
            hashString(binding.ScriptPath);
            hashString(binding.ScriptClassName);
            hashByte(static_cast<unsigned char>(source.Result));
            hashString(Container::StringView(source.LogicalPath.data(), source.LogicalPath.size()));
            hashUint64(static_cast<uint64_t>(source.Bytes.size()));
            for (size_t index = 0; index < source.Bytes.size(); ++index)
            {
                hashByte(static_cast<unsigned char>(source.Bytes[index]));
            }
        }

        if (outBatch.Changes.empty())
        {
            m_LastRejectedFingerprint = 0;
            m_bHasRejectedFingerprint = false;
            return EScriptSourcePollResult::NoChanges;
        }

        outBatch.Fingerprint = fingerprint;
        if (m_bHasRejectedFingerprint && m_LastRejectedFingerprint == fingerprint)
        {
            outBatch.Changes.clear();
            return EScriptSourcePollResult::NoChanges;
        }
        return EScriptSourcePollResult::Changes;
    }

    void ScriptSourceTracker::ReserveBindingCapacity(size_t requiredCapacity)
    {
        m_Bindings.reserve(requiredCapacity);
    }

    void ScriptSourceTracker::RegisterBinding(ScriptSourceApproval&& approval)
    {
        BindingRecord candidate;
        candidate.Approved = std::move(approval);
        for (BindingRecord& record : m_Bindings)
        {
            if (record.Approved.SlotIndex == candidate.Approved.SlotIndex &&
                record.Approved.Generation == candidate.Approved.Generation)
            {
                record = std::move(candidate);
                return;
            }
        }
        m_Bindings.push_back(std::move(candidate));
    }

    void ScriptSourceTracker::UnregisterBinding(uint32_t slotIndex, uint32_t generation)
    {
        for (auto iterator = m_Bindings.begin(); iterator != m_Bindings.end(); ++iterator)
        {
            if (iterator->Approved.SlotIndex == slotIndex && iterator->Approved.Generation == generation)
            {
                m_Bindings.erase(iterator);
                return;
            }
        }
    }

    void ScriptSourceTracker::RejectBatch(uint64_t fingerprint)
    {
        m_LastRejectedFingerprint = fingerprint;
        m_bHasRejectedFingerprint = true;
    }

    bool ScriptSourceTracker::CanApproveBatch(Container::Span<const ScriptSourceApproval> approvals) const noexcept
    {
        for (size_t approvalIndex = 0; approvalIndex < approvals.size(); ++approvalIndex)
        {
            const ScriptSourceApproval& approval = approvals[approvalIndex];
            if (approval.ApprovedPropertyPath != approval.ApprovedLogicalPath)
            {
                return false;
            }

            for (size_t duplicateIndex = 0; duplicateIndex < approvalIndex; ++duplicateIndex)
            {
                if (approvals[duplicateIndex].SlotIndex == approval.SlotIndex &&
                    approvals[duplicateIndex].PreviousGeneration == approval.PreviousGeneration)
                {
                    return false;
                }
            }

            bool bFound = false;
            for (const BindingRecord& record : m_Bindings)
            {
                if (record.Approved.SlotIndex == approval.SlotIndex &&
                    record.Approved.Generation == approval.PreviousGeneration)
                {
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                return false;
            }
        }
        return true;
    }

    void ScriptSourceTracker::ApproveBatch(Container::Span<ScriptSourceApproval> approvals) noexcept
    {
        for (ScriptSourceApproval& approval : approvals)
        {
            for (BindingRecord& record : m_Bindings)
            {
                if (record.Approved.SlotIndex == approval.SlotIndex &&
                    record.Approved.Generation == approval.PreviousGeneration)
                {
                    BindingRecord replacement;
                    replacement.Approved = std::move(approval);
                    record = std::move(replacement);
                    goto NextApproval;
                }
            }

            NORVES_LOG_ERROR("ScriptSourceTracker", "Approved binding was missing during tracker commit");
            std::abort();

        NextApproval:
            continue;
        }
    }
} // namespace NorvesLib::Core::Scripting
