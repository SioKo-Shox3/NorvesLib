#include "Rendering/CookedTextureUpload.h"

#include "Asset/AssetBlob.h"
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
#include <cstring>
#include <cstdlib>
#include <iostream>
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
using namespace NorvesLib::Core::Asset::CookedTextureFormatV0;
using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::MakeShared;
using NorvesLib::Core::Container::Span;

namespace
{
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

    size_t MipRecordOffsetFor(size_t mipIndex)
    {
        return HeaderSize + mipIndex * MipRecordSize;
    }

    uint32_t ExpectedMipDimension(uint32_t baseDimension, uint32_t mipIndex)
    {
        const uint32_t shifted = baseDimension >> mipIndex;
        return shifted == 0 ? 1 : shifted;
    }

    // 形式ごとの 1 ブロックの大きさ。テスト側で独立に持ち、実装の計算を写さない。
    struct TestBlock
    {
        uint32_t Width;
        uint32_t Height;
        uint32_t Bytes;
    };

    TestBlock TestBlockOf(CookedTexturePixelFormat pixelFormat)
    {
        switch (pixelFormat)
        {
        case CookedTexturePixelFormat::R8UNorm:
            return {1, 1, 1};
        case CookedTexturePixelFormat::RG8UNorm:
        case CookedTexturePixelFormat::R16UNorm:
            return {1, 1, 2};
        case CookedTexturePixelFormat::RGBA8UNorm:
            return {1, 1, 4};
        case CookedTexturePixelFormat::BC1:
        case CookedTexturePixelFormat::BC4:
            return {4, 4, 8};
        case CookedTexturePixelFormat::BC5:
        case CookedTexturePixelFormat::BC7:
            return {4, 4, 16};
        }
        return {1, 1, 0};
    }

    // versionMinor を省略すると v0.0 で書く。BC と R16 は v0.1 を明示して書く。
    std::vector<uint8_t> BuildTextureBytes(uint32_t width,
                                           uint32_t height,
                                           uint32_t layerCount,
                                           CookedTexturePixelFormat pixelFormat,
                                           CookedTextureColorSpace colorSpace,
                                           uint16_t versionMinor = VersionMinor)
    {
        const uint32_t mipCount = ComputeCookedTextureFullMipCount(width, height);
        const TestBlock block = TestBlockOf(pixelFormat);
        const size_t mipTableOffset = HeaderSize;
        const size_t mipTableSize = static_cast<size_t>(mipCount) * MipRecordSize;
        const size_t payloadOffset = mipTableOffset + mipTableSize;

        std::vector<std::vector<uint8_t>> mipPayloads;
        mipPayloads.reserve(mipCount);

        size_t payloadSize = 0;
        uint8_t nextValue = 0;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const uint32_t mipWidth = ExpectedMipDimension(width, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(height, mipIndex);
            const size_t blocksX = (mipWidth + block.Width - 1) / block.Width;
            const size_t blocksY = (mipHeight + block.Height - 1) / block.Height;
            const size_t dataSize = blocksX * blocksY * static_cast<size_t>(layerCount) * block.Bytes;
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
        std::memcpy(bytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, versionMinor);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(bytes, HeaderOffset::MipRecordSize, static_cast<uint32_t>(MipRecordSize));
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::MipTableOffset, static_cast<uint64_t>(mipTableOffset));
        WriteLe64(bytes, HeaderOffset::MipTableSize, static_cast<uint64_t>(mipTableSize));
        WriteLe64(bytes, HeaderOffset::PayloadOffset, static_cast<uint64_t>(payloadOffset));
        WriteLe64(bytes, HeaderOffset::PayloadSize, static_cast<uint64_t>(payloadSize));
        WriteLe32(bytes, HeaderOffset::Width, width);
        WriteLe32(bytes, HeaderOffset::Height, height);
        WriteLe32(bytes, HeaderOffset::LayerCount, layerCount);
        WriteLe32(bytes, HeaderOffset::MipCount, mipCount);
        WriteLe32(bytes, HeaderOffset::PixelFormat, static_cast<uint32_t>(pixelFormat));
        WriteLe32(bytes, HeaderOffset::ColorSpace, static_cast<uint32_t>(colorSpace));

        size_t payloadCursor = payloadOffset;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const std::vector<uint8_t> &mipBytes = mipPayloads[mipIndex];
            const size_t recordOffset = MipRecordOffsetFor(mipIndex);
            const uint32_t mipWidth = ExpectedMipDimension(width, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(height, mipIndex);

            WriteLe64(bytes, recordOffset + MipRecordOffset::DataOffset, static_cast<uint64_t>(payloadCursor));
            WriteLe64(bytes, recordOffset + MipRecordOffset::DataSize, static_cast<uint64_t>(mipBytes.size()));
            WriteLe32(bytes, recordOffset + MipRecordOffset::Width, mipWidth);
            WriteLe32(bytes, recordOffset + MipRecordOffset::Height, mipHeight);

            std::memcpy(bytes.data() + payloadCursor, mipBytes.data(), mipBytes.size());
            payloadCursor += mipBytes.size();
        }

        WriteLe64(bytes,
                  HeaderOffset::PayloadHash,
                  ComputeCookedTexturePayloadHash(bytes.data() + payloadOffset, payloadSize));
        return bytes;
    }

