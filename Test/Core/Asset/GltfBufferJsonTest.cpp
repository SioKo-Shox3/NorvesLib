#include "Resource/GltfBufferJson.h"
#include "Text/JsonDocument.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Gltf;

namespace
{
    Container::String ToString(const char* text)
    {
        Container::String result;
        while (*text)
        {
            result.push_back(static_cast<Container::String::value_type>(static_cast<unsigned char>(*text++)));
        }
        return result;
    }
    struct State
    {
        int Calls = 0;
    };
    ExternalBufferReadResult Read(Container::Span<const uint8_t> uri, Container::VariableArray<uint8_t>& output, void* context)
    {
        ++static_cast<State*>(context)->Calls;
        assert(uri.size() == 8 && std::memcmp(uri.data(),"file.bin",8) == 0);
        output.assign({10,20,30,40});
        return ExternalBufferReadResult::Success;
    }
    BufferResolveOutcome Resolve(const char* text, const ContainerView& view, State& state, BufferSet& output)
    {
        JsonDocument document;
        Container::String error;
        assert(JsonDocument::TryParse(ToString(text),document,&error));
        const auto result = ResolveJsonBuffers(document.GetRoot(),view,Read,&state,output);
        document.Reset();
        return result;
    }
}

int main()
{
    uint8_t bin[] = {1,2,3,0};
    ContainerView view;
    view.IsGlb = view.HasBin = true;
    view.Bin = bin;
    State state;
    BufferSet buffers;
    const char* mixed = R"({"buffers":[{"byteLength":3},{"byteLength":3,"uri":"file.bin"},{"byteLength":1,"uri":"data:application/octet-stream;base64,Zg=="}]})";
    assert(Resolve(mixed,view,state,buffers).Result == BufferResolveResult::Success);
    assert(buffers.GetCount() == 3 && state.Calls == 1);
    assert(buffers.GetBytes(0).data() == bin && buffers.GetBytes(0).size() == 3);
    assert(buffers.GetSourceBytes(1).size() == 4 && buffers.GetSourceBytes(1)[3] == 40);
    assert(buffers.GetBytes(2)[0] == 'f');
    for (const char* invalid : {R"([])",R"({})",R"({"buffers":null})",R"({"buffers":[]})",R"({"buffers":[null]})",
        R"({"buffers":[{}]})",R"({"buffers":[{"byteLength":0}]})",R"({"buffers":[{"byteLength":-1}]})",
        R"({"buffers":[{"byteLength":1.5}]})",R"({"buffers":[{"byteLength":"3"}]})",R"({"buffers":[{"byteLength":true}]})",
        R"({"buffers":[{"byteLength":null}]})",R"({"buffers":[{"byteLength":9007199254740992}]})",
        R"({"buffers":[{"byteLength":3,"uri":null}]})",R"({"buffers":[{"byteLength":3,"uri":42}]})",
        R"({"buffers":[{"byteLength":3,"byteLength":3}]})",R"({"buffers":[],"buffers":[{"byteLength":3}]})",
        R"({"buffers":[{"byteLength":3,"uri":"file.bin","uri":"file.bin"}]})",
        R"({"buffers":[{"byteLength":1,"uri":"\u00E9.bin"}]})"})
    {
        assert(Resolve(mixed,view,state,buffers).Result == BufferResolveResult::Success);
        const int calls = state.Calls;
        const auto result = Resolve(invalid,view,state,buffers);
        assert(result.Result == BufferResolveResult::InvalidDescriptor && buffers.GetCount() == 0 && state.Calls == calls);
    }
    assert(Resolve(R"({"buffers":[{"byteLength":1,"uri":""}]})",view,state,buffers).Result == BufferResolveResult::InvalidUri);
    assert(Resolve(R"({"buffers":[{"byteLength":1,"uri":"\u0000"}]})",view,state,buffers).Result == BufferResolveResult::InvalidUri);
    const auto missing = Resolve(R"({"buffers":[{"byteLength":3}]})",{},state,buffers);
    assert(missing.SourceError == BufferSourceResult::MissingBin && buffers.GetCount() == 0);
    const auto second = Resolve(R"({"buffers":[{"byteLength":1,"uri":"file.bin"},{"byteLength":3}]})",view,state,buffers);
    assert(second.BufferIndex == 1 && second.SourceError == BufferSourceResult::InvalidBinIndex && buffers.GetCount() == 0);
    assert(Resolve(R"({"extras":{"a":1},"buffers":[{"byteLength":3,"extras":{"uri":42}}]})",view,state,buffers).Result == BufferResolveResult::Success);
    assert(ResolveJsonBuffers({},view,Read,&state,buffers).Result == BufferResolveResult::InvalidDescriptor && buffers.GetCount() == 0);
    std::cout << "GltfBufferJsonTest PASS: descriptor_types_duplicates_lifetime\n";
    return 0;
}
