// 統合骨格のcold loaderと未登録所有bundle。GPU/async/cacheの受入れではない。
#include "Resource/SkeletalAssetLoader.h"
#include "Resource/SkeletalAssetLoaderTestAccess.h"
#include "SkeletalLoaderFixture.h"
#include "Asset/AssetSystem.h"
#include "Asset/AssetManifest.h"
#include "Tools/AssetCook/SkeletalBvhCook.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Object/ResourceRegistry.h"
#include "Logging/Logger.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <new>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Skeletal loader %s:%d %s\n", __FILE__, __LINE__, #x);                                \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace A = Core::Asset;
namespace R = Core::ResourceIO;
namespace F = NorvesLib::Tests::SkeletalLoaderFixture;
namespace Cook = NorvesLib::Tools::AssetCook;
using Bytes = C::VariableArray<uint8_t>;
namespace
{
    struct Fixture
    {
        std::filesystem::path Root;
        Fixture()
        {
            char name[80];
            std::snprintf(name, sizeof(name), "norves-loader-%lld",
                          static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            Root = std::filesystem::temp_directory_path() / name;
            CHECK(std::filesystem::create_directory(Root));
        }
        ~Fixture()
        {
            std::error_code e;
            std::filesystem::remove_all(Root, e);
        }
        void Write(const char* name, const Bytes& bytes)
        {
            std::ofstream f(Root / name, std::ios::binary | std::ios::trunc);
            CHECK(f);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            f.close();
            CHECK(!f.fail());
        }
        C::TSharedPtr<A::AssetSystem> Assets(const Bytes& payload, const char* logical = "Actors/Animal",
                                             const char* format = "nvskel.v0.skinned.pnujiw.u32",
                                             bool wrongType = false, bool badOuterHash = false, int metadataMode = 0)
        {
            uint64_t hash = 0;
            const auto type = wrongType ? A::MakeAssetPackageFourCC('M', 's', 'h', '0')
                                        : A::MakeAssetPackageFourCC('S', 'k', 'l', '0');
            const auto package = F::Package(payload, type, hash);
            Write("asset.nvpkg", package);
            auto assets = C::MakeShared<A::AssetSystem>(C::AnsiString(Root.generic_string().c_str()));
            const auto manifest =
                C::AnsiString("{\"version\":1,\"assets\":[{\"logical_path\":\"") + logical +
                "\",\"kind\":\"model\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"" +
                format + "\",\"cooked_package\":\"asset.nvpkg\",\"entry_name\":\"a\",\"entry_type\":\"" +
                (wrongType ? "Msh0" : "Skl0") + "\",\"cooked_hash\":\"" +
                A::FormatAssetHashHex(badOuterHash ? hash ^ 1 : hash) + "\",\"cooked_version\":0" +
                (metadataMode
                     ? (C::AnsiString(",\"metadata\":{\"vertex_count\":") + (metadataMode == 1 ? "4" : "3") +
                        ",\"index_count\":6,\"joint_count\":2,\"clip_count\":2" +
                        (metadataMode >= 2 ? C::AnsiString(",\"submesh_count\":") + (metadataMode == 3 ? "1" : "2") +
                                                 ",\"material_slot_count\":" + (metadataMode == 4 ? "1" : "2")
                                           : C::AnsiString{}) +
                        "}")
                     : C::AnsiString{}) +
                "}]}";
            const bool loaded = assets->LoadManifestFromJsonText(F::CoreText(manifest));
            if (!loaded)
            {
                std::fprintf(stderr, "Loader fixture manifest: %s\n", manifest.c_str());
            }
            CHECK(loaded);
            return assets;
        }
    };
    struct Sink : Core::Logging::ILogSink
    {
        C::VariableArray<C::String> Messages;
        void OnLog(const Core::Logging::LogEntry& entry) override
        {
            if (entry.category == "AssetLoadProfile")
            {
                Messages.push_back(entry.message);
            }
        }
        bool Has(const char* stage, const char* outcome) const
        {
            for (const auto& m : Messages)
            {
                if (m.find(stage) != C::String::npos && m.find(outcome) != C::String::npos)
                {
                    return true;
                }
            }
            return false;
        }
    };
    void SameData(const A::CookedSkeletalData& before, const Core::SkeletalAssetResource& asset)
    {
        const auto& d = before.Skeletal;
        CHECK(asset.GetClipCount() == d.Clips.size());
        CHECK(asset.GetMesh()->GetVertices().size() == d.Vertices.size() && asset.GetMesh()->GetIndices() == d.Indices);
        CHECK(asset.GetMesh()->GetMeshNodeGlobalTransform() == d.MeshNodeGlobalTransform);
        for (size_t i = 0; i < d.Vertices.size(); ++i)
        {
            const auto& a = d.Vertices[i];
            const auto& b = asset.GetMesh()->GetVertices()[i];
            CHECK(a.Position.X == b.Position.X && a.Position.Y == b.Position.Y && a.Position.Z == b.Position.Z);
            CHECK(a.Normal.X == b.Normal.X && a.Normal.Y == b.Normal.Y && a.Normal.Z == b.Normal.Z);
            CHECK(a.TexCoord.U == b.TexCoord.U && a.TexCoord.V == b.TexCoord.V && a.JointIndices == b.JointIndices &&
                  a.JointWeights == b.JointWeights);
        }

        CHECK(asset.GetMesh()->GetSubMeshes().size() == d.SubMeshes.size());
        CHECK(asset.GetMesh()->GetMaterialSlots().size() == d.MaterialSlots.size());
        CHECK(asset.GetSkeleton()->GetJoints().size() == d.Joints.size());
        for (size_t i = 0; i < d.SubMeshes.size(); ++i)
        {
            const auto& a = d.SubMeshes[i];
            const auto& b = asset.GetMesh()->GetSubMeshes()[i];
            CHECK(a.IndexStart == b.IndexStart && a.IndexCount == b.IndexCount && a.MaterialSlot == b.MaterialSlot &&
                  a.VertexCount == b.VertexCount && a.bNoShadow == b.bNoShadow && a.BoundsRadius == b.BoundsRadius);
            for (size_t j = 0; j < 3; ++j)
            {
                CHECK(a.BoundsCenter[j] == b.BoundsCenter[j]);
            }
        }
        for (size_t i = 0; i < d.MaterialSlots.size(); ++i)
        {
            CHECK(d.MaterialSlots[i].Name == asset.GetMesh()->GetMaterialSlots()[i].Name);
        }
        for (size_t i = 0; i < d.Joints.size(); ++i)
        {
            const auto& a = d.Joints[i];
            const auto& b = asset.GetSkeleton()->GetJoints()[i];
            CHECK(a.Name == b.Name && a.ParentIndex == b.ParentIndex && a.InverseBindMatrix == b.InverseBindMatrix);
        }

        for (size_t i = 0; i < d.Clips.size(); ++i)
        {
            CHECK(Cook::Detail::EqualBvhCookClip(d.Clips[i], asset.GetClip(i)->GetClip()));
        }
    }
    void SampleLiteral(const Core::SkeletalAssetResource& asset, C::StringView name, float time, bool rotated)
    {
        const auto clip = asset.GetClip(name);
        CHECK(clip);
        const auto& v = asset.GetMesh()->GetMeshNodeGlobalTransform();
        NorvesLib::Math::Matrix4x4 matrix(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11],
                                          v[12], v[13], v[14], v[15]);
        Core::Animation::SkeletalPoseSnapshot pose;
        CHECK(Core::Animation::SkeletalAnimationSampler::Sample(*asset.GetSkeleton(), *clip, *asset.GetMesh(), time,
                                                                matrix, pose));
        CHECK(pose.JointModelMatrices.size() == 2 && pose.BonePalette.size() == 2);
        CHECK(std::abs(pose.JointModelMatrices[0].values[0] - (rotated ? -1.f : 1.f)) < 1e-5f);
        CHECK(std::abs(pose.JointModelMatrices[1].values[13] - (rotated ? -3.f : 2.f)) < 1e-5f);
        CHECK(std::abs(pose.BonePalette[1].values[13] - (rotated ? -2.f : 1.f)) < 1e-5f);
    }
    bool LoadSuccess(const R::CookedSkeletalLoadPlan& plan, R::CookedSkeletalCpuAsset& out,
                     R::SkeletalAssetLoadReport& report)
    {
        const bool loaded = R::LoadCookedSkeletalForWorker(plan, out, report);
        if (!loaded)
        {
            std::fprintf(stderr, "Loader positive failure: status=%u resolve=%u parse=%u path=%s\n",
                         static_cast<unsigned>(report.Status), static_cast<unsigned>(report.ResolveStatus),
                         static_cast<unsigned>(report.ParseStatus), plan.LogicalPath.c_str());
        }
        return loaded;
    }
    void Run()
    {
        Fixture fixture;
        Core::ResourceRegistry registry;
        CHECK(registry.Initialize());
        auto existing = registry.CreateTransient<Core::AnimationClipResource>("existing");
        CHECK(existing);
        const auto handle = registry.GetHandle<Core::AnimationClipResource>(existing->GetResourceId());
        CHECK(handle.IsValid());
        const auto resources = registry.GetResourceCount(), paths = registry.GetCachedPathCount();
        const R::SkeletalAssetCreateContext context{&registry, NorvesLib::Thread::Thread::GetCurrentThreadId()};
        R::CookedSkeletalCpuAsset cpu;
        R::SkeletalAssetLoadReport report;
        C::TSharedPtr<Core::SkeletalAssetResource> asset;
        const auto current = F::BuildGoldenSkeletalV02();
        auto snapshot = fixture.Assets(current);
        CHECK(LoadSuccess({snapshot, "Actors/./Animal"}, cpu, report));
        CHECK(cpu.GetLogicalPath() == C::AnsiStringView("Actors/Animal") &&
              report.Status == R::SkeletalAssetLoadStatus::Success);
        const auto parsed = cpu.GetData();
        CHECK(parsed && parsed->VersionMinor == 2);
        CHECK(R::AssembleCookedSkeletalAsset(cpu, context, asset, report));
        CHECK(report.CreatedResources == 5 && asset->GetResourceId() != 0 && asset->GetMesh()->GetResourceId() != 0);
        CHECK(registry.GetResourceCount() == resources && registry.GetCachedPathCount() == paths &&
              registry.Resolve(handle) == existing);
        SameData(*parsed, *asset);
        SampleLiteral(*asset, C::StringView(_T("Wave")), 1.f, false);
        CHECK(asset->GetClipCount() == 2 && !asset->GetClip(C::StringView{}) &&
              !asset->GetClip(C::StringView(_T("missing"))));
        const auto secondName = asset->GetClip(size_t{1})->GetClip().Name;
        SampleLiteral(*asset, secondName, 1.f, true);
        C::TSharedPtr<Core::SkeletalAssetResource> repeated;
        CHECK(R::AssembleCookedSkeletalAsset(cpu, context, repeated, report));
        CHECK(repeated != asset && repeated->GetResourceId() != asset->GetResourceId());
        repeated.reset();
        // workerでは所有解析だけ。owner不一致の組立は入力と既存結果を保持する。
        R::CookedSkeletalCpuAsset workerCpu;
        R::SkeletalAssetLoadReport workerReport;
        bool loaded = false, rejected = false;
        NorvesLib::Thread::Thread worker(
            [&]()
            {
                loaded = R::LoadCookedSkeletalForWorker({snapshot, "Actors/Animal"}, workerCpu, workerReport);
                auto unchanged = asset;
                rejected = !R::AssembleCookedSkeletalAsset(workerCpu, context, unchanged, workerReport) &&
                           unchanged == asset && workerReport.Status == R::SkeletalAssetLoadStatus::WrongOwnerThread;
            });
        worker.Join();
        CHECK(loaded && rejected && workerCpu.GetData());
        CHECK(registry.GetResourceCount() == resources);
        auto invalidContext = context;
        invalidContext.OwnerThread = {};
        auto old = asset;
        CHECK(!R::AssembleCookedSkeletalAsset(cpu, invalidContext, asset, report) && asset == old);
        Core::ResourceRegistry uninitialized;
        invalidContext = context;
        invalidContext.Registry = &uninitialized;
        CHECK(!R::AssembleCookedSkeletalAsset(cpu, invalidContext, asset, report) && asset == old);
        R::CookedSkeletalCpuAsset empty;
        CHECK(!R::AssembleCookedSkeletalAsset(empty, context, asset, report) && asset == old);
        struct Fault
        {
            uint32_t FailAt = 0, Seen = 0;
            bool bThrow = false, bBreakLoad = false;
            C::VariableArray<C::TWeakPtr<Core::Resource>> Weak;
        };
        const auto probe =
            +[](R::Detail::SkeletalCreatePoint, uint32_t, const C::TSharedPtr<Core::Resource>& value, void* ptr)
        {
            auto& f = *static_cast<Fault*>(ptr);
            f.Weak.push_back(value);
            ++f.Seen;
            if (f.Seen != f.FailAt)
            {
                return true;
            }
            if (f.bThrow)
            {
                throw std::bad_alloc();
            }
            if (f.bBreakLoad)
            {
                auto mesh = C::StaticPointerCast<Core::SkinnedMeshResource>(f.Weak[0].lock());
                CHECK(mesh);
                mesh->SetIndices({});
                CHECK(!mesh->Load());
                return true;
            }
            return false;
        };
        for (bool throwing : {false, true})
        {
            for (uint32_t fail = 1; fail <= 5; ++fail)
            {
                Fault fault;
                fault.FailAt = fail;
                fault.bThrow = throwing;
                CHECK(!R::Detail::AssembleCookedSkeletalAssetWithProbe(cpu, context, asset, report, probe, &fault));
                CHECK(asset == old && cpu.GetData() == parsed && fault.Seen == fail);
                CHECK(registry.GetResourceCount() == resources && registry.GetCachedPathCount() == paths &&
                      registry.Resolve(handle) == existing);
                for (const auto& weak : fault.Weak)
                {
                    CHECK(weak.expired());
                }
            }
        }
        Fault broken;
        broken.FailAt = 5;
        broken.bBreakLoad = true;
        CHECK(!R::Detail::AssembleCookedSkeletalAssetWithProbe(cpu, context, asset, report, probe, &broken));
        CHECK(report.Status == R::SkeletalAssetLoadStatus::ResourceLoadFailed && asset == old);
        CHECK(cpu.GetData()->Skeletal.Indices.size() == 6);
        for (const auto& weak : broken.Weak)
        {
            CHECK(weak.expired());
        }
        const auto reject = [&](const C::TSharedPtr<A::AssetSystem>& input, const char* path = "Actors/Animal")
        {
            CHECK(!R::LoadCookedSkeletalForWorker({input, path}, cpu, report));
            CHECK(cpu.GetData() == parsed && asset == old);
            CHECK(registry.GetResourceCount() == resources && registry.GetCachedPathCount() == paths);
        };
        reject(fixture.Assets(current, "Actors/Animal", "wrong.format"));
        reject(fixture.Assets(current, "Actors/Animal", "nvskel.v0.skinned.pnujiw.u32", true));
        reject(fixture.Assets(current, "Actors/Animal", "nvskel.v0.skinned.pnujiw.u32", false, true));
        reject(fixture.Assets(current, "Actors/Animal", "nvskel.v0.skinned.pnujiw.u32", false, false, true));
        R::CookedSkeletalCpuAsset known;
        auto knownSnapshot = fixture.Assets(current, "Actors/Animal", "nvskel.v0.skinned.pnujiw.u32", false, false, 2);
        CHECK(LoadSuccess({knownSnapshot, "Actors/Animal"}, known, report));
        CHECK(known.GetData() && known.GetData()->Skeletal.SubMeshes.size() == 2);
        for (int mode : {3, 4})
        {
            reject(fixture.Assets(current, "Actors/Animal", "nvskel.v0.skinned.pnujiw.u32", false, false, mode));
            CHECK(report.Status == R::SkeletalAssetLoadStatus::MetadataMismatch);
        }
        auto bad = current;
        bad[0] ^= 1;
        reject(fixture.Assets(bad)); // 外側のhashはこの破損payloadへ正しく付け直す。
        auto missing = C::MakeShared<A::AssetSystem>(C::AnsiString(fixture.Root.generic_string().c_str()));
        fixture.Write("loose.nvskel", current);
        reject(missing, "loose.nvskel");
        snapshot = fixture.Assets(current);
        reject(snapshot, "loose.nvskel");
        CHECK(std::filesystem::remove(fixture.Root / "asset.nvpkg"));
        reject(snapshot);
        reject({});
        // 旧minorのpayloadを現在の汎用loaderに通す。表の省略はそのまま保持。
        for (uint16_t minor : {uint16_t{0}, uint16_t{1}})
        {
            auto bytes = F::BuildGoldenSkeletal();
            if (minor == 0)
            {
                F::WriteLe16(bytes, 14, 0);
                std::memset(bytes.data() + 192, 0, 64);
                F::RecomputeSkeletalHash(bytes);
            }
            R::CookedSkeletalCpuAsset legacy;
            auto legacySnapshot = fixture.Assets(bytes, "Other/Legacy");
            CHECK(LoadSuccess({legacySnapshot, "Other/Legacy"}, legacy, report));
            CHECK(legacy.GetData()->VersionMinor == minor);
            C::TSharedPtr<Core::SkeletalAssetResource> loadedAsset;
            CHECK(R::AssembleCookedSkeletalAsset(legacy, context, loadedAsset, report));
            SameData(*legacy.GetData(), *loadedAsset);
            SampleLiteral(*loadedAsset, C::StringView(_T("Wave")), 1.f, false);
        }
        const auto triple = F::BuildThreeClips();
        auto tripleSnapshot = fixture.Assets(triple, "Actors/Three");
        R::CookedSkeletalCpuAsset tripleCpu;
        CHECK(LoadSuccess({tripleSnapshot, "Actors/Three"}, tripleCpu, report));
        C::TSharedPtr<Core::SkeletalAssetResource> tripleAsset;
        CHECK(R::AssembleCookedSkeletalAsset(tripleCpu, context, tripleAsset, report));
        CHECK(tripleAsset->GetClipCount() == 3 && report.CreatedResources == 6);
        SameData(*tripleCpu.GetData(), *tripleAsset);
        SampleLiteral(*tripleAsset, C::StringView(_T("Idle")), 1.f, false);
        // 名前順を入れ替えても選択結果は同じ。各clipのchannel範囲は元のものを保つ。
        auto reordered = current;
        for (size_t i = 0; i < 32; ++i)
        {
            std::swap(reordered[704 + i], reordered[736 + i]);
        }
        F::WriteLe32(reordered, 720, 0);
        F::WriteLe32(reordered, 752, 2);
        for (size_t i = 0; i < 64; ++i)
        {
            std::swap(reordered[768 + i], reordered[832 + i]);
        }
        for (size_t i = 0; i < 128; ++i)
        {
            std::swap(reordered[896 + i], reordered[1024 + i]);
        }
        for (size_t i = 0; i < 4; ++i)
        {
            F::WriteLe32(reordered, 768 + i * 32 + 12, static_cast<uint32_t>(i * 2));
        }
        F::RecomputeSkeletalHash(reordered);
        R::CookedSkeletalCpuAsset ordered;
        auto orderSnapshot = fixture.Assets(reordered);
        CHECK(LoadSuccess({orderSnapshot, "Actors/Animal"}, ordered, report));
        C::TSharedPtr<Core::SkeletalAssetResource> orderedAsset;
        CHECK(R::AssembleCookedSkeletalAsset(ordered, context, orderedAsset, report));
        SampleLiteral(*orderedAsset, C::StringView(_T("Wave")), 1.f, false);
        SampleLiteral(*orderedAsset, secondName, 1.f, true);
        // duplicate/emptyはload成功・名前引き失敗。clip0 fallbackや自動renameを足さない。
        for (bool duplicate : {false, true})
        {
            auto names = current;
            F::WriteLe64(names, 736, 9);
            F::WriteLe32(names, 744, duplicate ? 4 : 0);
            F::RecomputeSkeletalHash(names);
            R::CookedSkeletalCpuAsset named;
            auto namedSnapshot = fixture.Assets(names);
            CHECK(LoadSuccess({namedSnapshot, "Actors/Animal"}, named, report));
            C::TSharedPtr<Core::SkeletalAssetResource> namedAsset;
            CHECK(R::AssembleCookedSkeletalAsset(named, context, namedAsset, report));
            CHECK(!namedAsset->GetClip(C::StringView{}));
            CHECK(duplicate ? !namedAsset->GetClip(C::StringView(_T("Wave")))
                            : bool(namedAsset->GetClip(C::StringView(_T("Wave")))));
        }
        C::TWeakPtr<Core::SkinnedMeshResource> weakMesh = asset->GetMesh();
        C::TWeakPtr<Core::SkeletonResource> weakSkeleton = asset->GetSkeleton();
        C::TWeakPtr<Core::AnimationClipResource> weakClip = asset->GetClip(size_t{1});
        auto lease = asset->GetMesh()->GetRenderAssetLease();
        CHECK(lease && lease->GetVertices().size() == 3);
        cpu = {};
        workerCpu = {};
        snapshot.reset();
        old.reset();
        SampleLiteral(*asset, C::StringView(_T("Wave")), 1.f, false);
        asset.reset();
        CHECK(weakMesh.expired() && weakSkeleton.expired() && weakClip.expired());
        CHECK(lease->GetVertices().size() == 3 && lease->GetIndices().size() == 6);
        lease.reset();
        CHECK(registry.GetResourceCount() == resources && registry.Resolve(handle) == existing);
    }
} // namespace
int main()
{
    auto& logger = Core::Logging::Logger::GetInstance();
    Core::Logging::LogConfig config;
    config.bAsyncLogging = false;
    config.outputType = Core::Logging::LogOutput::None;
    CHECK(logger.Initialize(config));
    Sink sink;
    logger.AddSink(&sink);
    Run();
    logger.Flush();
#if NORVES_ENABLE_LOGGING
    for (const char* stage :
         {"stage=skeletal_asset_resolve", "stage=skeletal_cooked_parse", "stage=skeletal_resource_create"})
    {
        CHECK(sink.Has(stage, "success=1") && sink.Has(stage, "success=0"));
    }
    std::puts("SKELETAL_ASSET_LOADER_PROFILE result=pass resolve_parse_create_success_failure");
#endif
    logger.RemoveSink(&sink);
    logger.Shutdown();
    std::puts(
        "SKELETAL_ASSET_LOADER result=pass cooked_only_owned_parse_owner_thread_unregistered_all_clips_failure_lifetime_sampler_no_async_cache_gpu");
    return 0;
}
