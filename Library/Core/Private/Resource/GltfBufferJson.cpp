#include "Resource/GltfBufferJson.h"
#include "Text/JsonDocument.h"

namespace NorvesLib::Core::Gltf
{
    namespace
    {
        bool EqualsAscii(const Container::String& value, const char* name)
        {
            size_t index = 0;
            while (index < value.size() && name[index] != 0)
            {
                if (static_cast<uint32_t>(value[index]) != static_cast<uint8_t>(name[index]))
                {
                    return false;
                }
                ++index;
            }
            return index == value.size() && name[index] == 0;
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
    }

    BufferResolveOutcome ResolveJsonBuffers(const JsonValue& root, const ContainerView& container,
        ExternalBufferReader reader, void* context, BufferSet& outSet)
    {
        auto fail = [&](size_t index = std::numeric_limits<size_t>::max())
        {
            outSet.Reset();
            return BufferResolveOutcome{BufferResolveResult::InvalidDescriptor,index};
        };
        try
        {
            if (!root.IsObject() || CountField(root,"buffers") != 1)
            {
                return fail();
            }
            const auto buffers = root.FindMember("buffers");
            if (!buffers.IsArray() || buffers.GetArraySize() == 0)
            {
                return fail();
            }
            const size_t count = buffers.GetArraySize();
            // 外側配列を先に確定し、各URI確保後にだけSpanを作る。以後はstorageを動かさない。
            Container::VariableArray<Container::VariableArray<uint8_t>> uriBytes(count);
            Container::VariableArray<BufferRequest> requests(count);
            for (size_t index = 0; index < count; ++index)
            {
                const auto object = buffers.GetArrayElement(index);
                if (!object.IsObject() || CountField(object,"byteLength") != 1 || CountField(object,"uri") > 1)
                {
                    return fail(index);
                }
                const auto lengthValue = object.FindMember("byteLength");
                if (!lengthValue.IsNumber())
                {
                    return fail(index);
                }
                const auto length = ParseBufferByteLength(lengthValue.AsNumber());
                if (!length.bValid)
                {
                    return fail(index);
                }
                auto& request = requests[index];
                request.ByteLength = length.Value;
                const auto uri = object.FindMember("uri");
                request.bHasUri = uri.IsValid();
                if (request.bHasUri)
                {
                    if (!uri.IsString())
                    {
                        return fail(index);
                    }
                    const auto& text = uri.AsString();
                    auto& owned = uriBytes[index];
                    owned.resize(text.size());
                    for (size_t character = 0; character < text.size(); ++character)
                    {
                        const uint32_t value = static_cast<uint32_t>(text[character]);
                        if (value > 0x7f)
                        {
                            return fail(index);
                        }
                        owned[character] = static_cast<uint8_t>(value);
                    }
                    request.Uri = {owned.data(),owned.size()};
                }
            }
            return BufferSet::Resolve({requests.data(),requests.size()},container,reader,context,outSet);
        }
        catch (...)
        {
            outSet.Reset();
            throw;
        }
    }
} // namespace NorvesLib::Core::Gltf
