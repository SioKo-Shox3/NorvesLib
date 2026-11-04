#include "Particle/ParticleSystem.h"
#include "Particle/VfxSchema.h"
#include "Rendering/CanvasView.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneView.h"

#if NORVES_ENABLE_CORE_TEXT
#include "Asset/AssetFileReader.h"
#include "Component/TextComponent.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"
#include "Resource/FontAtlas.h"
#include "RHI/IDevice.h"
#endif

#include <cassert>
#include <iostream>

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

    void TestTransientBoardDistribution(ParticleSystem& system, CanvasView& canvas, uint64_t screenObjectId)
    {
        Container::String error;
        VfxEmitterAsset emitter = MakeVfxDefaultEmitter("WorldParticle");
        assert(SetVfxEmitterProperty(emitter, "Space",
            VfxValue::MakeEnum(static_cast<uint32_t>(VfxSimulationSpace::World)), &error));
        VfxModule spawn = MakeVfxDefaultModule(VfxModuleType::SpawnOnEvent);
        assert(SetVfxModuleParameter(spawn, "EventName", VfxValue::MakeName(HitEffectEmitter::HitEventName), &error));
        assert(SetVfxModuleParameter(spawn, "Count", VfxValue::MakeUInt(1u), &error));
        VfxModule size = MakeVfxDefaultModule(VfxModuleType::InitSize);
        assert(SetVfxModuleParameter(size, "Size", VfxValue::MakeVector2(Math::Vector2(1.0f, 1.0f)), &error));
        emitter.Modules.push_back(spawn);
        emitter.Modules.push_back(size);
        emitter.Modules.push_back(MakeVfxDefaultModule(VfxModuleType::SpriteRenderer));
        VfxEffectAsset effect;
        effect.Name = "WorldParticle";
        effect.Emitters.push_back(emitter);
        VfxEffectSystem& effects = system.GetVfxEffects();
        const VfxEffectHandle effectHandle = effects.CreateInstance(effect, VfxInstanceDesc{}, &error);
        assert(effectHandle.IsValid());
        assert(effects.RaiseEvent(effectHandle, HitEffectEmitter::HitEventName, Math::Vector3(0.0f, 0.0f, -2.0f), 0.0));
        system.Tick(0.1f);

        VariableArray<BoardProxy> frameProxies;
        system.AppendBoardProxies(frameProxies);
        assert(frameProxies.size() == 2u);
        const BoardProxy* worldBoard = nullptr;
        const BoardProxy* screenBoard = nullptr;
        for (const BoardProxy& proxy : frameProxies)
        {
            if (proxy.Space == BoardSpace::WorldSpace)
            {
                assert(worldBoard == nullptr);
                worldBoard = &proxy;
            }
            else if (proxy.Space == BoardSpace::ScreenSpace)
            {
                assert(screenBoard == nullptr);
                screenBoard = &proxy;
            }
        }
        assert(worldBoard != nullptr && screenBoard != nullptr);
        assert(worldBoard->WorldBounds.Radius == 0.0f);
        assert(screenBoard->ObjectId == screenObjectId);
        const uint64_t worldObjectId = worldBoard->ObjectId;
        assert(worldObjectId != screenObjectId && worldObjectId != 100u && screenObjectId != 100u);

        SceneView sceneView;
        SceneViewSettings sceneSettings;
        sceneSettings.bEnableFrustumCulling = true;
        sceneSettings.bEnableInstancing = false;
        assert(sceneView.Initialize(sceneSettings));
        ViewportSnapshot viewport = MakePlan(640u);
        viewport.Camera.CullingMask = RenderLayer::All;
        viewport.Camera.AspectRatio = 640.0f / 480.0f;
        viewport.Camera.Viewport.Width = 640.0f;
        viewport.Camera.Viewport.Height = 480.0f;

        const auto countObject = [](const auto& commands, uint64_t objectId)
        {
            uint32_t count = 0u;
            for (const DrawCommand& command : commands)
            {
                if (command.Draw.ObjectId == objectId)
                {
                    ++count;
                }
            }
            return count;
        };

        DistributeTransientBoardProxies(frameProxies, &sceneView, &canvas);
        assert(sceneView.GetTransientBoardProxies().size() == 1u);
        assert(sceneView.GetTransientBoardProxies()[0].ObjectId == worldObjectId);
        assert(canvas.GetTransientBoardProxies().size() == 1u);
        assert(canvas.GetTransientBoardProxies()[0].ObjectId == screenObjectId);
        sceneView.PrepareDrawCommandsForViewport(viewport);
        canvas.PrepareBoardDrawCommands(viewport, 0u);
        assert(sceneView.GetTransparentCommands().size() == 1u);
        assert(countObject(sceneView.GetTransparentCommands(), worldObjectId) == 1u);
        assert(countObject(sceneView.GetTransparentCommands(), screenObjectId) == 0u);
        assert(canvas.GetBoardDrawCommands().size() == 2u);
        assert(countObject(canvas.GetBoardDrawCommands(), worldObjectId) == 0u);
        assert(countObject(canvas.GetBoardDrawCommands(), screenObjectId) == 1u);
        assert(countObject(canvas.GetBoardDrawCommands(), 100u) == 1u);

        frameProxies.clear();
        DistributeTransientBoardProxies(frameProxies, &sceneView, &canvas);
        sceneView.PrepareDrawCommandsForViewport(viewport);
        canvas.PrepareBoardDrawCommands(viewport, 0u);
        assert(sceneView.GetTransientBoardProxies().empty());
        assert(canvas.GetTransientBoardProxies().empty());
        assert(countObject(sceneView.GetTransparentCommands(), worldObjectId) == 0u);
        assert(countObject(sceneView.GetTransparentCommands(), screenObjectId) == 0u);
        assert(canvas.GetBoardDrawCommands().size() == 1u);
        assert(countObject(canvas.GetBoardDrawCommands(), worldObjectId) == 0u);
        assert(countObject(canvas.GetBoardDrawCommands(), screenObjectId) == 0u);
        assert(countObject(canvas.GetBoardDrawCommands(), 100u) == 1u);
        sceneView.Shutdown();
        std::cout << "一時の粒子の板の振り分けと置き換えを確認\n";
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
    TestTransientBoardDistribution(system, canvas, pausedFrame[0].ObjectId);
#if NORVES_ENABLE_CORE_TEXT
    world.Finalize();
    atlas.Shutdown(resources.Textures());
    resources.Shutdown();
#endif
    canvas.Shutdown();

    std::cout << "ParticleFrameSnapshotTest passed\\n";
    return 0;
}
