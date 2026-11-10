#include "Rendering/RenderExtension/RenderExtensionData.h"
#include "Logging/LogMacros.h"
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    RenderExtensionBlob::RenderExtensionBlob(Identity name, uint64_t layoutHash, uint32_t elementSize, uint32_t count,
                                             uint64_t revision, const void* data)
        : Name(name), LayoutHash(layoutHash), ElementSize(elementSize), Count(count), Revision(revision)
    {
        const size_t sizeBytes = GetSizeBytes();
        if (sizeBytes != 0)
        {
            const size_t blockCount = sizeBytes / sizeof(AlignedBlock) + (sizeBytes % sizeof(AlignedBlock) != 0);
            m_Data.resize(blockCount);
            std::memcpy(m_Data.data(), data, sizeBytes);
        }
    }

    const void* RenderExtensionBlob::GetData() const
    {
        return m_Data.empty() ? nullptr : m_Data.data();
    }

    size_t RenderExtensionBlob::GetSizeBytes() const
    {
        return static_cast<size_t>(ElementSize) * static_cast<size_t>(Count);
    }

    void RenderExtensionDataStore::SetBytes(
        Identity name, uint64_t layoutHash, uint32_t elementSize, uint32_t count, const void* data)
    {
        if (elementSize != 0 && static_cast<size_t>(count) > static_cast<size_t>(-1) / elementSize)
        {
            NORVES_LOG_WARNING("Rendering", "拡張データのバイト数が大きすぎます。");
            return;
        }

        const size_t sizeBytes = static_cast<size_t>(elementSize) * static_cast<size_t>(count);
        if (sizeBytes != 0 && data == nullptr)
        {
            NORVES_LOG_WARNING("Rendering", "拡張データのアドレスが指定されていません。");
            return;
        }

        Container::TSharedPtr<const RenderExtensionBlob>* destination = nullptr;
        for (auto& existing : m_Data)
        {
            if (existing->Name != name)
            {
                continue;
            }

            if (existing->LayoutHash == layoutHash && existing->ElementSize == elementSize &&
                existing->Count == count &&
                (sizeBytes == 0 || std::memcmp(existing->GetData(), data, sizeBytes) == 0))
            {
                return;
            }

            destination = &existing;
            break;
        }

        Container::TSharedPtr<const RenderExtensionBlob> blob =
            Container::MakeShared<RenderExtensionBlob>(name, layoutHash, elementSize, count, m_NextRevision, data);
        if (destination != nullptr)
        {
            *destination = blob;
        }
        else
        {
            m_Data.push_back(blob);
        }
        ++m_NextRevision;
    }

    void RenderExtensionDataStore::Remove(Identity name)
    {
        for (auto it = m_Data.begin(); it != m_Data.end(); ++it)
        {
            if ((*it)->Name == name)
            {
                m_Data.erase(it);
                return;
            }
        }
    }

    const RenderExtensionBlob* RenderExtensionDataStore::Find(Identity name) const
    {
        for (const auto& blob : m_Data)
        {
            if (blob->Name == name)
            {
                return blob.get();
            }
        }
        return nullptr;
    }

    uint32_t RenderExtensionDataStore::GetCount() const
    {
        return static_cast<uint32_t>(m_Data.size());
    }

    void RenderExtensionDataStore::SnapshotTo(RenderExtensionDataSet& out) const
    {
        out.clear();
        out.insert(out.end(), m_Data.begin(), m_Data.end());
    }

    const RenderExtensionBlob* FindRenderExtensionBlob(
        const RenderExtensionDataSet& set, Identity name, uint64_t layoutHash)
    {
        for (const auto& blob : set)
        {
            if (!blob || blob->Name != name)
            {
                continue;
            }
            if (blob->LayoutHash != layoutHash)
            {
#if NORVES_ENABLE_LOGGING
                if (!blob->m_bLayoutMismatchLogged.Exchange(true))
                {
                    NORVES_LOG_WARNING("Rendering", "拡張データのレイアウトが一致しません（名前のハッシュ: 0x%llx）。",
                                       static_cast<unsigned long long>(name.GetHash()));
                }
#endif
                return nullptr;
            }
            return blob.get();
        }
        return nullptr;
    }
} // namespace NorvesLib::Core::Rendering
