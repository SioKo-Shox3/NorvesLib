#include "Asset/AssetPackageFormat.h"
#include "Asset/AssetManifest.h"
#include "Asset/CookedTextureFormat.h"
#include "Rendering/RenderResourceManager.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::MakeShared;
using NorvesLib::Core::Container::String;

namespace PackageV1 = NorvesLib::Core::Asset::AssetPackageFormatV1;
namespace TextureV0 = NorvesLib::Core::Asset::CookedTextureFormatV0;

namespace
{
    struct TestPackageEntry
    {
        std::string Name;
        AssetPackageFourCC Type = 0;
        std::vector<uint8_t> Payload;
    };

    struct UpdateCall
    {
        uint32_t RowPitch = 0;
        uint32_t SlicePitch = 0;
        uint32_t MipLevel = 0;
        uint32_t ArrayIndex = 0;
        std::vector<uint8_t> Bytes;
    };

    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        FakeTexture(const NorvesLib::RHI::TextureDesc &desc, std::function<void()> onFirstUpdate)
            : Desc(desc), OnFirstUpdate(std::move(onFirstUpdate))
        {
        }

        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }

        void Update(const void *data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            assert(data != nullptr);

            UpdateCall call;
            call.RowPitch = rowPitch;
            call.SlicePitch = slicePitch;
            call.MipLevel = mipLevel;
            call.ArrayIndex = arrayIndex;
            const uint8_t *bytes = static_cast<const uint8_t *>(data);
            call.Bytes.assign(bytes, bytes + slicePitch);
            Updates.push_back(std::move(call));

            if (!bFirstUpdateCallbackInvoked && OnFirstUpdate)
            {
                bFirstUpdateCallbackInvoked = true;
                OnFirstUpdate();
            }
        }

        NorvesLib::RHI::TextureDesc Desc;
        std::vector<UpdateCall> Updates;
        std::function<void()> OnFirstUpdate;
        bool bFirstUpdateCallbackInvoked = false;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &desc) override
        {
            CreatedTextureDescs.push_back(desc);
            LastTexture = MakeShared<FakeTexture>(desc, OnFirstTextureUpdate);
            return LastTexture;
        }

        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc &) override { return {}; }
        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc &) override { return {}; }
        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc &) override { return {}; }
        NorvesLib::RHI::CommandListPtr CreateCommandList() override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc &) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc &) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc &) override { return {}; }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc &) override { return {}; }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        void WaitIdle() override {}
        NorvesLib::RHI::API GetAPI() const override { return NorvesLib::RHI::API::None; }
        const NorvesLib::RHI::DeviceCapabilities &GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4 &projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        std::vector<NorvesLib::RHI::TextureDesc> CreatedTextureDescs;
        NorvesLib::Core::Container::TSharedPtr<FakeTexture> LastTexture;
        std::function<void()> OnFirstTextureUpdate;
    };

    std::filesystem::path CreateTestRoot(const char *name)
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     (std::string("NorvesLibRRMPreparedTextureAssetTest_") + name + "_" + std::to_string(now));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }

    std::string ToAssetString(const std::filesystem::path &path)
    {
        return path.generic_string();
    }

    String ToCoreString(const std::string &text)
    {
#if defined(UNICODE)
        std::wstring wide;
        wide.reserve(text.size());
        for (char character : text)
        {
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
        }
        return String(wide.c_str());
#else
        return String(text.c_str());
#endif
    }

    std::string ToStdString(const NorvesLib::Core::Container::AnsiString &text)
    {
        return std::string(text.data(), text.size());
    }

    size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void WriteLe16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    uint32_t ExpectedMipDimension(uint32_t baseDimension, uint32_t mipIndex)
    {
        const uint32_t shifted = baseDimension >> mipIndex;
        return shifted == 0 ? 1 : shifted;
    }

    size_t MipRecordOffsetFor(size_t mipIndex)
    {
        return TextureV0::HeaderSize + mipIndex * TextureV0::MipRecordSize;
    }

    std::vector<uint8_t> BuildCookedTextureBytes(uint32_t width,
                                                 uint32_t height,
                                                 uint32_t layerCount,
                                                 CookedTexturePixelFormat pixelFormat,
                                                 CookedTextureColorSpace colorSpace)
    {
        const uint32_t mipCount = ComputeCookedTextureFullMipCount(width, height);
        const size_t bytesPerPixel = GetCookedTextureBytesPerPixel(pixelFormat);
        const size_t mipTableOffset = TextureV0::HeaderSize;
        const size_t mipTableSize = static_cast<size_t>(mipCount) * TextureV0::MipRecordSize;
        const size_t payloadOffset = mipTableOffset + mipTableSize;

        std::vector<std::vector<uint8_t>> mipPayloads;
        mipPayloads.reserve(mipCount);

        size_t payloadSize = 0;
        uint8_t nextValue = 1;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const uint32_t mipWidth = ExpectedMipDimension(width, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(height, mipIndex);
            const size_t dataSize = static_cast<size_t>(mipWidth) *
                                    static_cast<size_t>(mipHeight) *
                                    static_cast<size_t>(layerCount) *
                                    bytesPerPixel;
            std::vector<uint8_t> mipBytes(dataSize);
            for (uint8_t &value : mipBytes)
            {
                value = nextValue++;
            }
            payloadSize += dataSize;
            mipPayloads.push_back(std::move(mipBytes));
        }

        const size_t fileSize = payloadOffset + payloadSize;
        std::vector<uint8_t> bytes(fileSize, 0);
        std::memcpy(bytes.data() + TextureV0::HeaderOffset::Magic, TextureV0::Magic, TextureV0::MagicSize);
        WriteLe32(bytes, TextureV0::HeaderOffset::HeaderSize, static_cast<uint32_t>(TextureV0::HeaderSize));
        WriteLe16(bytes, TextureV0::HeaderOffset::VersionMajor, TextureV0::VersionMajor);
        WriteLe16(bytes, TextureV0::HeaderOffset::VersionMinor, TextureV0::VersionMinor);
        WriteLe32(bytes, TextureV0::HeaderOffset::EndianMarker, TextureV0::EndianMarker);
        WriteLe32(bytes, TextureV0::HeaderOffset::MipRecordSize, static_cast<uint32_t>(TextureV0::MipRecordSize));
        WriteLe64(bytes, TextureV0::HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, TextureV0::HeaderOffset::MipTableOffset, static_cast<uint64_t>(mipTableOffset));
        WriteLe64(bytes, TextureV0::HeaderOffset::MipTableSize, static_cast<uint64_t>(mipTableSize));
        WriteLe64(bytes, TextureV0::HeaderOffset::PayloadOffset, static_cast<uint64_t>(payloadOffset));
        WriteLe64(bytes, TextureV0::HeaderOffset::PayloadSize, static_cast<uint64_t>(payloadSize));
        WriteLe32(bytes, TextureV0::HeaderOffset::Width, width);
        WriteLe32(bytes, TextureV0::HeaderOffset::Height, height);
        WriteLe32(bytes, TextureV0::HeaderOffset::LayerCount, layerCount);
        WriteLe32(bytes, TextureV0::HeaderOffset::MipCount, mipCount);
        WriteLe32(bytes, TextureV0::HeaderOffset::PixelFormat, static_cast<uint32_t>(pixelFormat));
        WriteLe32(bytes, TextureV0::HeaderOffset::ColorSpace, static_cast<uint32_t>(colorSpace));

        size_t payloadCursor = payloadOffset;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const std::vector<uint8_t> &mipBytes = mipPayloads[mipIndex];
            const size_t recordOffset = MipRecordOffsetFor(mipIndex);
            WriteLe64(bytes, recordOffset + TextureV0::MipRecordOffset::DataOffset, static_cast<uint64_t>(payloadCursor));
            WriteLe64(bytes, recordOffset + TextureV0::MipRecordOffset::DataSize, static_cast<uint64_t>(mipBytes.size()));
            WriteLe32(bytes, recordOffset + TextureV0::MipRecordOffset::Width, ExpectedMipDimension(width, mipIndex));
            WriteLe32(bytes, recordOffset + TextureV0::MipRecordOffset::Height, ExpectedMipDimension(height, mipIndex));
            std::memcpy(bytes.data() + payloadCursor, mipBytes.data(), mipBytes.size());
            payloadCursor += mipBytes.size();
        }

        WriteLe64(bytes,
                  TextureV0::HeaderOffset::PayloadHash,
                  ComputeCookedTexturePayloadHash(bytes.data() + payloadOffset, payloadSize));
        return bytes;
    }

    std::vector<uint8_t> BuildPackage(const std::vector<TestPackageEntry> &entries)
    {
        const size_t alignment = PackageV1::MinimumAlignment;
        const size_t entryTableOffset = PackageV1::HeaderSize;
        const size_t entryTableSize = entries.size() * PackageV1::EntryRecordSize;
        const size_t nameTableOffset = AlignUp(entryTableOffset + entryTableSize, alignment);

        size_t nameTableSize = 0;
        for (const TestPackageEntry &entry : entries)
        {
            nameTableSize += entry.Name.size();
        }

        const size_t blobDataOffset = AlignUp(nameTableOffset + nameTableSize, alignment);
        size_t packageSize = blobDataOffset;
        for (const TestPackageEntry &entry : entries)
        {
            packageSize = AlignUp(packageSize, alignment);
            packageSize += entry.Payload.size();
        }

        std::vector<uint8_t> bytes(packageSize, 0);
        std::memcpy(bytes.data() + PackageV1::HeaderOffset::Magic,
                    PackageV1::Magic,
                    PackageV1::MagicSize);
        WriteLe32(bytes, PackageV1::HeaderOffset::HeaderSize, static_cast<uint32_t>(PackageV1::HeaderSize));
        WriteLe16(bytes, PackageV1::HeaderOffset::VersionMajor, PackageV1::VersionMajor);
        WriteLe16(bytes, PackageV1::HeaderOffset::VersionMinor, PackageV1::VersionMinor);
        WriteLe32(bytes, PackageV1::HeaderOffset::EndianMarker, PackageV1::EndianMarker);
        WriteLe32(bytes, PackageV1::HeaderOffset::EntryRecordSize, static_cast<uint32_t>(PackageV1::EntryRecordSize));
        WriteLe64(bytes, PackageV1::HeaderOffset::PackageSize, static_cast<uint64_t>(packageSize));
        WriteLe32(bytes, PackageV1::HeaderOffset::EntryCount, static_cast<uint32_t>(entries.size()));
        WriteLe64(bytes, PackageV1::HeaderOffset::EntryTableOffset, static_cast<uint64_t>(entryTableOffset));
        WriteLe64(bytes, PackageV1::HeaderOffset::EntryTableSize, static_cast<uint64_t>(entryTableSize));
        WriteLe64(bytes, PackageV1::HeaderOffset::NameTableOffset, static_cast<uint64_t>(nameTableOffset));
        WriteLe64(bytes, PackageV1::HeaderOffset::NameTableSize, static_cast<uint64_t>(nameTableSize));
        WriteLe64(bytes, PackageV1::HeaderOffset::BlobDataOffset, static_cast<uint64_t>(blobDataOffset));
        WriteLe32(bytes, PackageV1::HeaderOffset::Alignment, static_cast<uint32_t>(alignment));

        size_t nameCursor = nameTableOffset;
        size_t dataCursor = blobDataOffset;
        for (size_t index = 0; index < entries.size(); ++index)
        {
            const TestPackageEntry &entry = entries[index];
            const size_t recordOffset = entryTableOffset + index * PackageV1::EntryRecordSize;
            const size_t nameOffset = nameCursor;
            for (char character : entry.Name)
            {
                bytes[nameCursor++] = static_cast<uint8_t>(character);
            }

            dataCursor = AlignUp(dataCursor, alignment);
            const size_t dataOffset = dataCursor;
            for (uint8_t value : entry.Payload)
            {
                bytes[dataCursor++] = value;
            }

            WriteLe64(bytes, recordOffset + PackageV1::EntryOffset::NameOffset, static_cast<uint64_t>(nameOffset));
            WriteLe32(bytes, recordOffset + PackageV1::EntryOffset::NameSize, static_cast<uint32_t>(entry.Name.size()));
            WriteLe32(bytes, recordOffset + PackageV1::EntryOffset::Type, entry.Type);
            WriteLe32(bytes, recordOffset + PackageV1::EntryOffset::Compression, static_cast<uint32_t>(AssetPackageCompression::None));
            WriteLe64(bytes, recordOffset + PackageV1::EntryOffset::DataOffset, static_cast<uint64_t>(dataOffset));
            WriteLe64(bytes, recordOffset + PackageV1::EntryOffset::StoredSize, static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes, recordOffset + PackageV1::EntryOffset::UncompressedSize, static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes, recordOffset + PackageV1::EntryOffset::PayloadHash,
                      ComputeAssetPackagePayloadHash(entry.Payload.data(), entry.Payload.size()));
        }

        return bytes;
    }

    void WriteBinaryFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        assert(output.is_open());
        if (!bytes.empty())
        {
            output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
    }

    String BuildManifest(uint64_t cookedHash,
                         std::string logicalPath = "Textures/Cooked.tga",
                         std::string cookedPackage = "Cooked/Textures.nvpkg",
                         std::string entryName = "Textures/Cooked.nvtex",
                         std::string format = "nvtex.v0")
    {
        const std::string cookedHashText = ToStdString(FormatAssetHashHex(cookedHash));
        const std::string entryTypeText = ToStdString(FormatAssetPackageFourCCText(MakeAssetPackageFourCC('T', 'e', 'x', '0')));
        const std::string json =
            "{"
            "\"version\":1,"
            "\"assets\":["
            "{"
            "\"logical_path\":\"" + logicalPath + "\","
            "\"kind\":\"texture\","
            "\"source_hash\":\"0000000000000001\","
            "\"variant\":\"default\","
            "\"format\":\"" + format + "\","
            "\"cooked_package\":\"" + cookedPackage + "\","
            "\"entry_name\":\"" + entryName + "\","
            "\"entry_type\":\"" + entryTypeText + "\","
            "\"cooked_hash\":\"" + cookedHashText + "\","
            "\"cooked_version\":0"
            "}"
            "]"
            "}";
        return ToCoreString(json);
    }

    void ConfigureRoot(RenderResourceManager &manager, const std::filesystem::path &root)
    {
        assert(manager.SetTextureAssetRoot(ToCoreString(ToAssetString(root))));
    }

    void WriteCookedPackage(const std::filesystem::path &root,
                            const std::string &packagePath,
                            const std::string &entryName,
                            const std::vector<uint8_t> &textureBytes)
    {
        WriteBinaryFile(root / packagePath,
                        BuildPackage({{entryName,
                                       MakeAssetPackageFourCC('T', 'e', 'x', '0'),
                                       textureBytes}}));
    }

    PreparedTextureAsset PrepareReadyAsset(RenderResourceManager &manager,
                                           const std::filesystem::path &root,
                                           const std::vector<uint8_t> &textureBytes,
                                           const char *logicalPath = "Textures/Cooked.tga")
    {
        WriteCookedPackage(root, "Cooked/Textures.nvpkg", "Textures/Cooked.nvtex", textureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size()),
                          logicalPath,
                          "Cooked/Textures.nvpkg",
                          "Textures/Cooked.nvtex")));
        return manager.PrepareTextureAssetForWorker(ToCoreString(logicalPath));
    }

    void AssertLooseFallbackStatus(const PreparedTextureAsset &prepared, PreparedTextureAssetStatus expectedStatus)
    {
        assert(prepared.Status == expectedStatus);
        assert(!prepared.Failed());
        assert(prepared.ShouldUseLooseFallback());
        assert(!prepared.HasCookedPayload());
        assert(prepared.Source == TextureLoadSource::LooseStbi);
    }

    void AssertFailureStatus(const PreparedTextureAsset &prepared, PreparedTextureAssetStatus expectedStatus)
    {
        assert(prepared.Status == expectedStatus);
        assert(prepared.Failed());
        assert(!prepared.ShouldUseLooseFallback());
        assert(!prepared.HasCookedPayload());
    }

    void TestPrepareCookedReadyIncludesRequiredFields()
    {
        const std::filesystem::path root = CreateTestRoot("PrepareReadyFields");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        assert(prepared.Status == PreparedTextureAssetStatus::CookedReady);
        assert(!prepared.Failed());
        assert(!prepared.ShouldUseLooseFallback());
        assert(prepared.HasCookedPayload());
        assert(prepared.RequestPath == ToCoreString("Textures/Cooked.tga"));
        assert(!prepared.ResolvedFallbackPath.empty());
        assert(prepared.LogicalPath == NorvesLib::Core::Container::AnsiString("Textures/Cooked.tga"));
        assert(!prepared.CacheKey.empty());
        assert(prepared.Generation > 0);
        assert(prepared.FallbackMode == TextureAssetFallbackMode::FailOnCookedFailure);
        assert(prepared.Source == TextureLoadSource::CookedNvtex);
        assert(prepared.Reason.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestFinalizeCookedReadyUploadsAndCaches()
    {
        const std::filesystem::path root = CreateTestRoot("FinalizeUploadsAndCaches");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        const TextureHandle firstHandle = manager.FinalizePreparedTextureAsset(prepared);
        assert(firstHandle.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(device->LastTexture);
        assert(device->LastTexture->Updates.size() == 2);

        const TextureHandle cachedHandle = manager.FinalizePreparedTextureAsset(prepared);
        assert(cachedHandle == firstHandle);
        assert(device->CreatedTextureDescs.size() == 1);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestFinalizeAfterShutdownReturnsInvalidWithoutGpuWork()
    {
        const std::filesystem::path root = CreateTestRoot("FinalizeAfterShutdown");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        assert(prepared.Status == PreparedTextureAssetStatus::CookedReady);
        manager.Shutdown();

        const TextureHandle handle = manager.FinalizePreparedTextureAsset(prepared);
        assert(!handle.IsValid());
        assert(device->CreatedTextureDescs.empty());
        assert(manager.GetResourceStats().TextureCount == 0);

        std::filesystem::remove_all(root);
    }

    void TestFinalizePostUploadDuplicateCacheReleasesNewHandle()
    {
        const std::filesystem::path root = CreateTestRoot("FinalizePostUploadDuplicate");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        bool bNestedFinalizeStarted = false;
        TextureHandle nestedHandle = TextureHandle::Invalid();
        device->OnFirstTextureUpdate = [&]()
        {
            if (bNestedFinalizeStarted)
            {
                return;
            }

            bNestedFinalizeStarted = true;
            nestedHandle = manager.FinalizePreparedTextureAsset(prepared);
            assert(nestedHandle.IsValid());
        };

        const TextureHandle outerHandle = manager.FinalizePreparedTextureAsset(prepared);
        assert(bNestedFinalizeStarted);
        assert(outerHandle == nestedHandle);
        assert(device->CreatedTextureDescs.size() == 2);
        assert(manager.GetResourceStats().TextureCount == 1);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestFinalizeChecksGenerationBeforeUpload()
    {
        const std::filesystem::path root = CreateTestRoot("FinalizeGenerationBeforeUpload");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        assert(manager.ResetTextureAssetManifest());

        const TextureHandle handle = manager.FinalizePreparedTextureAsset(prepared);
        assert(!handle.IsValid());
        assert(device->CreatedTextureDescs.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestFinalizePostUploadGenerationRaceDiscardsUpload()
    {
        const std::filesystem::path root = CreateTestRoot("FinalizePostUploadRace");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        device->OnFirstTextureUpdate = [&manager]()
        {
            assert(manager.ResetTextureAssetManifest());
        };

        const TextureHandle handle = manager.FinalizePreparedTextureAsset(prepared);
        assert(!handle.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(manager.GetResourceStats().TextureCount == 0);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestIsPreparedTextureAssetCurrentGenerationTransitions()
    {
        const std::filesystem::path root = CreateTestRoot("GenerationTransitions");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        assert(manager.IsPreparedTextureAssetCurrent(prepared));
        assert(manager.ResetTextureAssetManifest());
        assert(!manager.IsPreparedTextureAssetCurrent(prepared));

        prepared = PrepareReadyAsset(manager, root, textureBytes);
        assert(manager.IsPreparedTextureAssetCurrent(prepared));
        assert(manager.SetTextureAssetRoot(ToCoreString(ToAssetString(root))));
        assert(!manager.IsPreparedTextureAssetCurrent(prepared));

        prepared = manager.PrepareTextureAssetForWorker(ToCoreString("Textures/Cooked.tga"));
        assert(manager.IsPreparedTextureAssetCurrent(prepared));
        assert(manager.SetTextureAssetFallbackMode(TextureAssetFallbackMode::DebugAllowLooseFallback));
        assert(!manager.IsPreparedTextureAssetCurrent(prepared));

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestManifestMissingReturnsLooseFallbackWithoutReadingLoose()
    {
        const std::filesystem::path root = CreateTestRoot("ManifestMissing");

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = manager.PrepareTextureAssetForWorker(ToCoreString("Textures/MissingLoose.tga"));
        AssertLooseFallbackStatus(prepared, PreparedTextureAssetStatus::ManifestMissingLooseFallback);
        assert(prepared.ResolvedFallbackPath == ToCoreString(ToAssetString(root / "Textures" / "MissingLoose.tga")));
        assert(device->CreatedTextureDescs.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestVariantMissingReturnsLooseFallbackWithoutReadingLoose()
    {
        const std::filesystem::path root = CreateTestRoot("VariantMissing");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            1, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        WriteCookedPackage(root, "Cooked/Textures.nvpkg", "Textures/Cooked.nvtex", textureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size()),
                          "Textures/Other.tga")));

        const PreparedTextureAsset prepared = manager.PrepareTextureAssetForWorker(ToCoreString("Textures/MissingVariant.tga"));
        AssertLooseFallbackStatus(prepared, PreparedTextureAssetStatus::VariantMissingLooseFallback);
        assert(device->CreatedTextureDescs.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestInvalidAndAbsolutePathStatuses()
    {
        const std::filesystem::path root = CreateTestRoot("InvalidAbsolute");

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        AssertFailureStatus(manager.PrepareTextureAssetForWorker(String()), PreparedTextureAssetStatus::InvalidRequest);
        AssertFailureStatus(manager.PrepareTextureAssetForWorker(ToCoreString("../escape.tga")), PreparedTextureAssetStatus::InvalidPath);

        const std::string absolutePath = ToAssetString(root / "Textures" / "Absolute.tga");
        AssertFailureStatus(manager.PrepareTextureAssetForWorker(ToCoreString(absolutePath)),
                            PreparedTextureAssetStatus::AbsolutePathUnsupported);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestCookedPackageFailuresMapExactly()
    {
        const std::filesystem::path root = CreateTestRoot("CookedFailureExact");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            1, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);
        const uint64_t textureHash = ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size());

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash, "Textures/MissingPackage.tga", "Cooked/Missing.nvpkg", "Textures/Cooked.nvtex")));
        AssertFailureStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/MissingPackage.tga")),
                            PreparedTextureAssetStatus::CookedPackageReadFailed);

        WriteBinaryFile(root / "Cooked" / "BadPackage.nvpkg", {'N', 'V', 'P', 'K', 'x'});
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash, "Textures/BadPackage.tga", "Cooked/BadPackage.nvpkg", "Textures/Cooked.nvtex")));
        AssertFailureStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/BadPackage.tga")),
                            PreparedTextureAssetStatus::CookedPackageParseFailed);

        WriteCookedPackage(root, "Cooked/EntryMissing.nvpkg", "Textures/Other.nvtex", textureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash, "Textures/EntryMissing.tga", "Cooked/EntryMissing.nvpkg", "Textures/Cooked.nvtex")));
        AssertFailureStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/EntryMissing.tga")),
                            PreparedTextureAssetStatus::CookedEntryMissing);

        WriteCookedPackage(root, "Cooked/HashMismatch.nvpkg", "Textures/Cooked.nvtex", textureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash + 1u, "Textures/HashMismatch.tga", "Cooked/HashMismatch.nvpkg", "Textures/Cooked.nvtex")));
        AssertFailureStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/HashMismatch.tga")),
                            PreparedTextureAssetStatus::CookedEntryHashMismatch);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestCookedFailuresMapToDebugLooseFallback()
    {
        const std::filesystem::path root = CreateTestRoot("CookedFailureDebugFallback");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            1, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);
        const std::vector<uint8_t> invalidTextureBytes = {'b', 'a', 'd'};
        const uint64_t textureHash = ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size());
        const uint64_t invalidTextureHash = ComputeAssetPackagePayloadHash(invalidTextureBytes.data(), invalidTextureBytes.size());

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(manager.SetTextureAssetFallbackMode(TextureAssetFallbackMode::DebugAllowLooseFallback));

        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash, "Textures/MissingPackage.tga", "Cooked/Missing.nvpkg", "Textures/Cooked.nvtex")));
        AssertLooseFallbackStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/MissingPackage.tga")),
                                  PreparedTextureAssetStatus::DebugLooseFallback);

        WriteBinaryFile(root / "Cooked" / "BadPackage.nvpkg", {'N', 'V', 'P', 'K', 'x'});
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash, "Textures/BadPackage.tga", "Cooked/BadPackage.nvpkg", "Textures/Cooked.nvtex")));
        AssertLooseFallbackStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/BadPackage.tga")),
                                  PreparedTextureAssetStatus::DebugLooseFallback);

        WriteCookedPackage(root, "Cooked/EntryMissing.nvpkg", "Textures/Other.nvtex", textureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash, "Textures/EntryMissing.tga", "Cooked/EntryMissing.nvpkg", "Textures/Cooked.nvtex")));
        AssertLooseFallbackStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/EntryMissing.tga")),
                                  PreparedTextureAssetStatus::DebugLooseFallback);

        WriteCookedPackage(root, "Cooked/HashMismatch.nvpkg", "Textures/Cooked.nvtex", textureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(textureHash + 1u, "Textures/HashMismatch.tga", "Cooked/HashMismatch.nvpkg", "Textures/Cooked.nvtex")));
        AssertLooseFallbackStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/HashMismatch.tga")),
                                  PreparedTextureAssetStatus::DebugLooseFallback);

        WriteCookedPackage(root, "Cooked/BadNvtex.nvpkg", "Textures/Cooked.nvtex", invalidTextureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(invalidTextureHash, "Textures/BadNvtex.tga", "Cooked/BadNvtex.nvpkg", "Textures/Cooked.nvtex")));
        AssertLooseFallbackStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/BadNvtex.tga")),
                                  PreparedTextureAssetStatus::DebugLooseFallback);

        assert(device->CreatedTextureDescs.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestCookedTextureParseFailure()
    {
        const std::filesystem::path root = CreateTestRoot("CookedTextureParseFailure");
        const std::vector<uint8_t> invalidTextureBytes = {'b', 'a', 'd'};
        const uint64_t invalidTextureHash = ComputeAssetPackagePayloadHash(invalidTextureBytes.data(), invalidTextureBytes.size());

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        WriteCookedPackage(root, "Cooked/BadNvtex.nvpkg", "Textures/Cooked.nvtex", invalidTextureBytes);
        assert(manager.LoadTextureAssetManifestFromJsonText(
            BuildManifest(invalidTextureHash, "Textures/BadNvtex.tga", "Cooked/BadNvtex.nvpkg", "Textures/Cooked.nvtex")));
        AssertFailureStatus(manager.PrepareTextureAssetForWorker(ToCoreString("Textures/BadNvtex.tga")),
                            PreparedTextureAssetStatus::CookedTextureParseFailed);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestSplitMip0RGBA8UNormLinearSucceeds()
    {
        const std::filesystem::path root = CreateTestRoot("SplitSucceeds");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(
            2, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        PreparedCookedTextureMip0RGBA8UNormLinearSplit split;
        String reason("expected failure");
        assert(manager.TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(prepared, split, &reason));
        assert(reason.empty());
        assert(split.Width == 2);
        assert(split.Height == 1);
        assert(split.R.size() == 2);
        assert(split.G.size() == 2);
        assert(split.B.size() == 2);
        assert(split.A.size() == 2);
        assert(split.R[0] == 1 && split.G[0] == 2 && split.B[0] == 3 && split.A[0] == 4);
        assert(split.R[1] == 5 && split.G[1] == 6 && split.B[1] == 7 && split.A[1] == 8);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void AssertSplitUnsupported(CookedTexturePixelFormat pixelFormat,
                                CookedTextureColorSpace colorSpace,
                                uint32_t layerCount,
                                const char *name)
    {
        const std::filesystem::path root = CreateTestRoot(name);
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(2, 1, layerCount, pixelFormat, colorSpace);

        auto device = MakeShared<FakeDevice>();
        RenderResourceManager manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const PreparedTextureAsset prepared = PrepareReadyAsset(manager, root, textureBytes);
        assert(prepared.Status == PreparedTextureAssetStatus::CookedReady);

        PreparedCookedTextureMip0RGBA8UNormLinearSplit split;
        split.Width = 99;
        split.R.push_back(1);
        String reason;
        assert(!manager.TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(prepared, split, &reason));
        assert(split.Width == 0);
        assert(split.R.empty());
        assert(!reason.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestSplitFailsForUnsupportedCookedPayload()
    {
        AssertSplitUnsupported(CookedTexturePixelFormat::RGBA8UNorm,
                               CookedTextureColorSpace::Linear,
                               2,
                               "SplitArrayUnsupported");
        AssertSplitUnsupported(CookedTexturePixelFormat::RGBA8UNorm,
                               CookedTextureColorSpace::SRGB,
                               1,
                               "SplitSrgbUnsupported");
        AssertSplitUnsupported(CookedTexturePixelFormat::R8UNorm,
                               CookedTextureColorSpace::Linear,
                               1,
                               "SplitR8Unsupported");
        AssertSplitUnsupported(CookedTexturePixelFormat::RG8UNorm,
                               CookedTextureColorSpace::Linear,
                               1,
                               "SplitRG8Unsupported");
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "RenderResourceManagerPreparedTextureAssetTest start\n";

    TestPrepareCookedReadyIncludesRequiredFields();
    TestFinalizeCookedReadyUploadsAndCaches();
    TestFinalizeAfterShutdownReturnsInvalidWithoutGpuWork();
    TestFinalizePostUploadDuplicateCacheReleasesNewHandle();
    TestFinalizeChecksGenerationBeforeUpload();
    TestFinalizePostUploadGenerationRaceDiscardsUpload();
    TestIsPreparedTextureAssetCurrentGenerationTransitions();
    TestManifestMissingReturnsLooseFallbackWithoutReadingLoose();
    TestVariantMissingReturnsLooseFallbackWithoutReadingLoose();
    TestInvalidAndAbsolutePathStatuses();
    TestCookedPackageFailuresMapExactly();
    TestCookedFailuresMapToDebugLooseFallback();
    TestCookedTextureParseFailure();
    TestSplitMip0RGBA8UNormLinearSucceeds();
    TestSplitFailsForUnsupportedCookedPayload();

    std::cout << "RenderResourceManagerPreparedTextureAssetTest passed\n";
    return 0;
}
