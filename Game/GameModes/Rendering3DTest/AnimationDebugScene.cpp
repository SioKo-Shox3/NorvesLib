#include "AnimationDebugScene.h"
#include "Core/Public/Animation/SkeletonResource.h"
#include "Core/Public/Component/AnimatorComponent.h"
#include "Core/Public/Component/HoldSlotComponent.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Component/SocketAttachmentComponent.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Resource/SkinnedMeshResource.h"
#include <cmath>
namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    namespace A = NorvesLib::Core::Animation;
    namespace M = NorvesLib::Math;
    namespace
    {
        bool Panel(SkinnedMeshResource& mesh, float halfWidth, float low, float high, bool skinTop)
        {
            C::VariableArray<S::SkeletalVertex> vertices;
            C::VariableArray<uint32_t> indices;
            for (uint32_t face = 0; face < 2; ++face)
            {
                const float xy[4][2] = {{-halfWidth, low}, {halfWidth, low}, {halfWidth, high}, {-halfWidth, high}};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    S::SkeletalVertex v;
                    v.Position = {xy[i][0], xy[i][1], 0};
                    v.Normal = {0, 0, face ? -1.f : 1.f};
                    v.TexCoord = {i == 1 || i == 2 ? 1.f : 0.f, i >= 2 ? 1.f : 0.f};
                    v.JointIndices = {skinTop && i >= 2 ? 1u : 0u, 0, 0, 0};
                    v.JointWeights = {1, 0, 0, 0};
                    vertices.push_back(v);
                }
                const uint32_t order[6] = {0, 1, 2, 0, 2, 3};
                for (uint32_t i = 0; i < 6; i += 3)
                {
                    indices.push_back(face * 4 + order[i]);
                    indices.push_back(face * 4 + order[i + (face ? 2 : 1)]);
                    indices.push_back(face * 4 + order[i + (face ? 1 : 2)]);
                }
            }
            mesh.SetVertices(std::move(vertices));
            mesh.SetIndices(std::move(indices));
            return mesh.Load();
        }
        S::SkeletalAnimationClip Motion(const char* name, float duration, bool yaw)
        {
            S::SkeletalAnimationClip clip;
            clip.Name = name;
            clip.DurationSeconds = duration;
            clip.Metadata.Root.NominalSpeed = 1;
            clip.Metadata.Markers = {{Identity("Left"), yaw ? duration * .1f : 0},
                                     {Identity("Right"), duration * (yaw ? .7f : .4f)}};
            S::SkeletalAnimationChannel channel;
            channel.JointIndex = 1;
            channel.Path = S::SkeletalAnimationPath::Rotation;
            for (unsigned i = 0; i <= 32; ++i)
            {
                const float time = duration * float(i) / 32, angle = .6f * std::sin(6.283185307f * float(i) / 32);
                const float q = std::sin(angle * .5f), w = std::cos(angle * .5f);
                channel.Samples.push_back({time, yaw ? S::SkeletalValue{0, q, 0, w} : S::SkeletalValue{0, 0, q, w}});
            }
            clip.Channels.push_back(std::move(channel));
            return clip;
        }
        struct Resolver final : A::IClipResolver
        {
            C::TSharedPtr<AnimationClipResource> Slow, Fast;
            C::TSharedPtr<AnimationClipResource> ResolveClip(C::StringView name) const override
            {
                return name == C::StringView("slow")   ? Slow
                       : name == C::StringView("fast") ? Fast
                                                       : C::TSharedPtr<AnimationClipResource>{};
            }
        };
    } // namespace
    AnimationDebugScene::~AnimationDebugScene()
    {
        Stop();
    }
    bool AnimationDebugScene::Prepare(GameMode::GameModeContext& ctx, Rendering::MaterialHandle material)
    {
        Stop();
        if (!material.IsValid())
            return false;
        auto& registry = GEngine.GetResourceRegistry();
        auto skeleton = registry.CreateTransient<SkeletonResource>("AnimationDebugSkeleton");
        auto mesh = registry.CreateTransient<SkinnedMeshResource>("AnimationDebugBody");
        auto itemMesh = registry.CreateTransient<SkinnedMeshResource>("AnimationDebugSword");
        Resolver resolver;
        resolver.Slow = registry.CreateTransient<AnimationClipResource>("AnimationDebugSlow");
        resolver.Fast = registry.CreateTransient<AnimationClipResource>("AnimationDebugFast");
        auto still = registry.CreateTransient<AnimationClipResource>("AnimationDebugStill");
        m_CharacterAsset = registry.CreateTransient<SkeletalAssetResource>("AnimationDebugCharacter");
        m_ItemAsset = registry.CreateTransient<SkeletalAssetResource>("AnimationDebugItem");
        m_Graph = registry.CreateTransient<AnimGraphResource>("AnimationDebugGraph");
        if (!skeleton || !mesh || !itemMesh || !resolver.Slow || !resolver.Fast || !still || !m_CharacterAsset ||
            !m_ItemAsset || !m_Graph)
            return false;
        C::VariableArray<S::SkeletalJoint> joints(2);
        joints[0].Name = "Root";
        joints[1].Name = "Head";
        joints[1].ParentIndex = 0;
        for (auto& joint : joints)
        {
            joint.InverseBindMatrix.fill(0);
            for (unsigned i = 0; i < 4; ++i)
                joint.InverseBindMatrix[i * 5] = 1;
        }
        skeleton->SetJoints(std::move(joints));
        if (!skeleton->Load() || !Panel(*mesh, .45f, -.7f, .7f, true) || !Panel(*itemMesh, .04f, 0, .8f, false))
            return false;
        resolver.Slow->SetClip(Motion("slow", 2, false));
        resolver.Fast->SetClip(Motion("fast", 1.4f, true));
        S::SkeletalAnimationClip itemClip;
        itemClip.Name = "still";
        itemClip.DurationSeconds = 1;
        S::SkeletalAnimationChannel root;
        root.JointIndex = 0;
        root.Path = S::SkeletalAnimationPath::Translation;
        root.Samples = {{0, {0, 0, 0, 0}}};
        itemClip.Channels.push_back(root);
        still->SetClip(std::move(itemClip));
        if (!resolver.Slow->Load() || !resolver.Fast->Load() || !still->Load())
            return false;
        m_CharacterAsset->SetClipResources(mesh, skeleton, {resolver.Slow, resolver.Fast});
        m_ItemAsset->SetResources(itemMesh, skeleton, still);
        if (!m_CharacterAsset->Load() || !m_ItemAsset->Load())
            return false;
        A::SocketReport socketReport;
        if (!skeleton->ApplySocketsJson(
                C::String(R"({"sockets":[{"name":"Mouth","parent":"Head","position":[0,0.55,0]}]})"), socketReport))
            return false;
        A::AnimGraphReport report;
        const C::String json(
            R"({"version":1,"params":[{"name":"Blend","type":"float","value":0.5},{"name":"Speed","type":"float","value":1},{"name":"Profile","type":"int"}],"syncGroups":[{"name":"Gait","speed":"Speed","minRate":0,"maxRate":2}],"nodes":[{"id":"slow","type":"clip","clip":"slow","syncGroup":"Gait"},{"id":"fast","type":"clip","clip":"fast","syncGroup":"Gait"},{"id":"mix","type":"blend2","children":["slow","fast"],"weight":"Blend"},{"id":"locomotion","type":"stateMachine","initial":"Blend","states":[{"name":"Slow","node":"slow"},{"name":"Blend","node":"mix"}]}],"root":"locomotion"})");
        if (!m_Graph->Compile(json, *skeleton, resolver, report) || !m_Graph->Load())
            return false;
        auto* character = ctx.ScopeRef.SpawnObject<Entity>();
        auto* item = ctx.ScopeRef.SpawnObject<Entity>();
        if (!character || !item)
            return false;
        character->SetPosition(2.8f, 0, 1.5f);
        auto* skinned = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(character);
        auto* animator = ctx.WorldRef.CreateComponent<Component::AnimatorComponent>(character);
        auto* slots = ctx.WorldRef.CreateComponent<Component::HoldSlotComponent>(character);
        auto* sword = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(item);
        auto* attachment = ctx.WorldRef.CreateComponent<Component::SocketAttachmentComponent>(item);
        if (!skinned || !animator || !slots || !sword || !attachment)
            return false;
        skinned->SetSkeletalAsset(m_CharacterAsset);
        skinned->SetMaterial(material);
        sword->SetSkeletalAsset(m_ItemAsset);
        sword->SetMaterial(material);
        sword->SetPlaying(false);
        if (!animator->SetGraph(m_Graph) ||
            !slots->ApplySlotsJson(C::String(R"({"holdSlots":[{"name":"Mouth","capacity":1,"acceptTags":["Sword"]}]})"),
                                   socketReport))
            return false;
        if (!attachment->ApplySettingsJson(
                C::String(
                    R"({"attachProfiles":[{"name":"Along"},{"name":"Side","rotation":[0,0,0.7071067811865475,0.7071067811865475]}],"grip":{"position":[0,0.05,0]}})"),
                socketReport))
            return false;
        attachment->BindProfileParameter(Identity("Profile"));
        const Identity tag("Sword");
        if (!attachment->Attach(character->GetObjectId(), Identity("Mouth"), Identity("Mouth"), .1f, {&tag, 1}))
            return false;
        m_Character = character->GetObjectId();
        m_Item = item->GetObjectId();
        LOG_INFO("ANIMATION_DEBUG scene=ready synthetic=1");
        return true;
    }
    void AnimationDebugScene::ActivateViews(World& world)
    {
#if defined(NORVES_ENABLE_IMGUI)
        if (m_Character && m_Item)
        {
            m_AnimatorView.Attach(world, m_Character);
            m_SocketView.Attach(world, m_Character, m_Item);
        }
#else
        (void)world;
#endif
    }
    void AnimationDebugScene::Stop()
    {
#if defined(NORVES_ENABLE_IMGUI)
        m_AnimatorView.Detach();
        m_SocketView.Detach();
#endif
        m_Character = 0;
        m_Item = 0;
        m_Graph.reset();
        m_ItemAsset.reset();
        m_CharacterAsset.reset();
    }
} // namespace Game::GameModes
