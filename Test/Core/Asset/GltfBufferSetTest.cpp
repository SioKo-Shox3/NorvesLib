#include "Resource/GltfBufferSet.h"
#include "Resource/GltfBufferFile.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Gltf;

namespace
{
    Container::Span<const uint8_t> Bytes(const char* text)
    {
        return {reinterpret_cast<const uint8_t*>(text),std::strlen(text)};
    }
    struct ReaderState
    {
        int Calls = 0;
        bool bFail = false;
        bool bThrow = false;
    };
    ExternalBufferReadResult Read(Container::Span<const uint8_t> uri,
        Container::VariableArray<uint8_t>& output, void* context)
    {
        auto& state = *static_cast<ReaderState*>(context);
        ++state.Calls;
        assert(uri.size() == 8 && std::memcmp(uri.data(),"file.bin",8) == 0);
        output.assign({10,20,30,40});
        if (state.bThrow)
        {
            throw std::runtime_error("reader_failure");
        }
        return state.bFail ? ExternalBufferReadResult::ReadFailed : ExternalBufferReadResult::Success;
    }
    void CheckFileReader()
    {
        std::random_device random;
        char name[80];
        std::snprintf(name,sizeof(name),"NorvesGltfBufferSetTest_%u_%u",random(),random());
        const auto directory = std::filesystem::temp_directory_path()/name;
        assert(std::filesystem::create_directory(directory));
        const auto inputPath = directory/"data.bin";
        {
            std::ofstream file(inputPath,std::ios::binary);
            const char content[] = {10,20,30,40};
            file.write(content,4);
            assert(file.good());
        }
        BufferFileContext context{directory/"model.gltf"};
        Container::VariableArray<uint8_t> output;
        assert(ReadBufferFile(Bytes("data.bin"),output,&context) == ExternalBufferReadResult::Success);
        assert(output.size() == 4 && output[3] == 40);
        assert(ReadBufferFile(Bytes("../data.bin"),output,&context) == ExternalBufferReadResult::InvalidPath && output.empty());
        assert(ReadBufferFile(Bytes("missing.bin"),output,&context) != ExternalBufferReadResult::Success && output.empty());
        assert(std::filesystem::create_directory(directory/"sub"));
        assert(ReadBufferFile(Bytes("sub"),output,&context) == ExternalBufferReadResult::InvalidFileType && output.empty());
        std::error_code error;
        std::filesystem::create_directory_symlink(directory.parent_path(),directory/"escape",error);
        if (!error)
        {
            assert(ReadBufferFile(Bytes("escape/anything.bin"),output,&context) == ExternalBufferReadResult::OutsideDirectory);
            assert(std::filesystem::remove(directory/"escape"));
        }
        else
        {
            std::cout << "symlink_fixture_unavailable\n";
        }
        assert(std::filesystem::remove(inputPath));
        assert(std::filesystem::remove(directory/"sub"));
        assert(std::filesystem::remove(directory));
    }
}

