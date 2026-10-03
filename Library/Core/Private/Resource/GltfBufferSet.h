#pragma once

#include "Resource/GltfBufferSource.h"
#include "Text/Base64.h"
#include "Container/VariableArray.h"
#include <limits>

namespace NorvesLib::Core::Gltf
{
    enum class BufferStorageKind : uint8_t { Unknown, ExternalFile, DataUri, GlbBin };
    enum class ExternalBufferReadResult : uint8_t
    {
        Success, InvalidPath, SourcePathError, OutsideDirectory, InvalidFileType, OpenFailed, InvalidSize, ReadFailed
    };
    enum class BufferResolveResult : uint8_t
    {
        Success, InvalidArgument, InvalidDescriptor, InvalidUri, InvalidEmbeddedData,
        UnsupportedDataMime, ReaderUnavailable, ExternalReadFailure, SourceTooShort
    };
    struct BufferRequest
    {
        size_t ByteLength = 0;
        bool bHasUri = false;
        Container::Span<const uint8_t> Uri;
    };
    struct BufferResolveOutcome
    {
        BufferResolveResult Result = BufferResolveResult::InvalidArgument;
        size_t BufferIndex = std::numeric_limits<size_t>::max();
        BufferSourceResult SourceError = BufferSourceResult::Success;
        Text::Base64DecodeResult Base64Error = Text::Base64DecodeResult::Success;
        ExternalBufferReadResult ReadError = ExternalBufferReadResult::Success;
    };
    // 同期callback。decodedUriは検証済み相対ASCII pathで、呼出中のみ借用する。
    // readerはsource directoryの実filesystem境界を守り、request/container/outSetを変更しない。
    using ExternalBufferReader = ExternalBufferReadResult (*)(Container::Span<const uint8_t> decodedUri,
        Container::VariableArray<uint8_t>& bytes, void* context);
    [[nodiscard]] bool IsValidRelativeBufferUri(Container::Span<const uint8_t> decodedUri) noexcept;

    class BufferSet
    {
    public:
        BufferSet() = default;
        BufferSet(const BufferSet&) = default;
        BufferSet(BufferSet&&) noexcept = default;
        BufferSet& operator=(const BufferSet& other);
        BufferSet& operator=(BufferSet&&) noexcept = default;
        // requests/GLBを借用して解決する。GLB入力はこのsetと借用viewより長く生存すること。
        // 失敗/例外ではoutSetを空にする。確保例外は伝播。reader中の再入/入力変更は非対応。
        [[nodiscard]] static BufferResolveOutcome Resolve(Container::Span<const BufferRequest> requests,
            const ContainerView& container, ExternalBufferReader reader, void* context, BufferSet& outSet);
        void Reset() noexcept;
        void Swap(BufferSet& other) noexcept;
        size_t GetCount() const noexcept;
        BufferStorageKind GetSourceKind(size_t index) const noexcept;
        size_t GetDeclaredByteLength(size_t index) const noexcept;
        // accessorには宣言範囲だけを渡す。範囲外indexは空view。
        Container::Span<const uint8_t> GetBytes(size_t index) const noexcept;
        // 外部hash用は余剰を含む実ファイル全体。data URIは復号全体、BINは宣言範囲。
        Container::Span<const uint8_t> GetSourceBytes(size_t index) const noexcept;
    private:
        struct Entry
        {
            BufferStorageKind Kind = BufferStorageKind::Unknown;
            size_t ByteLength = 0;
            Container::VariableArray<uint8_t> OwnedBytes;
            // 所有配列自身を指すSpanは保存せず、GLBだけを借用する。
            Container::Span<const uint8_t> BorrowedBin;
        };
        Container::VariableArray<Entry> m_Entries;
    };
}
