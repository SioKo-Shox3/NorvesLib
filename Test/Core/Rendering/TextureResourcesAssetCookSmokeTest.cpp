#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "FileStream/Package.h"
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

#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::MakeShared;
using NorvesLib::Core::Container::String;

namespace
{
    struct Options
    {
        std::filesystem::path AssetRoot;
        std::filesystem::path ManifestPath;
        std::filesystem::path PackagePath;
        std::string LogicalPath;
        std::string EntryName;
        uint32_t ExpectedWidth = 0;
        uint32_t ExpectedHeight = 0;
        uint32_t ExpectedMipCount = 0;
        NorvesLib::RHI::Format ExpectedFormat = NorvesLib::RHI::Format::UNKNOWN;
    };

    struct UpdateCall
    {
        uint32_t RowPitch = 0;
        uint32_t SlicePitch = 0;
        uint32_t MipLevel = 0;
        uint32_t ArrayIndex = 0;
        std::vector<uint8_t> Bytes;
    };

    void Require(bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "Requirement failed: " << message << "\n";
            std::exit(1);
        }
    }

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
            Require(data != nullptr, "texture update data must not be null");

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
        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
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

    String ToCoreString(const std::filesystem::path &path)
    {
        return ToCoreString(path.generic_string());
    }

    std::string ReadTextFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(input.is_open(), "manifest file must open");
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    uint32_t ParseU32(const std::string &value, const char *name)
    {
        uint32_t parsed = 0;
        const char *begin = value.data();
        const char *end = value.data() + value.size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc() || result.ptr != end)
        {
            std::cerr << "Invalid integer for " << name << ": " << value << "\n";
            std::exit(1);
        }
        return parsed;
    }

    NorvesLib::RHI::Format ParseFormat(const std::string &value)
    {
        if (value == "R8G8B8A8_SRGB")
        {
            return NorvesLib::RHI::Format::R8G8B8A8_SRGB;
        }
        if (value == "R8G8B8A8_UNORM")
        {
            return NorvesLib::RHI::Format::R8G8B8A8_UNORM;
        }
        if (value == "R8G8_UNORM")
        {
            return NorvesLib::RHI::Format::R8G8_UNORM;
        }
        if (value == "R8_UNORM")
        {
            return NorvesLib::RHI::Format::R8_UNORM;
        }

        std::cerr << "Unsupported expected format: " << value << "\n";
        std::exit(1);
    }

    uint32_t BytesPerPixel(NorvesLib::RHI::Format format)
    {
        switch (format)
        {
        case NorvesLib::RHI::Format::R8_UNORM:
            return 1;
        case NorvesLib::RHI::Format::R8G8_UNORM:
            return 2;
        case NorvesLib::RHI::Format::R8G8B8A8_UNORM:
        case NorvesLib::RHI::Format::R8G8B8A8_SRGB:
            return 4;
        default:
            return 0;
        }
    }

    Options ParseOptions(int argc, char **argv)
    {
        Options options;
        for (int index = 1; index < argc; ++index)
        {
            const std::string key = argv[index];
            Require(index + 1 < argc, "all arguments require a value");
            const std::string value = argv[++index];

            if (key == "--asset-root")
            {
                options.AssetRoot = value;
            }
            else if (key == "--manifest")
            {
                options.ManifestPath = value;
            }
            else if (key == "--package")
            {
                options.PackagePath = value;
            }
            else if (key == "--logical")
            {
                options.LogicalPath = value;
            }
            else if (key == "--entry")
            {
                options.EntryName = value;
            }
            else if (key == "--width")
            {
                options.ExpectedWidth = ParseU32(value, "--width");
            }
            else if (key == "--height")
            {
                options.ExpectedHeight = ParseU32(value, "--height");
            }
            else if (key == "--mips")
            {
                options.ExpectedMipCount = ParseU32(value, "--mips");
            }
            else if (key == "--format")
            {
                options.ExpectedFormat = ParseFormat(value);
            }
            else
            {
                std::cerr << "Unknown argument: " << key << "\n";
                std::exit(1);
            }
        }

        Require(!options.AssetRoot.empty(), "--asset-root is required");
        Require(!options.ManifestPath.empty(), "--manifest is required");
        Require(!options.PackagePath.empty(), "--package is required");
        Require(!options.LogicalPath.empty(), "--logical is required");
        Require(!options.EntryName.empty(), "--entry is required");
        Require(options.ExpectedWidth > 0, "--width is required");
        Require(options.ExpectedHeight > 0, "--height is required");
        Require(options.ExpectedMipCount > 0, "--mips is required");
        Require(options.ExpectedFormat != NorvesLib::RHI::Format::UNKNOWN, "--format is required");
        return options;
    }

    CookedTextureData LoadExpectedCookedTexture(const Options &options)
    {
        NorvesLib::FileStream::Package package;
        Require(package.Load(ToCoreString(options.PackagePath)), "generated package must load");

        const AssetPackageFourCC entryType = MakeAssetPackageFourCC('T', 'e', 'x', '0');
        AssetBlob blob = package.OpenEntry(NorvesLib::Core::Container::AnsiString(options.EntryName.c_str()), entryType);
        Require(blob.IsValid(), "generated package must contain texture entry");

        CookedTextureParseResult parseResult = ParseCookedTexture(blob);
        Require(parseResult.Succeeded(), "generated package texture entry must parse as .nvtex");
        return parseResult.Texture;
    }

    uint32_t ExpectedMipDimension(uint32_t baseDimension, uint32_t mipIndex)
    {
        const uint32_t shifted = baseDimension >> mipIndex;
        return shifted == 0 ? 1 : shifted;
    }

    void VerifyUploads(const Options &options,
                       const FakeDevice &device,
                       const CookedTextureData &expectedTexture)
    {
        Require(device.CreatedTextureDescs.size() == 1, "exactly one texture must be created");
        const NorvesLib::RHI::TextureDesc &desc = device.CreatedTextureDescs[0];
        Require(desc.Width == options.ExpectedWidth, "created texture width must match");
        Require(desc.Height == options.ExpectedHeight, "created texture height must match");
        Require(desc.MipLevels == options.ExpectedMipCount, "created texture mip count must match");
        Require(desc.ArraySize == 1, "created texture array size must be one");
        Require(desc.TextureFormat == options.ExpectedFormat, "created texture format must match");

        Require(expectedTexture.Width == options.ExpectedWidth, "parsed .nvtex width must match");
        Require(expectedTexture.Height == options.ExpectedHeight, "parsed .nvtex height must match");
        Require(expectedTexture.MipCount == options.ExpectedMipCount, "parsed .nvtex mip count must match");
        Require(expectedTexture.LayerCount == 1, "parsed .nvtex layer count must be one");

        Require(device.LastTexture != nullptr, "fake texture must exist");
        Require(device.LastTexture->Updates.size() == options.ExpectedMipCount, "update count must match mip count");

        const uint32_t bytesPerPixel = BytesPerPixel(options.ExpectedFormat);
        Require(bytesPerPixel > 0, "expected format must have a byte size");

        for (uint32_t mipIndex = 0; mipIndex < options.ExpectedMipCount; ++mipIndex)
        {
            const uint32_t mipWidth = ExpectedMipDimension(options.ExpectedWidth, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(options.ExpectedHeight, mipIndex);
            const uint32_t rowPitch = mipWidth * bytesPerPixel;
            const uint32_t slicePitch = rowPitch * mipHeight;
            const UpdateCall &update = device.LastTexture->Updates[mipIndex];
            const NorvesLib::Core::Container::Span<const uint8_t> expectedBytes =
                expectedTexture.GetMipBytes(mipIndex);

            Require(update.MipLevel == mipIndex, "update mip level must match");
            Require(update.ArrayIndex == 0, "update array index must be zero");
            Require(update.RowPitch == rowPitch, "update row pitch must match");
            Require(update.SlicePitch == slicePitch, "update slice pitch must match");
            Require(update.Bytes.size() == expectedBytes.size(), "update byte size must match .nvtex mip");
            Require(std::memcmp(update.Bytes.data(), expectedBytes.data(), expectedBytes.size()) == 0,
                    "uploaded bytes must match generated .nvtex mip payload");
        }

        Require(!device.LastTexture->Updates[0].Bytes.empty(), "mip0 upload must contain representative bytes");

        if (options.ExpectedMipCount > 1)
        {
            Require(device.LastTexture->Updates[1].RowPitch == ExpectedMipDimension(options.ExpectedWidth, 1) * bytesPerPixel,
                    "mip1 row pitch must match");
            Require(device.LastTexture->Updates[1].SlicePitch ==
                        ExpectedMipDimension(options.ExpectedWidth, 1) *
                            ExpectedMipDimension(options.ExpectedHeight, 1) *
                            bytesPerPixel,
                    "mip1 slice pitch must match");
        }
    }

    void ConfigureManager(RenderResources &manager,
                          const Options &options,
                          const std::string &manifestText)
    {
        Require(manager.Textures().SetTextureAssetRoot(ToCoreString(options.AssetRoot)), "texture asset root must be set");
        Require(manager.Textures().LoadTextureAssetManifestFromJsonText(ToCoreString(manifestText), ToCoreString(options.ManifestPath)),
                "texture asset manifest must load");
    }

    void TestSyncCookedPath(const Options &options,
                            const std::string &manifestText,
                            const CookedTextureData &expectedTexture)
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        Require(manager.Initialize(device), "sync manager must initialize");
        ConfigureManager(manager, options, manifestText);

        const TextureHandle handle = manager.Textures().LoadTexture(ToCoreString(options.LogicalPath));
        Require(handle.IsValid(), "sync cooked texture load must return a valid handle");
        VerifyUploads(options, *device, expectedTexture);

        manager.Shutdown();
    }

    void TestAsyncCookedPath(const Options &options,
                             const std::string &manifestText,
                             const CookedTextureData &expectedTexture)
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources manager;
        Require(manager.Initialize(device), "async manager must initialize");
        ConfigureManager(manager, options, manifestText);

        std::vector<TextureHandle> callbacks;
        const uint32_t requestId = manager.Textures().LoadTextureAsync(
            ToCoreString(options.LogicalPath),
            NorvesLib::Core::Delegate<void, TextureHandle>([&callbacks](TextureHandle handle) {
                callbacks.push_back(handle);
            }));
        Require(requestId != 0, "async cooked load must not complete through cache immediately");

        NorvesLib::Thread::JobSystem::Get().WaitForAll();
        Require(manager.Textures().FlushCompletedTextureLoads() == 1, "async flush must process one cooked texture");
        Require(callbacks.size() == 1, "async callback count must be one");
        Require(callbacks[0].IsValid(), "async callback texture handle must be valid");
        VerifyUploads(options, *device, expectedTexture);

        manager.Shutdown();
    }
}

int main(int argc, char **argv)
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "TextureResourcesAssetCookSmokeTest start\n";

    const Options options = ParseOptions(argc, argv);
    Require(std::filesystem::exists(options.AssetRoot), "asset root must exist");
    Require(std::filesystem::exists(options.ManifestPath), "manifest must exist");
    Require(std::filesystem::exists(options.PackagePath), "package must exist");
    Require(!std::filesystem::exists(options.AssetRoot / std::filesystem::path(options.LogicalPath)),
            "runtime root must not contain loose logical texture file");

    const std::string manifestText = ReadTextFile(options.ManifestPath);
    const CookedTextureData expectedTexture = LoadExpectedCookedTexture(options);

    NorvesLib::Thread::JobSystem::Get().Initialize(2);
    TestSyncCookedPath(options, manifestText, expectedTexture);
    TestAsyncCookedPath(options, manifestText, expectedTexture);
    NorvesLib::Thread::JobSystem::Get().Shutdown();

    std::cout << "TextureResourcesAssetCookSmokeTest passed\n";
    return 0;
}