    CookedTextureData BuildTexture(uint32_t width,
                                   uint32_t height,
                                   uint32_t layerCount,
                                   CookedTexturePixelFormat pixelFormat,
                                   CookedTextureColorSpace colorSpace,
                                   uint16_t versionMinor = VersionMinor)
    {
        const std::vector<uint8_t> bytes =
            BuildTextureBytes(width, height, layerCount, pixelFormat, colorSpace, versionMinor);
        AssetBlob blob = AssetBlob::CopyBytes(Span<const uint8_t>(bytes.data(), bytes.size()), "memory.nvtex");
        CookedTextureParseResult result = ParseCookedTexture(std::move(blob));
        assert(result.Succeeded());
        return std::move(result.Texture);
    }

    bool HasUsage(NorvesLib::RHI::ResourceUsage usage, NorvesLib::RHI::ResourceUsage flag)
    {
        return (usage & flag) != NorvesLib::RHI::ResourceUsage::None;
    }

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
            if (bFailTextureCreation)
            {
                return {};
            }

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

        bool bFailTextureCreation = false;
        NorvesLib::RHI::DeviceCapabilities Capabilities;
        std::vector<NorvesLib::RHI::TextureDesc> CreatedTextureDescs;
        NorvesLib::Core::Container::TSharedPtr<FakeTexture> LastTexture;
    };

    void AssertByteSequence(const std::vector<uint8_t> &bytes, uint8_t startValue)
    {
        for (size_t index = 0; index < bytes.size(); ++index)
        {
            assert(bytes[index] == static_cast<uint8_t>(startValue + index));
        }
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "CookedTextureUploadTest start\n";

    {
        FakeDevice device;
        const CookedTextureData texture =
            BuildTexture(4, 2, 2, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);

        const CookedTextureUploadResult result =
            CreateAndUploadCookedTexture(&device, texture, "array_srgb");

        assert(result.Succeeded());
        assert(result.Status == CookedTextureUploadStatus::Success);
        assert(result.UploadedBytes == 88);
        assert(result.CreateInfo.Width == 4);
        assert(result.CreateInfo.Height == 2);
        assert(result.CreateInfo.MipLevels == 3);
        assert(result.CreateInfo.ArraySize == 2);
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::RGBA8_SRGB);
        assert(result.CreateInfo.Type == TextureType::Texture2DArray);

        assert(device.CreatedTextureDescs.size() == 1);
        const NorvesLib::RHI::TextureDesc &desc = device.CreatedTextureDescs[0];
        assert(desc.Width == 4);
        assert(desc.Height == 2);
        assert(desc.Depth == 1);
        assert(desc.MipLevels == 3);
        assert(desc.ArraySize == 2);
        assert(desc.TextureFormat == NorvesLib::RHI::Format::R8G8B8A8_SRGB);
        assert(desc.Dimension == NorvesLib::RHI::TextureDimension::Texture2D);
        assert(!desc.IsCubemap);
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::ShaderRead));
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::TransferDst));

        assert(device.LastTexture);
        assert(device.LastTexture->Updates.size() == 6);

        const UpdateCall &mip0Layer0 = device.LastTexture->Updates[0];
        assert(mip0Layer0.RowPitch == 16);
        assert(mip0Layer0.SlicePitch == 32);
        assert(mip0Layer0.MipLevel == 0);
        assert(mip0Layer0.ArrayIndex == 0);
        AssertByteSequence(mip0Layer0.Bytes, 0);

        const UpdateCall &mip0Layer1 = device.LastTexture->Updates[1];
        assert(mip0Layer1.RowPitch == 16);
        assert(mip0Layer1.SlicePitch == 32);
        assert(mip0Layer1.MipLevel == 0);
        assert(mip0Layer1.ArrayIndex == 1);
        AssertByteSequence(mip0Layer1.Bytes, 32);

        const UpdateCall &mip1Layer0 = device.LastTexture->Updates[2];
        assert(mip1Layer0.RowPitch == 8);
        assert(mip1Layer0.SlicePitch == 8);
        assert(mip1Layer0.MipLevel == 1);
        assert(mip1Layer0.ArrayIndex == 0);
        AssertByteSequence(mip1Layer0.Bytes, 64);

        const UpdateCall &mip1Layer1 = device.LastTexture->Updates[3];
        assert(mip1Layer1.RowPitch == 8);
        assert(mip1Layer1.SlicePitch == 8);
        assert(mip1Layer1.MipLevel == 1);
        assert(mip1Layer1.ArrayIndex == 1);
        AssertByteSequence(mip1Layer1.Bytes, 72);

        const UpdateCall &mip2Layer0 = device.LastTexture->Updates[4];
        assert(mip2Layer0.RowPitch == 4);
        assert(mip2Layer0.SlicePitch == 4);
        assert(mip2Layer0.MipLevel == 2);
        assert(mip2Layer0.ArrayIndex == 0);
        AssertByteSequence(mip2Layer0.Bytes, 80);

        const UpdateCall &mip2Layer1 = device.LastTexture->Updates[5];
        assert(mip2Layer1.RowPitch == 4);
        assert(mip2Layer1.SlicePitch == 4);
        assert(mip2Layer1.MipLevel == 2);
        assert(mip2Layer1.ArrayIndex == 1);
        AssertByteSequence(mip2Layer1.Bytes, 84);
    }

    {
        FakeDevice device;
        const CookedTextureData texture =
            BuildTexture(1, 1, 1, CookedTexturePixelFormat::R8UNorm, CookedTextureColorSpace::Linear);
        const CookedTextureUploadResult result =
            CreateAndUploadCookedTexture(&device, texture, "r8_linear");

        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::R8_UNORM);
        assert(result.CreateInfo.Type == TextureType::Texture2D);
        assert(device.CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::R8_UNORM);
        assert(device.LastTexture->Updates.size() == 1);
        assert(device.LastTexture->Updates[0].RowPitch == 1);
        assert(device.LastTexture->Updates[0].SlicePitch == 1);
    }

    {
        FakeDevice device;
        const CookedTextureData texture =
            BuildTexture(2, 1, 1, CookedTexturePixelFormat::RG8UNorm, CookedTextureColorSpace::Linear);
        const CookedTextureUploadResult result =
            CreateAndUploadCookedTexture(&device, texture, "rg8_linear");

        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::RG8_UNORM);
        assert(device.CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::R8G8_UNORM);
        assert(device.LastTexture->Updates.size() == 2);
        assert(device.LastTexture->Updates[0].RowPitch == 4);
        assert(device.LastTexture->Updates[0].SlicePitch == 4);
    }

    {
        FakeDevice device;
        const CookedTextureData texture =
            BuildTexture(1, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);
        const CookedTextureUploadResult result =
            CreateAndUploadCookedTexture(&device, texture, "rgba8_linear");

        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::RGBA8_UNORM);
        assert(device.CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::R8G8B8A8_UNORM);
        assert(device.LastTexture->Updates.size() == 1);
        assert(device.LastTexture->Updates[0].RowPitch == 4);
        assert(device.LastTexture->Updates[0].SlicePitch == 4);
    }

    {
        TextureCreateInfo createInfo;
        CookedTextureData texture =
            BuildTexture(1, 1, 1, CookedTexturePixelFormat::RG8UNorm, CookedTextureColorSpace::Linear);
        texture.ColorSpace = CookedTextureColorSpace::SRGB;
        assert(BuildCookedTextureCreateInfo(texture, "bad_srgb", createInfo) ==
               CookedTextureUploadStatus::UnsupportedFormat);
    }

    {
        FakeDevice device;
        CookedTextureData texture =
            BuildTexture(2, 2, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);
        texture.Mips.pop_back();

        const CookedTextureUploadResult result =
            CreateAndUploadCookedTexture(&device, texture, "missing_mip");
        assert(result.Status == CookedTextureUploadStatus::InvalidMipData);
        assert(!result.Texture);
        assert(device.CreatedTextureDescs.empty());
    }

    {
        const CookedTextureData texture =
            BuildTexture(1, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);
        const CookedTextureUploadResult result =
            CreateAndUploadCookedTexture(nullptr, texture, "null_device");
        assert(result.Status == CookedTextureUploadStatus::InvalidDevice);
    }

    {
        FakeDevice device;
        device.bFailTextureCreation = true;
        const CookedTextureData texture =
            BuildTexture(1, 1, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);
        const CookedTextureUploadResult result =
            CreateAndUploadCookedTexture(&device, texture, "create_fail");
        assert(result.Status == CookedTextureUploadStatus::TextureCreationFailed);
        assert(!result.Texture);
        assert(device.CreatedTextureDescs.size() == 1);
    }

    // v0.1 のクック済み BC7: ミップごとにブロック単位のピッチで全ミップを上げる（端のミップは最小 1 ブロック）
    {
        FakeDevice device;
        const CookedTextureData texture = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7,
                                                       CookedTextureColorSpace::SRGB, VersionMinorBlockCompressed);
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "bc7_srgb");

        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::BC7_SRGB);
        assert(result.CreateInfo.MipLevels == 4);
        assert(result.CreateInfo.Type == TextureType::Texture2D);
        assert(device.CreatedTextureDescs.size() == 1);
        const NorvesLib::RHI::TextureDesc &desc = device.CreatedTextureDescs[0];
        assert(desc.TextureFormat == NorvesLib::RHI::Format::BC7_SRGB);
        assert(desc.Width == 8 && desc.Height == 8);
        assert(desc.MipLevels == 4);
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::ShaderRead));
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::TransferDst));

        assert(result.UploadedBytes == 64 + 16 + 16 + 16);
        assert(device.LastTexture->Updates.size() == 4);
        const uint32_t expectedRowPitch[] = {32, 16, 16, 16};
        const uint32_t expectedSlicePitch[] = {64, 16, 16, 16};
        const uint8_t expectedStart[] = {0, 64, 80, 96};
        for (uint32_t mip = 0; mip < 4; ++mip)
        {
            const UpdateCall &call = device.LastTexture->Updates[mip];
            assert(call.MipLevel == mip);
            assert(call.ArrayIndex == 0);
            assert(call.RowPitch == expectedRowPitch[mip]);
            assert(call.SlicePitch == expectedSlicePitch[mip]);
            assert(call.Bytes.size() == expectedSlicePitch[mip]);
            AssertByteSequence(call.Bytes, expectedStart[mip]);
        }
    }

    // 4 の倍数でない大きさ・BC1/BC4/BC5・BC7 の配列
    {
        FakeDevice device;
        const CookedTextureData texture = BuildTexture(6, 5, 1, CookedTexturePixelFormat::BC1,
                                                       CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "bc1_odd");
        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::BC1_UNORM);
        assert(device.CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::BC1_UNORM);
        assert(device.LastTexture->Updates.size() == 3);
        assert(device.LastTexture->Updates[0].RowPitch == 16);   // 6 画素 = 2 ブロック x 8 バイト
        assert(device.LastTexture->Updates[0].SlicePitch == 32); // 5 画素 = 2 ブロック行
        assert(device.LastTexture->Updates[1].RowPitch == 8);
        assert(device.LastTexture->Updates[1].SlicePitch == 8);
        assert(device.LastTexture->Updates[2].RowPitch == 8);
        assert(device.LastTexture->Updates[2].SlicePitch == 8);
        assert(result.UploadedBytes == 48);
    }

    {
        FakeDevice device;
        const CookedTextureData texture = BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC4,
                                                       CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "bc4");
        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::BC4_UNORM);
        assert(device.CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::BC4_UNORM);
        assert(device.LastTexture->Updates[0].RowPitch == 8);
    }

    {
        FakeDevice device;
        const CookedTextureData texture = BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC5,
                                                       CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "bc5");
        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::BC5_UNORM);
        assert(device.CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::BC5_UNORM);
        assert(device.LastTexture->Updates[0].RowPitch == 16);
    }

    {
        FakeDevice device;
        const CookedTextureData texture = BuildTexture(4, 4, 2, CookedTexturePixelFormat::BC7,
                                                       CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "bc7_array");
        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::BC7_UNORM);
        assert(result.CreateInfo.Type == TextureType::Texture2DArray);
        assert(device.LastTexture->Updates.size() == 6);  // 3 ミップ x 2 レイヤー
        assert(device.LastTexture->Updates[0].SlicePitch == 16);
        assert(device.LastTexture->Updates[1].ArrayIndex == 1);
        AssertByteSequence(device.LastTexture->Updates[1].Bytes, 16);
        assert(result.UploadedBytes == 96);
    }

    {
        FakeDevice device;
        const CookedTextureData texture = BuildTexture(3, 2, 1, CookedTexturePixelFormat::R16UNorm,
                                                       CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "r16");
        assert(result.Succeeded());
        assert(result.CreateInfo.PixelFormat == TextureCreateInfo::Format::R16_UNORM);
        assert(device.CreatedTextureDescs[0].TextureFormat == NorvesLib::RHI::Format::R16_UNORM);
        assert(device.LastTexture->Updates.size() == 2);
        assert(device.LastTexture->Updates[0].RowPitch == 6);
        assert(device.LastTexture->Updates[0].SlicePitch == 12);
    }

    // BC・R16 の色空間の組み合わせ違いは写像で拒否する（デバイスは作らない）
    {
        TextureCreateInfo createInfo;
        CookedTextureData bc4 = BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC4,
                                             CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        bc4.ColorSpace = CookedTextureColorSpace::SRGB;
        assert(BuildCookedTextureCreateInfo(bc4, "bad_bc4", createInfo) == CookedTextureUploadStatus::UnsupportedFormat);

        CookedTextureData r16 = BuildTexture(2, 2, 1, CookedTexturePixelFormat::R16UNorm,
                                             CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        r16.ColorSpace = CookedTextureColorSpace::SRGB;
        assert(BuildCookedTextureCreateInfo(r16, "bad_r16", createInfo) == CookedTextureUploadStatus::UnsupportedFormat);
    }

    // ブロック数が足りないミップ、BC に対応しないデバイスは、失敗として返す
    {
        FakeDevice device;
        CookedTextureData texture = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7,
                                                 CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        texture.Mips.pop_back();
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "bc7_missing_mip");
        assert(result.Status == CookedTextureUploadStatus::InvalidMipData);
        assert(device.CreatedTextureDescs.empty());
    }

    {
        FakeDevice device;
        device.bFailTextureCreation = true;
        const CookedTextureData texture = BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC7,
                                                       CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        const CookedTextureUploadResult result = CreateAndUploadCookedTexture(&device, texture, "bc7_no_bc_device");
        assert(result.Status == CookedTextureUploadStatus::TextureCreationFailed);
        assert(!result.Texture);
    }

    std::cout << "CookedTextureUploadTest passed\n";
    return 0;
}
