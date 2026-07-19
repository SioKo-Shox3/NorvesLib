#include "Particle/ParticleSystem.h"
#include "Rendering/CanvasView.h"
#include "Rendering/FramePacket.h"

#if NORVES_ENABLE_CORE_TEXT
#include "Asset/AssetFileReader.h"
#include "Component/TextComponent.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"
#include "Resource/FontAtlas.h"
#include "RHI/IDevice.h"
#endif

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Particle;
using namespace NorvesLib::Core::Rendering;

namespace
{
#if NORVES_ENABLE_CORE_TEXT
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
        void Update(const void*, uint32_t, uint32_t, uint32_t = 0u, uint32_t = 0u) override {}

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
#endif
    BoardProxy MakeProxy(uint64_t objectId, uint64_t componentId)
    {
        BoardProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.SortKey = 0u;
        proxy.Space = BoardSpace::ScreenSpace;
        proxy.LayerMask = RenderLayer::UI;
        return proxy;
    }

    ViewportRenderPlan MakePlan(uint32_t width)
    {
        ViewportRenderPlan plan;
        plan.bEnabled = true;
        plan.bHasCamera = true;
        plan.RenderWidth = width;
        plan.RenderHeight = 480u;
        plan.PixelRect.Width = static_cast<float>(width);
        plan.PixelRect.Height = 480.0f;
        plan.Camera.CullingMask = RenderLayer::UI;
        return plan;
    }

