#include "Resource/GltfBufferSet.h"
#include <utility>

namespace NorvesLib::Core::Gltf
{
    bool IsValidRelativeBufferUri(Container::Span<const uint8_t> uri) noexcept
    {
        if (uri.empty() || uri.data() == nullptr || uri[0] == '/')
        {
            return false;
        }
        size_t start = 0;
        for (size_t index = 0; index <= uri.size(); ++index)
        {
            if (index == uri.size() || uri[index] == '/')
            {
                const size_t length = index-start;
                if (length == 0 || uri[index-1] == '.' || uri[index-1] == ' ' ||
                    (length == 1 && uri[start] == '.') ||
                    (length == 2 && uri[start] == '.' && uri[start+1] == '.'))
                {
                    return false;
                }
                if (index == uri.size())
                {
                    break;
                }
                start = index+1;
            }
            else if (uri[index] < 0x20 || uri[index] > 0x7e || uri[index] == '\\' || uri[index] == ':')
            {
                return false;
            }
        }
        return true;
    }

    BufferResolveOutcome BufferSet::Resolve(Container::Span<const BufferRequest> requests,
        const ContainerView& container, ExternalBufferReader reader, void* context, BufferSet& outSet)
    {
        auto fail = [&](BufferResolveOutcome result)
        {
            outSet.Reset();
            return result;
        };
        if (requests.empty() || requests.data() == nullptr)
        {
            return fail({BufferResolveResult::InvalidArgument});
        }
        try
        {
            BufferSet candidate;
            candidate.m_Entries.reserve(requests.size());
            for (size_t index = 0; index < requests.size(); ++index)
            {
                const auto& request = requests[index];
                if (request.ByteLength == 0 || (!request.Uri.empty() && request.Uri.data() == nullptr) ||
                    (!request.bHasUri && !request.Uri.empty()))
                {
                    return fail({BufferResolveResult::InvalidDescriptor,index});
                }
                Entry entry;
                entry.ByteLength = request.ByteLength;
                if (!request.bHasUri)
                {
                    const auto bound = BindGlbBuffer(container,index,request.ByteLength);
                    if (bound.Result != BufferSourceResult::Success)
                    {
                        return fail({BufferResolveResult::InvalidEmbeddedData,index,bound.Result});
                    }
                    entry.Kind = BufferStorageKind::GlbBin;
                    entry.BorrowedBin = bound.Bytes;
                }
                else
                {
                    const auto uri = ParseDataUri(request.Uri);
                    if (uri.Result == BufferSourceResult::Success)
                    {
                        if (uri.View.Mime != DataUriMime::OctetStream && uri.View.Mime != DataUriMime::GltfBuffer)
                        {
                            return fail({BufferResolveResult::UnsupportedDataMime,index});
                        }
                        Container::VariableArray<uint8_t> base64(uri.View.PercentDecodedSize);
                        const auto escaped = DecodePercentBytes(uri.View.EncodedPayload,{base64.data(),base64.size()});
                        if (escaped.Result != BufferSourceResult::Success)
                        {
                            return fail({BufferResolveResult::InvalidEmbeddedData,index,escaped.Result});
                        }
                        const Container::Span<const uint8_t> encoded(base64.data(),base64.size());
                        const auto size = Text::GetBase64DecodedSize(encoded);
                        if (size.Result != Text::Base64DecodeResult::Success)
                        {
                            return fail({BufferResolveResult::InvalidEmbeddedData,index,BufferSourceResult::Success,size.Result});
                        }
                        entry.OwnedBytes.resize(size.Size);
                        const auto decoded = Text::DecodeBase64(encoded,{entry.OwnedBytes.data(),entry.OwnedBytes.size()});
                        if (decoded.Result != Text::Base64DecodeResult::Success)
                        {
                            return fail({BufferResolveResult::InvalidEmbeddedData,index,BufferSourceResult::Success,decoded.Result});
                        }
                        entry.Kind = BufferStorageKind::DataUri;
                    }
                    else if (uri.Result == BufferSourceResult::NotDataUri)
                    {
                        // query/fragmentをファイル名として扱わない。percent表記は復号後に検証する。
                        for (const uint8_t byte : request.Uri)
                        {
                            if (byte == '?' || byte == '#')
                            {
                                return fail({BufferResolveResult::InvalidUri,index});
                            }
                        }
                        const auto size = GetPercentDecodedSize(request.Uri);
                        if (size.Result != BufferSourceResult::Success)
                        {
                            return fail({BufferResolveResult::InvalidUri,index,size.Result});
                        }
                        Container::VariableArray<uint8_t> decodedUri(size.Size);
                        const auto decoded = DecodePercentBytes(request.Uri,{decodedUri.data(),decodedUri.size()});
                        if (decoded.Result != BufferSourceResult::Success ||
                            !IsValidRelativeBufferUri({decodedUri.data(),decodedUri.size()}))
                        {
                            return fail({BufferResolveResult::InvalidUri,index,decoded.Result});
                        }
                        if (!reader)
                        {
                            return fail({BufferResolveResult::ReaderUnavailable,index});
                        }
                        const auto read = reader({decodedUri.data(),decodedUri.size()},entry.OwnedBytes,context);
                        if (read != ExternalBufferReadResult::Success)
                        {
                            return fail({BufferResolveResult::ExternalReadFailure,index,BufferSourceResult::Success,
                                Text::Base64DecodeResult::Success,read});
                        }
                        entry.Kind = BufferStorageKind::ExternalFile;
                    }
                    else
                    {
                        return fail({BufferResolveResult::InvalidUri,index,uri.Result});
                    }
                    if (entry.OwnedBytes.size() < entry.ByteLength)
                    {
                        return fail({BufferResolveResult::SourceTooShort,index});
                    }
                }
                candidate.m_Entries.push_back(std::move(entry));
            }
            outSet.Swap(candidate);
            return {BufferResolveResult::Success};
        }
        catch (...)
        {
            outSet.Reset();
            throw;
        }
    }

