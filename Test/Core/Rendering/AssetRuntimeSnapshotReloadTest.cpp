#include "Asset/AssetSystem.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Rendering/TextureAssetResolver.h"
#include "Rendering/TextureAssetRuntime.h"
#include "Rendering/TextureHandleCache.h"
#include "Resource/ModelAssetRuntime.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"
#include "Thread/JobSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Asset = NorvesLib::Core::Asset;
namespace Container = NorvesLib::Core::Container;
namespace CookedModelSupport = NorvesLib::Test::CookedModelSupport;
namespace Rendering = NorvesLib::Core::Rendering;
namespace PackageV1 = NorvesLib::Core::Asset::AssetPackageFormatV1;
namespace TextureV0 = NorvesLib::Core::Asset::CookedTextureFormatV0;

namespace NorvesLib::Core::Rendering
{
    struct AssetRuntimeSnapshotReloadTestAccess
    {
        struct CacheKeys
        {
            Container::String Texture;
            Container::String Model;
        };

        struct CachePresence
        {
            bool bTexture = false;
            bool bModel = false;
        };

        struct State
        {
            const Asset::AssetSystem* pTextureSnapshot = nullptr;
            const Asset::AssetSystem* pModelSnapshot = nullptr;
            Container::AnsiString TextureRoot;
            Container::String ModelRoot;
            uint64_t TextureGeneration = 0;
            uint64_t ModelGeneration = 0;
            bool bTextureProbeCached = false;
            bool bModelProbeCached = false;
        };

        static State Observe(RenderResources& resources,
                             const Container::String& textureProbe,
                             const Container::String& modelProbe)
        {
            State state;
            TextureAssetRuntime* pTextureRuntime = resources.GetTextureAssetRuntimeForTesting();
            ModelAssetRuntime* pModelRuntime = resources.GetModelAssetRuntimeForTesting();
            assert(pTextureRuntime != nullptr);
            assert(pModelRuntime != nullptr);

            {
                Thread::ScopedLock textureLock(pTextureRuntime->m_TextureAssetMutex);
                TextureAssetResolver& resolver = pTextureRuntime->GetTextureAssetResolverLocked();
                state.pTextureSnapshot = resolver.m_System.get();
                state.TextureRoot = resolver.m_AssetRoot;
                state.TextureGeneration = resolver.m_Generation;
                const TextureAssetLoadPlan plan = resolver.BuildTextureLoadPlan(textureProbe);
                state.bTextureProbeCached = pTextureRuntime->m_TextureHandleCache &&
                    pTextureRuntime->m_TextureHandleCache->Find(plan.CacheKey).IsValid();
            }

            {
                Thread::ScopedLock modelLock(pModelRuntime->m_AssetMutex);
                state.pModelSnapshot = pModelRuntime->m_AssetSystem.get();
                state.ModelRoot = pModelRuntime->m_AssetRoot;
                state.ModelGeneration = pModelRuntime->m_Generation;
            }

            Resource::CookedModelLoadPlan modelPlan;
            if (pModelRuntime->TryBuildPlan(modelProbe, modelPlan))
            {
                state.bModelProbeCached = pModelRuntime->m_Cache.Acquire(
                    modelPlan.CacheKey,
                    false).bFound;
            }
            return state;
        }

        static CacheKeys ObserveCurrentCacheKeys(RenderResources& resources,
                                                 const Container::String& textureProbe,
                                                 const Container::String& modelProbe)
        {
            CacheKeys keys;
            TextureAssetRuntime* pTextureRuntime = resources.GetTextureAssetRuntimeForTesting();
            ModelAssetRuntime* pModelRuntime = resources.GetModelAssetRuntimeForTesting();
            assert(pTextureRuntime != nullptr);
            assert(pModelRuntime != nullptr);

            {
                Thread::ScopedLock textureLock(pTextureRuntime->m_TextureAssetMutex);
                TextureAssetResolver& resolver = pTextureRuntime->GetTextureAssetResolverLocked();
                const TextureAssetLoadPlan plan = resolver.BuildTextureLoadPlan(textureProbe);
                assert(plan.bPathValid);
                assert(!plan.CacheKey.empty());
                assert(pTextureRuntime->m_TextureHandleCache);
                keys.Texture = plan.CacheKey;
                assert(pTextureRuntime->m_TextureHandleCache->Find(keys.Texture).IsValid());
            }

            Resource::CookedModelLoadPlan modelPlan;
            assert(pModelRuntime->TryBuildPlan(modelProbe, modelPlan));
            assert(!modelPlan.CacheKey.empty());
            keys.Model = modelPlan.CacheKey;
            assert(pModelRuntime->m_Cache.Acquire(keys.Model, false).bFound);
            return keys;
        }

