#include "Animation/SkeletalAssetResource.h"
#define _ALLOW_KEYWORD_MACROS
#define private public
#include "Rendering/RenderingCoordinator.h"
#undef private

#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneView.h"
#include "Resource/SkinnedMeshResource.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;
namespace Skeletal = NorvesLib::Core::Skeletal;

namespace
{
    constexpr float Epsilon = 0.0001f;

    void AssertNear(float actual, float expected)
    {
        assert(std::fabs(actual - expected) <= Epsilon);
    }

    void SetIdentity(Container::FixedArray<float, 16>& matrix)
    {
        matrix.fill(0.0f);
        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
    }

    Skeletal::SkeletalAnimationSample MakeSample(float time, float x, float y, float z, float w)
    {
        Skeletal::SkeletalAnimationSample sample;
        sample.TimeSeconds = time;
        sample.Value = {x, y, z, w};
        return sample;
    }

    void SeedResources(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
                       const Container::TSharedPtr<SkeletonResource>& skeleton,
                       const Container::TSharedPtr<AnimationClipResource>& clip)
    {
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(2);
        vertices[0].Position = {1.0f, 0.0f, 0.0f};
        vertices[0].Normal = {1.0f, 0.0f, 0.0f};
        vertices[0].JointIndices[0] = 0;
        vertices[0].JointWeights[0] = 1.0f;
        vertices[1].Position = {-1.0f, 0.0f, 0.0f};
        vertices[1].Normal = {1.0f, 0.0f, 0.0f};
        vertices[1].JointIndices[0] = 0;
        vertices[1].JointWeights[0] = 1.0f;
        Container::VariableArray<uint32_t> indices = {0, 1, 0};
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(std::move(indices));
        assert(mesh->Load());

        Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        joints[0].ParentIndex = -1;
        SetIdentity(joints[0].InverseBindMatrix);
        joints[0].InverseBindMatrix[12] = -2.0f;
        skeleton->SetJoints(std::move(joints));
        assert(skeleton->Load());

        Skeletal::SkeletalAnimationClip data;
        data.Name = "Snapshot";
        data.DurationSeconds = 1.0f;
        Skeletal::SkeletalAnimationChannel translation;
        translation.JointIndex = 0;
        translation.Path = Skeletal::SkeletalAnimationPath::Translation;
        translation.Interpolation = Skeletal::SkeletalAnimationInterpolation::Linear;
        translation.Samples.push_back(MakeSample(0.0f, 12.0f, 0.0f, 0.0f, 0.0f));
        translation.Samples.push_back(MakeSample(1.0f, 14.0f, 0.0f, 0.0f, 0.0f));
        Skeletal::SkeletalAnimationChannel rotation;
        rotation.JointIndex = 0;
        rotation.Path = Skeletal::SkeletalAnimationPath::Rotation;
        rotation.Interpolation = Skeletal::SkeletalAnimationInterpolation::Linear;
        rotation.Samples.push_back(MakeSample(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        rotation.Samples.push_back(MakeSample(1.0f, 0.0f, 0.0f, 1.0f, 0.0f));
        data.Channels.push_back(std::move(translation));
        data.Channels.push_back(std::move(rotation));
        clip->SetClip(std::move(data));
        assert(clip->Load());
    }

    void AssertSnapshot(const FramePacket& packet, uint64_t componentId)
    {
        assert(packet.Scene.SkinnedMeshProxies.size() == 1);
        const SkinnedMeshProxy& proxy = packet.Scene.SkinnedMeshProxies[0];
        assert(proxy.ComponentId == componentId);
        assert(proxy.BonePalette.size() == 1);
        AssertNear(proxy.BonePalette[0].GetTranslationRow().x, 3.0f);
        AssertNear(proxy.BonePalette[0].GetTranslationRow().y, -2.0f);
        AssertNear(proxy.AnimatedBounds.Min.x, 3.0f);
        AssertNear(proxy.AnimatedBounds.Min.y, -3.0f);
        AssertNear(proxy.AnimatedBounds.Max.x, 3.0f);
        AssertNear(proxy.AnimatedBounds.Max.y, -1.0f);
    }

    void GeneratePacketSnapshot(RenderingCoordinator& coordinator,
                                const Container::TSharedPtr<SceneView>& sceneView,
                                FramePacket& packet)
    {
        coordinator.m_bInitialized = true;
        coordinator.m_MaxDrawCallsPerFrame = 16;
        coordinator.m_MainSceneView = sceneView;
        coordinator.m_CurrentPacket = &packet;
        coordinator.GenerateDrawCommands();
        coordinator.m_CurrentPacket = nullptr;
        coordinator.m_MainSceneView.reset();
        coordinator.m_bInitialized = false;
    }
} // namespace

int main()
{
    std::cout << "SkeletalFramePacketSnapshotTest start\n";

    ResourceRegistry registry;
    assert(registry.Initialize());
    auto mesh = registry.CreateTransient<SkinnedMeshResource>("SnapshotMesh");
    auto skeleton = registry.CreateTransient<SkeletonResource>("SnapshotSkeleton");
    auto clip = registry.CreateTransient<AnimationClipResource>("SnapshotClip");
    auto asset = registry.CreateTransient<SkeletalAssetResource>("SnapshotAsset");
    assert(mesh && skeleton && clip && asset);
    asset->SetResources(mesh, skeleton, clip);
    assert(!asset->IsLoaded());

    auto sceneView = Container::MakeShared<SceneView>();
    SceneViewSettings sceneSettings;
    assert(sceneView->Initialize(sceneSettings));

    World world;
    world.Initialize();
    world.SetSceneView(sceneView.get());
    Entity* entity = world.SpawnObject<Entity>();
    assert(entity);
    auto* component = world.CreateComponent<Component::SkinnedMeshComponent>(entity);
    assert(component);
    component->SetSkeletalAsset(asset);
    Math::Matrix4x4 meshNodeGlobal = Math::Matrix4x4::Identity;
    meshNodeGlobal.SetTranslationRow(Math::Vector3(10.0f, 0.0f, 0.0f));
    component->SetMeshNodeGlobalTransform(meshNodeGlobal);

    world.SyncToSceneView();
    assert(sceneView->GetSkinnedMeshProxies().empty());
    assert(component->IsRenderStateDirty());

    SeedResources(mesh, skeleton, clip);
    assert(asset->Load());
    world.SyncToSceneView();
    assert(sceneView->GetSkinnedMeshProxies().size() == 1);
    assert(!component->IsRenderStateDirty());

    Container::VariableArray<Skeletal::SkeletalJoint> singularJoints(1);
    singularJoints[0].Name = "SingularRoot";
    singularJoints[0].ParentIndex = -1;
    singularJoints[0].InverseBindMatrix.fill(0.0f);
    skeleton->SetJoints(std::move(singularJoints));
    assert(skeleton->Load());
    component->SetAnimationTimeSeconds(0.5f);
    world.SyncToSceneView();
    assert(sceneView->GetSkinnedMeshProxies().empty());
    assert(component->IsRenderStateDirty());

    SeedResources(mesh, skeleton, clip);
    assert(asset->Load());
    world.SyncToSceneView();
    assert(sceneView->GetSkinnedMeshProxies().size() == 1);
    assert(!component->IsRenderStateDirty());

    component->SetAnimationTimeSeconds(0.75f);
    component->SetLooping(true);
    world.Tick(0.5f);
    AssertNear(component->GetAnimationTimeSeconds(), 0.25f);
    component->SetLooping(false);
    world.Tick(2.0f);
    AssertNear(component->GetAnimationTimeSeconds(), 1.0f);
    component->SetAnimationTimeSeconds(0.5f);

    world.SyncToSceneView();
    assert(sceneView->GetSkinnedMeshProxies().size() == 1);
    const uint64_t componentId = component->GetComponentId();

    RenderingCoordinator coordinator;
    FramePacketManager manager;
    manager.Initialize();
    FramePacket* cancelled = manager.AcquireForWrite();
    assert(cancelled);
    GeneratePacketSnapshot(coordinator, sceneView, *cancelled);
    AssertSnapshot(*cancelled, componentId);
    manager.CancelWrite(cancelled);
    assert(cancelled->Scene.SkinnedMeshProxies.empty());
    assert(manager.IsEmpty());

    component->SetVisible(false);
    world.SyncToSceneView();
    assert(sceneView->GetSkinnedMeshProxies().empty());
    assert(!component->IsRenderStateDirty());
    FramePacket* staleCheck = manager.AcquireForWrite();
    assert(staleCheck);
    GeneratePacketSnapshot(coordinator, sceneView, *staleCheck);
    assert(staleCheck->Scene.SkinnedMeshProxies.empty());
    manager.CancelWrite(staleCheck);

    component->SetVisible(true);
    world.SyncToSceneView();
    assert(sceneView->GetSkinnedMeshProxies().size() == 1);
    FramePacket* written = manager.AcquireForWrite();
    assert(written);
    GeneratePacketSnapshot(coordinator, sceneView, *written);
    manager.FinishWrite(written);

    world.Finalize();
    sceneView->ClearAllProxies();
    asset.reset();
    mesh.reset();
    skeleton.reset();
    clip.reset();
    size_t removedCount = registry.CollectGarbage();
    removedCount += registry.CollectGarbage();
    assert(removedCount == 4);
    registry.Shutdown();

    FramePacket* read = manager.AcquireForRead();
    assert(read == written);
    AssertSnapshot(*read, componentId);
    manager.FinishRead(read);
    assert(read->Scene.SkinnedMeshProxies.empty());
    assert(manager.IsEmpty());

    FramePacket* recycled = manager.AcquireForWrite();
    assert(recycled);
    assert(recycled->Scene.SkinnedMeshProxies.empty());
    manager.CancelWrite(recycled);
    sceneView->Shutdown();

    std::cout << "SkeletalFramePacketSnapshotTest passed\n";
    return 0;
}
