#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Object/ResourceRegistry.h"
#include "Resource/SkeletalGltfData.h"
#include "Resource/SkinnedMeshResource.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <utility>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core;
namespace Container = NorvesLib::Core::Container;
namespace Skeletal = NorvesLib::Core::Skeletal;

namespace
{
    void SeedMesh(const Container::TSharedPtr<SkinnedMeshResource>& mesh)
    {
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
        vertices[0].Position.X = 7.0f;
        vertices[0].Normal.Z = 1.0f;
        vertices[0].JointIndices[0] = 0;
        vertices[0].JointWeights[0] = 1.0f;
        Container::VariableArray<uint32_t> indices = {0, 0, 0};
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(std::move(indices));
        Container::FixedArray<float, 16> meshNodeGlobal{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f, 1.0f};
        mesh->SetMeshNodeGlobalTransform(meshNodeGlobal);
    }

    void SeedSkeleton(const Container::TSharedPtr<SkeletonResource>& skeleton)
    {
        Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        joints[0].ParentIndex = -1;
        joints[0].InverseBindMatrix[0] = 1.0f;
        joints[0].InverseBindMatrix[5] = 1.0f;
        joints[0].InverseBindMatrix[10] = 1.0f;
        joints[0].InverseBindMatrix[15] = 1.0f;
        skeleton->SetJoints(std::move(joints));
    }

    void SeedClip(const Container::TSharedPtr<AnimationClipResource>& clip)
    {
        Skeletal::SkeletalAnimationClip data;
        data.Name = "Idle";
        data.DurationSeconds = 1.5f;
        Skeletal::SkeletalAnimationChannel channel;
        channel.JointIndex = 0;
        channel.Path = Skeletal::SkeletalAnimationPath::Translation;
        channel.Interpolation = Skeletal::SkeletalAnimationInterpolation::Linear;
        Skeletal::SkeletalAnimationSample sample;
        sample.TimeSeconds = 1.5f;
        sample.Value.Y = 2.0f;
        channel.Samples.push_back(sample);
        data.Channels.push_back(std::move(channel));
        clip->SetClip(std::move(data));
    }

