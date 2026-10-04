#include "Resource/ModelStaging.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace NorvesLib::Core::Container;
namespace Staging = NorvesLib::Core::ResourceIO::ModelStaging;
namespace
{
    using Bytes = VariableArray<uint8_t>;
    constexpr uint8_t Triangle[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,1,0,2,0};
    constexpr uint8_t ImagePng[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,72,153,118,226,63,0,5,230,2,194,63,121,106,233,0,0,0,0,73,69,78,68,174,66,96,130};
    constexpr const char* ImageGeometry = R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":104,"byteLength":70}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}])json";
    constexpr const char* Material = R"({"normalTexture":{"index":1},"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":2}}})";
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
            const AnsiString name = AnsiString("norves-loose-sources-") +
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
    AnsiString ImageJson(const char* buffers, const char* images, const char* material = Material)
    {
        return AnsiString("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[") + buffers + "]," + ImageGeometry +
            ",\"images\":[" + images + "],\"textures\":[{\"source\":0},{\"source\":1},{\"source\":2}],\"materials\":[" + material + "]}";
    }

    String CorePath(const std::filesystem::path& path)
    {
#if defined(UNICODE)
        return String(path.c_str());
#else
        return String(path.generic_string().c_str());
#endif
    }
    void AssertTextures(const Staging::ModelStagingData& model)
    {
        using Format = NorvesLib::Core::Rendering::TextureCreateInfo::Format;
        assert(Staging::GetStagedTextureCount(model) == 5);
        const Staging::StagedTextureData* rgba[] = {&model.AlbedoTexture, &model.NormalTexture};
        for (const auto* texture : rgba)
        {
            assert(texture->Width == 1 && texture->Height == 1 && texture->Format == Format::RGBA8_UNORM);
            assert(texture->PixelData.size() == 4 && !texture->HasPreparedTexture());
            const uint8_t expected[] = {100,150,200,255};
            assert(std::memcmp(texture->PixelData.data(), expected, 4) == 0);
        }
        const Staging::StagedTextureData* arm[] = {&model.AOTexture, &model.RoughnessTexture, &model.MetallicTexture};
        const uint8_t channels[] = {100,150,200};
        for (size_t index = 0; index < 3; ++index)
        {
            assert(arm[index]->Width == 1 && arm[index]->Height == 1 && arm[index]->Format == Format::R8_UNORM);
            assert(arm[index]->PixelData.size() == 1 && arm[index]->PixelData[0] == channels[index]);
        }
    }
    void SameGeometry(const Staging::ModelStagingData& a, const Staging::ModelStagingData& b)
    {
        assert(a.Vertices.size() == 3 && b.Vertices.size() == 3);
        assert(a.ClusterizedIndices.size() == b.ClusterizedIndices.size());
        assert(a.Clusters.size() == b.Clusters.size() && !a.Clusters.empty());
        for (size_t i = 0; i < a.Vertices.size(); ++i)
        {
            for (size_t c = 0; c < 3; ++c)
            {
                assert(a.Vertices[i].Position[c] == b.Vertices[i].Position[c]);
                assert(a.Vertices[i].Normal[c] == b.Vertices[i].Normal[c]);
            }
            for (size_t c = 0; c < 2; ++c)
            {
                assert(a.Vertices[i].TexCoord[c] == b.Vertices[i].TexCoord[c]);
            }
        }
        for (size_t i = 0; i < a.ClusterizedIndices.size(); ++i)
        {
            assert(a.ClusterizedIndices[i] == b.ClusterizedIndices[i]);
        }
    }
}

