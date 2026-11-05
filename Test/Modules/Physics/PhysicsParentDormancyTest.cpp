// 親の休眠と復帰が、子の物理・Tick・描画・シーンクエリへ伝わる契約を検証する。

#include "Animation/SkeletalAssetResource.h"
#include "Component/BillboardComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/SpotLightComponent.h"
#include "Component/TextComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "PhysicsModuleTestAccess.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include "Resource/FontAtlas.h"
#include "RHI/IDevice.h"
#include "Scene/SceneQuery.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <utility>
#include <Windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Modules::Physics;

namespace
{
    constexpr float FixedDeltaTime = 1.0f / 60.0f;
    using ProbeBase = Component::Component;

    class ParentDormancyTickProbe final : public ProbeBase
    {
        REFLECTION_CLASS(ParentDormancyTickProbe, ProbeBase)

    public:
        uint32_t Count = 0;

        static void RunTick(ProbeBase& component, float)
        {
            ++static_cast<ParentDormancyTickProbe&>(component).Count;
        }
    };

    IMPLEMENT_CLASS(ParentDormancyTickProbe, ProbeBase)

    // フォントのテクスチャは CPU の記述だけを保持し、GPU を使わない。
    class FakeTexture final : public RHI::ITexture
    {
    public:
        explicit FakeTexture(const RHI::TextureDesc& desc) : m_Desc(desc) {}
        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }
        bool IsCubemap() const override { return m_Desc.IsCubemap; }
        void Update(const void*, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}

