#include "Asset/AssetManifest.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Debug/DebugConfig.h"
#include "Logging/Logger.h"
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
#include <iterator>
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
using namespace NorvesLib::Core::Logging;
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

    struct ManifestAsset
    {
        std::string LogicalPath;
        std::string PackagePath;
        std::string EntryName;
        uint64_t CookedHash = 0;
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

    std::filesystem::path CreateTestRoot()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("NorvesLibTextureResourcesAssetLoadProfileContract_" + std::to_string(now));
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
        const CookedTextureColorSpace colorSpace = CookedTextureColorSpace::Linear;
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
            WriteLe32(bytes,
                      recordOffset + PackageV1::EntryOffset::Compression,
                      static_cast<uint32_t>(AssetPackageCompression::None));
            WriteLe64(bytes, recordOffset + PackageV1::EntryOffset::DataOffset, static_cast<uint64_t>(dataOffset));
            WriteLe64(bytes, recordOffset + PackageV1::EntryOffset::StoredSize, static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes,
                      recordOffset + PackageV1::EntryOffset::UncompressedSize,
                      static_cast<uint64_t>(entry.Payload.size()));
            WriteLe64(bytes,
                      recordOffset + PackageV1::EntryOffset::PayloadHash,
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

    String BuildManifest(const std::vector<ManifestAsset> &assets)
    {
        const std::string entryTypeText = ToStdString(
            FormatAssetPackageFourCCText(MakeAssetPackageFourCC('T', 'e', 'x', '0')));
        std::string json = "{\"version\":1,\"assets\":[";
        for (size_t index = 0; index < assets.size(); ++index)
        {
            const ManifestAsset &asset = assets[index];
            if (index > 0)
            {
                json += ",";
            }
            json += "{";
            json += "\"logical_path\":\"" + asset.LogicalPath + "\",";
            json += "\"kind\":\"texture\",";
            json += "\"source_hash\":\"0000000000000001\",";
            json += "\"variant\":\"default\",";
            json += "\"format\":\"nvtex.v0\",";
            json += "\"cooked_package\":\"" + asset.PackagePath + "\",";
            json += "\"entry_name\":\"" + asset.EntryName + "\",";
            json += "\"entry_type\":\"" + entryTypeText + "\",";
            json += "\"cooked_hash\":\"" + ToStdString(FormatAssetHashHex(asset.CookedHash)) + "\",";
            json += "\"cooked_version\":0";
            json += "}";
        }
        json += "]}";
        return ToCoreString(json);
    }

    void AssertContains(const std::string &text, const char *needle)
    {
        if (text.find(needle) == std::string::npos)
        {
            std::cerr << "Expected log substring missing: " << needle << "\n";
            std::exit(1);
        }
    }

    std::string ReadTextFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input.is_open());
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

#if NORVES_ENABLE_LOGGING
    void InitializeFileLogger(const std::filesystem::path &logPath)
    {
        LogConfig config;
        config.minLevel = LogLevel::Info;
        config.consoleMinLevel = LogLevel::Fatal;
        config.outputType = LogOutput::File;
        config.logFilePath = ToCoreString(ToAssetString(logPath));
        config.bAsyncLogging = false;
        config.bAutoFlush = true;
        config.bIncludeSourceInfo = false;
        assert(Logger::GetInstance().Initialize(config));
    }
#endif
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "TextureResourcesAssetLoadProfileContractTest start\n";

#if !NORVES_ENABLE_LOGGING
    std::cout << "TextureResourcesAssetLoadProfileContractTest skipped: NORVES_ENABLE_LOGGING == 0\n";
    return 0;
#else
    const std::filesystem::path root = CreateTestRoot();
    const std::filesystem::path logPath = root / "AssetLoadProfileContract.log";
    InitializeFileLogger(logPath);

    const std::vector<uint8_t> directTexture = BuildCookedTextureBytes(2, 1);
    const std::vector<uint8_t> asyncTexture = BuildCookedTextureBytes(2, 1);
    const std::vector<uint8_t> preparedTexture = BuildCookedTextureBytes(2, 1);

    WriteCookedPackage(root, "Cooked/Direct.nvpkg", "Textures/Direct.nvtex", directTexture);
    WriteCookedPackage(root, "Cooked/Async.nvpkg", "Textures/Async.nvtex", asyncTexture);
    WriteCookedPackage(root, "Cooked/Prepared.nvpkg", "Textures/Prepared.nvtex", preparedTexture);

    auto device = MakeShared<FakeDevice>();
    RenderResources manager;
    assert(manager.Initialize(device));
    assert(manager.Textures().SetTextureAssetRoot(ToCoreString(ToAssetString(root))));
    assert(manager.Textures().LoadTextureAssetManifestFromJsonText(
        BuildManifest({
            {"Textures/Direct.tga",
             "Cooked/Direct.nvpkg",
             "Textures/Direct.nvtex",
             ComputeAssetPackagePayloadHash(directTexture.data(), directTexture.size())},
            {"Textures/Async.tga",
             "Cooked/Async.nvpkg",
             "Textures/Async.nvtex",
             ComputeAssetPackagePayloadHash(asyncTexture.data(), asyncTexture.size())},
            {"Textures/Prepared.tga",
             "Cooked/Prepared.nvpkg",
             "Textures/Prepared.nvtex",
             ComputeAssetPackagePayloadHash(preparedTexture.data(), preparedTexture.size())},
        })));

    const TextureHandle directHandle = manager.Textures().LoadTexture(ToCoreString("Textures/Direct.tga"));
    assert(directHandle.IsValid());

    std::vector<TextureHandle> callbacks;
    const uint32_t asyncRequestId = manager.Textures().LoadTextureAsync(
        ToCoreString("Textures/Async.tga"),
        NorvesLib::Core::Delegate<void, TextureHandle>([&callbacks](TextureHandle handle) {
            callbacks.push_back(handle);
        }));
    assert(asyncRequestId != 0);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    assert(manager.Textures().FlushCompletedTextureLoads() == 1);
    assert(callbacks.size() == 1);
    assert(callbacks[0].IsValid());

    const PreparedTextureAsset prepared = manager.Textures().PrepareTextureAssetForWorker(
        ToCoreString("Textures/Prepared.tga"),
        String(),
        "worker",
        77);
    assert(prepared.Status == PreparedTextureAssetStatus::CookedReady);
    const TextureHandle preparedHandle = manager.Textures().FinalizePreparedTextureAsset(prepared, "main_render", 77);
    assert(preparedHandle.IsValid());

    PreparedCookedTextureMip0RGBA8UNormLinearSplit split;
    String splitReason;
    assert(manager.Textures().TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(prepared, split, &splitReason, "worker", 77));
    assert(splitReason.empty());
    assert(split.Width == 2);
    assert(split.Height == 1);

    manager.Shutdown();
    Logger::GetInstance().Flush();
    Logger::GetInstance().Shutdown();

    const std::string logText = ReadTextFile(logPath);
    AssertContains(logText, "stage=texture_asset_resolve role=caller");
    AssertContains(logText, "stage=texture_cooked_parse role=caller source=cooked_nvtex");
    AssertContains(logText, "stage=texture_cooked_upload role=caller source=cooked_nvtex");
    AssertContains(logText, "stage=texture_asset_resolve role=worker");
    AssertContains(logText, "stage=texture_cooked_parse role=worker source=cooked_nvtex");
    AssertContains(logText, "stage=texture_cooked_upload role=main_render source=cooked_nvtex");
    AssertContains(logText, "stage=texture_prepare_asset");
    AssertContains(logText, "stage=texture_prepared_cooked_upload role=main_render source=cooked_nvtex");
    AssertContains(logText, "stage=texture_prepared_finalize role=main_render source=cooked_nvtex");
    AssertContains(logText, "stage=texture_prepared_split");

    std::filesystem::remove_all(root);

    std::cout << "TextureResourcesAssetLoadProfileContractTest passed\n";
    return 0;
#endif
}