        static bool SeedCurrentModelWithoutLease(RenderResources& resources,
                                                 const Container::String& logicalPath,
                                                 ModelHandle handle)
        {
            ModelAssetRuntime* pModelRuntime = resources.GetModelAssetRuntimeForTesting();
            assert(pModelRuntime != nullptr);
            return pModelRuntime->SeedCurrentModelForTesting(logicalPath, handle);
        }

        static CachePresence ObserveCacheEntries(RenderResources& resources,
                                                  const CacheKeys& keys)
        {
            CachePresence presence;
            TextureAssetRuntime* pTextureRuntime = resources.GetTextureAssetRuntimeForTesting();
            ModelAssetRuntime* pModelRuntime = resources.GetModelAssetRuntimeForTesting();
            assert(pTextureRuntime != nullptr);
            assert(pModelRuntime != nullptr);

            {
                Thread::ScopedLock textureLock(pTextureRuntime->m_TextureAssetMutex);
                presence.bTexture = pTextureRuntime->m_TextureHandleCache &&
                    pTextureRuntime->m_TextureHandleCache->Find(keys.Texture).IsValid();
            }
            presence.bModel = pModelRuntime->m_Cache.Acquire(keys.Model, false).bFound;
            return presence;
        }
    };
} // namespace NorvesLib::Core::Rendering

namespace
{
    using ByteArray = std::vector<uint8_t>;

