#include "Tools/AssetCook/MeshCooker.h"
#include "Container/Span.h"
#include "Asset/CookedMeshFormat.h"
#include "Asset/AssetPackageFormat.h"
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
    Bytes Glb(AnsiStringView json, bool bIncludeBin = true, Span<const uint8_t> binary = Triangle)
    {
        const size_t jsonSize = (json.size() + 3) & ~size_t{3};
        const size_t binSize = (binary.size() + 3) & ~size_t{3};
        Bytes bytes;
        Append32(bytes, 0x46546c67);
        Append32(bytes, 2);
        Append32(bytes, static_cast<uint32_t>(20 + jsonSize + (bIncludeBin ? 8 + binSize : 0)));
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
            Append32(bytes, static_cast<uint32_t>(binSize));
            Append32(bytes, 0x004e4942);
            for (const auto value : binary)
            {
                bytes.push_back(value);
            }
            while (bytes.size() % 4 != 0)
            {
                bytes.push_back(0);
            }
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

    constexpr uint8_t ImagePng[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,72,153,118,226,63,0,5,230,2,194,63,121,106,233,0,0,0,0,73,69,78,68,174,66,96,130};
    constexpr const char* ImageGeometry = R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":104,"byteLength":70}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}])json";
    constexpr const char* Material = R"({"normalTexture":{"index":1},"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":2}}})";
    AnsiString ImageJson(const char* buffers, const char* images, const char* material = Material)
    {
        return AnsiString("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[") + buffers + "]," + ImageGeometry +
            ",\"images\":[" + images + "],\"textures\":[{\"source\":0},{\"source\":1},{\"source\":2}],\"materials\":[" + material + "]}";
    }
    void CheckEmbeddedImages(const std::filesystem::path& directory, AnsiStringView path)
    {
        using namespace NorvesLib::Core::Asset;
        Bytes binary;
        for (const auto value : Triangle)
        {
            binary.push_back(value);
        }
        binary.push_back(0);
        binary.push_back(0);
        for (const auto value : ImagePng)
        {
            binary.push_back(value);
        }
        Write(directory / "triangle.bin", binary);
        const char* externalBuffers = R"({"uri":"triangle.bin","byteLength":174})";
        const char* views = R"({"bufferView":4,"mimeType":"image/png"},{"bufferView":4,"mimeType":"image/png"},{"bufferView":4,"mimeType":"image/png"})";
        const char* files = R"({"uri":"triangle.gltf.img0.png"},{"uri":"triangle.gltf.img1.png"},{"uri":"triangle.gltf.img2.png"})";
        MeshCookResult baseline;
        AnsiString error;
        assert(Cook(Copy(ImageJson(externalBuffers, files)), path, baseline, error));
        assert(baseline.EmbeddedImages.empty()); // 画像fileを実際に置かなくても外部参照は保存する。

        const Bytes glb = Glb(ImageJson(R"({"byteLength":174})", views), true, binary);
        MeshCookResult embedded;
        assert(Cook(glb, path, embedded, error));
        SamePayload(baseline, embedded);
        assert(embedded.EmbeddedImages.size() == 3);
        for (size_t i = 0; i < 3; ++i)
        {
            const auto& image = embedded.EmbeddedImages[i];
            assert(image.ImageIndex == i && image.IsBorrowed());
            assert(image.Roles == static_cast<uint8_t>(i == 0 ? MeshImageRole::Albedo : i == 1 ? MeshImageRole::Normal : MeshImageRole::Arm));
            assert(AnsiStringView(image.Format) == AnsiStringView(i == 0 ? "nvtex.v0.rgba8.srgb" : "nvtex.v0.rgba8.linear"));
            assert(image.GetBytes().size() == sizeof(ImagePng) && std::memcmp(image.GetBytes().data(), ImagePng, sizeof(ImagePng)) == 0);
            assert(image.SourceHash == ComputeAssetPackagePayloadHash(ImagePng, sizeof(ImagePng)));
            const auto begin = reinterpret_cast<uintptr_t>(glb.data());
            const auto imageBegin = reinterpret_cast<uintptr_t>(image.GetBytes().data());
            assert(imageBegin >= begin && imageBegin - begin < glb.size());
        }
        const auto parsed = ParseCookedMesh(AssetBlob::CopyBytes(embedded.NvmeshBytes, "embedded-image-test"));
        assert(parsed.Succeeded());
        assert(parsed.Mesh.GetString(parsed.Mesh.Materials[0].AlbedoTexture) == AnsiStringView("Models/triangle.gltf.img0.png"));
        assert(parsed.Mesh.GetString(parsed.Mesh.Materials[0].NormalTexture) == AnsiStringView("Models/triangle.gltf.img1.png"));
        assert(parsed.Mesh.GetString(parsed.Mesh.Materials[0].ArmTexture) == AnsiStringView("Models/triangle.gltf.img2.png"));
        const uint64_t originalHash = embedded.SourceHash;
        Bytes modified = glb;
        modified[static_cast<size_t>(embedded.EmbeddedImages[0].GetBytes().data() - glb.data()) + 45] ^= 1;
        MeshCookResult changed;
        assert(Cook(modified, path, changed, error));
        assert(changed.SourceHash != originalHash);
        SamePayload(embedded, changed); // image bytesのdecodeは次段のtexture cookerが担当する。

        // 外部buffer内画像はCookローカルのBufferSetを離れても所有bytesが残る。
        MeshCookResult owned;
        assert(Cook(Copy(ImageJson(externalBuffers, views)), path, owned, error));
        SamePayload(baseline, owned);
        for (const auto& image : owned.EmbeddedImages)
        {
            assert(!image.IsBorrowed() && image.GetBytes().size() == sizeof(ImagePng) && image.GetBytes().data() != nullptr);
            assert(std::memcmp(image.GetBytes().data(), ImagePng, sizeof(ImagePng)) == 0);
        }
        const char* dataBuffer = R"({"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAACJUE5HDQoaCgAAAA1JSERSAAAAAQAAAAEIBgAAAB8VxIkAAAANSURBVHicY0iZduI/AAXmAsI/eWrpAAAAAElFTkSuQmCC","byteLength":174})";
        assert(Cook(Copy(ImageJson(dataBuffer, views)), path, owned, error));
        SamePayload(baseline, owned);
        for (const auto& image : owned.EmbeddedImages)
        {
            assert(!image.IsBorrowed() && image.GetBytes().size() == sizeof(ImagePng));
            assert(std::memcmp(image.GetBytes().data(), ImagePng, sizeof(ImagePng)) == 0);
        }
        MeshEmbeddedImage copy = owned.EmbeddedImages[0];
        MeshEmbeddedImage assigned;
        assigned = owned.EmbeddedImages[1];
        assigned = assigned;
        assert(copy.GetBytes().data() != owned.EmbeddedImages[0].GetBytes().data());
        owned = MeshCookResult{};
        MeshEmbeddedImage moved = std::move(copy);
        MeshEmbeddedImage moveAssigned;
        moveAssigned = std::move(assigned);
        assert(std::memcmp(moved.GetBytes().data(), ImagePng, sizeof(ImagePng)) == 0);
        assert(std::memcmp(moveAssigned.GetBytes().data(), ImagePng, sizeof(ImagePng)) == 0);
        const auto bytesBefore = moved.GetBytes().data();
        assert(!moved.SetBytes(moved.GetBytes(), true));
        assert(!moved.SetBytes({}, false));
        assert(moved.GetBytes().data() == bytesBefore && !moved.IsBorrowed());
        MeshEmbeddedImage borrowedCopy = embedded.EmbeddedImages[0];
        embedded = MeshCookResult{};
        assert(borrowedCopy.IsBorrowed() && std::memcmp(borrowedCopy.GetBytes().data(), ImagePng, sizeof(ImagePng)) == 0);

        const char* dataImages = R"({"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNImXbiPwAF5gLCP3lq6QAAAABJRU5ErkJggg=="},{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNImXbiPwAF5gLCP3lq6QAAAABJRU5ErkJggg=="},{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNImXbiPwAF5gLCP3lq6QAAAABJRU5ErkJggg=="})";
        assert(Cook(Copy(ImageJson(externalBuffers, dataImages)), path, owned, error));
        SamePayload(baseline, owned);
        for (const auto& image : owned.EmbeddedImages)
        {
            assert(!image.IsBorrowed() && image.GetBytes().size() == sizeof(ImagePng));
            assert(std::memcmp(image.GetBytes().data(), ImagePng, sizeof(ImagePng)) == 0);
        }
        const char* linearShared = R"({"normalTexture":{"index":1},"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1}}})";
        assert(Cook(Copy(ImageJson(externalBuffers, dataImages, linearShared)), path, owned, error));
        assert(owned.EmbeddedImages.size() == 2 && owned.EmbeddedImages[1].Roles ==
            (static_cast<uint8_t>(MeshImageRole::Normal) | static_cast<uint8_t>(MeshImageRole::Arm)));
        const MeshCookResult beforeConflict = owned;
        const auto previousHash = owned.SourceHash;
        const char* conflict = R"({"normalTexture":{"index":0},"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})";
        assert(!Cook(Copy(ImageJson(externalBuffers, dataImages, conflict)), path, owned, error));
        assert(AnsiStringView(error) == AnsiStringView("embedded image roles require conflicting texture formats"));
        assert(owned.SourceHash == previousHash && owned.EmbeddedImages.size() == 2);
        SamePayload(beforeConflict, owned);
        for (size_t i = 0; i < owned.EmbeddedImages.size(); ++i)
        {
            const auto& before = beforeConflict.EmbeddedImages[i];
            const auto& after = owned.EmbeddedImages[i];
            assert(before.ImageIndex == after.ImageIndex && before.Roles == after.Roles);
            assert(before.Format == after.Format && before.LogicalPath == after.LogicalPath && before.SourceHash == after.SourceHash);
            assert(before.GetBytes().size() == after.GetBytes().size());
            assert(std::memcmp(before.GetBytes().data(), after.GetBytes().data(), before.GetBytes().size()) == 0);
        }
        // JPEGの宣言からpath拡張子を決める検査。ここでも画像内容のdecodeは主張しない。
        const char* jpegImages = R"({"uri":"data:image/jpeg;base64,/9j/"},{"uri":"data:image/jpeg;base64,/9j/"},{"uri":"data:image/jpeg;base64,/9j/"})";
        assert(Cook(Copy(ImageJson(externalBuffers, jpegImages)), path, owned, error));
        assert(AnsiStringView(owned.EmbeddedImages[0].LogicalPath) == AnsiStringView("Models/triangle.gltf.img0.jpg"));

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
    CheckEmbeddedImages(directory.Path, path);
    std::cout << "StaticGltfBufferCookTest PASS: external_glb_data_uri_payload_hash_rejection\n";
    return 0;
}
