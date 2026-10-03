#include "Tools/AssetCook/MeshCooker.h"
#include "Container/Span.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Tools::AssetCook;

namespace
{
    using Bytes = VariableArray<uint8_t>;
    constexpr const char* Geometry = R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}])json";
    constexpr const char* Encoded = "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA";
    constexpr uint8_t Triangle[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,1,0,2,0};
    AnsiString Json(const char* buffers)
    {
        return AnsiString("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[") + buffers + "]," + Geometry + "}";
    }
    Bytes Copy(AnsiStringView text)
    {
        Bytes bytes;
        bytes.resize(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }
    void Append32(Bytes& bytes, uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<uint8_t>(value >> shift));
        }
    }
    Bytes Glb(AnsiStringView json, bool bIncludeBin = true)
    {
        const size_t jsonSize = (json.size() + 3) & ~size_t{3};
        Bytes bytes;
        Append32(bytes, 0x46546c67);
        Append32(bytes, 2);
        Append32(bytes, static_cast<uint32_t>(20 + jsonSize + (bIncludeBin ? 112 : 0)));
        Append32(bytes, static_cast<uint32_t>(jsonSize));
        Append32(bytes, 0x4e4f534a);
        for (const char value : json)
        {
            bytes.push_back(static_cast<uint8_t>(value));
        }
        while (bytes.size() < 20 + jsonSize)
        {
            bytes.push_back(' ');
        }
        if (bIncludeBin)
        {
            Append32(bytes, 104);
            Append32(bytes, 0x004e4942);
            for (const auto value : Triangle)
            {
                bytes.push_back(value);
            }
            bytes.push_back(0);
            bytes.push_back(0);
        }
        return bytes;
    }
    uint64_t HashPart(uint64_t hash, Span<const uint8_t> bytes)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            hash = (hash ^ ((static_cast<uint64_t>(bytes.size()) >> (8 * i)) & 255)) * 1099511628211ull;
        }
        for (const auto value : bytes)
        {
            hash = (hash ^ value) * 1099511628211ull;
        }
        return hash;
    }
    void Write(const std::filesystem::path& path, Span<const uint8_t> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        assert(file.is_open());
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        assert(file.good());
    }
    struct TemporaryDirectory
    {
        std::filesystem::path Path;
        TemporaryDirectory()
        {
            // 並列試験の既存directoryを再利用しない。
            char suffix[64] = {};
            const auto converted = std::to_chars(suffix, suffix + sizeof(suffix),
                std::chrono::steady_clock::now().time_since_epoch().count());
            assert(converted.ec == std::errc{});
            const AnsiString name = AnsiString("norves-cook-buffers-") +
                AnsiString(AnsiStringView(suffix, static_cast<size_t>(converted.ptr - suffix)));
            Path = std::filesystem::temp_directory_path() / std::filesystem::path(name.begin(), name.end());
            assert(std::filesystem::create_directory(Path));
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }
    };
    bool Cook(const Bytes& bytes, AnsiStringView path, MeshCookResult& result, AnsiString& error)
    {
        return CookGltfToNvmesh(bytes.data(), bytes.size(), "nvmesh.v0.mesh3d.pnt.u32.clustered",
            path, "Models/triangle.gltf", result, error);
    }
    void SamePayload(const MeshCookResult& expected, const MeshCookResult& actual)
    {
        assert(actual.VertexCount == 3 && actual.IndexCount == 3);
        assert(actual.ClusterCount == expected.ClusterCount);
        assert(actual.NvmeshBytes.size() == expected.NvmeshBytes.size());
        assert(std::memcmp(actual.NvmeshBytes.data(), expected.NvmeshBytes.data(), actual.NvmeshBytes.size()) == 0);
    }
} // namespace

int main()
{
    TemporaryDirectory directory;
    const AnsiString path((directory.Path / "triangle.gltf").string().c_str());
    Write(directory.Path / "triangle.bin", Triangle);
    const Bytes external = Copy(Json(R"({"uri":"triangle.bin","byteLength":102})"));
    MeshCookResult baseline;
    AnsiString error;
    assert(Cook(external, path, baseline, error));
    assert(baseline.SourceHash == HashPart(HashPart(14695981039346656037ull, external), Triangle));

    const AnsiString inlineDescriptor = AnsiString("{\"uri\":\"data:application/octet-stream;base64,") + Encoded + "\",\"byteLength\":102}";
    const Bytes embedded = Copy(Json(inlineDescriptor.c_str()));
    MeshCookResult result;
    assert(Cook(embedded, path, result, error));
    SamePayload(baseline, result);
    assert(result.SourceHash == HashPart(14695981039346656037ull, embedded));
    Bytes glb = Glb(Json(R"({"byteLength":102})"));
    assert(Cook(glb, path, result, error));
    SamePayload(baseline, result);
    assert(result.SourceHash == HashPart(14695981039346656037ull, glb));

    // GLB内でも追加の外部bufferをsource hashに含め、BINを二重に含めない。
    const Bytes mixed = Glb(Json(R"({"byteLength":102},{"uri":"triangle.bin","byteLength":102})"));
    assert(Cook(mixed, path, result, error));
    SamePayload(baseline, result);
    assert(result.SourceHash == HashPart(HashPart(14695981039346656037ull, mixed), Triangle));

    // 外部bufferの余剰byteはpayloadに影響しないが、source hashから落とさない。
    Bytes extra;
    for (const auto value : Triangle)
    {
        extra.push_back(value);
    }
    extra.push_back(77);
    Write(directory.Path / "triangle.bin", extra);
    assert(Cook(external, path, result, error));
    SamePayload(baseline, result);
    assert(result.SourceHash == HashPart(HashPart(14695981039346656037ull, external), extra));
    assert(result.SourceHash != baseline.SourceHash);
    Bytes bom;
    bom.assign({0xef, 0xbb, 0xbf});
    for (const auto value : external)
    {
        bom.push_back(value);
    }
    assert(Cook(bom, path, result, error));
    SamePayload(baseline, result);
    assert(result.SourceHash == HashPart(HashPart(14695981039346656037ull, bom), extra));

    // percentを外部pathで復号する。Traversalや非正規paddingは受理しない。
    assert(Cook(Copy(Json(R"({"uri":"triangle%2ebin","byteLength":102})")), path, result, error));
    SamePayload(baseline, result);
    for (const char* bad : {
        R"({"byteLength":102})",
        R"({"uri":"../triangle.bin","byteLength":102})",
        R"({"uri":"%2e%2e/triangle.bin","byteLength":102})",
        R"({"uri":"data:application/octet-stream;base64,AA==","byteLength":102})",
        R"({"uri":"data:application/octet-stream;base64,AB==","byteLength":102})",
        R"({"uri":null,"byteLength":102})"})
    {
        error.clear();
        assert(!Cook(Copy(Json(bad)), path, result, error));
        assert(!error.empty());
        SamePayload(baseline, result); // 失敗時に既存出力を壊さない。
    }
    glb[glb.size() - 1] = 1;
    assert(!Cook(glb, path, result, error));
    assert(!Cook(Glb(Json(R"({"byteLength":102})"), false), path, result, error));
    glb = Glb(Json(R"({"byteLength":102})"));
    glb[8] ^= 1;
    assert(!Cook(glb, path, result, error));
    glb = Glb(Json(R"({"byteLength":102})"));
    glb[20] = 0;
    assert(!Cook(glb, path, result, error));
    std::cout << "StaticGltfBufferCookTest PASS: external_glb_data_uri_payload_hash_rejection\n";
    return 0;
}
