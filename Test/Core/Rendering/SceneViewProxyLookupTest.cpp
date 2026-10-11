#include "Rendering/SceneView.h"
#include "Container/UnorderedSet.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;

namespace
{
    constexpr uint32_t ProxyCount = 1000;

    MeshProxy MakeMeshProxy(uint64_t objectId, uint64_t componentId, bool bValid = true)
    {
        MeshProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.MeshHandle.Id = bValid ? objectId * 10 + 1 : 0;
        proxy.MaterialCount = 1;
        proxy.Materials[0].Id = componentId * 10 + 1;
        proxy.WorldBounds.Radius = 1.0f;
        proxy.bVisible = bValid;
        return proxy;
    }

    LightProxy MakeLightProxy(uint64_t lightId, float intensity = 1.0f, bool bVisible = true)
    {
        LightProxy proxy;
        proxy.LightId = lightId;
        proxy.Intensity = intensity;
        proxy.bVisible = bVisible;
        return proxy;
    }

    MegaGeometryProxy MakeMegaGeometryProxy(uint64_t objectId, uint64_t componentId, bool bValid = true)
    {
        MegaGeometryProxy proxy;
        proxy.ObjectId = objectId;
        proxy.ComponentId = componentId;
        proxy.MegaMeshHandle.Id = bValid ? objectId * 100 + 1 : 0;
        proxy.WorldBounds.Radius = 1.0f;
        proxy.bVisible = bValid;
        return proxy;
    }

