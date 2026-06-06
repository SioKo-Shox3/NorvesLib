#include "Asset/AssetBlob.h"
#include <algorithm>
#include <utility>

namespace NorvesLib::Core::Asset
{
    AssetBlob AssetBlob::Invalid()
    {
        return AssetBlob{};
    }

    AssetBlob AssetBlob::CopyBytes(ByteSpan bytes, const Container::AnsiString &sourcePath)
    {
        if (bytes.size() > 0 && bytes.data() == nullptr)
        {
            return Invalid();
        }

        auto ownedBytes = Container::MakeShared<ByteArray>();
        ownedBytes->resize(bytes.size());

        if (!bytes.empty())
        {
            std::copy(bytes.begin(), bytes.end(), ownedBytes->begin());
        }

        return FromOwnedBytes(ownedBytes, sourcePath);
    }

    AssetBlob AssetBlob::FromOwnedBytes(const Container::TSharedPtr<const ByteArray> &bytes,
                                        const Container::AnsiString &sourcePath)
    {
        if (!bytes)
        {
            return Invalid();
        }

        return FromOwnedRange(bytes, 0, bytes->size(), sourcePath);
    }

    AssetBlob AssetBlob::FromOwnedRange(const Container::TSharedPtr<const ByteArray> &owner,
                                        size_t offset,
                                        size_t size,
                                        const Container::AnsiString &sourcePath)
    {
        if (!owner)
        {
            return Invalid();
        }

        const size_t ownerSize = owner->size();
        if (offset > ownerSize || size > ownerSize - offset)
        {
            return Invalid();
        }

        const uint8_t *data = nullptr;
        if (ownerSize > 0)
        {
            data = owner->data() + offset;
        }

        return CreateWithOwner(owner, data, size, offset == 0 && size == ownerSize ? StorageKind::OwnedBytes : StorageKind::OwnedRange, sourcePath);
    }

    AssetBlob::ByteSpan AssetBlob::GetSpan() const noexcept
    {
        if (!m_bValid)
        {
            return ByteSpan{};
        }

        return ByteSpan{m_Data, m_Size};
    }

    AssetBlob AssetBlob::TryCreateSubBlob(size_t offset, size_t size) const
    {
        if (!m_bValid || offset > m_Size || size > m_Size - offset)
        {
            return Invalid();
        }

        const uint8_t *data = nullptr;
        if (m_Size > 0)
        {
            data = m_Data + offset;
        }

        return CreateWithOwner(m_LifetimeOwner, data, size, StorageKind::OwnedRange, m_SourcePath);
    }

    AssetBlob AssetBlob::CreateWithOwner(Container::TSharedPtr<const void> owner,
                                         const uint8_t *data,
                                         size_t size,
                                         StorageKind storageKind,
                                         const Container::AnsiString &sourcePath)
    {
        if (!owner)
        {
            return Invalid();
        }

        if (size > 0 && data == nullptr)
        {
            return Invalid();
        }

        AssetBlob blob;
        blob.m_LifetimeOwner = std::move(owner);
        blob.m_Data = data;
        blob.m_Size = size;
        blob.m_StorageKind = storageKind;
        blob.m_bValid = true;
        blob.m_SourcePath = sourcePath;
        return blob;
    }
}