    BufferSet& BufferSet::operator=(const BufferSet& other)
    {
        if (this != &other)
        {
            // 内側の所有bytesのcopyが失敗しても、旧metadataとbytesを一緒に保持する。
            BufferSet candidate(other);
            Swap(candidate);
        }
        return *this;
    }

    void BufferSet::Reset() noexcept
    {
        m_Entries.clear();
    }
    void BufferSet::Swap(BufferSet& other) noexcept
    {
        m_Entries.swap(other.m_Entries);
    }
    size_t BufferSet::GetCount() const noexcept
    {
        return m_Entries.size();
    }
    BufferStorageKind BufferSet::GetSourceKind(size_t index) const noexcept
    {
        return index < m_Entries.size() ? m_Entries[index].Kind : BufferStorageKind::Unknown;
    }
    size_t BufferSet::GetDeclaredByteLength(size_t index) const noexcept
    {
        return index < m_Entries.size() ? m_Entries[index].ByteLength : 0;
    }
    Container::Span<const uint8_t> BufferSet::GetSourceBytes(size_t index) const noexcept
    {
        if (index >= m_Entries.size())
        {
            return {};
        }
        const auto& entry = m_Entries[index];
        return entry.Kind == BufferStorageKind::GlbBin ? entry.BorrowedBin
            : Container::Span<const uint8_t>(entry.OwnedBytes.data(),entry.OwnedBytes.size());
    }
    Container::Span<const uint8_t> BufferSet::GetBytes(size_t index) const noexcept
    {
        const auto source = GetSourceBytes(index);
        return index < m_Entries.size() && source.size() >= m_Entries[index].ByteLength
            ? Container::Span<const uint8_t>(source.data(),m_Entries[index].ByteLength)
            : Container::Span<const uint8_t>{};
    }
}