    const MeshProxy* FindMeshProxy(const SceneView& view, uint64_t componentId)
    {
        for (const MeshProxy& proxy : view.GetMeshProxies())
        {
            if (proxy.ComponentId == componentId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    uint32_t CountMeshProxiesForObjectId(const SceneView& view, uint64_t objectId)
    {
        uint32_t count = 0;
        for (const MeshProxy& proxy : view.GetMeshProxies())
        {
            if (proxy.ObjectId == objectId)
            {
                ++count;
            }
        }
        return count;
    }

    const LightProxy* FindLightProxy(const SceneView& view, uint64_t lightId)
    {
        for (const LightProxy& proxy : view.GetLightProxies())
        {
            if (proxy.LightId == lightId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    const MegaGeometryProxy* FindMegaGeometryProxy(const SceneView& view, uint64_t objectId)
    {
        for (const MegaGeometryProxy& proxy : view.GetMegaGeometryProxies())
        {
            if (proxy.ObjectId == objectId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    void TestMeshProxyLookup()
    {
        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));

        for (uint32_t i = 1; i <= ProxyCount; ++i)
        {
            view.AddMeshProxy(MakeMeshProxy(i, i + 1000));
        }
        assert(view.GetMeshProxies().size() == ProxyCount);

        view.AddMeshProxy(MakeMeshProxy(10, 9000));
        assert(view.GetMeshProxies().size() == ProxyCount + 1);
        assert(FindMeshProxy(view, 9000) != nullptr);
        assert(FindMeshProxy(view, 9000)->ObjectId == 10);
        assert(CountMeshProxiesForObjectId(view, 10) == 2);

        view.AddMeshProxy(MakeMeshProxy(10, 9001, false));
        assert(view.GetMeshProxies().size() == ProxyCount + 1);
        assert(FindMeshProxy(view, 9001) == nullptr);

        view.AddMeshProxy(MakeMeshProxy(ProxyCount + 1, 9002, false));
        assert(view.GetMeshProxies().size() == ProxyCount + 1);

        view.UpdateMeshProxy(MakeMeshProxy(20, 1020, false));
        assert(view.GetMeshProxies().size() == ProxyCount + 1);
        assert(FindMeshProxy(view, 1020) != nullptr);
        assert(!FindMeshProxy(view, 1020)->IsValid());

        view.UpdateMeshProxy(MakeMeshProxy(20, 1020));
        assert(view.GetMeshProxies().size() == ProxyCount + 1);
        assert(FindMeshProxy(view, 1020)->ObjectId == 20);
        assert(FindMeshProxy(view, 1020)->IsValid());

        view.UpdateMeshProxy(MakeMeshProxy(ProxyCount + 10, 9200, false));
        assert(view.GetMeshProxies().size() == ProxyCount + 1);
        assert(FindMeshProxy(view, 9200) == nullptr);

        view.UpdateMeshProxy(MakeMeshProxy(ProxyCount + 10, 9201));
        assert(view.GetMeshProxies().size() == ProxyCount + 2);
        assert(FindMeshProxy(view, 9201) != nullptr);
        assert(FindMeshProxy(view, 9201)->ObjectId == ProxyCount + 10);

        view.RemoveMeshProxy(1100);
        assert(view.GetMeshProxies().size() == ProxyCount + 1);
        assert(FindMeshProxy(view, 1100) == nullptr);

        view.UpdateMeshProxy(MakeMeshProxy(ProxyCount + 10, 9201));
        assert(FindMeshProxy(view, 9201)->ObjectId == ProxyCount + 10);
        view.RemoveMeshProxy(9201);
        assert(view.GetMeshProxies().size() == ProxyCount);
        assert(FindMeshProxy(view, 9201) == nullptr);

        Container::UnorderedSet<uint64_t> liveComponentIds;
        for (uint32_t i = 1; i <= ProxyCount; ++i)
        {
            const uint64_t componentId = i + 1000;
            if (componentId != 1100 && i % 3 == 0)
            {
                liveComponentIds.insert(componentId);
            }
        }
        view.RemoveStaleMeshProxies(liveComponentIds);
        assert(view.GetMeshProxies().size() == liveComponentIds.size());
        for (const MeshProxy& proxy : view.GetMeshProxies())
        {
            assert(liveComponentIds.find(proxy.ComponentId) != liveComponentIds.end());
        }

        view.UpdateMeshProxy(MakeMeshProxy(999, 9400));
        assert(FindMeshProxy(view, 9400) != nullptr);
        assert(FindMeshProxy(view, 9400)->ObjectId == 999);
        view.RemoveMeshProxy(9400);
        assert(FindMeshProxy(view, 9400) == nullptr);

        view.ClearAllProxies();
        assert(view.GetMeshProxies().empty());
        view.AddMeshProxy(MakeMeshProxy(100, 9500));
        assert(view.GetMeshProxies().size() == 1);
        assert(FindMeshProxy(view, 9500) != nullptr);
        assert(FindMeshProxy(view, 9500)->ObjectId == 100);

        view.Shutdown();
    }

    void TestMeshProxySameEntityMultiComponentCoverage()
    {
        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));

        constexpr uint64_t SharedObjectId = 77;
        constexpr uint64_t FirstComponentId = 5001;
        constexpr uint64_t SecondComponentId = 5002;

        view.AddMeshProxy(MakeMeshProxy(SharedObjectId, FirstComponentId));
        view.AddMeshProxy(MakeMeshProxy(SharedObjectId, SecondComponentId));
        assert(view.GetMeshProxies().size() == 2);
        assert(CountMeshProxiesForObjectId(view, SharedObjectId) == 2);
        assert(FindMeshProxy(view, FirstComponentId) != nullptr);
        assert(FindMeshProxy(view, SecondComponentId) != nullptr);

        view.UpdateMeshProxy(MakeMeshProxy(SharedObjectId, SecondComponentId, false));
        assert(view.GetMeshProxies().size() == 2);
        assert(FindMeshProxy(view, SecondComponentId) != nullptr);
        assert(!FindMeshProxy(view, SecondComponentId)->IsValid());
        assert(FindMeshProxy(view, FirstComponentId)->IsValid());

        view.RemoveMeshProxy(FirstComponentId);
        assert(view.GetMeshProxies().size() == 1);
        assert(FindMeshProxy(view, FirstComponentId) == nullptr);
        assert(FindMeshProxy(view, SecondComponentId) != nullptr);

        Container::UnorderedSet<uint64_t> liveMeshComponentIds;
        liveMeshComponentIds.insert(SecondComponentId);
        view.RemoveStaleMeshProxies(liveMeshComponentIds);
        assert(view.GetMeshProxies().size() == 1);
        assert(FindMeshProxy(view, SecondComponentId) != nullptr);

        liveMeshComponentIds.clear();
        view.RemoveStaleMeshProxies(liveMeshComponentIds);
        assert(view.GetMeshProxies().empty());

        view.Shutdown();
    }

    void TestMeshVisibleCacheInvalidation()
    {
        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));

        view.AddMeshProxy(MakeMeshProxy(1, 101));
        view.PrepareDrawCommands();
        assert(view.GetVisibleMeshProxies().size() == 1);

        view.AddMeshProxy(MakeMeshProxy(2, 102));
        assert(view.GetVisibleMeshProxies().empty());

        view.PrepareDrawCommands();
        assert(view.GetVisibleMeshProxies().size() == 2);

        view.RemoveMeshProxy(101);
        assert(view.GetVisibleMeshProxies().empty());

        view.PrepareDrawCommands();
        assert(view.GetVisibleMeshProxies().size() == 1);

        Container::UnorderedSet<uint64_t> liveComponentIds;
        view.RemoveStaleMeshProxies(liveComponentIds);
        assert(view.GetVisibleMeshProxies().empty());

        view.AddMeshProxy(MakeMeshProxy(3, 103));
        view.PrepareDrawCommands();
        assert(view.GetVisibleMeshProxies().size() == 1);
        view.ClearAllProxies();
        assert(view.GetVisibleMeshProxies().empty());

        view.Shutdown();
    }

    void TestLightProxyLookup()
    {
        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));

        for (uint32_t i = 1; i <= ProxyCount; ++i)
        {
            view.AddLightProxy(MakeLightProxy(i));
        }
        assert(view.GetLightProxies().size() == ProxyCount);

        view.AddLightProxy(MakeLightProxy(10, 2.0f));
        assert(view.GetLightProxies().size() == ProxyCount);
        assert(FindLightProxy(view, 10)->Intensity == 2.0f);

        view.AddLightProxy(MakeLightProxy(10, 3.0f, false));
        assert(view.GetLightProxies().size() == ProxyCount);
        assert(FindLightProxy(view, 10)->Intensity == 2.0f);

        view.AddLightProxy(MakeLightProxy(ProxyCount + 1, 0.0f));
        assert(view.GetLightProxies().size() == ProxyCount);

        view.UpdateLightProxy(MakeLightProxy(20, 0.0f));
        assert(view.GetLightProxies().size() == ProxyCount);
        assert(FindLightProxy(view, 20)->Intensity == 0.0f);
        assert(!FindLightProxy(view, 20)->IsValid());

        view.UpdateLightProxy(MakeLightProxy(20, 4.0f));
        assert(FindLightProxy(view, 20)->Intensity == 4.0f);

        view.UpdateLightProxy(MakeLightProxy(ProxyCount + 10, 0.0f));
        assert(view.GetLightProxies().size() == ProxyCount);

        view.UpdateLightProxy(MakeLightProxy(ProxyCount + 10, 5.0f));
        assert(view.GetLightProxies().size() == ProxyCount + 1);

        view.RemoveLightProxy(100);
        assert(view.GetLightProxies().size() == ProxyCount);
        assert(FindLightProxy(view, 100) == nullptr);

        view.UpdateLightProxy(MakeLightProxy(ProxyCount + 10, 6.0f));
        assert(FindLightProxy(view, ProxyCount + 10)->Intensity == 6.0f);
        view.RemoveLightProxy(ProxyCount + 10);
        assert(view.GetLightProxies().size() == ProxyCount - 1);

        Container::UnorderedSet<uint64_t> liveLightIds;
        for (uint32_t i = 1; i <= ProxyCount; ++i)
        {
            if (i != 100 && i % 4 == 0)
            {
                liveLightIds.insert(i);
            }
        }
        view.RemoveStaleLightProxies(liveLightIds);
        assert(view.GetLightProxies().size() == liveLightIds.size());
        for (const LightProxy& proxy : view.GetLightProxies())
        {
            assert(liveLightIds.find(proxy.LightId) != liveLightIds.end());
        }

        view.UpdateLightProxy(MakeLightProxy(1000, 7.0f));
        assert(FindLightProxy(view, 1000)->Intensity == 7.0f);
        view.RemoveLightProxy(1000);
        assert(FindLightProxy(view, 1000) == nullptr);

        view.Shutdown();
    }

    void TestMegaGeometryProxyLookup()
    {
        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));