int main()
{
    static_assert(std::is_nothrow_move_constructible_v<BufferSet>);
    static_assert(std::is_nothrow_move_assignable_v<BufferSet>);
    uint8_t bin[] = {1,2,3,0};
    ContainerView container;
    container.IsGlb = container.HasBin = true;
    container.Bin = bin;
    BufferRequest requests[] = {{3,false,{}}, {3,true,Bytes("file%2Ebin")},
        {2,true,Bytes("data:application/octet-stream;base64,QUJD")}};
    ReaderState state;
    BufferSet set;
    assert(BufferSet::Resolve(requests,container,Read,&state,set).Result == BufferResolveResult::Success);
    assert(state.Calls == 1 && set.GetCount() == 3);
    assert(set.GetSourceKind(0) == BufferStorageKind::GlbBin && set.GetBytes(0).data() == bin && set.GetBytes(0).size() == 3);
    assert(set.GetSourceKind(1) == BufferStorageKind::ExternalFile && set.GetBytes(1).size() == 3 && set.GetSourceBytes(1).size() == 4);
    assert(set.GetSourceBytes(1)[3] == 40 && set.GetDeclaredByteLength(1) == 3);
    assert(set.GetSourceKind(2) == BufferStorageKind::DataUri && set.GetBytes(2).size() == 2 && set.GetSourceBytes(2).size() == 3);
    assert(std::memcmp(set.GetSourceBytes(2).data(),"ABC",3) == 0);
    assert(set.GetBytes(99).empty() && set.GetSourceKind(99) == BufferStorageKind::Unknown);
    const auto oldExternal = set.GetSourceBytes(1).data();
    BufferSet copied = set;
    assert(copied.GetSourceBytes(1).data() != oldExternal && copied.GetBytes(0).data() == bin);
    assert(std::memcmp(copied.GetSourceBytes(1).data(),set.GetSourceBytes(1).data(),4) == 0);
    BufferSet assigned;
    BufferRequest shortData{1,true,Bytes("data:application/octet-stream;base64,Zg==")};
    assert(BufferSet::Resolve({&shortData,1},{},nullptr,nullptr,assigned).Result == BufferResolveResult::Success);
    assigned = set;
    assert(assigned.GetCount() == 3 && assigned.GetSourceBytes(1).data() != set.GetSourceBytes(1).data());
    const auto assignedBytes = assigned.GetSourceBytes(1).data();
    assigned = assigned;
    assert(assigned.GetSourceBytes(1).data() == assignedBytes && assigned.GetBytes(2)[1] == 'B');
    set.Reset();
    assert(assigned.GetSourceBytes(1)[3] == 40 && assigned.GetBytes(2)[0] == 'A');
    assert(copied.GetSourceBytes(1)[3] == 40 && copied.GetBytes(2)[0] == 'A');
    BufferSet moveAssigned;
    assert(BufferSet::Resolve({&shortData,1},{},nullptr,nullptr,moveAssigned).Result == BufferResolveResult::Success);
    moveAssigned = std::move(assigned);
    assert(moveAssigned.GetCount() == 3 && moveAssigned.GetBytes(0).data() == bin && moveAssigned.GetSourceBytes(1)[3] == 40);
    assigned.Reset();
    BufferSet moved = std::move(copied);
    assert(moved.GetCount() == 3 && moved.GetBytes(0).data() == bin && moved.GetBytes(2)[1] == 'B');
    BufferRequest tooShort{5,true,Bytes("file.bin")};
    assert(BufferSet::Resolve({&tooShort,1},{},Read,&state,moved).Result == BufferResolveResult::SourceTooShort && moved.GetCount() == 0);
    BufferRequest external{1,true,Bytes("file.bin")};
    state.bFail = true;
    const auto failed = BufferSet::Resolve({&external,1},{},Read,&state,moved);
    assert(failed.Result == BufferResolveResult::ExternalReadFailure && failed.ReadError == ExternalBufferReadResult::ReadFailed && moved.GetCount() == 0);
    state.bFail = false;
    assert(BufferSet::Resolve(requests,container,Read,&state,moved).Result == BufferResolveResult::Success);
    state.bThrow = true;
    try
    {
        (void)BufferSet::Resolve({&external,1},{},Read,&state,moved);
        assert(false);
    }
    catch (const std::runtime_error&)
    {
        assert(moved.GetCount() == 0);
    }
    state.bThrow = false;
    for (const char* uri : {"", "../file.bin", "%2e%2e/file.bin", "/file.bin", "C:/file.bin", "a\\file.bin", "a//b", "a/./b", "a/", "a%00b", "a#b", "a?b"})
    {
        const int calls = state.Calls;
        BufferRequest invalid{1,true,Bytes(uri)};
        assert(BufferSet::Resolve({&invalid,1},{},Read,&state,moved).Result == BufferResolveResult::InvalidUri);
        assert(state.Calls == calls && moved.GetCount() == 0);
    }
    BufferRequest noUri{1,false,{}};
    assert(BufferSet::Resolve({&noUri,1},{},nullptr,nullptr,moved).SourceError == BufferSourceResult::MissingBin);
    BufferRequest image{1,true,Bytes("data:image/png;base64,Zg==")};
    assert(BufferSet::Resolve({&image,1},{},nullptr,nullptr,moved).Result == BufferResolveResult::UnsupportedDataMime);
    BufferRequest bad64{1,true,Bytes("data:application/gltf-buffer;base64,!!!!")};
    assert(BufferSet::Resolve({&bad64,1},{},nullptr,nullptr,moved).Base64Error == Text::Base64DecodeResult::InvalidCharacter);
    assert(BufferSet::Resolve({&external,1},{},nullptr,nullptr,moved).Result == BufferResolveResult::ReaderUnavailable);
    assert(BufferSet::Resolve({},container,nullptr,nullptr,moved).Result == BufferResolveResult::InvalidArgument);
    BufferRequest invalidLength{0,true,Bytes("file.bin")};
    assert(BufferSet::Resolve({&invalidLength,1},{},Read,&state,moved).Result == BufferResolveResult::InvalidDescriptor);
    CheckFileReader();
    std::cout << "GltfBufferSetTest PASS: ownership_copy_move_sources_failure_file_containment\n";
    return 0;
}
