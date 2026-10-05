// 明示NVMESH v1の材質cook・派生画像・cacheを実codecとPNG decodeで反証する。
#include "Tools/AssetCook/MeshCooker.h"
#include "Tools/AssetCook/MeshMaterialV1Plan.h"
#include "Tools/AssetCook/TextureCooker.h"
#include "Tools/AssetCook/GeometryInspection.h"
#include "Tools/AssetCook/SingleAssetCook.h"
#include "Tools/AssetCook/CookCacheDecision.h"
#include "Tools/AssetCook/CookOutputPackage.h"
#include "Tools/AssetCook/ModelCookCache.h"
#include "Tools/AssetCook/NativeCookPath.h"
#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Asset/AssetManifest.h"
#include "Resource/ImportSettingsFile.h"
#include "Text/UnicodeText.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "GR79 line %d: %s\n", __LINE__, #x);                                                  \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace MaterialV1Test
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace A = NorvesLib::Core::Asset;
    namespace I = NorvesLib::Core::AssetImport;
    namespace C = NorvesLib::Core::Container;
    using Bytes = C::VariableArray<uint8_t>;
    using Text = C::AnsiString;
    using View = C::AnsiStringView;
    using ByteView = C::Span<const uint8_t>;
    template <class T> using Array = C::VariableArray<T>;
    constexpr const char* V1 = "nvmesh.v1.mesh3d.pnt.u32.clustered";
    constexpr const char* V0 = "nvmesh.v0.mesh3d.pnt.u32.clustered";
    ByteView Data(View text)
    {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }
    Bytes Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        CHECK(file);
        Bytes out;
        char c;
        while (file.get(c))
        {
            out.push_back(static_cast<uint8_t>(c));
        }
        CHECK(file.eof());
        return out;
    }
    void Write(const std::filesystem::path& path, ByteView bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        CHECK(file);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        CHECK(!file.fail());
    }
    void Write(const std::filesystem::path& path, const char* text)
    {
        Write(path, Data(text));
    }
    void U32(Bytes& bytes, uint32_t value, bool big = false)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            bytes.push_back(static_cast<uint8_t>(value >> (8 * (big ? 3 - i : i))));
        }
    }
    uint32_t Crc(ByteView bytes)
    {
        uint32_t value = UINT32_MAX;
        for (uint8_t c : bytes)
        {
            value ^= c;
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
            }
        }
        return ~value;
    }
    void Chunk(Bytes& png, const char* type, ByteView data)
    {
        U32(png, static_cast<uint32_t>(data.size()), true);
        const auto start = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        U32(png, Crc({png.data() + start, png.size() - start}), true);
    }
    Bytes Png(uint32_t width, uint32_t height, ByteView pixels)
    {
        CHECK(pixels.size() == static_cast<size_t>(width) * height * 4);
        Bytes result{137, 80, 78, 71, 13, 10, 26, 10}, header;
        U32(header, width, true);
        U32(header, height, true);
        for (uint8_t c : {8, 6, 0, 0, 0})
        {
            header.push_back(c);
        }
        Chunk(result, "IHDR", header);
        Bytes filtered;
        for (size_t y = 0; y < height; ++y)
        {
            filtered.push_back(0);
            const auto begin = pixels.data() + y * width * 4;
            filtered.insert(filtered.end(), begin, begin + width * 4);
        }
        CHECK(filtered.size() <= 65535);
        const auto length = static_cast<uint16_t>(filtered.size());
        Bytes zlib{0x78,
                   0x01,
                   1,
                   static_cast<uint8_t>(length),
                   static_cast<uint8_t>(length >> 8),
                   static_cast<uint8_t>(~length),
                   static_cast<uint8_t>((~length) >> 8)};
        zlib.insert(zlib.end(), filtered.begin(), filtered.end());
        uint32_t a = 1, b = 0;
        for (uint8_t c : filtered)
        {
            a = (a + c) % 65521;
            b = (b + a) % 65521;
        }
        U32(zlib, (b << 16) | a, true);
        Chunk(result, "IDAT", zlib);
        Chunk(result, "IEND", {});
        return result;
    }
    constexpr const char* Geometry =
        R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}])json";
    constexpr uint8_t Triangle[] = {0, 0, 0, 0, 0,   0,  0, 0,   0,  0,   0,   0,  0, 0, 128, 63, 0,   0,  0, 0,   0,
                                    0, 0, 0, 0, 0,   0,  0, 0,   0,  128, 63,  0,  0, 0, 0,   0,  0,   0,  0, 0,   0,
                                    0, 0, 0, 0, 128, 63, 0, 0,   0,  0,   0,   0,  0, 0, 0,   0,  128, 63, 0, 0,   0,
                                    0, 0, 0, 0, 0,   0,  0, 128, 63, 0,   0,   0,  0, 0, 0,   0,  0,   0,  0, 128, 63,
                                    0, 0, 0, 0, 0,   0,  0, 0,   0,  0,   128, 63, 0, 0, 1,   0,  2,   0};
    Text Json(View material, View images = {}, bool glb = false, bool bHasMaterial = true, View extraMaterials = {})
    {
        Text geometry = Geometry;
        if (bHasMaterial)
        {
            // TString::replaceの既知別問題に依存せずprefix/suffixを構成する。
            const auto at = geometry.find("\"indices\":3");
            CHECK(at != Text::npos);
            Text changed;
            changed.append(geometry.data(), at);
            changed.append("\"indices\":3,\"material\":0");
            changed.append(geometry.data() + at + 11, geometry.size() - at - 11);
            geometry = std::move(changed);
        }
        Text out = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":102";
        if (!glb)
        {
            out.append(",\"uri\":\"triangle.bin\"");
        }
        out.append("}],");
        out.append(geometry);
        if (bHasMaterial || !extraMaterials.empty())
        {
            out.append(",\"materials\":[");
            out.append(material);
            if (!extraMaterials.empty())
            {
                out.append(",");
                out.append(extraMaterials);
            }
            out.append("]");
        }
        if (!images.empty())
        {
            out.append(",");
            out.append(images);
        }
        out.append("}");
        return out;
    }
    Bytes Glb(View json)
    {
        const auto jsonSize = (json.size() + 3) & ~size_t{3};
        const auto binSize = (sizeof(Triangle) + 3) & ~size_t{3};
        Bytes bytes;
        U32(bytes, 0x46546c67);
        U32(bytes, 2);
        U32(bytes, static_cast<uint32_t>(28 + jsonSize + binSize));
        U32(bytes, static_cast<uint32_t>(jsonSize));
        U32(bytes, 0x4e4f534a);
        bytes.insert(bytes.end(), json.begin(), json.end());
        while (bytes.size() < 20 + jsonSize)
        {
            bytes.push_back(' ');
        }
        U32(bytes, static_cast<uint32_t>(binSize));
        U32(bytes, 0x004e4942);
        bytes.insert(bytes.end(), std::begin(Triangle), std::end(Triangle));
        while (bytes.size() % 4)
        {
            bytes.push_back(0);
        }
        return bytes;
    }
    bool Close(float a, float b)
    {
        return std::fabs(a - b) < 0.00001f;
    }
    A::CookedMeshParseResult Parse(const MeshCookResult& result)
    {
        return A::ParseCookedMesh(A::AssetBlob::CopyBytes(result.NvmeshBytes, "material-v1-test"));
    }
    struct Fixture
    {
        std::filesystem::path Root, Source, Sidecar;
        explicit Fixture(const std::filesystem::path& root)
            : Root(root), Source(root / "model.gltf"), Sidecar(root / "model.gltf.import.json")
        {
            CHECK(std::filesystem::create_directory(Root));
            Write(Root / "triangle.bin", Triangle);
        }
        void Set(View material, View images = {}, View sidecar = {}, bool bHasMaterial = true, View extra = {})
        {
            const auto json = Json(material, images, false, bHasMaterial, extra);
            Write(Source, Data(json));
            if (sidecar.empty())
            {
                std::filesystem::remove(Sidecar);
            }
            else
            {
                Write(Sidecar, Data(sidecar));
            }
        }
        MeshCookResult Cook(const char* format = V1)
        {
            const auto bytes = Read(Source);
            MeshCookResult out;
            Text error;
            const auto success = CookGltfToNvmeshNativePath(bytes.data(), bytes.size(), format, Source,
                                                            "Models/material.gltf", out, error);
            if (!success)
            {
                std::fprintf(stderr, "v1 cook: %s\n", error.c_str());
            }
            CHECK(success);
            return out;
        }
        ModelCookFingerprint Fingerprint(const char* format = V1)
        {
            const auto bytes = Read(Source);
            ModelCookFingerprint out;
            Text error;
            CHECK(FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), format, Source,
                                                       "Models/material.gltf", out, error));
            return out;
        }
        void Reject(View expected)
        {
            const auto bytes = Read(Source);
            MeshCookResult out;
            out.SourceHash = 42;
            Text error;
            CHECK(!CookGltfToNvmeshNativePath(bytes.data(), bytes.size(), V1, Source, "Models/material.gltf", out,
                                              error));
            CHECK(out.SourceHash == 42 && error.find(expected) != Text::npos);
            ModelCookFingerprint fingerprint;
            fingerprint.SourceHash = 43;
            CHECK(!FingerprintModelCookSourceNativePath(bytes.data(), bytes.size(), V1, Source, "Models/material.gltf",
                                                        fingerprint, error));
            CHECK(fingerprint.SourceHash == 43);
        }
    };
    void GeometryAndRaw()
    {
        Array<InspectionVertex> vertices(5);
        vertices[1].Position[0] = 1;
        vertices[2].Position[1] = 1;
        vertices[3].Position[2] = 1;
        vertices[4].Position[0] = -0.0f;
        Array<uint32_t> indices{0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
        Array<uint32_t> order(vertices.size()), representatives(vertices.size()), parents(vertices.size());
        Array<GeometryClosureEdge> edges(32);
        GeometryClosureInspection closure;
        CHECK(InspectGeometryClosure(vertices, indices, order, representatives, parents, edges, {}, closure));
        CHECK(closure.bAlmostClosed && closure.UniqueEdges == 6 && closure.BoundaryEdges == 0 &&
              representatives[0] == representatives[4]);
        auto open = indices;
        open.resize(9);
        CHECK(InspectGeometryClosure(vertices, open, order, representatives, parents, edges, {}, closure));
        CHECK(!closure.bAlmostClosed && closure.BoundaryEdges == 3);
        CHECK(InspectGeometryClosure(vertices, open, order, representatives, parents, edges, {0.5}, closure) &&
              closure.bAlmostClosed);
        auto bad = indices;
        bad.insert(bad.end(), {0, 2, 1});
        CHECK(InspectGeometryClosure(vertices, bad, order, representatives, parents, edges, {}, closure));
        CHECK(!closure.bAlmostClosed && closure.NonManifoldEdges == 3);
        bad = indices;
        std::swap(bad[1], bad[2]);
        CHECK(InspectGeometryClosure(vertices, bad, order, representatives, parents, edges, {}, closure));
        CHECK(!closure.bAlmostClosed && closure.SameDirectionPairs == 3);
        bad = indices;
        bad.insert(bad.end(), {0, 4, 1});
        CHECK(InspectGeometryClosure(vertices, bad, order, representatives, parents, edges, {}, closure));
        CHECK(!closure.bAlmostClosed && closure.WeldedDegenerateTriangles == 1);
        Bytes pixels{200, 120, 60, 20, 100, 240, 10, 40, 70, 90, 220, 60, 255, 30, 80, 100};
        const auto png = Png(2, 2, pixels);
        DecodedTextureRgba8 decoded;
        Text error;
        CHECK(DecodeTextureRgba8(png, decoded, error) && decoded.Width == 2 && decoded.Height == 2 &&
              decoded.Pixels == pixels);
        for (const char* format :
             {"nvtex.v0.rgba8.linear", "nvtex.v0.rgba8.srgb", "nvtex.v0.r8.linear", "nvtex.v0.rg8.linear"})
        {
            TextureCookResult encoded, raw;
            std::string legacyError;
            CHECK(CookTextureToNvtex(png.data(), png.size(), format, "fixture.png", encoded, legacyError));
            CHECK(CookRgba8ToNvtex(pixels, 2, 2, format, raw, error));
            CHECK(raw.NvtexBytes == encoded.NvtexBytes);
        }
        TextureCookResult held;
        held.Width = 77;
        CHECK(!CookRgba8ToNvtex(pixels, 3, 2, "nvtex.v0.rgba8.linear", held, error) && held.Width == 77);
        MeshEmbeddedImage image;
        CHECK(image.SetRawRgba8(pixels, 2, 2));
        image.ImageIndex = SyntheticArmImageIndex;
        auto copy = image;
        MeshEmbeddedImage assigned;
        assigned = copy;
        CHECK(assigned.Payload == MeshImagePayload::RawRgba8 && assigned.Width == 2 && assigned.Height == 2 &&
              !assigned.IsBorrowed());
        CHECK(std::memcmp(assigned.GetBytes().data(), pixels.data(), pixels.size()) == 0);
        CHECK(!assigned.SetRawRgba8(pixels, 0, 2) && assigned.Width == 2);
        CHECK(assigned.SetBytes(png, false) && assigned.Payload == MeshImagePayload::Encoded && assigned.Width == 0 &&
              assigned.Height == 0);
    }
    void Run(const std::filesystem::path& root)
    {
        GeometryAndRaw();
        {
            Fixture f(root / "scalars");
            const char* material =
                R"({"name":"Body","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.8,0.5],"metallicFactor":0.3,"roughnessFactor":0.6},"normalTexture":{"index":0,"scale":2},"alphaMode":"MASK","alphaCutoff":0.3})";
            Bytes pixels(16, 128);
            const auto png = Png(2, 2, pixels);
            Write(f.Root / "normal.png", png);
            const char* images = R"("textures":[{"source":0}],"images":[{"uri":"normal.png"}])";
            f.Set(material, images);
            const auto cooked = f.Cook();
            const auto repeated = f.Cook();
            CHECK(cooked.NvmeshBytes == repeated.NvmeshBytes && cooked.SourceHash == f.Fingerprint().SourceHash);
            const auto parsed = Parse(cooked);
            CHECK(parsed.Succeeded() && parsed.Mesh.VersionMajor == 1);
            const auto& pbr = parsed.Mesh.Materials[0].Pbr;
            CHECK(Close(pbr.BaseColor[0], .2f) && Close(pbr.BaseColor[3], .5f) && Close(pbr.Metallic, .3f) &&
                  Close(pbr.Roughness, .6f));
            CHECK(pbr.NormalScale == 2 && Close(pbr.AlphaCutoff, .3f));
            CHECK((pbr.Flags & A::CookedMaterialFormatV1::DoubleSided) != 0 &&
                  (pbr.Flags & A::CookedMaterialFormatV1::AlphaModeMask) == 2);
            CHECK(cooked.EmbeddedImages.size() == 1 && cooked.EmbeddedImages[0].Format == "nvtex.v0.rgba8.linear");
            const auto glb = Glb(Json(material, images, true));
            MeshCookResult binary;
            Text error;
            CHECK(CookGltfToNvmeshNativePath(glb.data(), glb.size(), V1, f.Root / "model.glb", "Models/material.gltf",
                                             binary, error));
            CHECK(binary.NvmeshBytes == cooked.NvmeshBytes);
            f.Set(material, images,
                  R"({"version":1,"material":{"doubleSided":"force_false","alphaMode":"force_opaque"}})");
            const auto forced = Parse(f.Cook());
            CHECK(forced.Succeeded() && (forced.Mesh.Materials[0].Pbr.Flags & 7) == 0);
        }
        constexpr const char* MrImages = R"("textures":[{"source":0}],"images":[{"uri":"mr.png"}])";
        constexpr const char* MrMaterial =
            R"({"name":"Body","pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":0},"metallicFactor":0.6,"roughnessFactor":0.5}})";
        {
            Fixture f(root / "folded");
            Bytes pixels(32 * 32 * 4);
            for (size_t i = 0; i < 1024; ++i)
            {
                pixels[i * 4] = static_cast<uint8_t>(i);
                pixels[i * 4 + 1] = 254;
                pixels[i * 4 + 2] = static_cast<uint8_t>(192 + i % 64);
                pixels[i * 4 + 3] = 255;
            }
            pixels[1] = 0;
            Write(f.Root / "mr.png", Png(32, 32, pixels));
            f.Set(MrMaterial, MrImages, R"({"version":1,"material":{"profile":"ai_generated"}})");
            const auto cooked = f.Cook();
            const auto fingerprint = f.Fingerprint();
            const auto parsed = Parse(cooked);
            CHECK(parsed.Succeeded() && cooked.SourceHash == fingerprint.SourceHash && cooked.EmbeddedImages.empty() &&
                  fingerprint.EmbeddedImages.empty());
            const auto& material = parsed.Mesh.Materials[0];
            CHECK(material.ArmTexture.StringLength == 0 &&
                  (material.Pbr.Flags & A::CookedMaterialFormatV1::ArmUseMask) == 0);
            CHECK(material.Pbr.Metallic == 0 && material.Pbr.OcclusionStrength == 1 && material.Pbr.Roughness > .49f);
            pixels[5] = 250;
            Write(f.Root / "mr.png", Png(32, 32, pixels));
            CHECK(f.Fingerprint().SourceHash != fingerprint.SourceHash &&
                  f.Cook().SourceHash == f.Fingerprint().SourceHash);
            f.Set(MrMaterial, MrImages, R"({"material":{"profile":"ai_generated"},"version":1})");
            const auto canonical = f.Fingerprint();
            f.Set(MrMaterial, MrImages, R"({"version":1,"material":{"profile":"ai_generated"}})");
            CHECK(f.Fingerprint().SourceHash == canonical.SourceHash);
        }
        {
            Fixture f(root / "baked");
            Bytes pixels{40, 128, 192, 31, 80, 64, 128, 32, 120, 255, 64, 33, 160, 0, 255, 34};
            Write(f.Root / "mr.png", Png(2, 2, pixels));
            Bytes ao{80, 80, 80, 255, 160, 160, 160, 255};
            Write(f.Root / "ao.png", Png(2, 1, ao));
            const char* images =
                R"("textures":[{"source":0},{"source":1}],"images":[{"uri":"mr.png"},{"uri":"ao.png"}])";
            const char* material =
                R"({"name":"Body","pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":0},"metallicFactor":0.6,"roughnessFactor":0.5},"occlusionTexture":{"index":1,"strength":0.3}})";
            f.Set(
                material, images,
                R"({"version":1,"material":{"profile":"ai_generated"},"materials":[{"name":"Body","arm":{"roughness":"texture","metallic":"texture"}}]})");
            const auto cooked = f.Cook();
            const auto parsed = Parse(cooked);
            CHECK(parsed.Succeeded() && cooked.EmbeddedImages.size() == 1);
            const auto& image = cooked.EmbeddedImages[0];
            CHECK(image.Payload == MeshImagePayload::RawRgba8 && image.Width == 2 && image.Height == 2 &&
                  image.ImageIndex == SyntheticArmImageIndex);
            const auto raw = image.GetBytes();
            CHECK(raw[0] == 255 && raw[1] == 64 && raw[2] == 115 && raw[3] == 31);
            CHECK(parsed.Mesh.Materials[0].Pbr.Metallic < 0 && parsed.Mesh.Materials[0].Pbr.Roughness < 0);
            CHECK((parsed.Mesh.Materials[0].Pbr.Flags & A::CookedMaterialFormatV1::ArmUseMask) ==
                  (A::CookedMaterialFormatV1::ArmUseRoughness | A::CookedMaterialFormatV1::ArmUseMetallic));
            CHECK(cooked.SourceHash == f.Fingerprint().SourceHash);
            f.Set(
                material, images,
                R"({"version":1,"material":{"arm":{"occlusion":"texture","roughness":"texture","metallic":"texture"}}})");
            f.Reject("arm");
            f.Set(
                material, images,
                R"({"version":1,"material":{"arm":{"occlusion":"constant:0.7","roughness":"texture","metallic":"texture"}}})");
            const auto constantAo = f.Cook();
            CHECK(constantAo.EmbeddedImages[0].GetBytes()[0] == 179);
            CHECK(Close(Parse(constantAo).Mesh.Materials[0].Pbr.OcclusionStrength, .7f));
        }
        {
            Fixture f(root / "emission");
            Bytes pixels(16, 255);
            Write(f.Root / "em.png", Png(2, 2, pixels));
            const char* images = R"("textures":[{"source":0}],"images":[{"uri":"em.png"}])";
            const char* material =
                R"({"name":"Glow","emissiveFactor":[1,0.5,0.25],"emissiveTexture":{"index":0},"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":2}}})";
            f.Set(material, images);
            f.Reject("emissiveNitsPerUnit");
            f.Set(material, images, R"({"version":1,"material":{"emissiveNitsPerUnit":10}})");
            const auto cooked = f.Cook();
            const auto parsed = Parse(cooked);
            CHECK(parsed.Succeeded());
            const auto& p = parsed.Mesh.Materials[0].Pbr;
            CHECK(Close(p.EmissiveColor[0] * p.EmissiveNits, 20) && Close(p.EmissiveColor[1] * p.EmissiveNits, 10));
            CHECK(cooked.EmbeddedImages.size() == 1 && cooked.EmbeddedImages[0].Format == "nvtex.v0.rgba8.srgb");
            f.Set(R"({"name":"Dark","emissiveTexture":{"index":0}})", images);
            const auto dark = Parse(f.Cook());
            CHECK(dark.Succeeded() && dark.Mesh.Materials[0].Pbr.EmissiveNits == 0 &&
                  dark.Mesh.Materials[0].EmissiveTexture.StringLength != 0);
        }
        {
            Fixture f(root / "selectors");
            f.Set(R"({"name":"Body"})", {}, R"({"version":1,"materials":[{"name":"Missing"}]})");
            f.Reject("material_selector");
            f.Set(R"({"name":"Body"})", {},
                  R"({"version":1,"materials":[{"name":"Body"},{"index":0,"expectedName":"Body"}]})");
            f.Reject("material_selector");
            f.Set(R"({"name":"Body"})", {}, R"({"version":1,"materials":[{"name":"Body","surface":"Stone"}]})");
            f.Reject("surface_requires_GR81");
            f.Set(R"({"name":"Body"})", {}, {}, true, R"({"name":"Body"})");
            CHECK(f.Cook().DuplicateMaterialNameGroups == 1 && f.Fingerprint().DuplicateMaterialNameGroups == 1);
            f.Set("{}", {}, R"({"version":1,"materials":[{"index":0,"expectedName":""}]})");
            CHECK(Parse(f.Cook()).Succeeded());
            f.Set("{}", {}, {}, false);
            CHECK(Parse(f.Cook()).Succeeded());
        }
        {
            Fixture f(root / "service");
            Bytes pixels(16, 255);
            pixels[1] = 128;
            pixels[2] = 230;
            Write(f.Root / "mr.png", Png(2, 2, pixels));
            const char* material = R"({"name":"Body","pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":0}}})";
            const char* images = R"("textures":[{"source":0}],"images":[{"uri":"mr.png"}])";
            f.Set(material, images, R"({"version":1,"material":{"arm":{"roughness":"texture","metallic":"texture"}}})");
            SingleAssetCookRequest request;
            request.InputPath = f.Source;
            request.PackagePath = f.Root / "Cooked/model.nvpkg";
            request.ManifestPath = f.Root / "manifest.json";
            request.LogicalPath = "Models/material.gltf";
            request.Kind = "model";
            request.EntryName = "__model__";
            request.EntryTypeText = "Msh0";
            request.Format = V1;
            request.Variant = "default";
            request.bSkipIfUnchanged = true;
            Text error;
            CookDecisionContext before;
            CHECK(DecideCookCache(request, 7, true, nullptr, nullptr, before, error) == CookDecision::Cook);
            CHECK(CookSingleAsset(before.Request, error));
            const auto manifestBytes = Read(request.ManifestPath);
            C::String manifestText;
            for (auto byte : manifestBytes)
            {
                CHECK(byte < 128);
                manifestText.push_back(static_cast<C::String::value_type>(byte));
            }
            A::AssetManifest manifest;
            CHECK(manifest.LoadFromJsonText(manifestText));
            CHECK(manifest.GetReferenceCount() == 2);
            auto model = manifest.Resolve(request.LogicalPath, A::AssetKind::Model).Reference;
            CHECK(model.CookedVersion == 1 && model.Format == V1);
            const auto package = Read(request.PackagePath);
            CookOutputPackageFingerprint verified;
            CHECK(ValidateCookOutputPackage(model, package, verified, error));
            model.CookedVersion = 0;
            CHECK(!ValidateCookOutputPackage(model, package, verified, error));
            model.CookedVersion = 1;
            model.Format = V0;
            CHECK(!ValidateCookOutputPackage(model, package, verified, error));
            CookOutputRecord record;
            CHECK(CaptureCookOutputRecord(before, manifest, record, error));
            CookDecisionContext current;
            CHECK(DecideCookCache(request, 7, true, &record, &manifest, current, error) == CookDecision::Skip);
            CHECK(IsModelCookCacheCurrent(request.ManifestPath, request.PackagePath, request.LogicalPath,
                                          request.Variant, request.Format, request.EntryName, f.Fingerprint()));
            const auto packageTime = std::filesystem::last_write_time(request.PackagePath);
            const auto manifestTime = std::filesystem::last_write_time(request.ManifestPath);
            CHECK(CookSingleAsset(request, error));
            CHECK(package == Read(request.PackagePath) && manifestBytes == Read(request.ManifestPath));
            CHECK(packageTime == std::filesystem::last_write_time(request.PackagePath) &&
                  manifestTime == std::filesystem::last_write_time(request.ManifestPath));
            pixels[1] = 32;
            Write(f.Root / "mr.png", Png(2, 2, pixels));
            CHECK(DecideCookCache(request, 7, true, &record, &manifest, current, error) == CookDecision::Cook);
            CHECK(!IsModelCookCacheCurrent(request.ManifestPath, request.PackagePath, request.LogicalPath,
                                           request.Variant, request.Format, request.EntryName, f.Fingerprint()));
            CHECK(CookSingleAsset(request, error));
            CHECK(manifestBytes != Read(request.ManifestPath));
            const auto beforeSettings = f.Fingerprint().SourceHash;
            f.Set(material, images, R"({"version":1,"units":{"scale":2},"material":{"arm":{"roughness":"texture","metallic":"texture"}}})");
            CHECK(beforeSettings != f.Fingerprint().SourceHash);
            CHECK(!IsModelCookCacheCurrent(request.ManifestPath, request.PackagePath, request.LogicalPath,
                request.Variant, request.Format, request.EntryName, f.Fingerprint()));

            // sourceの外部画像をpackageで上書きする指定を、出力前に拒否する。
            const auto sourceBefore = Read(f.Root / "mr.png");
            const auto manifestBefore = Read(request.ManifestPath);
            request.PackagePath = f.Root / "mr.png";
            CHECK(!CookSingleAsset(request, error));
            CHECK(sourceBefore == Read(f.Root / "mr.png") && manifestBefore == Read(request.ManifestPath));
        }
        std::puts("GLTF_MATERIAL_V1 result=pass wire128_arm_emission_selectors_geometry_raw_texture_cache_alias");
    }
} // namespace MaterialV1Test
int main()
{
#if defined(_WIN32)
    wchar_t temp[32768]{};
    const auto length = GetTempPathW(32768, temp);
    CHECK(length && length < 32768);
    wchar_t name[128]{};
    std::swprintf(name, 128, L"NorvesMaterialV1-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    const auto root = std::filesystem::path(temp) / name;
    CHECK(std::filesystem::create_directory(root));
    MaterialV1Test::Run(root);
    CHECK(std::filesystem::remove_all(root) > 0);
    return 0;
#else
    return 125;
#endif
}