        for (uint32_t i = 1; i <= ProxyCount; ++i)
        {
            view.AddMegaGeometryProxy(MakeMegaGeometryProxy(i, i + 2000));
        }
        assert(view.GetMegaGeometryProxies().size() == ProxyCount);

        view.AddMegaGeometryProxy(MakeMegaGeometryProxy(10, 9000));
        assert(view.GetMegaGeometryProxies().size() == ProxyCount);
        assert(FindMegaGeometryProxy(view, 10)->ComponentId == 9000);

        view.AddMegaGeometryProxy(MakeMegaGeometryProxy(10, 9001, false));
        assert(view.GetMegaGeometryProxies().size() == ProxyCount);
        assert(FindMegaGeometryProxy(view, 10)->ComponentId == 9000);

        view.AddMegaGeometryProxy(MakeMegaGeometryProxy(ProxyCount + 1, 9002, false));
        assert(view.GetMegaGeometryProxies().size() == ProxyCount);

        view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(20, 9100, false));
        assert(view.GetMegaGeometryProxies().size() == ProxyCount);
        assert(FindMegaGeometryProxy(view, 20)->ComponentId == 9100);
        assert(!FindMegaGeometryProxy(view, 20)->IsValid());

        view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(20, 9101));
        assert(FindMegaGeometryProxy(view, 20)->ComponentId == 9101);

        view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(ProxyCount + 10, 9200, false));
        assert(view.GetMegaGeometryProxies().size() == ProxyCount);

        view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(ProxyCount + 10, 9201));
        assert(view.GetMegaGeometryProxies().size() == ProxyCount + 1);

        view.RemoveMegaGeometryProxy(100);
        assert(view.GetMegaGeometryProxies().size() == ProxyCount);
        assert(FindMegaGeometryProxy(view, 100) == nullptr);

        view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(ProxyCount + 10, 9300));
        assert(FindMegaGeometryProxy(view, ProxyCount + 10)->ComponentId == 9300);
        view.RemoveMegaGeometryProxy(ProxyCount + 10);
        assert(view.GetMegaGeometryProxies().size() == ProxyCount - 1);
        assert(FindMegaGeometryProxy(view, ProxyCount + 10) == nullptr);

        Container::UnorderedSet<uint64_t> liveObjectIds;
        for (uint32_t i = 1; i <= ProxyCount; ++i)
        {
            if (i != 100 && i % 5 == 0)
            {
                liveObjectIds.insert(i);
            }
        }
        view.RemoveStaleMegaGeometryProxies(liveObjectIds);
        assert(view.GetMegaGeometryProxies().size() == liveObjectIds.size());
        for (const MegaGeometryProxy& proxy : view.GetMegaGeometryProxies())
        {
            assert(liveObjectIds.find(proxy.ObjectId) != liveObjectIds.end());
        }

        view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(1000, 9400));
        assert(FindMegaGeometryProxy(view, 1000)->ComponentId == 9400);
        view.RemoveMegaGeometryProxy(1000);
        assert(FindMegaGeometryProxy(view, 1000) == nullptr);

        view.ClearMegaGeometryProxies();
        assert(view.GetMegaGeometryProxies().empty());
        view.AddMegaGeometryProxy(MakeMegaGeometryProxy(500, 9500));
        view.AddMegaGeometryProxy(MakeMegaGeometryProxy(500, 9501));
        assert(view.GetMegaGeometryProxies().size() == 1);
        assert(FindMegaGeometryProxy(view, 500)->ComponentId == 9501);
        view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(500, 9502));
        assert(view.GetMegaGeometryProxies().size() == 1);
        assert(FindMegaGeometryProxy(view, 500)->ComponentId == 9502);

        view.Shutdown();
    }

    bool SamePreviousAndWorld(const MegaGeometryProxy& proxy)
    {
        return std::memcmp(&proxy.PreviousWorldTransform, &proxy.WorldTransform, sizeof(proxy.WorldTransform)) == 0;
    }

    // 差分の同期の土台: 書いた位置の記録（取り出し・外したときの末尾の移動・クリア・全量の同期）と、前の変換の履歴
    void TestMegaGeometryDeltaTracking()
    {
        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));

        Container::VariableArray<uint32_t> taken;
        for (uint64_t i = 1; i <= 4; ++i)
        {
            view.PublishMegaGeometryProxy(MakeMegaGeometryProxy(i, i + 10));
        }
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.size() == 4 && taken[0] == 0 && taken[1] == 1 && taken[2] == 2 && taken[3] == 3);
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.empty());
        // 新しい proxy の前の変換は今の変換
        assert(SamePreviousAndWorld(*view.FindMegaGeometryProxy(2)));

        // 動いた proxy は、前回公開した変換が前の変換になる。次の同期の最初に揃う
        MegaGeometryProxy moved = MakeMegaGeometryProxy(2, 12);
        moved.WorldTransform.values[12] = 5.0f;
        view.PublishMegaGeometryProxy(moved);
        const MegaGeometryProxy* found = view.FindMegaGeometryProxy(2);
        assert(found->WorldTransform.values[12] == 5.0f);
        assert(found->PreviousWorldTransform.values[12] == 0.0f);
        assert(!SamePreviousAndWorld(*found));
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.size() == 1 && taken[0] == 1);

        view.BeginMegaGeometrySync();
        assert(SamePreviousAndWorld(*view.FindMegaGeometryProxy(2)));
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.size() == 1 && taken[0] == 1);
        view.BeginMegaGeometrySync();
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.empty());

        // 同期の最初に揃えてから動かすと、前の変換は前回の同期の変換になる（毎フレーム動く proxy）
        MegaGeometryProxy movedAgain = moved;
        movedAgain.WorldTransform.values[12] = 9.0f;
        view.PublishMegaGeometryProxy(movedAgain);
        assert(view.FindMegaGeometryProxy(2)->PreviousWorldTransform.values[12] == 5.0f);
        view.BeginMegaGeometrySync();
        MegaGeometryProxy movedThird = moved;
        movedThird.WorldTransform.values[12] = 12.0f;
        view.PublishMegaGeometryProxy(movedThird);
        assert(view.FindMegaGeometryProxy(2)->PreviousWorldTransform.values[12] == 9.0f);
        view.TakeMegaGeometryChangedIndices(taken);

        // 外すと、末尾の proxy が外した位置へ入る。外した位置だけが書かれる（末尾の消えた位置は配列の大きさが運ぶ）
        view.RemoveMegaGeometryProxy(1);
        assert(view.GetMegaGeometryProxies().size() == 3);
        assert(view.GetMegaGeometryProxies()[0].ObjectId == 4);
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.size() == 1 && taken[0] == 0);
        // 末尾を外しても書かれる位置は無い
        view.RemoveMegaGeometryProxy(3);
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.empty() && view.GetMegaGeometryProxies().size() == 2);

        // 生存集合での外し（全量の同期・旧の全走査）も位置を記録する
        view.PublishMegaGeometryProxy(MakeMegaGeometryProxy(7, 17));
        view.TakeMegaGeometryChangedIndices(taken);
        Container::UnorderedSet<uint64_t> live;
        live.insert(7);
        view.RemoveStaleMegaGeometryProxies(live);
        assert(view.GetMegaGeometryProxies().size() == 1 && view.GetMegaGeometryProxies()[0].ObjectId == 7);
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.size() == 1 && taken[0] == 0);

        // クリアは世代を進め、記録を捨てる
        view.PublishMegaGeometryProxy(MakeMegaGeometryProxy(8, 18));
        const uint64_t generationBefore = view.GetSceneGeneration();
        view.ClearMegaGeometryProxies();
        assert(view.GetSceneGeneration() == generationBefore + 1);
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.empty());

        // 全量の同期（--mega-scene-full-sync）は位置を記録しない（パケットが全量を写す）
        view.SetMegaFullSync(true);
        assert(view.IsMegaFullSync());
        for (uint64_t i = 1; i <= 3; ++i)
        {
            view.UpdateMegaGeometryProxy(MakeMegaGeometryProxy(i, i + 20));
        }
        view.TakeMegaGeometryChangedIndices(taken);
        assert(taken.empty() && view.GetMegaGeometryProxies().size() == 3);

        view.Shutdown();
    }
}

int main()
{
    std::cout << "SceneViewProxyLookupTest start\n";

    TestMeshProxyLookup();
    TestMeshProxySameEntityMultiComponentCoverage();
    TestMeshVisibleCacheInvalidation();
    TestLightProxyLookup();
    TestMegaGeometryProxyLookup();
    TestMegaGeometryDeltaTracking();

    std::cout << "SceneViewProxyLookupTest passed\n";
    return 0;
}