    void WriteLe16(ByteArray& bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(ByteArray& bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(ByteArray& bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    ByteArray BuildCookedTexture()
    {
        constexpr uint32_t width = 1;
        constexpr uint32_t height = 1;
        constexpr uint32_t layerCount = 1;
        constexpr uint32_t mipCount = 1;
        constexpr size_t payloadSize = 4;
        constexpr size_t mipTableOffset = TextureV0::HeaderSize;
        constexpr size_t mipTableSize = TextureV0::MipRecordSize;
        constexpr size_t payloadOffset = mipTableOffset + mipTableSize;
        constexpr size_t fileSize = payloadOffset + payloadSize;

        ByteArray bytes(fileSize, 0);
        std::memcpy(bytes.data() + TextureV0::HeaderOffset::Magic,
                    TextureV0::Magic,
                    TextureV0::MagicSize);
        WriteLe32(bytes, TextureV0::HeaderOffset::HeaderSize, static_cast<uint32_t>(TextureV0::HeaderSize));
        WriteLe16(bytes, TextureV0::HeaderOffset::VersionMajor, TextureV0::VersionMajor);
        WriteLe16(bytes, TextureV0::HeaderOffset::VersionMinor, TextureV0::VersionMinor);
        WriteLe32(bytes, TextureV0::HeaderOffset::EndianMarker, TextureV0::EndianMarker);
        WriteLe32(bytes,
                  TextureV0::HeaderOffset::MipRecordSize,
                  static_cast<uint32_t>(TextureV0::MipRecordSize));
        WriteLe64(bytes, TextureV0::HeaderOffset::FileSize, fileSize);
        WriteLe64(bytes, TextureV0::HeaderOffset::MipTableOffset, mipTableOffset);
        WriteLe64(bytes, TextureV0::HeaderOffset::MipTableSize, mipTableSize);
        WriteLe64(bytes, TextureV0::HeaderOffset::PayloadOffset, payloadOffset);
        WriteLe64(bytes, TextureV0::HeaderOffset::PayloadSize, payloadSize);
        WriteLe32(bytes, TextureV0::HeaderOffset::Width, width);
        WriteLe32(bytes, TextureV0::HeaderOffset::Height, height);
        WriteLe32(bytes, TextureV0::HeaderOffset::LayerCount, layerCount);
        WriteLe32(bytes, TextureV0::HeaderOffset::MipCount, mipCount);
        WriteLe32(bytes,
                  TextureV0::HeaderOffset::PixelFormat,
                  static_cast<uint32_t>(Asset::CookedTexturePixelFormat::RGBA8UNorm));
        WriteLe32(bytes,
                  TextureV0::HeaderOffset::ColorSpace,
                  static_cast<uint32_t>(Asset::CookedTextureColorSpace::SRGB));

        WriteLe64(bytes,
                  mipTableOffset + TextureV0::MipRecordOffset::DataOffset,
                  payloadOffset);
        WriteLe64(bytes,
                  mipTableOffset + TextureV0::MipRecordOffset::DataSize,
                  payloadSize);
        WriteLe32(bytes, mipTableOffset + TextureV0::MipRecordOffset::Width, width);
        WriteLe32(bytes, mipTableOffset + TextureV0::MipRecordOffset::Height, height);
        bytes[payloadOffset + 0] = 11;
        bytes[payloadOffset + 1] = 22;
        bytes[payloadOffset + 2] = 33;
        bytes[payloadOffset + 3] = 255;
        WriteLe64(bytes,
                  TextureV0::HeaderOffset::PayloadHash,
                  Asset::ComputeCookedTexturePayloadHash(bytes.data() + payloadOffset, payloadSize));
        return bytes;
    }

    ByteArray BuildSingleEntryPackage(const ByteArray& payload,
                                      const std::string& entryName,
                                      Asset::AssetPackageFourCC entryType)
    {
        const size_t entryTableOffset = PackageV1::HeaderSize;
        const size_t entryTableSize = PackageV1::EntryRecordSize;
        const size_t nameTableOffset = CookedModelSupport::AlignUp(
            entryTableOffset + entryTableSize,
            PackageV1::MinimumAlignment);
        const size_t blobDataOffset = CookedModelSupport::AlignUp(
            nameTableOffset + entryName.size(),
            PackageV1::MinimumAlignment);
        const size_t packageSize = blobDataOffset + payload.size();

        ByteArray bytes(packageSize, 0);
        std::memcpy(bytes.data() + PackageV1::HeaderOffset::Magic,
                    PackageV1::Magic,
                    PackageV1::MagicSize);
        WriteLe32(bytes, PackageV1::HeaderOffset::HeaderSize, static_cast<uint32_t>(PackageV1::HeaderSize));
        WriteLe16(bytes, PackageV1::HeaderOffset::VersionMajor, PackageV1::VersionMajor);
        WriteLe16(bytes, PackageV1::HeaderOffset::VersionMinor, PackageV1::VersionMinor);
        WriteLe32(bytes, PackageV1::HeaderOffset::EndianMarker, PackageV1::EndianMarker);
        WriteLe32(bytes,
                  PackageV1::HeaderOffset::EntryRecordSize,
                  static_cast<uint32_t>(PackageV1::EntryRecordSize));
        WriteLe64(bytes, PackageV1::HeaderOffset::PackageSize, packageSize);
        WriteLe32(bytes, PackageV1::HeaderOffset::EntryCount, 1);
        WriteLe64(bytes, PackageV1::HeaderOffset::EntryTableOffset, entryTableOffset);
        WriteLe64(bytes, PackageV1::HeaderOffset::EntryTableSize, entryTableSize);
        WriteLe64(bytes, PackageV1::HeaderOffset::NameTableOffset, nameTableOffset);
        WriteLe64(bytes, PackageV1::HeaderOffset::NameTableSize, entryName.size());
        WriteLe64(bytes, PackageV1::HeaderOffset::BlobDataOffset, blobDataOffset);
        WriteLe32(bytes, PackageV1::HeaderOffset::Alignment, PackageV1::MinimumAlignment);

        std::memcpy(bytes.data() + nameTableOffset, entryName.data(), entryName.size());
        std::memcpy(bytes.data() + blobDataOffset, payload.data(), payload.size());
        WriteLe64(bytes, entryTableOffset + PackageV1::EntryOffset::NameOffset, nameTableOffset);
        WriteLe32(bytes,
                  entryTableOffset + PackageV1::EntryOffset::NameSize,
                  static_cast<uint32_t>(entryName.size()));
        WriteLe32(bytes, entryTableOffset + PackageV1::EntryOffset::Type, entryType);
        WriteLe32(bytes,
                  entryTableOffset + PackageV1::EntryOffset::Compression,
                  static_cast<uint32_t>(Asset::AssetPackageCompression::None));
        WriteLe64(bytes, entryTableOffset + PackageV1::EntryOffset::DataOffset, blobDataOffset);
        WriteLe64(bytes, entryTableOffset + PackageV1::EntryOffset::StoredSize, payload.size());
        WriteLe64(bytes, entryTableOffset + PackageV1::EntryOffset::UncompressedSize, payload.size());
        WriteLe64(bytes,
                  entryTableOffset + PackageV1::EntryOffset::PayloadHash,
                  Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size()));
        return bytes;
    }

    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const NorvesLib::RHI::BufferDesc& desc)
            : m_Desc(desc),
              m_Bytes(static_cast<size_t>(desc.Size))
        {
            ++LiveCount;
        }

        ~FakeBuffer() override
        {
            --LiveCount;
        }

        uint64_t GetSize() const override { return m_Desc.Size; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < m_Bytes.size() ? m_Bytes.data() + static_cast<size_t>(offset) : nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            assert(data != nullptr);
            assert(offset + size <= m_Bytes.size());
            std::memcpy(m_Bytes.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
        }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }

        static std::atomic<int> LiveCount;

    private:
        NorvesLib::RHI::BufferDesc m_Desc;
        ByteArray m_Bytes;
    };

