#include "Asset/AssetFileReader.h"
#include "Component/TextComponent.h"
#include "Object/ObjectCast.h"
#include "Object/World.h"
#include "Rendering/CanvasView.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include "Resource/FontAtlas.h"
#include "RHI/IDevice.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc& desc)
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
        void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}

        NorvesLib::RHI::TextureDesc Desc;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc& desc) override
        {
            return Container::MakeShared<FakeTexture>(desc);
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
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(const NorvesLib::Math::Matrix4x4& value,
                                                                  bool = true) const override
        {
            return value;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
    };
}

int main()
{
    auto device = Container::MakeShared<FakeDevice>();
    RenderResources resources;
    assert(resources.Initialize(device));
    Asset::AssetFileReader reader;
    FontAtlas atlas;
    assert(atlas.Build(FontAtlasDesc{}, reader, resources.Textures()));

    BoardComponent board;
    assert(CastTo<TextComponent>(&board) == nullptr);
    World world;
    world.Initialize();
    Entity* entity = world.SpawnObject<Entity>();
    TextComponent* text = world.CreateComponent<TextComponent>(entity);
    Container::VariableArray<BoardProxy> proxies;

    text->SetText("A");
    text->BuildGlyphBoardProxies(proxies);
    assert(proxies.empty());
    text->SetFontAtlas(&atlas);
    text->SetText("");
    text->BuildGlyphBoardProxies(proxies);
    assert(proxies.empty());

    constexpr float LetterSpacing = 3.0f;
    text->SetLetterSpacing(LetterSpacing);
    text->SetText("A g?\x7F\nB");
    text->SetTint(NorvesLib::Math::Vector4(0.25f, 0.5f, 0.75f, 1.0f));
    text->SetBoardSpace(BoardSpace::WorldSpace);
    world.UpdateWorldTransforms();
    text->RefreshRenderTransformCache();
    text->BuildGlyphBoardProxies(proxies);

    const FontAtlasGlyph& glyphA = atlas.GetGlyph('A');
    const FontAtlasGlyph& glyphG = atlas.GetGlyph('g');
    const FontAtlasGlyph& glyphSpace = atlas.GetGlyph(' ');
    const FontAtlasGlyph& glyphB = atlas.GetGlyph('B');
    assert(proxies.size() == 5);
    assert(proxies[0].Texture == atlas.GetTextureHandle());
    assert(proxies[0].UVRect == glyphA.UVRect);
    assert(proxies[2].UVRect == atlas.GetGlyph('?').UVRect);
    assert(proxies[3].UVRect == atlas.GetGlyph('?').UVRect);
    assert(proxies[4].Space == BoardSpace::ScreenSpace);
    assert(proxies[0].Tint == NorvesLib::Math::Vector4(0.25f, 0.5f, 0.75f, 1.0f));

    const NorvesLib::Math::Vector3 aPosition = proxies[0].WorldTransform.GetTranslationRow();
    const NorvesLib::Math::Vector3 gPosition = proxies[1].WorldTransform.GetTranslationRow();
    const NorvesLib::Math::Vector3 bPosition = proxies[4].WorldTransform.GetTranslationRow();
    assert(gPosition.y > aPosition.y);
    assert(aPosition.y == atlas.GetLineAdvancePx() - glyphA.BearingPx.y);
    assert(gPosition.y == atlas.GetLineAdvancePx() - glyphG.BearingPx.y);
    assert(gPosition.x == glyphA.AdvancePx + LetterSpacing + glyphSpace.AdvancePx + LetterSpacing + glyphG.BearingPx.x);
    assert(bPosition.y == atlas.GetLineAdvancePx() * 2.0f - glyphB.BearingPx.y);

    SceneView sceneView;
    SceneViewSettings sceneSettings;
    sceneSettings.bEnableFrustumCulling = false;
    sceneSettings.bEnableDistanceCulling = false;
    assert(sceneView.Initialize(sceneSettings));
    CanvasView canvas;
    ViewSettings canvasSettings;
    canvasSettings.Width = 640;
    canvasSettings.Height = 480;
    assert(canvas.Initialize(canvasSettings));
    world.SetSceneView(&sceneView);
    world.SetScreenSpaceBoardSink(&canvas);
    world.SyncToSceneView();
    assert(sceneView.GetBoardProxies().empty());
    assert(canvas.GetBoardProxies().empty());
    assert(canvas.GetTransientBoardProxies().size() == 5);

    text->SetVisible(false);
    world.SyncToSceneView();
    assert(canvas.GetTransientBoardProxies().empty());
    text->SetVisible(true);
    text->SetFontAtlas(nullptr);
    world.SyncToSceneView();
    assert(canvas.GetTransientBoardProxies().empty());
    text->SetFontAtlas(&atlas);
    text->SetText("");
    world.SyncToSceneView();
    assert(canvas.GetTransientBoardProxies().empty());

    canvas.Shutdown();
    sceneView.Shutdown();
    atlas.Shutdown(resources.Textures());
    resources.Shutdown();
    world.Finalize();
    std::cout << "TextComponentBoardProxyTest passed\n";
    return 0;
}
