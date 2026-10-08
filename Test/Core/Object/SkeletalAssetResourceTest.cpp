// 複数clip束のAPI互換・子資産の状態・名前と所有寿命を実Resourceで確認する。
#include "Animation/SkeletalAssetResource.h"
#include "Object/ResourceRegistry.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <utility>
#undef assert
#define assert(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << #condition << " line=" << __LINE__ << "\n"; \
            std::abort(); \
        } \
    } while (false)
using namespace NorvesLib::Core;
namespace Container = NorvesLib::Core::Container;
namespace Skeletal = NorvesLib::Core::Skeletal;
namespace
{
    void SeedClip(const Container::TSharedPtr<AnimationClipResource>& clip, const Container::String& name)
    {
        Skeletal::SkeletalAnimationClip data;
        data.Name=name; data.DurationSeconds=1;
        Skeletal::SkeletalAnimationChannel channel;
        channel.Path=Skeletal::SkeletalAnimationPath::Translation;
        channel.JointIndex=0;
        Skeletal::SkeletalAnimationSample sample;
        sample.TimeSeconds=1; sample.Value.Y=1;
        channel.Samples.push_back(sample);
        data.Channels.push_back(std::move(channel));
        clip->SetClip(std::move(data));
    }
}
int main()
{
    ResourceRegistry registry;
    assert(registry.Initialize());
    auto mesh=registry.CreateTransient<SkinnedMeshResource>("ClipMesh");
    auto skeleton=registry.CreateTransient<SkeletonResource>("ClipSkeleton");
    auto asset=registry.CreateTransient<SkeletalAssetResource>("ClipAggregate");
    auto idle=registry.CreateTransient<AnimationClipResource>("ClipA");
    auto run=registry.CreateTransient<AnimationClipResource>("ClipB");
    auto jump=registry.CreateTransient<AnimationClipResource>("ClipC");
    assert(mesh && skeleton && asset && idle && run && jump);
    Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
    vertices[0].Normal.Z=1; vertices[0].JointWeights[0]=1;
    mesh->SetVertices(std::move(vertices)); mesh->SetIndices({0,0,0});
    Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
    joints[0].Name="Root"; joints[0].ParentIndex=-1;
    joints[0].InverseBindMatrix[0]=joints[0].InverseBindMatrix[5]=joints[0].InverseBindMatrix[10]=joints[0].InverseBindMatrix[15]=1;
    skeleton->SetJoints(std::move(joints));
    SeedClip(idle,"Idle"); SeedClip(run,"Run"); SeedClip(jump,"Jump");
    asset->SetClipResources(mesh,skeleton,{idle,run,jump});
    assert(!asset->IsLoaded() && !asset->IsValid() && !asset->Load());
    assert(mesh->Load() && skeleton->Load() && idle->Load() && run->Load() && jump->Load());
    assert(asset->Load() && asset->IsValid());
    assert(asset->GetClipCount()==3 && asset->GetAnimationClip()==idle);
    assert(asset->GetClip(size_t{1})==run && !asset->GetClip(size_t{3}) && !asset->GetClip(SIZE_MAX));
    assert(asset->GetClip(Container::StringView(_T("Run")))==run);
    assert(!asset->GetClip(Container::StringView(_T("run"))) && !asset->GetClip(Container::StringView(_T("missing"))));
    assert(!asset->GetClip(Container::StringView{}) && !asset->GetClip(Container::StringView(nullptr,1)));
    assert(asset->GetMemorySize()>=sizeof(SkeletalAssetResource)+3*sizeof(Container::TSharedPtr<AnimationClipResource>));
    Container::VariableArray<Container::TSharedPtr<AnimationClipResource>> input{idle,run,jump};
    asset->SetClipResources(mesh,skeleton,input); input.clear();
    assert(asset->GetClip(size_t{0})==idle && asset->GetClipCount()==3);
    const auto& legacyReference=asset->GetAnimationClip();
    asset->SetResources(mesh,skeleton,run);
    assert(asset->GetClipCount()==1 && legacyReference==run && asset->GetClip(size_t{0})==run);
    asset->SetResources(mesh,skeleton,{}); // 単数の旧空brace呼び出しも曖昧にしない。
    assert(!asset->IsValid() && !asset->GetAnimationClip());
    asset->SetClipResources(mesh,skeleton,{});
    assert(asset->GetClipCount()==0 && !asset->Load());
    asset->SetClipResources(mesh,skeleton,{idle,{}});
    assert(!asset->IsValid() && !asset->Load());
    asset->SetClipResources(mesh,skeleton,{idle,run,jump});
    run->Unload(); assert(!asset->Load());
    SeedClip(run,"Run"); assert(run->Load() && asset->Load());
    SeedClip(jump,"Run");
    assert(asset->Load() && !asset->GetClip(Container::StringView(_T("Run"))));
    assert(asset->GetClip(size_t{1})==run && asset->GetClip(size_t{2})==jump);
    SeedClip(jump,"");
    assert(asset->Load() && asset->GetClip(Container::StringView(_T("Run")))==run && !asset->GetClip(Container::StringView{}));
    using Char=Container::String::value_type;
    const auto seedHiddenName = [](const auto& clip, Char last)
    {
        auto data=clip->GetClip();
        data.Name=Container::String{Char('A'),Char(0),last};
        clip->SetClip(std::move(data));
        assert(clip->GetClip().Name.size()==3 && clip->GetClip().Name[2]==last);
    };
    const Char hiddenB[]{Char('A'),Char(0),Char('B')};
    const Char hiddenC[]{Char('A'),Char(0),Char('C')};
    const Char hiddenD[]{Char('A'),Char(0),Char('D')};
    seedHiddenName(run,Char('B'));
    assert(asset->GetClip(Container::StringView(hiddenB,3))==run && !asset->GetClip(Container::StringView(hiddenC,3)));
    seedHiddenName(jump,Char('C'));
    assert(asset->GetClip(Container::StringView(hiddenB,3))==run && asset->GetClip(Container::StringView(hiddenC,3))==jump);
    assert(!asset->GetClip(Container::StringView(hiddenD,3)));
    const size_t aggregateBytes=asset->GetMemorySize();
    const size_t childBytes=idle->GetMemorySize();
    auto expanded=idle->GetClip();
    expanded.Channels[0].Samples.resize(128);
    for (size_t index=0;index<128;++index)
    {
        expanded.Channels[0].Samples[index].TimeSeconds=float(index)/127;
    }
    idle->SetClip(std::move(expanded));
    assert(idle->GetMemorySize()>childBytes && asset->GetMemorySize()==aggregateBytes);
    asset->Unload();
    assert(asset->GetMemorySize()==aggregateBytes);
    assert(!asset->GetMesh() && !asset->GetSkeleton() && !asset->GetAnimationClip() && asset->GetClipCount()==0);
    assert(!asset->GetClip(size_t{0}) && !asset->GetClip(Container::StringView(_T("Idle"))));
    asset->SetClipResources(mesh,skeleton,{idle,run,jump});
    const auto meshHandle=registry.GetHandle<SkinnedMeshResource>(mesh->GetResourceId());
    const auto skeletonHandle=registry.GetHandle<SkeletonResource>(skeleton->GetResourceId());
    const auto idleHandle=registry.GetHandle<AnimationClipResource>(idle->GetResourceId());
    const auto runHandle=registry.GetHandle<AnimationClipResource>(run->GetResourceId());
    const auto jumpHandle=registry.GetHandle<AnimationClipResource>(jump->GetResourceId());
    Container::TWeakPtr<AnimationClipResource> retained=run;
    mesh.reset(); skeleton.reset(); idle.reset(); run.reset(); jump.reset();
    assert(registry.CollectGarbage()==0 && !retained.expired());
    assert(registry.Resolve(meshHandle) && registry.Resolve(skeletonHandle) && registry.Resolve(idleHandle) &&
        registry.Resolve(runHandle) && registry.Resolve(jumpHandle));
    asset.reset();
    size_t removed=registry.CollectGarbage(); removed+=registry.CollectGarbage();
    assert(removed==6 && retained.expired());
    assert(!registry.Resolve(meshHandle) && !registry.Resolve(skeletonHandle) && !registry.Resolve(idleHandle) &&
        !registry.Resolve(runHandle) && !registry.Resolve(jumpHandle));
    registry.Shutdown();
    std::cout << "SkeletalAssetResourceTest PASS: multiple_clips_lookup_legacy_child_state_memory_lifetime\n";
    return 0;
}
