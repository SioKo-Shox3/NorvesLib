#pragma once

#include "Resource/GltfBufferSet.h"

namespace NorvesLib::Core
{
    class JsonValue;
}

namespace NorvesLib::Core::Gltf
{
    enum class ImageSourceKind : uint8_t { Unknown, ExternalFile, DataUri, BufferView };
    enum class ImageSourceResult : uint8_t
    {
        Success, InvalidDescriptor, InvalidMime, InvalidUri, InvalidEmbeddedData, InvalidBufferView
    };
    struct ImageByteRangeOutcome
    {
        bool bValid = false;
        Container::Span<const uint8_t> Bytes;
    };
    // bufferの宣言範囲内だけを参照する。lengthは正、加算前の減算で越境を判定する。
    [[nodiscard]] inline ImageByteRangeOutcome BindImageByteRange(Container::Span<const uint8_t> buffer,
        size_t offset, size_t length) noexcept
    {
        if (buffer.data() == nullptr || offset > buffer.size() || length == 0 || length > buffer.size() - offset)
        {
            return {};
        }
        return {true, {buffer.data() + offset, length}};
    }

    class ImageSource
    {
    public:
        ImageSource() = default;
        ImageSource(const ImageSource&) = default;
        ImageSource(ImageSource&&) noexcept = default;
        ImageSource& operator=(const ImageSource& other);
        ImageSource& operator=(ImageSource&&) noexcept = default;
        // file URIは解決だけでI/Oしない。data URIは所有、bufferViewはindexと範囲だけを保持する。
        // root/buffersはこの呼出中に不変。失敗/例外はoutSourceを空にし、確保例外は伝播する。
        [[nodiscard]] static ImageSourceResult Resolve(const JsonValue& root, size_t imageIndex,
            const BufferSet& buffers, ImageSource& outSource);
        void Reset() noexcept;
        void Swap(ImageSource& other) noexcept;
        ImageSourceKind GetKind() const noexcept;
        DataUriMime GetMime() const noexcept;
        // bufferView以外は無効indexを返す。
        size_t GetBufferIndex() const noexcept;
        Container::Span<const uint8_t> GetExternalUri() const noexcept;
        // bufferViewはResolveに渡した同じ内容のbuffersが必要。返却viewより長く保持すること。
        // data URIの返却viewはこのImageSourceへ借用し、file URIでは空を返す。
        Container::Span<const uint8_t> GetBytes(const BufferSet& buffers) const noexcept;
    private:
        ImageSourceKind m_Kind = ImageSourceKind::Unknown;
        DataUriMime m_Mime = DataUriMime::Unknown;
        size_t m_BufferIndex = 0;
        size_t m_Offset = 0;
        size_t m_Length = 0;
        Container::VariableArray<uint8_t> m_ExternalUri;
        Container::VariableArray<uint8_t> m_OwnedBytes;
    };
} // namespace NorvesLib::Core::Gltf
