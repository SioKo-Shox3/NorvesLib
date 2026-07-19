#include "Asset/AssetFileReader.h"
#include "Object/ObjectCast.h"
#include "Rendering/RenderResources.h"
#include "Resource/FontAtlas.h"
#include "RHI/IDevice.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;

namespace
{
    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        FakeTexture(const NorvesLib::RHI::TextureDesc& desc, uint32_t* pDestructionCount)
            : Desc(desc)
            , m_pDestructionCount(pDestructionCount)
        {
        }

        ~FakeTexture() override
        {
            ++*m_pDestructionCount;
        }

        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }
        void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}

        NorvesLib::RHI::TextureDesc Desc;

    private:
        uint32_t* m_pDestructionCount = nullptr;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc& desc) override
        {
            return Container::MakeShared<FakeTexture>(desc, &DestroyedTextureCount);
        }

        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc&) override { return {}; }
        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc&) override { return {}; }
        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc&) override { return {}; }
        NorvesLib::RHI::CommandListPtr CreateCommandList() override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc&) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc&) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc&) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc&) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc&) override { return {}; }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc&) override { return {}; }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        NorvesLib::RHI::API GetAPI() const override { return NorvesLib::RHI::API::None; }
        const NorvesLib::RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(const NorvesLib::Math::Matrix4x4& projection,
                                                                  bool = true) const override
        {
            return projection;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        uint32_t DestroyedTextureCount = 0;
    };

    uint64_t HashPixels(const Container::VariableArray<uint8_t>& pixels)
    {
        uint64_t hash = 1469598103934665603ull;
        for (uint8_t pixel : pixels)
        {
            hash ^= pixel;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    struct AtlasState
    {
        TextureHandle Handle;
        uint32_t Width = 0;
        uint32_t Height = 0;
        float LineAdvance = 0.0f;
        FontAtlasGlyph Glyph;
        size_t PixelCount = 0;
        uint64_t PixelHash = 0;
    };

    AtlasState CaptureAtlasState(const FontAtlas& atlas)
    {
        AtlasState state;
        state.Handle = atlas.GetTextureHandle();
        state.Width = atlas.GetWidth();
        state.Height = atlas.GetHeight();
        state.LineAdvance = atlas.GetLineAdvancePx();
        state.Glyph = atlas.GetGlyph('A');
        state.PixelCount = atlas.GetPixels().size();
        state.PixelHash = HashPixels(atlas.GetPixels());
        return state;
    }

    void AssertAtlasStateUnchanged(const FontAtlas& atlas, const AtlasState& state)
    {
        const FontAtlasGlyph& glyph = atlas.GetGlyph('A');
        assert(atlas.GetTextureHandle() == state.Handle);
        assert(atlas.GetWidth() == state.Width);
        assert(atlas.GetHeight() == state.Height);
        assert(atlas.GetLineAdvancePx() == state.LineAdvance);
        assert(glyph.SizePx == state.Glyph.SizePx);
        assert(glyph.BearingPx == state.Glyph.BearingPx);
        assert(glyph.AdvancePx == state.Glyph.AdvancePx);
        assert(glyph.UVRect == state.Glyph.UVRect);
        assert(glyph.bValid == state.Glyph.bValid);
        assert(atlas.GetPixels().size() == state.PixelCount);
        assert(HashPixels(atlas.GetPixels()) == state.PixelHash);
    }

    void TestFontAtlasBuildAndLifetime()
    {
        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        Asset::AssetFileReader reader;
        FontAtlas atlas;
        Resource resource;
        assert(CastTo<FontAtlas>(&resource) == nullptr);
        assert(atlas.Build(FontAtlasDesc{}, reader, resources.Textures()));
        assert(atlas.IsValid());
        assert(atlas.GetWidth() > 0 && atlas.GetHeight() > 0 && !atlas.GetPixels().empty());
        assert(atlas.GetLineAdvancePx() > 0.0f);

        bool bHasCoverage = false;
        for (size_t index = 3; index < atlas.GetPixels().size(); index += 4)
        {
            if (atlas.GetPixels()[index] != 0)
            {
                bHasCoverage = true;
                break;
            }
        }
        assert(bHasCoverage);

        const FontAtlasGlyph& a = atlas.GetGlyph('A');
        const FontAtlasGlyph& question = atlas.GetGlyph('?');
        assert(a.bValid && question.bValid && atlas.GetGlyph(0x2603).UVRect == question.UVRect);
        assert(a.UVRect.x > 0.0f && a.UVRect.y > 0.0f && a.UVRect.z > 0.0f && a.UVRect.w > 0.0f);
        assert(a.UVRect.z == a.SizePx.x / static_cast<float>(atlas.GetWidth()));
        assert(a.UVRect.w == a.SizePx.y / static_cast<float>(atlas.GetHeight()));

        const AtlasState builtState = CaptureAtlasState(atlas);
        Resource* pResource = &atlas;
        assert(!pResource->Load());
        pResource->Unload();
        AssertAtlasStateUnchanged(atlas, builtState);
        assert(!atlas.Build(FontAtlasDesc{}, reader, resources.Textures()));
        AssertAtlasStateUnchanged(atlas, builtState);

        assert(device->DestroyedTextureCount == 0);
        atlas.Shutdown(resources.Textures());
        assert(!atlas.IsValid() && !atlas.GetTextureHandle().IsValid());
        assert(device->DestroyedTextureCount == 1);
        atlas.Shutdown(resources.Textures());
        assert(device->DestroyedTextureCount == 1);

        FontAtlas failedAtlas;
        FontAtlasDesc missing;
        missing.FontLogicalPath = "Fonts/missing.ttf";
        assert(!failedAtlas.Build(missing, reader, resources.Textures()));
        assert(!failedAtlas.Build(FontAtlasDesc{}, reader, resources.Textures()));
        resources.Shutdown();
    }
}

int main()
{
    TestFontAtlasBuildAndLifetime();
    std::cout << "FontAtlasTest passed\n";
    return 0;
}