int main()
{
    TemporaryDirectory directory;
    Bytes binary;
    for (const auto byte : Triangle) { binary.push_back(byte); }
    binary.push_back(0); binary.push_back(0);
    for (const auto byte : ImagePng) { binary.push_back(byte); }
    Write(directory.Path / "source.bin", binary);
    Write(directory.Path / "image.png", ImagePng);
    const char* externalBuffer = R"({"byteLength":174,"uri":"source.bin"})";
    const char* viewImages = R"({"bufferView":4,"mimeType":"image/png"},{"bufferView":4,"mimeType":"image/png"},{"bufferView":4,"mimeType":"image/png"})";
    const char* externalImages = R"({"uri":"image.png"},{"uri":"image.png"},{"uri":"image.png"})";
    const auto source = directory.Path / "model.gltf";
    const auto path = CorePath(source);
    // 絶対resolved pathはI/O、相対request pathは既存logical referenceの組立に使う。
    const String request("Models/model.gltf");
    Write(source, Copy(ImageJson(externalBuffer, externalImages)));
    Staging::ModelStagingData baseline;
    assert(Staging::BuildModelStagingFromLooseGltf(path, path, baseline, "test", 0));
    AssertTextures(baseline);
    assert(baseline.TextureReferences.Albedo.RequestPath.empty());
    Staging::ModelStagingData deferred;
    assert(Staging::BuildModelStagingFromLooseGltf(request, path, deferred, "test", 0));
    assert(deferred.TextureReferences.Albedo.RequestPath == "Models/image.png");
    assert(Staging::GetStagedTextureCount(deferred) == 0);
    SameGeometry(baseline, deferred);

    const AnsiString encodedBuffer = R"({"byteLength":174,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAACJUE5HDQoaCgAAAA1JSERSAAAAAQAAAAEIBgAAAB8VxIkAAAANSURBVHicY0iZduI/AAXmAsI/eWrpAAAAAElFTkSuQmCC"})";
    const AnsiString image = R"({"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNImXbiPwAF5gLCP3lq6QAAAABJRU5ErkJggg=="})";
    const AnsiString uriImages = image + "," + image + "," + image;
    const Bytes glb = Glb(ImageJson(R"({"byteLength":174})", viewImages), true, binary);
    const Bytes bufferUri = Copy(ImageJson(encodedBuffer.c_str(), viewImages));
    const Bytes imageUri = Copy(ImageJson(externalBuffer, uriImages.c_str()));
    const Bytes externalView = Copy(ImageJson(externalBuffer, viewImages));
    for (const Bytes* bytes : {&glb, &bufferUri, &imageUri, &externalView})
    {
        Write(source, *bytes);
        Staging::ModelStagingData staging;
        assert(Staging::BuildModelStagingFromLooseGltf(request, path, staging, "test", 0));
        SameGeometry(baseline, staging);
        AssertTextures(staging);
        assert(!staging.TextureReferences.Albedo.HasReference() && !staging.TextureReferences.Normal.HasReference() &&
            !staging.TextureReferences.Arm.HasReference());
        // loader内の元file/buffer/image storageは既に破棄済み。出力pixelsはそれぞれ所有する。
        assert(staging.AlbedoTexture.PixelData.data() != staging.NormalTexture.PixelData.data());
        assert(staging.AOTexture.PixelData.data() != staging.RoughnessTexture.PixelData.data());
        assert(staging.RoughnessTexture.PixelData.data() != staging.MetallicTexture.PixelData.data());
    }

    const auto oldVertices = baseline.Vertices.data();
    const auto oldPixels = baseline.AlbedoTexture.PixelData.data();
    Bytes malformed = glb; malformed[8] ^= 1;
    Bytes invalidPng = binary; invalidPng[104] = 0;
    const Bytes wrongImage = Glb(ImageJson(R"({"byteLength":174})", viewImages), true, invalidPng);
    const Bytes wrongView = Glb(ImageJson(R"({"byteLength":174})",
        R"({"bufferView":99,"mimeType":"image/png"},{"bufferView":4,"mimeType":"image/png"},{"bufferView":4,"mimeType":"image/png"})"), true, binary);
    const auto validJson = ImageJson(externalBuffer, uriImages.c_str());
    const Bytes required = Copy(AnsiString(R"({"extensionsRequired":["KHR_texture_transform"],)") + validJson.substr(1));
    // signatureは正しいが画像本体が無い場合はdecoderで失敗し、候補geometryも公開しない。
    const Bytes truncatedImage = Copy(ImageJson(externalBuffer,
        R"({"uri":"data:image/png;base64,iVBORw0KGgo="},{"uri":"data:image/png;base64,iVBORw0KGgo="},{"uri":"data:image/png;base64,iVBORw0KGgo="})"));
    const Bytes mismatchedMime = Glb(ImageJson(R"({"byteLength":174})",
        R"({"bufferView":4,"mimeType":"image/jpeg"},{"bufferView":4,"mimeType":"image/png"},{"bufferView":4,"mimeType":"image/png"})"), true, binary);
    const Bytes* invalidInputs[] = {&malformed, &wrongImage, &wrongView, &required, &truncatedImage, &mismatchedMime};
    for (const Bytes* bytes : invalidInputs)
    {
        Write(source, *bytes);
        assert(!Staging::BuildModelStagingFromLooseGltf(request, path, baseline, "test", 0));
        assert(baseline.Vertices.data() == oldVertices && baseline.AlbedoTexture.PixelData.data() == oldPixels);
        AssertTextures(baseline);
    }
    std::cout << "GltfLooseSourceTest PASS: external_glb_data_uri_geometry_owned_five_textures_failure_preservation\n";
    return 0;
}