    std::string ReadSource()
    {
        const std::string path = std::string(NORVES_SOURCE_ROOT) + "/Library/Core/Private/Engine/ApplicationProcessor.cpp";
        std::ifstream input(path);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    size_t FindRequired(const std::string& source, const char* text)
    {
        const size_t position = source.find(text);
        assert(position != std::string::npos);
        return position;
    }

    size_t FindMatchingBrace(const std::string& source, size_t openBrace)
    {
        uint32_t depth = 0u;
        for (size_t index = openBrace; index < source.size(); ++index)
        {
            if (source[index] == '{')
            {
                ++depth;
            }
            else if (source[index] == '}')
            {
                --depth;
                if (depth == 0u)
                {
                    return index;
                }
            }
        }
        assert(false);
        return std::string::npos;
    }

    uint32_t CountOccurrences(const std::string& source, const char* text)
    {
        uint32_t count = 0u;
        size_t position = 0u;
        while ((position = source.find(text, position)) != std::string::npos)
        {
            ++count;
            position += std::strlen(text);
        }
        return count;
    }
}

int main()
{
    ParticleSystem system;
    ParticleEmitterDesc desc;
    desc.SpawnRate = 1.0f;
    desc.Lifetime = 10.0f;
    const ParticleEmitterHandle handle = system.CreateEmitter(desc);
    assert(handle.IsValid());
    system.Tick(1.0f);

    VariableArray<BoardProxy> pausedFrame;
    system.AppendBoardProxies(pausedFrame);
    assert(pausedFrame.size() == 1u);
    VariableArray<BoardProxy> nextPausedFrame;
    system.AppendBoardProxies(nextPausedFrame);
    assert(nextPausedFrame.size() == pausedFrame.size());
    assert(nextPausedFrame[0].WorldTransform.GetTranslationRow() == pausedFrame[0].WorldTransform.GetTranslationRow());

    CanvasView canvas;
    ViewSettings settings;
    settings.Width = 640u;
    settings.Height = 480u;
    assert(canvas.Initialize(settings));
    canvas.SetBoardInstanceBatchingEnabled(false);
    canvas.UpdateBoardProxy(10u, MakeProxy(100u, 10u));
    VariableArray<BoardProxy> frameTransient;
#if NORVES_ENABLE_CORE_TEXT
    auto device = Container::MakeShared<FakeDevice>();
    RenderResources resources;
    assert(resources.Initialize(device));
    Asset::AssetFileReader reader;
    FontAtlas atlas;
    assert(atlas.Build(FontAtlasDesc{}, reader, resources.Textures()));
    World world;
    world.Initialize();
    world.SetScreenSpaceBoardSink(&canvas);
    Entity* entity = world.SpawnObject<Entity>();
    Component::TextComponent* text = world.CreateComponent<Component::TextComponent>(entity);
    text->SetText("A");
    text->SetFontAtlas(&atlas);
    world.SyncToSceneView();
    world.CollectTransientBoardProxies(frameTransient);
    assert(frameTransient.size() == 1u);
    const uint64_t textObjectId = frameTransient[0].ObjectId;
    canvas.UpdateBoardProxy(10u, MakeProxy(100u, 10u));
#else
    frameTransient.push_back(MakeProxy(200u, 20u));
    const uint64_t textObjectId = 200u;
#endif
    system.AppendBoardProxies(frameTransient);
    canvas.SetTransientBoardProxies(frameTransient);
    ViewportRenderPlan plan = MakePlan(640u);
    canvas.PrepareBoardDrawCommands(plan, 0u);
    assert(canvas.GetBoardDrawCommands().size() == 3u);
    assert(canvas.GetBoardDrawCommands()[0].Draw.ObjectId == 100u);
    assert(canvas.GetBoardDrawCommands()[1].Draw.ObjectId == textObjectId);
    assert(canvas.GetBoardDrawCommands()[2].Draw.ObjectId == pausedFrame[0].ObjectId);

    FramePacket packet;
    packet.DrawCommands = canvas.GetBoardDrawCommands();
    packet.InstanceData = canvas.GetBoardInstanceData();
    frameTransient.clear();
    canvas.SetTransientBoardProxies(frameTransient);
    plan = MakePlan(320u);
    canvas.PrepareBoardDrawCommands(plan, 0u);
    assert(frameTransient.empty());
    assert(canvas.GetTransientBoardProxies().empty());
    assert(canvas.GetBoardDrawCommands().size() == 1u);
    assert(canvas.GetBoardDrawCommands()[0].Draw.ObjectId == 100u);
    assert(packet.DrawCommands.size() == 3u);
    assert(packet.DrawCommands[1].Draw.ObjectId == textObjectId);
    assert(packet.InstanceData.size() == 3u);
    assert(packet.InstanceData[0].CustomData[0] == 640.0f);
#if NORVES_ENABLE_CORE_TEXT
    world.Finalize();
    atlas.Shutdown(resources.Textures());
    resources.Shutdown();
#endif
    canvas.Shutdown();

    const std::string source = ReadSource();
    const size_t worldTick = FindRequired(source, "GEngine->GetWorld().Tick(deltaTime);");
    const size_t particleTick = FindRequired(source, "GEngine->GetParticleSystem().Tick(deltaTime);");
    const size_t sync = FindRequired(source, "GEngine->GetWorld().SyncToSceneView(");
    const size_t collect = FindRequired(source, "GEngine->GetWorld().CollectTransientBoardProxies(frameTransient);");
    const size_t append = FindRequired(source, "GEngine->GetParticleSystem().AppendBoardProxies(frameTransient);");
    const size_t canvasSet = FindRequired(source, "canvasView->SetTransientBoardProxies(frameTransient);");
    const size_t sceneQuery = FindRequired(source, "GEngine->GetSceneQuery().Rebuild(GEngine->GetWorld());");
    const size_t guard = source.rfind("if (bAdvanceSim)", worldTick);
    assert(guard != std::string::npos);
    const size_t guardOpenBrace = source.find('{', guard);
    const size_t guardCloseBrace = FindMatchingBrace(source, guardOpenBrace);
    assert(guardOpenBrace < worldTick && worldTick < guardCloseBrace);
    assert(guardOpenBrace < particleTick && particleTick < guardCloseBrace);
    assert(guardCloseBrace < sync);
    assert(sync < collect);
    assert(collect < append);
    assert(append < canvasSet);
    assert(canvasSet < sceneQuery);
    assert(CountOccurrences(source, "SetTransientBoardProxies(frameTransient)") == 1u);

    std::cout << "ParticleFrameSnapshotTest passed\\n";
    return 0;
}