    std::atomic<int> FakeBuffer::LiveCount{0};

    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
            ++LiveCount;
        }

        ~FakeTexture() override
        {
            --LiveCount;
        }

        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }
        bool IsCubemap() const override { return m_Desc.IsCubemap; }
        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            assert(data != nullptr);
            assert(rowPitch == 4);
            assert(slicePitch == 4);
            assert(mipLevel == 0);
            assert(arrayIndex == 0);
            ++UpdateCount;
        }

        static std::atomic<int> LiveCount;
        uint32_t UpdateCount = 0;

    private:
        NorvesLib::RHI::TextureDesc m_Desc;
    };

    std::atomic<int> FakeTexture::LiveCount{0};

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc& desc) override
        {
            ++CreatedBufferCount;
            return Container::MakeShared<FakeBuffer>(desc);
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc& desc) override
        {
            ++CreatedTextureCount;
            return Container::MakeShared<FakeTexture>(desc);
        }

        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::CommandListPtr CreateCommandList() override
        {
            return {};
        }

        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc&) override
        {
            return {};
        }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc&) override
        {
            return {};
        }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc&) override
        {
            return {};
        }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc&) override
        {
            return {};
        }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return {};
        }

        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }

        void WaitIdle() override
        {
        }

        NorvesLib::RHI::API GetAPI() const override
        {
            return NorvesLib::RHI::API::None;
        }

        const NorvesLib::RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return m_Capabilities;
        }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        uint32_t CreatedBufferCount = 0;
        uint32_t CreatedTextureCount = 0;

    private:
        NorvesLib::RHI::DeviceCapabilities m_Capabilities;
    };

    std::string ToStdString(const Container::AnsiString& text)
    {
        return std::string(text.data(), text.size());
    }

    class AssetFixture final
    {
    public:
        explicit AssetFixture(const char* label, bool bIncludeUnleasedModel)
            : Label(label)
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            Root = std::filesystem::temp_directory_path() /
                (std::string("NorvesLibAssetRuntimeSnapshotReload_") + label + "_" + std::to_string(stamp));
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root);

            TexturePath = CookedModelSupport::ToCoreString(std::string("Textures/") + label + ".nvtex");
            ModelPath = CookedModelSupport::ToCoreString(std::string("Models/") + label + ".nvmesh");
            UnleasedModelPath = CookedModelSupport::ToCoreString(
                std::string("Models/") + label + "-Unleased.nvmesh");

            const ByteArray texturePayload = BuildCookedTexture();
            const ByteArray modelPayload = CookedModelSupport::BuildCookedModelMesh();
            const std::string textureEntry = std::string("Textures/") + label + ".nvtex";
            const std::string modelEntry = std::string("Models/") + label + ".nvmesh";
            const std::string unleasedEntry = std::string("Models/") + label + "-Unleased.nvmesh";
            CookedModelSupport::WriteBinaryFile(
                Root / "Cooked" / "Textures.nvpkg",
                BuildSingleEntryPackage(
                    texturePayload,
                    textureEntry,
                    Asset::MakeAssetPackageFourCC('T', 'e', 'x', '0')));
            CookedModelSupport::WriteBinaryFile(
                Root / "Cooked" / "Models.nvpkg",
                CookedModelSupport::BuildModelPackage(modelPayload, modelEntry));
            if (bIncludeUnleasedModel)
            {
                CookedModelSupport::WriteBinaryFile(
                    Root / "Cooked" / "UnleasedModels.nvpkg",
                    CookedModelSupport::BuildModelPackage(modelPayload, unleasedEntry));
            }

            const std::string textureHash = ToStdString(Asset::FormatAssetHashHex(
                Asset::ComputeAssetPackagePayloadHash(texturePayload.data(), texturePayload.size())));
            const std::string modelHash = ToStdString(Asset::FormatAssetHashHex(
                Asset::ComputeAssetPackagePayloadHash(modelPayload.data(), modelPayload.size())));
            std::string json =
                "{\"version\":1,\"assets\":["
                "{\"logical_path\":\"" + textureEntry +
                "\",\"kind\":\"texture\",\"source_hash\":\"0000000000000001\","
                "\"variant\":\"default\",\"format\":\"nvtex.v0.rgba8.srgb\","
                "\"cooked_package\":\"Cooked/Textures.nvpkg\",\"entry_name\":\"" + textureEntry +
                "\",\"entry_type\":\"Tex0\",\"cooked_hash\":\"" + textureHash +
                "\",\"cooked_version\":0},"
                "{\"logical_path\":\"" + modelEntry +
                "\",\"kind\":\"model\",\"source_hash\":\"0000000000000002\","
                "\"variant\":\"default\",\"format\":\"nvmesh.v0\","
                "\"cooked_package\":\"Cooked/Models.nvpkg\",\"entry_name\":\"" + modelEntry +
                "\",\"entry_type\":\"Msh0\",\"cooked_hash\":\"" + modelHash +
                "\",\"cooked_version\":0}";
            if (bIncludeUnleasedModel)
            {
                json +=
                    ",{\"logical_path\":\"" + unleasedEntry +
                    "\",\"kind\":\"model\",\"source_hash\":\"0000000000000003\","
                    "\"variant\":\"default\",\"format\":\"nvmesh.v0\","
                    "\"cooked_package\":\"Cooked/UnleasedModels.nvpkg\",\"entry_name\":\"" + unleasedEntry +
                    "\",\"entry_type\":\"Msh0\",\"cooked_hash\":\"" + modelHash +
                    "\",\"cooked_version\":0}";
            }
            json += "]}";

            System = Container::MakeShared<Asset::AssetSystem>(Root.generic_string().c_str());
            assert(System->LoadManifestFromJsonText(CookedModelSupport::ToCoreString(json)));
        }

        ~AssetFixture()
        {
            System.reset();
            std::filesystem::remove_all(Root);
        }

        Container::String RootString() const
        {
            return CookedModelSupport::ToCoreString(Root.generic_string());
        }

        std::filesystem::path Root;
        std::string Label;
        Container::String TexturePath;
        Container::String ModelPath;
        Container::String UnleasedModelPath;
        Container::TSharedPtr<Asset::AssetSystem> System;
    };

    void AssertCandidateBIdentity(const Asset::AssetSystem& candidateA,
                                  const Asset::AssetSystem& candidateB)
    {
        const Asset::AssetManifestResolveResult textureA = candidateA.FindCookedVariant(
            "Textures/B.nvtex",
            Asset::AssetKind::Texture);
        const Asset::AssetManifestResolveResult textureB = candidateB.FindCookedVariant(
            "Textures/B.nvtex",
            Asset::AssetKind::Texture);
        const Asset::AssetManifestResolveResult modelA = candidateA.FindCookedVariant(
            "Models/B.nvmesh",
            Asset::AssetKind::Model);
        const Asset::AssetManifestResolveResult modelB = candidateB.FindCookedVariant(
            "Models/B.nvmesh",
            Asset::AssetKind::Model);

        assert(!textureA.ShouldUseCooked());
        assert(textureB.ShouldUseCooked());
        assert(textureB.Reference.CookedPackage == "Cooked/Textures.nvpkg");
        assert(!modelA.ShouldUseCooked());
        assert(modelB.ShouldUseCooked());
        assert(modelB.Reference.CookedPackage == "Cooked/Models.nvpkg");
    }

    void AssertStateUnchanged(
        const Rendering::AssetRuntimeSnapshotReloadTestAccess::State& before,
        const Rendering::AssetRuntimeSnapshotReloadTestAccess::State& after)
    {
        assert(after.pTextureSnapshot == before.pTextureSnapshot);
        assert(after.pModelSnapshot == before.pModelSnapshot);
        assert(after.TextureRoot == before.TextureRoot);
        assert(after.ModelRoot == before.ModelRoot);
        assert(after.TextureGeneration == before.TextureGeneration);
        assert(after.ModelGeneration == before.ModelGeneration);
        assert(after.bTextureProbeCached == before.bTextureProbeCached);
        assert(after.bModelProbeCached == before.bModelProbeCached);
    }
}

