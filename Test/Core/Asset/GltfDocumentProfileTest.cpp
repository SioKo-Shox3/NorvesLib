#include "Resource/GltfDocumentProfile.h"
#include "Text/JsonDocument.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Tools/AssetCook/MeshCooker.h"
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
    struct Fixture
    {
        const char* Json;
        RequiredExtensionsStatus Expected;
    };
}

int main()
{
    using Status = RequiredExtensionsStatus;
    const Fixture fixtures[] = {
        {R"({})", Status::Success},
        {R"({"extensionsRequired":[]})", Status::Success},
        {R"({"extensionsUsed":["KHR_draco_mesh_compression"]})", Status::Success},
        {R"({"extensionsUsed":null})", Status::Success},
        {R"({"extras":{"extensionsRequired":["unknown"]}})", Status::Success},
        {R"({"ExtensionsRequired":["unknown"]})", Status::Success},
        {R"([])", Status::InvalidRoot},
        {R"(null)", Status::InvalidRoot},
        {R"({"extensionsRequired":null})", Status::InvalidDeclaration},
        {R"({"extensionsRequired":true})", Status::InvalidDeclaration},
        {R"({"extensionsRequired":"unknown"})", Status::InvalidDeclaration},
        {R"({"extensionsRequired":{}})", Status::InvalidDeclaration},
        {R"({"extensionsRequired":[1]})", Status::InvalidDeclaration},
        {R"({"extensionsRequired":["unknown",null]})", Status::InvalidDeclaration},
        {R"({"extensionsRequired":[],"extensionsRequired":[]})", Status::DuplicateDeclaration},
        {R"({"extensionsRequired":null,"extensionsRequired":[]})", Status::DuplicateDeclaration},
        {R"({"extensionsRequired":["KHR_draco_mesh_compression"]})", Status::Unsupported},
        {R"({"extensionsRequired":["EXT_meshopt_compression"]})", Status::Unsupported},
        {R"({"extensionsRequired":["KHR_mesh_quantization"]})", Status::Unsupported},
        {R"({"extensionsRequired":["KHR_texture_transform"]})", Status::Unsupported},
        {R"({"extensionsRequired":["future_unknown"]})", Status::Unsupported},
        {R"({"extensionsRequired":[""]})", Status::Unsupported}
    };
    for (const auto& fixture : fixtures)
    {
        JsonDocument document;
        const auto json = ToString(fixture.Json);
        assert(JsonDocument::TryParse(json, document));
        assert(CheckRequiredExtensions(document.GetRoot()) == fixture.Expected);
        if (fixture.Expected == Status::Unsupported || fixture.Expected == Status::InvalidDeclaration ||
            fixture.Expected == Status::DuplicateDeclaration)
        {
            // 必須拡張の拒否がgeometryの欠落より先に行われることを実入口で検査する。
            NorvesLib::Tools::AssetCook::MeshCookResult result;
            result.SourceHash = 123;
            Container::AnsiString error;
            assert(!NorvesLib::Tools::AssetCook::CookGltfToNvmesh(
                reinterpret_cast<const uint8_t*>(fixture.Json), std::strlen(fixture.Json),
                "nvmesh.v0.mesh3d.pnt.u32.clustered", "fixture.gltf", "fixture", result, error));
            assert(error == RequiredExtensionsError(fixture.Expected) && result.SourceHash == 123);
            const auto decoded = Skeletal::DecodeSkeletalGltf(json, ToString("fixture.gltf"));
            assert(decoded.Status == Skeletal::SkeletalGltfDecodeStatus::InvalidDocument);
        }
    }
    struct MorphFixture { const char* Json; bool Expected; };
    const MorphFixture morphs[] = {
        {R"({})", false},
        {R"({"extras":{"weights":[1]}})", false},
        {R"({"meshes":[{"primitives":[{"targets":null}]}]})", true},
        {R"({"meshes":[{"weights":[]}]})", true},
        {R"({"nodes":[{"weights":null}]})", true},
        {R"({"animations":[{"channels":[{"target":{"path":"weights"}}]}]})", true},
        {R"({"animations":[{"channels":[{"target":{"path":"translation"}}]}]})", false}
    };
    for (const auto& fixture : morphs)
    {
        JsonDocument document;
        assert(JsonDocument::TryParse(ToString(fixture.Json), document));
        assert(HasMorphData(document.GetRoot()) == fixture.Expected);
    }
    assert(CheckRequiredExtensions({}) == Status::InvalidRoot);
    assert(std::strcmp(RequiredExtensionsError(Status::Success), "") == 0);
    std::cout << "GltfDocumentProfileTest PASS: required_extensions_contract\n";
    return 0;
}
