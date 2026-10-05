// CPU/FakeDeviceの契約試験。実GPU描画・画像goldenの代替ではない。
#include "Resource/ImportedOpaqueRuntime.h"
#include "Resource/ModelAssetLoader.h"
#include "Rendering/ConstantMaterialTextureCache.h"
#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "Asset/AssetSystem.h"
#include "Tools/AssetCook/AssetCookOutput.h"
#include "Tools/AssetCook/TextureCooker.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <limits>
#include <utility>
#include <stdexcept>
#if defined(_WIN32)
#include <Windows.h>
#endif
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "opaque runtime line %d: %s\n", __LINE__, #x);                                        \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace ImportedRuntimeTest
{
    namespace C = NorvesLib::Core::Container;
    namespace A = NorvesLib::Core::Asset;
    namespace R = NorvesLib::Core::Rendering;
    namespace S = NorvesLib::Core::ResourceIO::ModelStaging;
    namespace M = NorvesLib::Core::ResourceIO;
    namespace H = NorvesLib::RHI;
    namespace Cook = NorvesLib::Tools::AssetCook;
    namespace Support = NorvesLib::Test::CookedModelSupport;
    using Bytes = C::VariableArray<uint8_t>;
    using Text = C::AnsiString;
    template <class T> using Array = C::VariableArray<T>;
    using C::MakeShared;
    using ByteView = C::Span<const uint8_t>;
    struct Update
    {
        uint32_t Row = 0, Slice = 0, Mip = 0, Layer = 0;
        Bytes Pixels;
    };
    class FakeTexture final : public H::ITexture
    {
      public:
        explicit FakeTexture(const H::TextureDesc& desc, bool* throwUpdate = nullptr)
            : ThrowUpdate(throwUpdate), Desc(desc)
        {
        }
        uint32_t GetWidth() const override
        {
            return Desc.Width;
        }
        uint32_t GetHeight() const override
        {
            return Desc.Height;
        }
        uint32_t GetDepth() const override
        {
            return Desc.Depth;
        }
        uint32_t GetMipLevels() const override
        {
            return Desc.MipLevels;
        }
        uint32_t GetArraySize() const override
        {
            return Desc.ArraySize;
        }
        H::Format GetFormat() const override
        {
            return Desc.TextureFormat;
        }
        H::ResourceUsage GetUsage() const override
        {
            return Desc.Usage;
        }
        bool IsCubemap() const override
        {
            return Desc.IsCubemap;
        }
        void Update(const void* data, uint32_t row, uint32_t slice, uint32_t mip = 0, uint32_t layer = 0) override
        {
            if (ThrowUpdate && *ThrowUpdate)
            {
                throw std::runtime_error("test texture update failure");
            }
            CHECK(data);
            ImportedRuntimeTest::Update update;
            update.Row = row;
            update.Slice = slice;
            update.Mip = mip;
            update.Layer = layer;
            const auto* bytes = static_cast<const uint8_t*>(data);
            update.Pixels.assign(bytes, bytes + slice);
            Updates.push_back(std::move(update));
        }
        bool* ThrowUpdate = nullptr;
        H::TextureDesc Desc;
        Array<ImportedRuntimeTest::Update> Updates;
    };
    class FakeBuffer final : public H::IBuffer
    {
      public:
        explicit FakeBuffer(const H::BufferDesc& desc) : Desc(desc), Data(static_cast<size_t>(desc.Size))
        {
        }
        uint64_t GetSize() const override
        {
            return Desc.Size;
        }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Data.size() ? Data.data() + static_cast<size_t>(offset) : nullptr;
        }
        void Unmap() override
        {
        }
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            CHECK(data && offset <= Data.size() && size <= Data.size() - offset);
            std::memcpy(Data.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
        }
        H::ResourceUsage GetUsage() const override
        {
            return Desc.Usage;
        }
        H::BufferDesc Desc;
        Bytes Data;
    };
    class FakeDevice final : public H::IDevice
    {
      public:
        H::BufferPtr CreateBuffer(const H::BufferDesc& desc) override
        {
            if (bFailBuffer)
            {
                return {};
            }
            auto b = MakeShared<FakeBuffer>(desc);
            Buffers.push_back(b);
            return b;
        }
        H::TexturePtr CreateTexture(const H::TextureDesc& desc) override
        {
            ++TextureAttempts;
            if (bFailTexture || TextureAttempts == FailTextureAttempt)
            {
                return {};
            }
            auto t = MakeShared<FakeTexture>(desc, &bThrowTextureUpdate);
            Textures.push_back(t);
            return t;
        }
        H::SamplerPtr CreateSampler(const H::SamplerDesc&) override
        {
            return {};
        }
        H::ShaderPtr CreateShader(const H::ShaderDesc&) override
        {
            return {};
        }
        H::CommandListPtr CreateCommandList() override
        {
            return {};
        }
        H::SwapChainPtr CreateSwapChain(const H::SwapChainDesc&) override
        {
            return {};
        }
        H::RenderPassPtr CreateRenderPass(const H::RenderPassDesc&) override
        {
            return {};
        }
        H::FramebufferPtr CreateFramebuffer(const H::FramebufferDesc&) override
        {
            return {};
        }
        H::PipelinePtr CreateGraphicsPipeline(const H::GraphicsPipelineDesc&) override
        {
            return {};
        }
        H::PipelinePtr CreateComputePipeline(const H::ComputePipelineDesc&) override
        {
            return {};
        }
        H::DescriptorSetPtr CreateDescriptorSet(const H::DescriptorSetDesc&) override
        {
            return {};
        }
        H::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return {};
        }
        H::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }
        void WaitIdle() override
        {
        }
        H::API GetAPI() const override
        {
            return H::API::None;
        }
        const H::DeviceCapabilities& GetCapabilities() const override
        {
            return Capabilities;
        }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(const NorvesLib::Math::Matrix4x4& p,
                                                                bool flip = true) const override
        {
            (void)flip;
            return p;
        }
        H::DeviceCapabilities Capabilities;
        Array<C::TSharedPtr<FakeTexture>> Textures;
        Array<C::TSharedPtr<FakeBuffer>> Buffers;
        size_t TextureAttempts = 0;
        bool bFailTexture = false, bFailBuffer = false, bThrowTextureUpdate = false;
        size_t FailTextureAttempt = 0;
    };
    void Put(Bytes& bytes, size_t offset, uint64_t value, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
        }
    }
    uint64_t Read(const Bytes& bytes, size_t offset, size_t count)
    {
        uint64_t value = 0;
        for (size_t i = 0; i < count; ++i)
        {
            value |= uint64_t(bytes[offset + i]) << (8 * i);
        }
        return value;
    }
    Bytes Mesh()
    {
        namespace V0 = A::CookedMeshFormatV0;
        namespace V1 = A::CookedMeshFormatV1;
        const Bytes old(Support::BuildCookedModelMesh());
        Bytes bytes(old.size() + 112, 0);
        std::memcpy(bytes.data(), old.data(), 320);
        std::memcpy(bytes.data(), V1::Magic, V1::MagicSize);
        Put(bytes, V0::HeaderOffset::VersionMajor, 1, 2);
        Put(bytes, V0::HeaderOffset::MaterialRecordSize, 128, 4);
        Put(bytes, V0::HeaderOffset::ClusterRecordSize, 128, 4);
        Put(bytes, V0::HeaderOffset::FileSize, bytes.size(), 8);
        Put(bytes, V0::HeaderOffset::MaterialTableSize, 128, 8);
        Put(bytes, V0::HeaderOffset::ClusterTableOffset, 448, 8);
        Put(bytes, V0::HeaderOffset::ClusterTableSize, 128, 8);
        Put(bytes, V0::HeaderOffset::StringTableOffset, 576, 8);
        Put(bytes, V0::HeaderOffset::VertexPayloadOffset, 576, 8);
        const uint64_t oldIndices = Read(old, V0::HeaderOffset::IndexPayloadOffset, 8);
        Put(bytes, V0::HeaderOffset::IndexPayloadOffset, oldIndices + 112, 8);
        std::memcpy(bytes.data() + 448, old.data() + 384, 80);
        std::memcpy(bytes.data() + 576, old.data() + 464, old.size() - 464);
        A::CookedMaterialRecord material;
        material.BaseColor[0] = .2f;
        material.BaseColor[1] = .4f;
        material.BaseColor[2] = .8f;
        material.BaseColor[3] = .25f;
        material.Metallic = .25f;
        material.Roughness = .75f;
        material.OcclusionStrength = .6f;
        CHECK(A::WriteCookedMaterialRecord(material, 0, {bytes.data() + 320, 128}) == A::CookedMaterialStatus::Success);
        Put(bytes, V0::HeaderOffset::PayloadHash,
            A::ComputeCookedMeshPayloadHash(bytes.data() + 256, bytes.size() - 256), 8);
        CHECK(A::ParseCookedMesh(A::AssetBlob::CopyBytes(bytes)).Succeeded());
        return bytes;
    }
    A::CookedTextureData Texture(const Bytes& pixels, bool srgb = false)
    {
        Cook::TextureCookResult cooked;
        Text reason;
        CHECK(Cook::CookRgba8ToNvtex(pixels, 2, 2, srgb ? "nvtex.v0.rgba8.srgb" : "nvtex.v0.rgba8.linear", cooked,
                                     reason));
        const auto parsed = A::ParseCookedTexture(A::AssetBlob::CopyBytes(cooked.NvtexBytes));
        CHECK(parsed.Succeeded());
        return parsed.Texture;
    }
    S::ModelStagingData Stage()
    {
        const auto parsed = A::ParseCookedMesh(A::AssetBlob::CopyBytes(Mesh()));
        CHECK(parsed.Succeeded());
        S::ModelStagingData result;
        CHECK(M::BuildModelStagingFromCookedMesh(parsed.Mesh, "runtime", "Models/runtime", result));
        return result;
    }
    void Pure()
    {
        Text reason;
        auto material = Stage().ImportedMaterial;
        for (float alpha : {0.0f, .5f, 1.0f})
        {
            material.BaseColor[3] = alpha;
            CHECK(S::ValidateImportedOpaqueMaterial(material, reason));
        }
        material.EmissivePath = "Textures/missing_emissive";
        CHECK(S::ValidateImportedOpaqueMaterial(material, reason));
        material.EmissiveColor[0] = material.EmissiveColor[1] = material.EmissiveColor[2] = 1;
        material.EmissiveLuminanceNits = 10;
        CHECK(!S::ValidateImportedOpaqueMaterial(material, reason) && reason.find("emissiveTexture") != Text::npos);
        material.EmissivePath.clear();
        CHECK(S::ValidateImportedOpaqueMaterial(material, reason));
        auto bad = material;
        bad.NormalScale = 0;
        CHECK(!S::ValidateImportedOpaqueMaterial(bad, reason));
        bad = material;
        bad.bDoubleSided = true;
        CHECK(!S::ValidateImportedOpaqueMaterial(bad, reason));
        for (auto alpha : {S::ImportedAlphaMode::Mask, S::ImportedAlphaMode::Blend})
        {
            bad = material;
            bad.Alpha = alpha;
            CHECK(!S::ValidateImportedOpaqueMaterial(bad, reason));
        }
        bad = material;
        bad.Shading = R::ShadingModel::Unlit;
        CHECK(!S::ValidateImportedOpaqueMaterial(bad, reason));
        bad = material;
        bad.Roughness = std::numeric_limits<float>::quiet_NaN();
        CHECK(!S::ValidateImportedOpaqueMaterial(bad, reason));
        bad = material;
        bad.ArmMask = 1;
        CHECK(!S::ValidateImportedOpaqueMaterial(bad, reason));
        bad = material;
        bad.ArmPath = "../escape";
        CHECK(!S::ValidateImportedOpaqueMaterial(bad, reason));
        const Bytes pixels{10, 30, 50, 255, 20, 40, 60, 255, 70, 90, 110, 255, 80, 100, 120, 255};
        const auto linear = Texture(pixels);
        const auto srgb = Texture(pixels, true);
        CHECK(S::ValidateImportedTexture(srgb, A::CookedTextureColorSpace::SRGB, true, reason));
        CHECK(!S::ValidateImportedTexture(linear, A::CookedTextureColorSpace::SRGB, true, reason));
        for (uint8_t mask = 0; mask < 8; ++mask)
        {
            S::ImportedArmChannels channels;
            CHECK(S::SplitImportedArmChannels(linear, mask, channels, reason));
            CHECK(channels.Mips.size() == (mask ? 2u : 0u));
            if (!mask)
            {
                continue;
            }
            for (size_t c = 0; c < 3; ++c)
            {
                CHECK(channels.Mips[0].Pixels[c].size() == ((mask & (1u << c)) ? 4u : 0u));
                for (size_t i = 0; i < channels.Mips[0].Pixels[c].size(); ++i)
                {
                    CHECK(channels.Mips[0].Pixels[c][i] == pixels[i * 4 + c]);
                }
            }
        }
        S::ImportedArmChannels held;
        held.Mips.resize(1);
        held.Mips[0].Width = 77;
        CHECK(!S::SplitImportedArmChannels(linear, 8, held, reason) && held.Mips.size() == 1 &&
              held.Mips[0].Width == 77);
        CHECK(S::SplitImportedArmChannels({}, 0, held, reason) && held.Mips.empty());
        auto broken = srgb;
        Bytes wire(broken.SourceBlob.GetSpan().begin(), broken.SourceBlob.GetSpan().end());
        wire[broken.Mips[1].DataOffset + 3] = 0;
        broken.SourceBlob = A::AssetBlob::CopyBytes(wire);
        CHECK(!S::ValidateImportedTexture(broken, A::CookedTextureColorSpace::SRGB, true, reason));
        CHECK(S::ValidateImportedTexture(broken, A::CookedTextureColorSpace::SRGB, false, reason));
        broken = srgb;
        broken.Mips[1].Width = 2;
        CHECK(!S::ValidateImportedTexture(broken, A::CookedTextureColorSpace::SRGB, true, reason));
        broken = srgb;
        broken.Mips.resize(1);
        broken.MipCount = 1;
        CHECK(!S::ValidateImportedTexture(broken, A::CookedTextureColorSpace::SRGB, true, reason));
        broken = srgb;
        broken.SourceBlob = {};
        CHECK(!S::ValidateImportedTexture(broken, A::CookedTextureColorSpace::SRGB, true, reason));
    }
    void Constants()
    {
        auto device = MakeShared<FakeDevice>();
        R::ConstantMaterialTextureCache cache;
        const auto first = cache.GetOrCreate(device.get(), .5f);
        CHECK(first && device->Textures.size() == 1);
        CHECK(device->Textures[0]->Updates.size() == 1 &&
              device->Textures[0]->Updates[0].Pixels == Bytes({128, 128, 128, 255}));
        CHECK(first == cache.GetOrCreate(device.get(), .5001f) && device->Textures.size() == 1);
        CHECK(cache.GetOrCreate(device.get(), -1) && cache.GetOrCreate(device.get(), 2));
        CHECK(device->Textures.size() == 3 && device->Textures[1]->Updates[0].Pixels[0] == 0 &&
              device->Textures[2]->Updates[0].Pixels[0] == 255);
        CHECK(!cache.GetOrCreate(device.get(), std::numeric_limits<float>::infinity()));
        CHECK(!cache.GetOrCreate(nullptr, .2f));
        device->bFailTexture = true;
        CHECK(!cache.GetOrCreate(device.get(), .2f));
        device->bFailTexture = false;
        CHECK(cache.GetOrCreate(device.get(), .2f) && device->Textures.size() == 4);
        auto other = MakeShared<FakeDevice>();
        CHECK(cache.GetOrCreate(other.get(), .5f) && other->Textures.size() == 1);
        cache.Clear();
        CHECK(cache.GetOrCreate(other.get(), .5f) && other->Textures.size() == 2);
        cache.Clear();
    }
    void Write(const std::filesystem::path& path, ByteView bytes)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        CHECK(f);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        f.close();
        CHECK(!f.fail());
    }
    C::String CoreText(const Text& text)
    {
        return C::String(C::StringView(text.data(), text.size()));
    }
    Text Package(const std::filesystem::path& root, const char* name, const char* logical, const Bytes& payload,
                 bool model = false, uint32_t version = 0, const char* textureFormat = "nvtex.v0.rgba8.linear")
    {
        Bytes package;
        uint64_t hash = 0;
        std::string error;
        CHECK(Cook::Detail::BuildSingleSkeletalEntryPackage("asset",
                                                            model ? A::MakeAssetPackageFourCC('M', 's', 'h', '0')
                                                                  : A::MakeAssetPackageFourCC('T', 'e', 'x', '0'),
                                                            payload, package, hash, error));
        Write(root / name, package);
        Text out = "{\"logical_path\":\"";
        out += logical;
        out += "\",\"kind\":\"";
        out += model ? "model" : "texture";
        out += "\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"";
        out += model ? "nvmesh.v1.mesh3d.pnt.u32.clustered" : textureFormat;
        out += "\",\"cooked_package\":\"";
        out += name;
        out += "\",\"entry_name\":\"asset\",\"entry_type\":\"";
        out += model ? "Msh0" : "Tex0";
        out += "\",\"cooked_hash\":\"";
        out += A::FormatAssetHashHex(hash);
        out += "\",\"cooked_version\":";
        out += version ? "1" : "0";
        out += "}";
        return out;
    }
    Text Manifest(const Text& entries)
    {
        return Text("{\"version\":1,\"assets\":[") + entries + "]}";
    }
    Bytes Nvtex(const Bytes& pixels, const char* format = "nvtex.v0.rgba8.linear")
    {
        Cook::TextureCookResult out;
        Text reason;
        CHECK(Cook::CookRgba8ToNvtex(pixels, 2, 2, format, out, reason));
        return out.NvtexBytes;
    }
    void SetupTextures(R::RenderResources& resources, const std::filesystem::path& root, const Text& entries)
    {
        CHECK(resources.Textures().SetTextureAssetRoot(Support::ToCoreString(root.generic_string())));
        CHECK(resources.Textures().LoadTextureAssetManifestFromJsonText(CoreText(Manifest(entries))));
    }
    void Runtime(const std::filesystem::path& root)
    {
        const Bytes pixels{10, 30, 50, 255, 20, 40, 60, 255, 70, 90, 110, 255, 80, 100, 120, 255};
        const auto armEntry = Package(root, "arm.nvpkg", "Textures/arm", Nvtex(pixels));
        for (uint8_t mask = 0; mask < 8; ++mask)
        {
            R::RenderResources resources;
            auto device = MakeShared<FakeDevice>();
            CHECK(resources.Initialize(device));
            SetupTextures(resources, root, armEntry);
            auto staging = Stage();
            auto& m = staging.ImportedMaterial;
            m.ArmPath = "Textures/arm";
            m.ArmMask = mask;
            m.EmissivePath = "Textures/does_not_exist";
            if (!mask)
            {
                std::filesystem::rename(root / "arm.nvpkg", root / "arm-held.nvpkg");
            }
            auto status = S::ModelFinalizeStatus::Failed;
            const auto model =
                S::FinalizeModelStaging(staging, {resources.Textures(), resources.MegaGeometry()}, "test", 1, &status);
            if (!mask)
            {
                std::filesystem::rename(root / "arm-held.nvpkg", root / "arm.nvpkg");
            }
            CHECK(model.IsValid() && status == S::ModelFinalizeStatus::Success);
            const auto handle = resources.MegaGeometry().GetModelMegaMeshHandle(model);
            const auto* gpu = resources.MegaGeometry().GetMegaMeshGPUData(handle);
            CHECK(gpu);
            CHECK(gpu->Material.BaseColor[0] == .2f && gpu->Material.BaseColor[3] == .25f);
            CHECK(gpu->Material.Metallic == .25f && gpu->Material.Roughness == .75f &&
                  gpu->Material.OcclusionStrength == .6f);
            CHECK(device->Textures.size() == std::popcount(static_cast<unsigned>(mask)) && device->Buffers.size() == 3);
            const H::ITexture* textures[] = {gpu->Material.AOTexture.get(), gpu->Material.RoughnessTexture.get(),
                                             gpu->Material.MetallicTexture.get()};
            for (size_t c = 0; c < 3; ++c)
            {
                CHECK(static_cast<bool>(textures[c]) == ((mask & (1u << c)) != 0));
                if (textures[c])
                {
                    const auto* texture = static_cast<const FakeTexture*>(textures[c]);
                    CHECK(texture->Desc.TextureFormat == H::Format::R8_UNORM);
                    CHECK(texture->Updates.size() == 2 && texture->Updates[0].Pixels.size() == 4);
                    CHECK(texture->Updates[1].Mip == 1 &&
                          texture->Updates[1].Pixels == Bytes({static_cast<uint8_t>(45 + c * 20)}));
                    for (size_t i = 0; i < 4; ++i)
                    {
                        CHECK(texture->Updates[0].Pixels[i] == pixels[i * 4 + c]);
                    }
                }
            }
            CHECK(resources.GetResourceStats().TextureCount == 0);
            Array<C::TWeakPtr<FakeTexture>> weak;
            for (const auto& texture : device->Textures)
            {
                weak.push_back(texture);
            }
            device->Textures.clear();
            for (const auto& texture : weak)
            {
                CHECK(!texture.expired());
            }
            resources.MegaGeometry().ReleaseModel(model);
            for (const auto& texture : weak)
            {
                CHECK(texture.expired());
            }
            resources.Shutdown();
        }
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            R::RenderResources resources;
            auto device = MakeShared<FakeDevice>();
            CHECK(resources.Initialize(device));
            auto staging = Stage();
            auto& m = staging.ImportedMaterial;
            if (mode == 0)
            {
                m.NormalScale = 2;
            }
            if (mode == 1)
            {
                m.bDoubleSided = true;
            }
            if (mode == 2)
            {
                m.Alpha = S::ImportedAlphaMode::Mask;
            }
            if (mode == 3)
            {
                m.EmissivePath = "Textures/em";
                m.EmissiveLuminanceNits = 1;
                m.EmissiveColor[0] = m.EmissiveColor[1] = m.EmissiveColor[2] = 1;
            }
            auto status = S::ModelFinalizeStatus::Success;
            CHECK(
                !S::FinalizeModelStaging(staging, {resources.Textures(), resources.MegaGeometry()}, "test", 1, &status)
                     .IsValid());
            CHECK(status == S::ModelFinalizeStatus::UnsupportedImportedMaterial && device->Textures.empty() &&
                  device->Buffers.empty());
            resources.Shutdown();
        }
        for (unsigned failure = 0; failure < 3; ++failure)
        {
            R::RenderResources resources;
            auto device = MakeShared<FakeDevice>();
            CHECK(resources.Initialize(device));
            SetupTextures(resources, root, armEntry);
            auto staging = Stage();
            staging.ImportedMaterial.ArmPath = "Textures/arm";
            staging.ImportedMaterial.ArmMask = 7;
            if (failure == 0)
            {
                device->FailTextureAttempt = 2;
            }
            if (failure == 1)
            {
                device->bThrowTextureUpdate = true;
            }
            if (failure == 2)
            {
                device->bFailBuffer = true;
            }
            bool bThrew = false;
            R::ModelHandle result;
            try
            {
                result = S::FinalizeModelStaging(staging, {resources.Textures(), resources.MegaGeometry()}, "test", 1);
            }
            catch (const std::runtime_error&)
            {
                bThrew = true;
            }
            CHECK(!result.IsValid() && bThrew == (failure == 1));
            CHECK(device->Textures.size() == (failure == 2 ? 3u : 1u));
            CHECK(resources.GetResourceStats().TextureCount == 0);
            Array<C::TWeakPtr<FakeTexture>> weak;
            for (const auto& texture : device->Textures)
            {
                weak.push_back(texture);
            }
            device->Textures.clear();
            for (const auto& texture : weak)
            {
                CHECK(texture.expired());
            }
            resources.Shutdown();
        }
        for (bool transparent : {false, true})
        {
            Bytes image = pixels;
            if (transparent)
            {
                image[3] = 0;
            }
            const auto albedo = Package(root, "albedo.nvpkg", "Textures/albedo", Nvtex(image, "nvtex.v0.rgba8.srgb"),
                                        false, 0, "nvtex.v0.rgba8.srgb");
            const auto normal = Package(root, "normal.nvpkg", "Textures/normal", Nvtex(pixels));
            R::RenderResources resources;
            auto device = MakeShared<FakeDevice>();
            CHECK(resources.Initialize(device));
            SetupTextures(resources, root, albedo + "," + normal);
            auto staging = Stage();
            staging.ImportedMaterial.AlbedoPath = "Textures/albedo";
            staging.ImportedMaterial.NormalPath = "Textures/normal";
            staging.ImportedMaterial.BaseColor[3] = 0;
            const auto handle =
                S::FinalizeModelStaging(staging, {resources.Textures(), resources.MegaGeometry()}, "test", 1);
            if (transparent)
            {
                CHECK(!handle.IsValid() && device->Textures.empty() && device->Buffers.empty());
            }
            else
            {
                CHECK(handle.IsValid() && device->Textures.size() == 2);
                CHECK(device->Textures[0]->Desc.TextureFormat == H::Format::R8G8B8A8_SRGB);
                CHECK(device->Textures[1]->Desc.TextureFormat == H::Format::R8G8B8A8_UNORM);
                CHECK(device->Textures[0]->Updates.size() == 2 && device->Textures[1]->Updates.size() == 2);
            }
            resources.Shutdown();
        }
        const auto payload = Mesh();
        for (uint32_t version : {0u, 1u})
        {
            const auto row = Package(root, "model.nvpkg", "Models/runtime", payload, true, version);
            auto assets = MakeShared<A::AssetSystem>(Text(root.generic_string().c_str()));
            CHECK(assets->LoadManifestFromJsonText(CoreText(Manifest(row))));
            M::CookedModelLoadPlan plan;
            plan.AssetSystem = assets;
            plan.RequestPath = "Models/runtime";
            plan.NormalizedLogicalPath = "Models/runtime";
            M::CookedModelCpuLoadResult result;
            const auto success = M::LoadCookedModelForWorker(plan, 1, result);
            CHECK(success == (version == 1) && result.bSuccess == success);
            R::RenderResources resources;
            auto device = MakeShared<FakeDevice>();
            CHECK(resources.Initialize(device));
            const auto model =
                M::LoadCookedModel(*assets, "Models/runtime", {resources.Textures(), resources.MegaGeometry()});
            CHECK(model.IsValid() == (version == 1) && device->Buffers.size() == (version == 1 ? 3u : 0u));
            resources.Shutdown();
        }
        std::puts(
            "IMPORTED_OPAQUE_RUNTIME result=pass profile_alpha_zero_emission_selected_mips_scalar_cache_version_sync_worker_fake_device_no_gpu_claim");
    }
} // namespace ImportedRuntimeTest
int main()
{
#if defined(_WIN32)
    ImportedRuntimeTest::Pure();
    ImportedRuntimeTest::Constants();
    wchar_t temp[32768]{};
    const auto length = GetTempPathW(32768, temp);
    CHECK(length && length < 32768);
    wchar_t name[128]{};
    std::swprintf(name, 128, L"NorvesImportedOpaque-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    const auto root = std::filesystem::path(temp) / name;
    CHECK(std::filesystem::create_directory(root));
    ImportedRuntimeTest::Runtime(root);
    CHECK(std::filesystem::remove_all(root) > 0);
    return 0;
#else
    return 125;
#endif
}
