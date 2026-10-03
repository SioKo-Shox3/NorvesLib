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

using namespace NorvesLib::Core;
namespace Staging = NorvesLib::Core::Resource::ModelStaging;

namespace
{
    Container::String CorePath(const std::filesystem::path& path)
    {
#if defined(UNICODE)
        return Container::String(path.c_str());
#else
        return Container::String(path.generic_string().c_str());
#endif
    }
    void Write(const std::filesystem::path& path, const void* data, size_t size)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        assert(file.is_open());
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        file.flush();
        assert(file.good());
    }
    struct Directory
    {
        std::filesystem::path Path;
        Directory()
        {
            char number[64] = {};
            const auto converted = std::to_chars(number, number + sizeof(number),
                std::chrono::steady_clock::now().time_since_epoch().count());
            assert(converted.ec == std::errc{});
            const Container::AnsiString name = Container::AnsiString("NorvesAccessorStaging-") +
                Container::AnsiString(Container::AnsiStringView(number, static_cast<size_t>(converted.ptr - number)));
            Path = std::filesystem::temp_directory_path() / std::filesystem::path(name.begin(), name.end());
            assert(std::filesystem::create_directory(Path));
        }
        ~Directory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }
    };
} // namespace

int main()
{
    const uint8_t binary[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,0,0,128,63,0,0,1,0,2,0};
    const char* variants[] = {
        R"json({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]})json",
        R"json({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":35},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]})json",
        R"json({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":101}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]})json",
        R"json({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":4294967280,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","byteOffset":16},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]})json",
        R"json({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":0,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":0,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":0,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":0,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]})json"
    };
    Directory directory;
    const auto file = directory.Path / "triangle.gltf";
    const auto path = CorePath(file);
    Write(directory.Path / "triangle.bin", binary, sizeof(binary));
    Staging::ModelStagingData staging;
    Write(file, variants[0], std::strlen(variants[0]));
    assert(Staging::BuildModelStagingFromLooseGltf(path, path, staging, "test", 0));
    assert(staging.Vertices.size() == 3 && staging.ClusterizedIndices.size() == 3 && !staging.Clusters.empty());
    assert(staging.Vertices[1].Position[0] == 1 && staging.Vertices[2].Position[1] == 1);
    const auto oldVertices = staging.Vertices.data();
    for (size_t i = 1; i < sizeof(variants) / sizeof(variants[0]); ++i)
    {
        Write(file, variants[i], std::strlen(variants[i]));
        assert(!Staging::BuildModelStagingFromLooseGltf(path, path, staging, "test", 0));
        assert(staging.Vertices.data() == oldVertices && staging.Vertices.size() == 3);
    }
    // 宣言byte範囲内でもindex値が頂点数を超える入力はクラスタ生成前に拒否する。
    Write(file, variants[0], std::strlen(variants[0]));
    for (const uint16_t invalidIndex : {uint16_t{3}, uint16_t{65535}})
    {
        uint8_t invalidBinary[sizeof(binary)];
        std::memcpy(invalidBinary, binary, sizeof(binary));
        invalidBinary[100] = static_cast<uint8_t>(invalidIndex);
        invalidBinary[101] = static_cast<uint8_t>(invalidIndex >> 8);
        Write(directory.Path / "triangle.bin", invalidBinary, sizeof(invalidBinary));
        assert(!Staging::BuildModelStagingFromLooseGltf(path, path, staging, "test", 0));
        assert(staging.Vertices.data() == oldVertices && staging.Vertices.size() == 3);
    }
    Write(directory.Path / "triangle.bin", binary, sizeof(binary));
    // 正常なgeometryへ必須拡張だけを追加し、拒否と既存出力保持を確認する。
    for (const char* extension : {"KHR_draco_mesh_compression", "EXT_meshopt_compression",
        "KHR_mesh_quantization", "KHR_texture_transform"})
    {
        Container::AnsiString json = "{\"extensionsRequired\":[\"";
        json += extension;
        json += "\"],";
        json += variants[0] + 1;
        Write(file, json.data(), json.size());
        assert(!Staging::BuildModelStagingFromLooseGltf(path, path, staging, "test", 0));
        assert(staging.Vertices.data() == oldVertices && staging.Vertices.size() == 3);
    }
    std::cout << "GltfAccessorStagingTest PASS: valid_mesh_view_declared_offset_count_bounds\n";
    return 0;
}
