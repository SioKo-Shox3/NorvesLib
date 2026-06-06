#pragma once

#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    /**
     * @brief Immutable asset byte storage with explicit lifetime ownership.
     *
     * AssetBlob is the value passed across worker queues instead of a raw pointer or Span.
     * GetSpan() is a non-owning view and is valid only while the AssetBlob instance, or
     * another copy sharing the same owner, remains alive.
     */
    class AssetBlob
    {
    public:
        using ByteArray = Container::VariableArray<uint8_t>;
        using ByteSpan = Container::Span<const uint8_t>;

        enum class StorageKind : uint8_t
        {
            Invalid,
            OwnedBytes,
            OwnedRange
        };

        AssetBlob() = default;

        static AssetBlob Invalid();
        static AssetBlob CopyBytes(ByteSpan bytes, const Container::AnsiString &sourcePath = {});
        static AssetBlob FromOwnedBytes(const Container::TSharedPtr<const ByteArray> &bytes, const Container::AnsiString &sourcePath = {});
        static AssetBlob FromOwnedRange(const Container::TSharedPtr<const ByteArray> &owner,
                                        size_t offset,
                                        size_t size,
                                        const Container::AnsiString &sourcePath = {});

        [[nodiscard]] bool IsValid() const noexcept { return m_bValid; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_Size == 0; }
        [[nodiscard]] const uint8_t *GetData() const noexcept { return m_bValid ? m_Data : nullptr; }
        [[nodiscard]] size_t GetSize() const noexcept { return m_bValid ? m_Size : 0; }
        [[nodiscard]] ByteSpan GetSpan() const noexcept;
        [[nodiscard]] StorageKind GetStorageKind() const noexcept { return m_StorageKind; }
        [[nodiscard]] const Container::AnsiString &GetSourcePath() const noexcept { return m_SourcePath; }

        [[nodiscard]] AssetBlob TryCreateSubBlob(size_t offset, size_t size) const;

    private:
        static AssetBlob CreateWithOwner(Container::TSharedPtr<const void> owner,
                                         const uint8_t *data,
                                         size_t size,
                                         StorageKind storageKind,
                                         const Container::AnsiString &sourcePath);

        Container::TSharedPtr<const void> m_LifetimeOwner;
        const uint8_t *m_Data = nullptr;
        size_t m_Size = 0;
        StorageKind m_StorageKind = StorageKind::Invalid;
        bool m_bValid = false;
        Container::AnsiString m_SourcePath;
    };
}