    void AssertAggregateData(const SkeletalAssetResource& asset)
    {
        assert(asset.GetMesh());
        assert(asset.GetSkeleton());
        assert(asset.GetAnimationClip());
        assert(asset.GetMesh()->GetVertices().size() == 1);
        assert(asset.GetMesh()->GetVertices()[0].Position.X == 7.0f);
        assert(asset.GetMesh()->GetIndices().size() == 3);
        assert(asset.GetMesh()->GetMeshNodeGlobalTransform()[12] == 5.0f);
        assert(asset.GetSkeleton()->GetJoints().size() == 1);
        assert(asset.GetSkeleton()->GetJoints()[0].Name == "Root");
        assert(asset.GetAnimationClip()->GetClip().Name == "Idle");
        assert(asset.GetAnimationClip()->GetClip().DurationSeconds == 1.5f);
        assert(asset.GetAnimationClip()->GetClip().Channels[0].Samples[0].Value.Y == 2.0f);
    }
} // namespace

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "SkeletalResourceLifetimeTest start\n";
    ResourceRegistry registry;
    assert(registry.Initialize());

    auto mesh = registry.CreateTransient<SkinnedMeshResource>("M9Mesh");
    auto skeleton = registry.CreateTransient<SkeletonResource>("M9Skeleton");
    auto clip = registry.CreateTransient<AnimationClipResource>("M9Clip");
    auto asset = registry.CreateTransient<SkeletalAssetResource>("M9Aggregate");
    assert(mesh && skeleton && clip && asset);
    SeedMesh(mesh);
    SeedSkeleton(skeleton);
    SeedClip(clip);

    // Aggregate validity follows child load state, not pointer presence alone.
    asset->SetResources(mesh, skeleton, clip);
    assert(!asset->IsLoaded());
    assert(!asset->IsValid());
    assert(asset->GetResourceState() == ResourceState::Failed);

    assert(mesh->Load());
    assert(skeleton->Load());
    assert(clip->Load());
    assert(mesh->IsValid());
    assert(skeleton->IsValid());
    assert(clip->IsValid());
    asset->SetResources(mesh, skeleton, clip);
    assert(asset->IsLoaded());
    assert(asset->IsValid());
    clip->Unload();
    assert(!clip->IsValid());
    assert(!asset->Load());
    assert(!asset->IsLoaded());
    assert(!asset->IsValid());
    SeedClip(clip);
    assert(clip->Load());
    asset->SetResources(mesh, skeleton, clip);
    assert(asset->IsLoaded());
    assert(asset->IsValid());

    asset->Unload();
    assert(!asset->IsLoaded());
    assert(!asset->IsValid());
    assert(!asset->GetMesh());
    assert(!asset->GetSkeleton());
    assert(!asset->GetAnimationClip());
    asset->SetResources(mesh, skeleton, clip);
    assert(asset->IsLoaded());
    assert(asset->IsValid());

    const ResourceHandle<SkinnedMeshResource> meshHandle =
        registry.GetHandle<SkinnedMeshResource>(mesh->GetResourceId());
    const ResourceHandle<SkeletonResource> skeletonHandle =
        registry.GetHandle<SkeletonResource>(skeleton->GetResourceId());
    const ResourceHandle<AnimationClipResource> clipHandle =
        registry.GetHandle<AnimationClipResource>(clip->GetResourceId());
    const ResourceHandle<SkeletalAssetResource> staleAssetHandle =
        registry.GetHandle<SkeletalAssetResource>(asset->GetResourceId());
    assert(meshHandle.IsValid());
    assert(skeletonHandle.IsValid());
    assert(clipHandle.IsValid());
    assert(staleAssetHandle.IsValid());

    Container::TSharedPtr<SkeletalAssetResource> firstLease = asset;
    Container::TSharedPtr<SkeletalAssetResource> lastLease = asset;
    asset.reset();
    mesh.reset();
    skeleton.reset();
    clip.reset();

    // Removing aggregate strong ownership would make at least the three child handles die here.
    assert(registry.CollectGarbage() == 0);
    assert(registry.Resolve(meshHandle));
    assert(registry.Resolve(skeletonHandle));
    assert(registry.Resolve(clipHandle));
    {
        Container::TSharedPtr<SkeletalAssetResource> resolved = registry.Resolve(staleAssetHandle);
        assert(resolved);
        AssertAggregateData(*resolved);
    }

    firstLease.reset();
    assert(registry.CollectGarbage() == 0);
    AssertAggregateData(*lastLease);

    // The registry may visit child pools before or after the aggregate pool, so two sweeps are the deterministic fixpoint.
    lastLease.reset();
    size_t removedCount = registry.CollectGarbage();
    removedCount += registry.CollectGarbage();
    assert(removedCount == 4);
    assert(!registry.Resolve(staleAssetHandle));
    assert(!registry.Resolve(meshHandle));
    assert(!registry.Resolve(skeletonHandle));
    assert(!registry.Resolve(clipHandle));

    auto replacement = registry.CreateTransient<SkeletalAssetResource>("M9Replacement");
    assert(replacement);
    const ResourceHandle<SkeletalAssetResource> replacementHandle =
        registry.GetHandle<SkeletalAssetResource>(replacement->GetResourceId());
    assert(replacementHandle.IsValid());
    assert(replacementHandle.Index == staleAssetHandle.Index);
    assert(replacementHandle.Generation != staleAssetHandle.Generation);
    assert(!registry.Resolve(staleAssetHandle));
    assert(registry.Resolve(replacementHandle) == replacement);

    replacement.reset();
    assert(registry.CollectGarbage() == 1);
    assert(!registry.Resolve(replacementHandle));
    registry.Shutdown();

    std::cout << "SkeletalResourceLifetimeTest passed\n";
    return 0;
}
