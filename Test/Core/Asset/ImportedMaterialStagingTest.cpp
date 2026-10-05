// 実所有型のCPU adapterを検証する。FakeDevice/GPU描画の合格とは扱わない。
#include "Resource/ModelMaterialStaging.h"
#include "Resource/ModelAssetLoader.h"
#include "Rendering/RenderResources.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "material staging line %d: %s\n", __LINE__, #x);                                      \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace ImportedMaterialTest
{
    namespace A = NorvesLib::Core::Asset;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::ResourceIO::ModelStaging;
    namespace R = NorvesLib::Core::Rendering;
    using Status = S::MaterialStagingStatus;
    using Bytes = C::VariableArray<uint8_t>;
    using Text = C::AnsiString;
    A::CookedMeshData Make(const char* arm = "Textures/arm.nvtex")
    {
        A::CookedMeshData mesh;
        mesh.VersionMajor = 1;
        A::CookedMeshMaterial material;
        Bytes strings;
        const char* paths[] = {"Textures/犬/base.png", "Textures/normal.png", arm, "Textures/emissive.png"};
        A::CookedMeshStringRef* references[] = {&material.AlbedoTexture, &material.NormalTexture, &material.ArmTexture,
                                                &material.EmissiveTexture};
        A::CookedMaterialStringRef* wire[] = {&material.Pbr.Albedo, &material.Pbr.Normal, &material.Pbr.Arm,
                                              &material.Pbr.Emissive};
        for (size_t i = 0; i < 4; ++i)
        {
            const Text text = paths[i];
            if (!text.empty())
            {
                references[i]->StringOffset = strings.size();
                references[i]->StringLength = text.size();
                wire[i]->Offset = strings.size();
                wire[i]->Length = static_cast<uint32_t>(text.size());
                strings.insert(strings.end(), text.begin(), text.end());
            }
        }
        mesh.SourceBlob = A::AssetBlob::CopyBytes(strings, "owned-path-fixture");
        mesh.StringTableSize = strings.size();
        auto& p = material.Pbr;
        p.BaseColor[0] = .13f;
        p.BaseColor[1] = .42f;
        p.BaseColor[2] = .71f;
        p.BaseColor[3] = .31f;
        p.EmissiveColor[0] = .2f / .54384f;
        p.EmissiveColor[1] = .6f / .54384f;
        p.EmissiveColor[2] = 1.0f / .54384f;
        p.EmissiveNits = 123.25f;
        p.Metallic = -2;
        p.Roughness = -3;
        p.OcclusionStrength = .37f;
        p.NormalScale = -2.5f;
        p.AlphaCutoff = 1.7f;
        mesh.Materials.push_back(material);
        return mesh;
    }
    void Same(float a, float b)
    {
        CHECK(std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b));
    }
    void CheckValues(const A::CookedMaterialRecord& p, const S::ImportedMaterialStaging& s)
    {
        CHECK(s.Layout == S::ImportedMaterialLayout::PackedArmV1 && s.Shading == R::ShadingModel::DefaultLit);
        CHECK(static_cast<uint8_t>(s.Shading) != A::CookedMaterialFormatV1::DefaultLit);
        for (size_t i = 0; i < 4; ++i)
        {
            Same(p.BaseColor[i], s.BaseColor[i]);
        }
        for (size_t i = 0; i < 3; ++i)
        {
            Same(p.EmissiveColor[i], s.EmissiveColor[i]);
        }
        Same(p.EmissiveNits, s.EmissiveLuminanceNits);
        Same(p.Metallic, s.Metallic);
        Same(p.Roughness, s.Roughness);
        Same(p.OcclusionStrength, s.OcclusionStrength);
        Same(p.NormalScale, s.NormalScale);
        Same(p.AlphaCutoff, s.AlphaCutoff);
        CHECK(s.AlbedoPath == "Textures/犬/base.png" && s.NormalPath == "Textures/normal.png" &&
              s.ArmPath == "Textures/arm.nvtex" && s.EmissivePath == "Textures/emissive.png");
    }
    void Reject(const A::CookedMeshData& mesh, Status expected, size_t index = 0)
    {
        S::ImportedMaterialStaging out;
        out.AlbedoPath = "held";
        out.NormalScale = 77;
        out.ArmMask = 99;
        CHECK(S::BuildImportedMaterialStaging(mesh, index, out) == expected);
        CHECK(out.Layout == S::ImportedMaterialLayout::Absent && out.AlbedoPath == "held" && out.NormalScale == 77 &&
              out.ArmMask == 99);
    }
    void Run()
    {
        for (uint32_t mask = 0; mask < 8; ++mask)
        {
            for (uint32_t alpha = 0; alpha < 3; ++alpha)
            {
                for (uint32_t sided = 0; sided < 2; ++sided)
                {
                    auto mesh = Make();
                    auto& p = mesh.Materials[0].Pbr;
                    p.Flags = (mask << 3) | (alpha << 1) | sided;
                    S::ImportedMaterialStaging out;
                    CHECK(S::BuildImportedMaterialStaging(mesh, 0, out) == Status::Success);
                    CheckValues(p, out);
                    CHECK(out.ArmMask == mask && static_cast<uint8_t>(out.Alpha) == alpha &&
                          out.bDoubleSided == (sided != 0));
                    const auto held = p;
                    mesh = {};
                    CheckValues(held, out);
                    auto copy = out;
                    out = {};
                    CheckValues(held, copy);
                }
            }
        }
        {
            auto mesh = Make("");
            S::ImportedMaterialStaging out;
            CHECK(S::BuildImportedMaterialStaging(mesh, 0, out) == Status::Success && out.ArmPath.empty() &&
                  out.ArmMask == 0);
            mesh.Materials[0].Pbr.Flags = A::CookedMaterialFormatV1::ArmUseAO;
            Reject(mesh, Status::InvalidRecord);
        }
        {
            auto m = Make();
            m.VersionMajor = 0;
            Reject(m, Status::UnsupportedVersion);
            m.VersionMajor = 2;
            Reject(m, Status::UnsupportedVersion);
        }
        {
            auto m = Make();
            Reject(m, Status::InvalidMaterialIndex, 1);
        }
        {
            auto m = Make();
            m.Materials[0].Pbr.ShadingModelId = 1;
            Reject(m, Status::InvalidRecord);
        }
        {
            auto m = Make();
            m.Materials[0].Pbr.Flags = 64;
            Reject(m, Status::InvalidRecord);
            m.Materials[0].Pbr.Flags = 6;
            Reject(m, Status::InvalidRecord);
        }
        {
            auto m = Make();
            m.Materials[0].Pbr.NormalScale = std::numeric_limits<float>::infinity();
            Reject(m, Status::InvalidRecord);
        }
        {
            auto m = Make();
            m.Materials[0].Pbr.BaseColor[0] = std::numeric_limits<float>::quiet_NaN();
            Reject(m, Status::InvalidRecord);
        }
        {
            auto m = Make();
            m.Materials[0].Pbr.EmissiveNits = 0;
            Reject(m, Status::InvalidRecord);
        }
        {
            auto m = Make();
            ++m.Materials[0].NormalTexture.StringOffset;
            Reject(m, Status::InvalidReference);
        }
        {
            auto m = Make();
            m.SourceBlob = {};
            Reject(m, Status::InvalidReference);
        }
        {
            auto m = Make();
            m.StringTableOffset = std::numeric_limits<size_t>::max();
            Reject(m, Status::InvalidReference);
        }
        {
            auto m = Make();
            m.Materials[0].Pbr.Arm.Length = UINT32_MAX;
            Reject(m, Status::InvalidRecord);
        }
        {
            auto m = Make("");
            m.Materials[0].Pbr.Arm.Offset = m.Materials[0].ArmTexture.StringOffset = 1;
            Reject(m, Status::InvalidReference);
        }
        {
            auto m = Make();
            auto bytes = Bytes(m.SourceBlob.GetSpan().begin(), m.SourceBlob.GetSpan().end());
            bytes[m.Materials[0].ArmTexture.StringOffset + 1] = 0;
            m.SourceBlob = A::AssetBlob::CopyBytes(bytes);
            Reject(m, Status::InvalidPath);
        }
        {
            auto m = Make();
            m.Materials[0].Pbr.NormalScale = -0.0f;
            S::ImportedMaterialStaging out;
            CHECK(S::BuildImportedMaterialStaging(m, 0, out) == Status::Success);
            Same(out.NormalScale, -0.0f);
        }
        for (const char* path :
             {"../escape", "Assets/arm.png", "/absolute", "C:/drive.png", "Textures\\arm.png", "Textures//arm.png",
              "Textures/./arm.png", "Textures/arm\n.png", "Textures/\xc0\xaf.png"})
        {
            Reject(Make(path), Status::InvalidPath);
        }
        {
            auto m = Make();
            S::ImportedMaterialStaging imported;
            CHECK(S::BuildImportedMaterialStaging(m, 0, imported) == Status::Success);
            S::ModelStagingData staging;
            staging.ImportedMaterial = imported;
            R::RenderResources resources;
            auto finalizeStatus = S::ModelFinalizeStatus::Success;
            CHECK(!S::FinalizeModelStaging(staging, {resources.Textures(), resources.MegaGeometry()}, "test", 1, &finalizeStatus)
                       .IsValid());
            CHECK(finalizeStatus == S::ModelFinalizeStatus::UnsupportedImportedMaterial);
            CHECK(staging.ImportedMaterial.AlbedoPath == imported.AlbedoPath);
            CHECK(!NorvesLib::Core::ResourceIO::BuildModelStagingFromCookedMesh(m, "v1", "v1.nvmesh", staging));
            A::CookedMeshData legacy;
            CHECK(NorvesLib::Core::ResourceIO::BuildModelStagingFromCookedMesh(legacy, "v0", "v0.nvmesh", staging));
            CHECK(staging.ImportedMaterial.Layout == S::ImportedMaterialLayout::Absent);
        }
        std::puts("IMPORTED_MATERIAL_STAGING result=pass packed_arm_values_utf8_paths_ownership_atomic_renderer_guard");
    }
} // namespace ImportedMaterialTest
int main()
{
    ImportedMaterialTest::Run();
    return 0;
}