int main()
{
    AssetFixture fixtureA("A", true);
    AssetFixture fixtureB("B", false);
    Container::TSharedPtr<Asset::AssetSystem> candidateA = fixtureA.System;
    Container::TSharedPtr<Asset::AssetSystem> candidateB = fixtureB.System;
    Container::TSharedPtr<const Asset::AssetSystem> immutableA = candidateA;
    Container::TSharedPtr<const Asset::AssetSystem> immutableB = candidateB;
    AssertCandidateBIdentity(*candidateA, *candidateB);
    NorvesLib::Thread::JobSystem::Get().Initialize(
        2,
        NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);

    Rendering::RenderResources resources;
    assert(!resources.ReloadAssetRuntimeSnapshot(fixtureA.RootString(), immutableA));
    assert(!resources.ReloadAssetRuntimeSnapshot(
        fixtureA.RootString(),
        Container::TSharedPtr<const Asset::AssetSystem>()));

    auto device = Container::MakeShared<FakeDevice>();
    assert(resources.Initialize(device));
    assert(!resources.ReloadAssetRuntimeSnapshot("", immutableA));
    assert(resources.ReloadAssetRuntimeSnapshot(fixtureA.RootString(), immutableA));

    const Rendering::TextureHandle textureA = resources.Textures().LoadTexture(fixtureA.TexturePath);
    assert(textureA.IsValid());
    NorvesLib::RHI::ITexture* pTextureA = resources.Textures().GetRHITexture(textureA);
    assert(pTextureA != nullptr);
    assert(static_cast<FakeTexture*>(pTextureA)->UpdateCount == 1);

    Rendering::ModelHandle leasedModelA = Rendering::ModelHandle::Invalid();
    uint32_t leasedModelACallbackCount = 0;
    const uint32_t leasedModelARequest = resources.MegaGeometry().LoadModelAsync(
        fixtureA.ModelPath,
        [&leasedModelA, &leasedModelACallbackCount](Rendering::ModelHandle handle)
        {
            ++leasedModelACallbackCount;
            leasedModelA = handle;
        });
    assert(leasedModelARequest != 0);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    assert(resources.MegaGeometry().FlushCompletedModelLoads(0) == 1);
    assert(leasedModelACallbackCount == 1);
    assert(leasedModelA.IsValid());
    const auto leasedMegaA = resources.MegaGeometry().GetModelMegaMeshHandle(leasedModelA);
    assert(leasedMegaA.IsValid());
    assert(resources.MegaGeometry().GetMegaMeshGPUData(leasedMegaA) != nullptr);

    const Rendering::ModelHandle unleasedModelA = resources.MegaGeometry().LoadModel(
        *candidateA,
        fixtureA.UnleasedModelPath);
    assert(unleasedModelA.IsValid());
    const auto unleasedMegaA = resources.MegaGeometry().GetModelMegaMeshHandle(unleasedModelA);
    assert(unleasedMegaA.IsValid());
    assert(resources.MegaGeometry().GetMegaMeshGPUData(unleasedMegaA) != nullptr);
    assert(Rendering::AssetRuntimeSnapshotReloadTestAccess::SeedCurrentModelWithoutLease(
        resources,
        fixtureA.UnleasedModelPath,
        unleasedModelA));

    const Rendering::AssetRuntimeSnapshotReloadTestAccess::CacheKeys cacheA =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::ObserveCurrentCacheKeys(
            resources,
            fixtureA.TexturePath,
            fixtureA.ModelPath);
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateA =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(
            resources,
            fixtureA.TexturePath,
            fixtureA.ModelPath);
    assert(stateA.pTextureSnapshot == candidateA.get());
    assert(stateA.pModelSnapshot == candidateA.get());
    assert(stateA.TextureRoot == fixtureA.Root.generic_string().c_str());
    assert(stateA.ModelRoot == fixtureA.RootString());
    assert(stateA.bTextureProbeCached);
    assert(stateA.bModelProbeCached);

    assert(!resources.ReloadAssetRuntimeSnapshot(
        fixtureB.RootString(),
        Container::TSharedPtr<const Asset::AssetSystem>()));
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateAfterNull =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(
            resources,
            fixtureA.TexturePath,
            fixtureA.ModelPath);
    AssertStateUnchanged(stateA, stateAfterNull);

    assert(!resources.ReloadAssetRuntimeSnapshot("", immutableB));
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateAfterInvalidRoot =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(
            resources,
            fixtureA.TexturePath,
            fixtureA.ModelPath);
    AssertStateUnchanged(stateA, stateAfterInvalidRoot);

    const uint32_t pendingModel = resources.MegaGeometry().LoadModelAsync("Models/Busy.nvmesh");
    assert(pendingModel != 0);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    assert(resources.MegaGeometry().GetPendingAsyncModelLoadCount() == 1);
    assert(!resources.ReloadAssetRuntimeSnapshot(fixtureB.RootString(), immutableB));
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateWhileModelBusy =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(
            resources,
            fixtureA.TexturePath,
            fixtureA.ModelPath);
    AssertStateUnchanged(stateA, stateWhileModelBusy);
    assert(resources.MegaGeometry().CancelPendingModelLoadsAndWait());
    assert(resources.MegaGeometry().GetPendingAsyncModelLoadCount() == 0);

    assert(resources.ReloadAssetRuntimeSnapshot(fixtureB.RootString(), immutableB));
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateB =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(
            resources,
            fixtureB.TexturePath,
            fixtureB.ModelPath);
    assert(stateB.pTextureSnapshot == candidateB.get());
    assert(stateB.pModelSnapshot == candidateB.get());
    assert(stateB.TextureRoot == fixtureB.Root.generic_string().c_str());
    assert(stateB.ModelRoot == fixtureB.RootString());
    assert(stateB.TextureGeneration == stateA.TextureGeneration + 1);
    assert(stateB.ModelGeneration == stateA.ModelGeneration + 1);
    assert(!stateB.bTextureProbeCached);
    assert(!stateB.bModelProbeCached);

    assert(resources.Textures().SetTextureAssetRoot(fixtureB.RootString()));
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateAfterPublicRootSet =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(
            resources,
            fixtureB.TexturePath,
            fixtureB.ModelPath);
    assert(stateAfterPublicRootSet.pTextureSnapshot == candidateB.get());
    assert(stateAfterPublicRootSet.pModelSnapshot == candidateB.get());
    assert(stateAfterPublicRootSet.TextureRoot == fixtureB.Root.generic_string().c_str());
    assert(stateAfterPublicRootSet.ModelRoot == fixtureB.RootString());
    assert(stateAfterPublicRootSet.TextureGeneration == stateB.TextureGeneration + 1);
    assert(stateAfterPublicRootSet.ModelGeneration == stateB.ModelGeneration);
    assert(!stateAfterPublicRootSet.bTextureProbeCached);
    assert(!stateAfterPublicRootSet.bModelProbeCached);

    const Rendering::AssetRuntimeSnapshotReloadTestAccess::CachePresence retiredCacheA =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::ObserveCacheEntries(resources, cacheA);
    assert(!retiredCacheA.bTexture);
    assert(!retiredCacheA.bModel);

    assert(resources.Textures().GetRHITexture(textureA) == pTextureA);
    assert(resources.MegaGeometry().GetModelMegaMeshHandle(leasedModelA) == leasedMegaA);
    assert(resources.MegaGeometry().GetMegaMeshGPUData(leasedMegaA) != nullptr);
    assert(!resources.MegaGeometry().GetModelMegaMeshHandle(unleasedModelA).IsValid());
    assert(resources.MegaGeometry().GetMegaMeshGPUData(unleasedMegaA) == nullptr);

    const Rendering::TextureHandle textureB = resources.Textures().LoadTexture(fixtureB.TexturePath);
    assert(textureB.IsValid());
    assert(resources.Textures().GetRHITexture(textureB) != nullptr);
    assert(textureB != textureA);

    Rendering::ModelHandle modelB = Rendering::ModelHandle::Invalid();
    uint32_t modelBCallbackCount = 0;
    const uint32_t modelBRequest = resources.MegaGeometry().LoadModelAsync(
        fixtureB.ModelPath,
        [&modelB, &modelBCallbackCount](Rendering::ModelHandle handle)
        {
            ++modelBCallbackCount;
            modelB = handle;
        });
    assert(modelBRequest != 0);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    assert(resources.MegaGeometry().FlushCompletedModelLoads(0) == 1);
    assert(modelBCallbackCount == 1);
    assert(modelB.IsValid());
    const auto megaB = resources.MegaGeometry().GetModelMegaMeshHandle(modelB);
    assert(megaB.IsValid());
    assert(resources.MegaGeometry().GetMegaMeshGPUData(megaB) != nullptr);

    resources.MegaGeometry().ReleaseModel(leasedModelA);
    assert(!resources.MegaGeometry().GetModelMegaMeshHandle(leasedModelA).IsValid());
    assert(resources.MegaGeometry().GetMegaMeshGPUData(leasedMegaA) == nullptr);
    resources.Textures().ReleaseTexture(textureA);
    assert(resources.Textures().GetRHITexture(textureA) == nullptr);
    resources.Textures().ReleaseTexture(textureB);
    assert(resources.Textures().GetRHITexture(textureB) == nullptr);
    resources.MegaGeometry().ReleaseModel(modelB);
    assert(resources.MegaGeometry().GetModelMegaMeshHandle(modelB) == megaB);

    resources.Shutdown();
    assert(!resources.ReloadAssetRuntimeSnapshot(fixtureA.RootString(), immutableA));
    assert(FakeBuffer::LiveCount.load() == 0);
    assert(FakeTexture::LiveCount.load() == 0);
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "AssetRuntimeSnapshotReloadTest passed" << std::endl;
    return 0;
}