    private:
        RHI::TextureDesc m_Desc;
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            return Container::MakeShared<FakeTexture>(desc);
        }
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc&) override { return {}; }
        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc&) override { return {}; }
        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc&) override { return {}; }
        RHI::CommandListPtr CreateCommandList() override { return {}; }
        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc&) override { return {}; }
        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc&) override { return {}; }
        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc&) override { return {}; }
        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc&) override { return {}; }
        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc&) override { return {}; }
        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc&) override { return {}; }
        RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        RHI::API GetAPI() const override { return RHI::API::None; }
        const RHI::DeviceCapabilities& GetCapabilities() const override { return m_Capabilities; }
        Math::Matrix4x4 AdjustProjectionForClipSpace(const Math::Matrix4x4& value, bool = true) const override
        {
            return value;
        }

    private:
        RHI::DeviceCapabilities m_Capabilities;
    };

    // 最小の骨・三角形・静止クリップから、GPU を必要としない描画用アセットを作る。
    Container::TSharedPtr<SkeletalAssetResource> MakeSkeletalAsset(ResourceRegistry& resources)
    {
        auto mesh = resources.CreateTransient<SkinnedMeshResource>("ParentDormancy.Mesh");
        auto skeleton = resources.CreateTransient<SkeletonResource>("ParentDormancy.Skeleton");
        auto clip = resources.CreateTransient<AnimationClipResource>("ParentDormancy.Clip");
        auto asset = resources.CreateTransient<SkeletalAssetResource>("ParentDormancy.Asset");
        assert(mesh && skeleton && clip && asset);

        Container::VariableArray<Skeletal::SkeletalVertex> vertices(3);
        vertices[1].Position.X = 1.0f;
        vertices[2].Position.Y = 1.0f;
        for (auto& vertex : vertices)
        {
            vertex.Normal.Z = 1.0f;
            vertex.JointWeights[0] = 1.0f;
        }
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(Container::VariableArray<uint32_t>{0, 1, 2});
        assert(mesh->Load());

        Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        joints[0].ParentIndex = -1;
        for (uint32_t index = 0; index < 16; index += 5)
        {
            joints[0].InverseBindMatrix[index] = 1.0f;
        }
        skeleton->SetJoints(std::move(joints));
        assert(skeleton->Load());
        Skeletal::SkeletalAnimationClip animation;
        animation.Name = "Idle";
        animation.DurationSeconds = 1.0f;
        clip->SetClip(std::move(animation));
        assert(clip->Load());
        asset->SetResources(mesh, skeleton, clip);
        assert(asset->Load());
        return asset;
    }

    template <typename T>
    size_t CountComponentProxies(const Container::VariableArray<T>& proxies, uint64_t componentId)
    {
        size_t count = 0;
        for (const auto& proxy : proxies)
        {
            count += proxy.ComponentId == componentId ? 1u : 0u;
        }
        return count;
    }

    void TestParentDormancy(Engine::Engine& engine, IPhysicsModule& physics)
    {
        ResourceRegistry resourceRegistry;
        assert(resourceRegistry.Initialize());
        const auto asset = MakeSkeletalAsset(resourceRegistry);
        auto device = Container::MakeShared<FakeDevice>();
        RenderResources renderResources;
        assert(renderResources.Initialize(device));
        FontAtlas atlas;
#if NORVES_ENABLE_CORE_TEXT
        Asset::AssetFileReader reader;
        assert(atlas.Build(FontAtlasDesc{}, reader, renderResources.Textures()));
        assert(atlas.IsValid() && atlas.GetGlyph('A').bValid);
#else
        std::puts("Text は Core のテキスト機能が無効な構成のため検査対象外。");
#endif

        SceneView view;
        assert(view.Initialize(SceneViewSettings{}));
        World world;
        world.Initialize();
        world.SetSceneView(&view);
        Component::ComponentTickRegistration registration;
        registration.Name = "Test.ParentDormancy.Tick";
        registration.Run = &ParentDormancyTickProbe::RunTick;
        world.RegisterComponentTick(ParentDormancyTickProbe::StaticClass(), registration);

        Entity* parent = world.SpawnEntity<Entity>();
        Entity* child = world.SpawnEntity<Entity>(parent);
        Entity* triggerEntity = world.SpawnEntity<Entity>();
        assert(parent && child && triggerEntity);
        // 親自身を光線から離し、子だけが原点に来る階層にする。
        parent->SetLocalPosition(10.0f, 0.0f, 0.0f);
        child->SetLocalPosition(-10.0f, 0.0f, 0.0f);
        triggerEntity->SetLocalPosition(1.5f, 0.0f, 0.0f);
        auto* collider = world.CreateComponent<ColliderComponent>(child);
        auto* body = world.CreateComponent<RigidBodyComponent>(child);
        auto* trigger = world.CreateComponent<ColliderComponent>(triggerEntity);
        auto* tick = world.CreateComponent<ParentDormancyTickProbe>(child);
        assert(collider && body && trigger && tick);
        assert(collider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(body->SetBodyType(EPhysicsBodyType::Kinematic) == EPhysicsResult::Success);
        assert(trigger->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(trigger->SetTrigger(true) == EPhysicsResult::Success);
        tick->SetTickEnabled(true);

        auto* mesh = world.CreateComponent<Component::MeshComponent>(child);
        auto* skinned = world.CreateComponent<Component::SkinnedMeshComponent>(child);
        auto* mega = world.CreateComponent<Component::MegaGeometryComponent>(child);
        auto* board = world.CreateComponent<Component::BoardComponent>(child);
        auto* billboard = world.CreateComponent<Component::BillboardComponent>(child);
        auto* text = world.CreateComponent<Component::TextComponent>(child);
        auto* point = world.CreateComponent<Component::PointLightComponent>(child);
        auto* directional = world.CreateComponent<Component::DirectionalLightComponent>(child);
        auto* spot = world.CreateComponent<Component::SpotLightComponent>(child);
        assert(mesh && skinned && mega && board && billboard && text && point && directional && spot);
        mesh->SetMeshHandle(MeshDataHandle{100});
        skinned->SetSkeletalAsset(asset);
        skinned->SetPlaying(false);
        mega->SetMegaMeshHandle(MegaGeometry::MegaMeshHandle{200});
        board->SetTextureHandle(TextureHandle{300});
        board->SetBoardSpace(BoardSpace::WorldSpace);
        billboard->SetTextureHandle(TextureHandle{301});
        text->SetText("A");
        text->SetFontAtlas(&atlas);

        uint32_t triggerBegins = 0;
        uint32_t triggerEnds = 0;
        uint32_t childBegins = 0;
        uint32_t childEnds = 0;
        PhysicsCallbackHandle triggerBeginHandle, triggerEndHandle, childBeginHandle, childEndHandle;
        const ColliderHandle childHandle = collider->GetColliderHandle();
        const ColliderHandle triggerHandle = trigger->GetColliderHandle();
        const BodyHandle bodyHandle = body->GetBodyHandle();
        assert(childHandle.IsValid() && triggerHandle.IsValid() && bodyHandle.IsValid());
        assert(trigger->AddOnOverlapBegin(Delegate<void, const PhysicsContactEvent&>(
            [&](const PhysicsContactEvent& event)
            {
                assert(event.Self == triggerHandle && event.Other == childHandle);
                ++triggerBegins;
            }), triggerBeginHandle) == EPhysicsResult::Success);
        assert(trigger->AddOnOverlapEnd(Delegate<void, const PhysicsContactEvent&>(
            [&](const PhysicsContactEvent& event)
            {
                assert(event.Self == triggerHandle && event.Other == childHandle);
                ++triggerEnds;
            }), triggerEndHandle) == EPhysicsResult::Success);
        assert(collider->AddOnOverlapBegin(Delegate<void, const PhysicsContactEvent&>(
            [&](const PhysicsContactEvent& event)
            {
                assert(event.Self == childHandle && event.Other == triggerHandle);
                ++childBegins;
            }), childBeginHandle) == EPhysicsResult::Success);
        assert(collider->AddOnOverlapEnd(Delegate<void, const PhysicsContactEvent&>(
            [&](const PhysicsContactEvent& event)
            {
                assert(event.Self == childHandle && event.Other == triggerHandle);
                ++childEnds;
            }), childEndHandle) == EPhysicsResult::Success);

        const auto step = [&]()
        {
            physics.StepPreFixed(FixedDeltaTime);
            physics.StepFixed(FixedDeltaTime);
        };
        const auto checkPublication = [&](bool bExpectedActive)
        {
            // 作業中の候補ではなく、公開済みのスナップショットと facade の照会を確かめる。
            PhysicsShapeProxy proxy;
            assert(PhysicsModuleTestAccess::CopyPublishedProxy(physics, childHandle, proxy) == bExpectedActive);
            assert(PhysicsModuleTestAccess::IsBodyActive(physics, bodyHandle) == bExpectedActive);
            if (bExpectedActive)
            {
                assert(proxy.Body == bodyHandle);
            }
            Container::VariableArray<PhysicsOverlapHit> hits;
            assert(engine.GetSceneQuery().OverlapSphere(Math::Sphere(Math::Vector3(), 0.1f), hits)
                == (bExpectedActive ? EPhysicsSceneQueryResult::Success : EPhysicsSceneQueryResult::NoHit));
            size_t childHits = 0;
            for (const auto& hit : hits)
            {
                if (hit.Collider == childHandle)
                {
                    ++childHits;
                    assert(hit.Body == bodyHandle);
                }
            }
            assert(childHits == (bExpectedActive ? 1u : 0u));
            const Math::Ray ray(Math::Vector3(0.0f, 0.0f, -5.0f), Math::Vector3::UnitZ);
            PhysicsRaycastHit physicsHit;
            const auto result = engine.GetSceneQuery().Raycast(ray, 10.0f, physicsHit);
            assert(result == (bExpectedActive ? EPhysicsSceneQueryResult::Success : EPhysicsSceneQueryResult::NoHit));
            if (bExpectedActive)
            {
                assert(physicsHit.Collider == childHandle && physicsHit.Body == bodyHandle);
            }

            // ECS の有効性で絞る Entity の Raycast も、同じ階層で照合する。
            engine.GetSceneQuery().Rebuild(world);
            RaycastHit entityHit;
            assert(engine.GetSceneQuery().Raycast(ray, entityHit) == bExpectedActive);
            if (bExpectedActive)
            {
                assert(entityHit.HitEntity == child);
            }
        };
        const auto checkRendering = [&](bool bExpectedActive)
        {
            world.SyncToSceneView();
            const size_t expected = bExpectedActive ? 1u : 0u;
            assert(CountComponentProxies(view.GetMeshProxies(), mesh->GetComponentId()) == expected);
            assert(CountComponentProxies(view.GetSkinnedMeshProxies(), skinned->GetComponentId()) == expected);
            assert(CountComponentProxies(view.GetMegaGeometryProxies(), mega->GetComponentId()) == expected);
            assert(CountComponentProxies(view.GetBoardProxies(), board->GetComponentId()) == expected);
            assert(CountComponentProxies(view.GetBoardProxies(), billboard->GetComponentId()) == expected);
            for (const auto* light : {static_cast<Component::LightComponent*>(point),
                static_cast<Component::LightComponent*>(directional), static_cast<Component::LightComponent*>(spot)})
            {
                size_t count = 0;
                for (const auto& proxy : view.GetLightProxies())
                {
                    count += proxy.LightId == light->GetComponentId() ? 1u : 0u;
                }
                assert(count == expected);
            }
            // Text のグリフは永続 Board ではなく、World の一時 Board の公開入口に出る。
            Container::VariableArray<BoardProxy> glyphs;
            world.CollectTransientBoardProxies(glyphs);
#if NORVES_ENABLE_CORE_TEXT
            assert(CountComponentProxies(glyphs, text->GetComponentId()) == expected);
#else
            assert(CountComponentProxies(glyphs, text->GetComponentId()) == 0);
#endif
        };

        step();
        assert(triggerBegins == 1 && triggerEnds == 0 && childBegins == 1 && childEnds == 0);
        world.Tick(FixedDeltaTime);
        assert(tick->Count == 1);
        checkPublication(true);
        checkRendering(true);

        // 初回 Begin を除き、休眠中と復帰後の子への配送を別々に数える。
        childBegins = 0;
        parent->SetActive(false);
        step();
        assert(triggerBegins == 1 && triggerEnds == 1 && childBegins == 0 && childEnds == 0);
        for (uint32_t index = 0; index < 4; ++index)
        {
            checkPublication(false);
            world.Tick(FixedDeltaTime);
            assert(tick->Count == 1);
            checkRendering(false);
            step();
            assert(triggerBegins == 1 && triggerEnds == 1 && childBegins == 0 && childEnds == 0);
        }
        assert(child->IsActive() && collider->IsActive() && body->IsActive());

        parent->SetActive(true);
        step();
        assert(triggerBegins == 2 && triggerEnds == 1 && childBegins == 1 && childEnds == 0);
        for (uint32_t index = 0; index < 4; ++index)
        {
            checkPublication(true);
            world.Tick(FixedDeltaTime);
            assert(tick->Count == index + 2);
            checkRendering(true);
            step();
            assert(triggerBegins == 2 && triggerEnds == 1 && childBegins == 1 && childEnds == 0);
        }

        std::puts("親の休眠・復帰: 物理と Tick、全種の描画 proxy、Raycast、イベントの単発配送を確認。");
        world.Finalize();
        engine.GetSceneQuery().Clear();
#if NORVES_ENABLE_CORE_TEXT
        atlas.Shutdown(renderResources.Textures());
#endif
        renderResources.Shutdown();
        resourceRegistry.Shutdown();
    }
} // namespace

int main()
{
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    // 既存の物理試験と同じく、レジストリよりエンジンを長生きさせる。
    auto* engine = new Engine::Engine();
    ModuleRegistry& registry = GetModuleRegistry();
    IPhysicsModule* physics = RegisterPhysicsModule(registry);
    assert(physics && registry.InstallAll(*engine));
    TestParentDormancy(*engine, *physics);
    registry.ShutdownAll(*engine);
    std::puts("PhysicsParentDormancyTest: 成功");
    return 0;
}
