#include "Resource/GltfImageSource.h"
#include "Text/JsonDocument.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <utility>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Gltf;

namespace
{
    Container::String ToString(const char* text)
    {
        Container::String result;
        while (*text)
        {
            result.push_back(static_cast<Container::String::value_type>(static_cast<uint8_t>(*text++)));
        }
        return result;
    }
    ImageSourceResult Resolve(const char* text, const BufferSet& buffers, ImageSource& source, size_t index = 0)
    {
        JsonDocument document;
        Container::String error;
        assert(JsonDocument::TryParse(ToString(text), document, &error));
        const auto result = ImageSource::Resolve(document.GetRoot(), index, buffers, source);
        document.Reset();
        return result;
    }
    ExternalBufferReadResult Read(Container::Span<const uint8_t>, Container::VariableArray<uint8_t>& bytes, void*)
    {
        bytes.assign({10, 20, 30, 40, 50, 60});
        return ExternalBufferReadResult::Success;
    }
} // namespace

int main()
{
    static_assert(std::is_nothrow_move_constructible_v<ImageSource>);
    static_assert(std::is_nothrow_move_assignable_v<ImageSource>);
    const uint8_t bin[] = {1, 2, 3, 4, 5, 6, 7, 8};
    ContainerView container;
    container.IsGlb = container.HasBin = true;
    container.Bin = bin;
    BufferRequest requests[] = {{8, false, {}}, {4, true, {reinterpret_cast<const uint8_t*>("a.bin"), 5}}};
    BufferSet buffers;
    assert(BufferSet::Resolve(requests, container, Read, nullptr, buffers).Result == BufferResolveResult::Success);
    ImageSource source;
    assert(Resolve(R"({"images":[{"uri":"textures/a%20b.png"}]})", buffers, source) == ImageSourceResult::Success);
    assert(source.GetKind() == ImageSourceKind::ExternalFile && source.GetMime() == DataUriMime::Unknown);
    assert(source.GetExternalUri().size() == 16);
    assert(std::memcmp(source.GetExternalUri().data(), "textures/a b.png", 16) == 0);
    assert(source.GetBytes(buffers).empty());
    assert(source.GetBufferIndex() == std::numeric_limits<size_t>::max());
    ImageSource externalCopy = source;
    ImageSource externalAssigned;
    externalAssigned = source;
    assert(externalCopy.GetExternalUri().data() != source.GetExternalUri().data());
    source.Reset();
    ImageSource externalMoved = std::move(externalCopy);
    source = std::move(externalAssigned);
    assert(externalMoved.GetExternalUri().size() == 16 && source.GetExternalUri().size() == 16);
    assert(std::memcmp(externalMoved.GetExternalUri().data(), "textures/a b.png", 16) == 0);
    assert(std::memcmp(source.GetExternalUri().data(), "textures/a b.png", 16) == 0);
    assert(Resolve(R"({"images":[{"uri":"a.jpg","mimeType":"image/jpeg"}]})", buffers, source) == ImageSourceResult::Success);
    assert(source.GetMime() == DataUriMime::Jpeg);

    // ここでは画像ファイルの完全性ではなく、記述とbytesの解決だけを確認する。
    const char* png = R"({"images":[{"uri":"data:image/png;base64,iVBORw0KGgo="}]})";
    assert(Resolve(png, buffers, source) == ImageSourceResult::Success);
    const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    assert(source.GetKind() == ImageSourceKind::DataUri && source.GetMime() == DataUriMime::Png);
    assert(source.GetBytes(buffers).size() == 8 && std::memcmp(source.GetBytes(buffers).data(), signature, 8) == 0);
    assert(source.GetExternalUri().empty());
    ImageSource copy = source;
    ImageSource assigned;
    assigned = source;
    assigned = assigned;
    assert(copy.GetBytes(buffers).data() != source.GetBytes(buffers).data());
    source.Reset();
    assert(copy.GetBytes(buffers)[0] == 137 && assigned.GetBytes(buffers)[7] == 10);
    ImageSource moved = std::move(copy);
    source = std::move(assigned);
    assert(moved.GetBytes(buffers)[0] == 137 && source.GetBytes(buffers)[7] == 10);
    assert(Resolve(R"({"images":[{"uri":"data:IMAGE/JPEG;base64,%2F9j%2F","mimeType":"image/jpeg"}]})", buffers, source) == ImageSourceResult::Success);
    assert(source.GetMime() == DataUriMime::Jpeg && source.GetBytes(buffers).size() == 3);
    assert(source.GetBytes(buffers)[0] == 255 && source.GetBytes(buffers)[1] == 216);

    const char* view = R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteOffset":2,"byteLength":3}]})";
    assert(Resolve(view, buffers, source) == ImageSourceResult::Success);
    assert(source.GetKind() == ImageSourceKind::BufferView && source.GetBytes(buffers).data() == bin + 2);
    assert(source.GetBytes(buffers).size() == 3 && source.GetBufferIndex() == 0);
    copy = source;
    source.Reset();
    assert(copy.GetBytes(buffers).data() == bin + 2);
    const char* ownedView = R"({"images":[{"bufferView":0,"mimeType":"image/jpeg"}],"bufferViews":[{"buffer":1,"byteLength":4}]})";
    assert(Resolve(ownedView, buffers, source) == ImageSourceResult::Success);
    assert(source.GetBytes(buffers).data() == buffers.GetBytes(1).data());
    assert(source.GetBytes(buffers)[3] == 40 && source.GetBufferIndex() == 1);

    for (const char* bad : {
        R"({})", R"({"images":null})", R"({"images":[]})", R"({"images":[null]})",
        R"({"images":[{}]})", R"({"images":[{"uri":null}]})", R"({"images":[{"uri":""}]})",
        R"({"images":[{"uri":"a.png","uri":"a.png"}]})",
        R"({"images":[],"images":[{"uri":"a.png"}]})",
        R"({"images":[{"uri":"a.png","bufferView":0,"mimeType":"image/png"}]})",
        R"({"images":[{"uri":"a.png","mimeType":null}]})",
        R"({"images":[{"uri":"a.png","mimeType":"image/png","mimeType":"image/png"}]})",
        R"({"images":[{"uri":"data:image/png;base64,AB=="}]})",
        R"({"images":[{"uri":"data:image/png;base64,"}]})",
        R"({"images":[{"uri":"data:application/octet-stream;base64,AA=="}]})",
        R"({"images":[{"uri":"data:image/webp;base64,AA=="}]})",
        R"({"images":[{"uri":"data:image/png;base64,AA==","mimeType":"image/jpeg"}]})",
        R"({"images":[{"uri":"%2e%2e/a.png"}]})", R"({"images":[{"uri":"a%00.png"}]})",
        R"({"images":[{"uri":"a.png#x"}]})", R"({"images":[{"uri":"\u3042.png"}]})",
        R"({"images":[{"bufferView":0}]})",
        R"({"images":[{"bufferView":null,"mimeType":"image/png"}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/ktx2"}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteOffset":7,"byteLength":2}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":1,"byteLength":5}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":2,"byteLength":1}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteOffset":9007199254740992,"byteLength":1}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteLength":4,"byteStride":4}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteLength":4,"byteStride":null}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteLength":4,"byteStride":4,"byteStride":4}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteLength":0}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"byteOffset":null,"byteLength":1}]})",
        R"({"images":[{"bufferView":0,"mimeType":"image/png"}],"bufferViews":[{"buffer":0,"buffer":0,"byteLength":1}]})"})
    {
        assert(Resolve(png, buffers, source) == ImageSourceResult::Success);
        assert(Resolve(bad, buffers, source) != ImageSourceResult::Success);
        assert(source.GetKind() == ImageSourceKind::Unknown && source.GetBytes(buffers).empty() && source.GetExternalUri().empty());
    }
    assert(Resolve(png, buffers, source, 1) == ImageSourceResult::InvalidDescriptor);
    assert(Resolve(view, buffers, source) == ImageSourceResult::Success);
    buffers.Reset();
    assert(source.GetBytes(buffers).empty());
    std::cout << "GltfImageSourceTest PASS: descriptors_owned_data_borrowed_views_mime_failure\n";
    return 0;
}
