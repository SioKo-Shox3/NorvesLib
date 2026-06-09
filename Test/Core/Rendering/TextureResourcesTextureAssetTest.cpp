#include "Asset/AssetPackageFormat.h"
#include "Asset/AssetManifest.h"
#include "Asset/CookedTextureFormat.h"
#include "Rendering/RenderResources.h"
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
#include "Thread/JobSystem.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc &desc)
            : Desc(desc)
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
        }

        NorvesLib::RHI::TextureDesc Desc;
        std::vector<UpdateCall> Updates;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &desc) override
        {
            CreatedTextureDescs.push_back(desc);
            LastTexture = MakeShared<FakeTexture>(desc);
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
    };

    std::filesystem::path CreateTestRoot(const char *name)
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     (std::string("NorvesLibTextureResourcesTextureAssetTest_") + name + "_" + std::to_string(now));
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

    std::vector<uint8_t> BuildCookedTextureBytes(uint32_t width, uint32_t height)
    {
        const uint32_t layerCount = 1;
        const CookedTexturePixelFormat pixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
        const CookedTextureColorSpace colorSpace = CookedTextureColorSpace::SRGB;
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
                                    layerCount *
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

    std::vector<uint8_t> BuildTga1x1()
    {
        return {
            0, 0, 2,
            0, 0, 0, 0, 0,
            0, 0, 0, 0,
            1, 0, 1, 0,
            32, 8,
            11, 22, 33, 255};
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
                         std::string entryName = "Textures/Cooked.nvtex")
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
            "\"format\":\"nvtex.v0.rgba8.srgb\","
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

    void ConfigureRoot(RenderResources &manager, const std::filesystem::path &root)
    {
        assert(manager.Textures().SetTextureAssetRoot(ToCoreString(ToAssetString(root))));
    }

    void WriteCookedPackage(const std::filesystem::path &root, const std::vector<uint8_t> &textureBytes)
    {
        WriteBinaryFile(root / "Cooked" / "Textures.nvpkg",
                        BuildPackage({{"Textures/Cooked.nvtex",
                                       MakeAssetPackageFourCC('T', 'e', 'x', '0'),
                                       textureBytes}}));
    }

    void TestPreInitTextureAssetConfigSurvivesInitialize()
    {
        const std::filesystem::path root = CreateTestRoot("PreInitConfig");
        WriteBinaryFile(root / "Textures" / "Cooked.tga", BuildTga1x1());

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        ConfigureRoot(manager, root);
        assert(manager.Textures().SetTextureAssetFallbackMode(TextureAssetFallbackMode::DebugAllowLooseFallback));
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(1, "Textures/Cooked.tga", "Cooked/Missing.nvpkg")));

        assert(manager.Initialize(device));
        const TextureHandle handle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(handle.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(device->LastTexture);
        assert(device->LastTexture->Updates.size() == 1);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestShutdownTextureAssetDelegatesReturnInvalidOrZero()
    {
        const std::filesystem::path root = CreateTestRoot("ShutdownDelegates");

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        ConfigureRoot(manager, root);
        assert(manager.Initialize(device));
        manager.Shutdown();

        bool bCallbackInvoked = false;
        assert(!manager.Textures().LoadTexture(ToCoreString("Textures/Missing.tga")).IsValid());
        assert(manager.Textures().LoadTextureAsync(
                   ToCoreString("Textures/Missing.tga"),
                   NorvesLib::Core::Delegate<void, TextureHandle>([&bCallbackInvoked](TextureHandle) {
                       bCallbackInvoked = true;
                   })) == 0);
        assert(!bCallbackInvoked);
        assert(manager.Textures().FlushCompletedTextureLoads() == 0);
        assert(manager.Textures().GetPendingAsyncLoadCount() == 0);

        std::filesystem::remove_all(root);
    }

    void TestReinitializePreservesConfigAndClearsTextureAssetCache()
    {
        const std::filesystem::path root = CreateTestRoot("ReinitializeConfigCache");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(2, 1);
        WriteCookedPackage(root, textureBytes);

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        ConfigureRoot(manager, root);
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size()))));

        assert(manager.Initialize(device));
        const TextureHandle firstHandle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(firstHandle.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        manager.Shutdown();

        assert(manager.Initialize(device));
        const TextureHandle secondHandle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(secondHandle.IsValid());
        assert(secondHandle != firstHandle);
        assert(device->CreatedTextureDescs.size() == 2);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestSyncCookedPathDoesNotNeedLooseFile()
    {
        const std::filesystem::path root = CreateTestRoot("SyncCooked");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(2, 1);
        WriteCookedPackage(root, textureBytes);

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size()))));

        const TextureHandle handle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(handle.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(device->CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::R8G8B8A8_SRGB);
        assert(device->CreatedTextureDescs[0].MipLevels == 2);
        assert(device->LastTexture);
        assert(device->LastTexture->Updates.size() == 2);
        assert(device->LastTexture->Updates[0].MipLevel == 0);
        assert(device->LastTexture->Updates[1].MipLevel == 1);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestAsyncCookedPathAndPendingGenerationGuard()
    {
        const std::filesystem::path root = CreateTestRoot("AsyncCooked");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(2, 1);
        WriteCookedPackage(root, textureBytes);

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size()))));

        std::vector<TextureHandle> callbacks;
        bool bConfigMutationRejectedDuringFlushCallback = false;
        const uint32_t requestA = manager.Textures().LoadTextureAsync(
            ToCoreString("Textures/Cooked.tga"),
            NorvesLib::Core::Delegate<void, TextureHandle>(
                [&callbacks, &manager, &root, &bConfigMutationRejectedDuringFlushCallback](TextureHandle handle) {
                    callbacks.push_back(handle);
                    bConfigMutationRejectedDuringFlushCallback =
                        !manager.Textures().SetTextureAssetRoot(ToCoreString(ToAssetString(root)));
                }));
        assert(manager.Textures().GetPendingAsyncLoadCount() == 1);
        const uint32_t requestB = manager.Textures().LoadTextureAsync(
            ToCoreString("Textures/Cooked.tga"),
            NorvesLib::Core::Delegate<void, TextureHandle>([&callbacks](TextureHandle handle) {
                callbacks.push_back(handle);
            }));

        assert(requestA != 0);
        assert(requestB == requestA);
        assert(manager.Textures().GetPendingAsyncLoadCount() == 1);
        assert(!manager.Textures().SetTextureAssetRoot(ToCoreString(ToAssetString(root))));

        NorvesLib::Thread::JobSystem::Get().WaitForAll();
        const uint32_t requestC = manager.Textures().LoadTextureAsync(
            ToCoreString("Textures/Cooked.tga"),
            NorvesLib::Core::Delegate<void, TextureHandle>([&callbacks](TextureHandle handle) {
                callbacks.push_back(handle);
            }));
        assert(requestC == requestA);
        assert(manager.Textures().GetPendingAsyncLoadCount() == 1);

        assert(manager.Textures().FlushCompletedTextureLoads() == 1);
        assert(manager.Textures().GetPendingAsyncLoadCount() == 0);
        assert(bConfigMutationRejectedDuringFlushCallback);
        assert(callbacks.size() == 3);
        assert(callbacks[0].IsValid());
        assert(callbacks[1].IsValid());
        assert(callbacks[2].IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(device->LastTexture);
        assert(device->LastTexture->Updates.size() == 2);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestAsyncCacheHitCallbackRunsAfterCacheLockRelease()
    {
        const std::filesystem::path root = CreateTestRoot("AsyncCacheHitLock");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(2, 1);
        WriteCookedPackage(root, textureBytes);

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size()))));

        const TextureHandle cachedHandle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(cachedHandle.IsValid());

        bool bCallbackInvoked = false;
        bool bConfigMutationSucceeded = false;
        TextureHandle callbackHandle = TextureHandle::Invalid();
        const uint32_t requestId = manager.Textures().LoadTextureAsync(
            ToCoreString("Textures/Cooked.tga"),
            NorvesLib::Core::Delegate<void, TextureHandle>(
                [&manager, &bCallbackInvoked, &bConfigMutationSucceeded, &callbackHandle](TextureHandle handle) {
                    bCallbackInvoked = true;
                    callbackHandle = handle;
                    bConfigMutationSucceeded = manager.Textures().ResetTextureAssetManifest();
                }));

        assert(requestId == 0);
        assert(bCallbackInvoked);
        assert(callbackHandle == cachedHandle);
        assert(bConfigMutationSucceeded);
        assert(manager.Textures().GetPendingAsyncLoadCount() == 0);
        assert(device->CreatedTextureDescs.size() == 1);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestAsyncDebugFallbackLogsEvenWhenLooseMissing()
    {
        const std::filesystem::path root = CreateTestRoot("AsyncDebugFallbackLooseMissing");

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(manager.Textures().SetTextureAssetFallbackMode(TextureAssetFallbackMode::DebugAllowLooseFallback));
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(1, "Textures/Cooked.tga", "Cooked/Missing.nvpkg")));

        std::vector<TextureHandle> callbacks;
        const uint32_t requestId = manager.Textures().LoadTextureAsync(
            ToCoreString("Textures/Cooked.tga"),
            NorvesLib::Core::Delegate<void, TextureHandle>([&callbacks](TextureHandle handle) {
                callbacks.push_back(handle);
            }));

        assert(requestId != 0);
        NorvesLib::Thread::JobSystem::Get().WaitForAll();
        assert(manager.Textures().FlushCompletedTextureLoads() == 1);
        assert(callbacks.size() == 1);
        assert(!callbacks[0].IsValid());
        assert(device->CreatedTextureDescs.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestManifestMissingUsesLooseFallback()
    {
        const std::filesystem::path root = CreateTestRoot("LooseFallback");
        WriteBinaryFile(root / "Textures" / "Loose.tga", BuildTga1x1());

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);

        const TextureHandle handle = manager.Textures().LoadTexture(ToCoreString("Textures/Loose.tga"));
        assert(handle.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(device->LastTexture);
        assert(device->LastTexture->Updates.size() == 1);
        assert(device->LastTexture->Updates[0].SlicePitch == 4);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestInvalidManifestDoesNotFallback()
    {
        const std::filesystem::path root = CreateTestRoot("InvalidManifest");
        WriteBinaryFile(root / "Textures" / "Loose.tga", BuildTga1x1());

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(!manager.Textures().LoadTextureAssetManifestFromJsonText(ToCoreString("{"), ToCoreString("broken.manifest.json")));

        const TextureHandle handle = manager.Textures().LoadTexture(ToCoreString("Textures/Loose.tga"));
        assert(!handle.IsValid());
        assert(device->CreatedTextureDescs.empty());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestDebugFallbackForCookedFailure()
    {
        const std::filesystem::path root = CreateTestRoot("DebugFallback");
        WriteBinaryFile(root / "Textures" / "Cooked.tga", BuildTga1x1());

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(manager.Textures().SetTextureAssetFallbackMode(TextureAssetFallbackMode::DebugAllowLooseFallback));
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(1, "Textures/Cooked.tga", "Cooked/Missing.nvpkg")));

        const TextureHandle handle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(handle.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(device->LastTexture);
        assert(device->LastTexture->Updates.size() == 1);

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    void TestGenerationChangeClearsAssetCache()
    {
        const std::filesystem::path root = CreateTestRoot("Generation");
        const std::vector<uint8_t> textureBytes = BuildCookedTextureBytes(2, 1);
        WriteCookedPackage(root, textureBytes);

        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        assert(manager.Initialize(device));
        ConfigureRoot(manager, root);
        assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
            BuildManifest(ComputeAssetPackagePayloadHash(textureBytes.data(), textureBytes.size()))));

        const TextureHandle cookedHandle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(cookedHandle.IsValid());
        assert(manager.Textures().ResetTextureAssetManifest());

        const TextureHandle missingLooseHandle = manager.Textures().LoadTexture(ToCoreString("Textures/Cooked.tga"));
        assert(!missingLooseHandle.IsValid());

        manager.Shutdown();
        std::filesystem::remove_all(root);
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "TextureResourcesTextureAssetTest start\n";

    NorvesLib::Thread::JobSystem::Get().Initialize(2);

    TestPreInitTextureAssetConfigSurvivesInitialize();
    TestShutdownTextureAssetDelegatesReturnInvalidOrZero();
    TestReinitializePreservesConfigAndClearsTextureAssetCache();
    TestSyncCookedPathDoesNotNeedLooseFile();
    TestAsyncCookedPathAndPendingGenerationGuard();
    TestAsyncCacheHitCallbackRunsAfterCacheLockRelease();
    TestManifestMissingUsesLooseFallback();
    TestInvalidManifestDoesNotFallback();
    TestDebugFallbackForCookedFailure();
    TestAsyncDebugFallbackLogsEvenWhenLooseMissing();
    TestGenerationChangeClearsAssetCache();

    NorvesLib::Thread::JobSystem::Get().Shutdown();

    std::cout << "TextureResourcesTextureAssetTest passed\n";
    return 0;
}
