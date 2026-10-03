#include "Resource/GltfImageSource.h"
#include "Resource/GltfBufferJson.h"
#include "Text/JsonDocument.h"
#include <utility>

namespace NorvesLib::Core::Gltf
{
    namespace
    {
        bool EqualsAscii(const Container::String& value, const char* expected)
        {
            size_t index = 0;
            while (index < value.size() && expected[index] != 0)
            {
                if (static_cast<uint32_t>(value[index]) != static_cast<uint8_t>(expected[index]))
                {
                    return false;
                }
                ++index;
            }
            return index == value.size() && expected[index] == 0;
        }
        size_t CountField(const JsonValue& object, const char* name)
        {
            size_t count = 0;
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                if (EqualsAscii(object.GetMemberName(index), name))
                {
                    ++count;
                }
            }
            return count;
        }
        bool ReadSize(const JsonValue& object, const char* name, bool bOptional, bool bAllowZero, size_t& out)
        {
            const size_t count = CountField(object, name);
            if (count == 0 && bOptional)
            {
                out = 0;
                return true;
            }
            const auto value = object.FindMember(name);
            if (count != 1 || !value.IsNumber())
            {
                return false;
            }
            const double number = value.AsNumber();
            if (number == 0 && bAllowZero)
            {
                out = 0;
                return true;
            }
            const auto size = ParseBufferByteLength(number);
            out = size.Value;
            return size.bValid;
        }
        bool ReadAscii(const JsonValue& value, Container::VariableArray<uint8_t>& bytes)
        {
            if (!value.IsString())
            {
                return false;
            }
            const auto& text = value.AsString();
            bytes.resize(text.size());
            for (size_t index = 0; index < text.size(); ++index)
            {
                const uint32_t character = static_cast<uint32_t>(text[index]);
                if (character > 0x7f)
                {
                    return false;
                }
                bytes[index] = static_cast<uint8_t>(character);
            }
            return true;
        }
        DataUriMime ReadMime(const JsonValue& value)
        {
            if (!value.IsString())
            {
                return DataUriMime::Unknown;
            }
            const auto& text = value.AsString();
            if (EqualsAscii(text, "image/png"))
            {
                return DataUriMime::Png;
            }
            return EqualsAscii(text, "image/jpeg") ? DataUriMime::Jpeg : DataUriMime::Unknown;
        }
    } // namespace

    ImageSource& ImageSource::operator=(const ImageSource& other)
    {
        if (this != &other)
        {
            ImageSource candidate(other);
            Swap(candidate);
        }
        return *this;
    }
    void ImageSource::Reset() noexcept
    {
        ImageSource empty;
        Swap(empty);
    }
    void ImageSource::Swap(ImageSource& other) noexcept
    {
        std::swap(m_Kind, other.m_Kind);
        std::swap(m_Mime, other.m_Mime);
        std::swap(m_BufferIndex, other.m_BufferIndex);
        std::swap(m_Offset, other.m_Offset);
        std::swap(m_Length, other.m_Length);
        m_ExternalUri.swap(other.m_ExternalUri);
        m_OwnedBytes.swap(other.m_OwnedBytes);
    }
    ImageSourceKind ImageSource::GetKind() const noexcept
    {
        return m_Kind;
    }
    DataUriMime ImageSource::GetMime() const noexcept
    {
        return m_Mime;
    }
    Container::Span<const uint8_t> ImageSource::GetExternalUri() const noexcept
    {
        return {m_ExternalUri.data(), m_ExternalUri.size()};
    }
    Container::Span<const uint8_t> ImageSource::GetBytes(const BufferSet& buffers) const noexcept
    {
        if (m_Kind == ImageSourceKind::DataUri)
        {
            return {m_OwnedBytes.data(), m_OwnedBytes.size()};
        }
        if (m_Kind == ImageSourceKind::BufferView)
        {
            return BindImageByteRange(buffers.GetBytes(m_BufferIndex), m_Offset, m_Length).Bytes;
        }
        return {};
    }
    ImageSourceResult ImageSource::Resolve(const JsonValue& root, size_t imageIndex,
        const BufferSet& buffers, ImageSource& outSource)
    {
        auto fail = [&](ImageSourceResult result)
        {
            outSource.Reset();
            return result;
        };
        try
        {
            if (!root.IsObject() || CountField(root, "images") != 1)
            {
                return fail(ImageSourceResult::InvalidDescriptor);
            }
            const auto images = root.FindMember("images");
            if (!images.IsArray() || imageIndex >= images.GetArraySize())
            {
                return fail(ImageSourceResult::InvalidDescriptor);
            }
            const auto image = images.GetArrayElement(imageIndex);
            const size_t uriCount = CountField(image, "uri");
            const size_t viewCount = CountField(image, "bufferView");
            const size_t mimeCount = CountField(image, "mimeType");
            if (!image.IsObject() || !((uriCount == 1 && viewCount == 0) || (uriCount == 0 && viewCount == 1)) || mimeCount > 1)
            {
                return fail(ImageSourceResult::InvalidDescriptor);
            }
            ImageSource candidate;
            if (mimeCount != 0)
            {
                candidate.m_Mime = ReadMime(image.FindMember("mimeType"));
                if (candidate.m_Mime == DataUriMime::Unknown)
                {
                    return fail(ImageSourceResult::InvalidMime);
                }
            }
            if (viewCount == 1)
            {
                size_t viewIndex = 0;
                if (candidate.m_Mime == DataUriMime::Unknown ||
                    !ReadSize(image, "bufferView", false, true, viewIndex) || CountField(root, "bufferViews") != 1)
                {
                    return fail(ImageSourceResult::InvalidBufferView);
                }
                const auto views = root.FindMember("bufferViews");
                if (!views.IsArray() || viewIndex >= views.GetArraySize())
                {
                    return fail(ImageSourceResult::InvalidBufferView);
                }
                const auto view = views.GetArrayElement(viewIndex);
                // byteStrideは頂点属性専用で、画像viewには指定できない。
                if (!view.IsObject() || CountField(view, "byteStride") != 0 ||
                    !ReadSize(view, "buffer", false, true, candidate.m_BufferIndex) ||
                    !ReadSize(view, "byteOffset", true, true, candidate.m_Offset) ||
                    !ReadSize(view, "byteLength", false, false, candidate.m_Length) ||
                    !BindImageByteRange(buffers.GetBytes(candidate.m_BufferIndex), candidate.m_Offset, candidate.m_Length).bValid)
                {
                    return fail(ImageSourceResult::InvalidBufferView);
                }
                candidate.m_Kind = ImageSourceKind::BufferView;
            }
            else
            {
                Container::VariableArray<uint8_t> uri;
                if (!ReadAscii(image.FindMember("uri"), uri) || uri.empty())
                {
                    return fail(ImageSourceResult::InvalidUri);
                }
                const Container::Span<const uint8_t> uriSpan(uri.data(), uri.size());
                const auto parsed = ParseDataUri(uriSpan);
                if (parsed.Result == BufferSourceResult::Success)
                {
                    if ((parsed.View.Mime != DataUriMime::Png && parsed.View.Mime != DataUriMime::Jpeg) ||
                        (candidate.m_Mime != DataUriMime::Unknown && candidate.m_Mime != parsed.View.Mime))
                    {
                        return fail(ImageSourceResult::InvalidMime);
                    }
                    Container::VariableArray<uint8_t> encoded(parsed.View.PercentDecodedSize);
                    if (DecodePercentBytes(parsed.View.EncodedPayload, {encoded.data(), encoded.size()}).Result != BufferSourceResult::Success)
                    {
                        return fail(ImageSourceResult::InvalidEmbeddedData);
                    }
                    const Container::Span<const uint8_t> encodedSpan(encoded.data(), encoded.size());
                    const auto size = Text::GetBase64DecodedSize(encodedSpan);
                    if (size.Result != Text::Base64DecodeResult::Success || size.Size == 0)
                    {
                        return fail(ImageSourceResult::InvalidEmbeddedData);
                    }
                    candidate.m_OwnedBytes.resize(size.Size);
                    if (Text::DecodeBase64(encodedSpan, {candidate.m_OwnedBytes.data(), candidate.m_OwnedBytes.size()}).Result != Text::Base64DecodeResult::Success)
                    {
                        return fail(ImageSourceResult::InvalidEmbeddedData);
                    }
                    candidate.m_Kind = ImageSourceKind::DataUri;
                    candidate.m_Mime = parsed.View.Mime;
                }
                else if (parsed.Result == BufferSourceResult::NotDataUri)
                {
                    for (const uint8_t character : uri)
                    {
                        if (character == '?' || character == '#')
                        {
                            return fail(ImageSourceResult::InvalidUri);
                        }
                    }
                    const auto size = GetPercentDecodedSize(uriSpan);
                    if (size.Result != BufferSourceResult::Success)
                    {
                        return fail(ImageSourceResult::InvalidUri);
                    }
                    candidate.m_ExternalUri.resize(size.Size);
                    if (DecodePercentBytes(uriSpan, {candidate.m_ExternalUri.data(), candidate.m_ExternalUri.size()}).Result != BufferSourceResult::Success ||
                        !IsValidRelativeBufferUri({candidate.m_ExternalUri.data(), candidate.m_ExternalUri.size()}))
                    {
                        return fail(ImageSourceResult::InvalidUri);
                    }
                    candidate.m_Kind = ImageSourceKind::ExternalFile;
                }
                else
                {
                    return fail(ImageSourceResult::InvalidEmbeddedData);
                }
            }
            outSource.Swap(candidate);
            return ImageSourceResult::Success;
        }
        catch (...)
        {
            outSource.Reset();
            throw;
        }
    }
} // namespace NorvesLib::Core::Gltf
