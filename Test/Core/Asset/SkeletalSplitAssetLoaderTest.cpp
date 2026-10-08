// 実NVPK/AssetSystemを通すcold-load。一manifest世代のCPU結果から実poseを評価する。
#include "RigSplitTestFixture.h"
#include "Object/ResourceRegistry.h"
#include "Animation/SkeletalAnimationSampler.h"
#include <cmath>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace X = NorvesLib::Tests::RigSplitFixture;
namespace Core = NorvesLib::Core;
namespace S = Core::Skeletal;
namespace C = Core::Container;
namespace R = Core::ResourceIO;
namespace A = Core::Animation;
namespace T = NorvesLib::Thread;
namespace
{
    void Load(F::Fixture& f)
    {
        const auto cooked = X::CookSource(f.Json);
        X::WritePackages(cooked);
        auto request = X::Request();
        request.BankPath = "Animations/Walk.nvclip";
        const auto second = X::CookSource(F::Replace(f.Json, "\"name\":\"Wave\"", "\"name\":\"Walk\""), request);
        X::WriteEntry(second.Bank);
        const F::Text marker = "{\"logical_path\":\"Animations/Walk.nvclip\"";
        const auto start = second.ManifestJson.find(C::AnsiStringView(marker.data(), marker.size()));
        RIG_CHECK(start != F::Text::npos);
        const auto json =
            cooked.ManifestJson.substr(0, cooked.ManifestJson.size() - 2) + "," + second.ManifestJson.substr(start);
        auto plan = X::LoadPlan(f, json);
        plan.BankPaths.push_back(request.BankPath);
        std::filesystem::remove("RigV1Fixture/fixture.bin");
        S::CookedRigSplitCpuAsset cpu;
        R::RigSplitLoadReport report;
        RIG_CHECK(R::LoadRigSplitForWorker(plan, cpu, report) && cpu.GetData()->Clips.size() == 2 &&
                  report.PackageBytesRead > 0);
        const auto total = report.PackageBytesRead;
        Core::ResourceRegistry registry;
        RIG_CHECK(registry.Initialize());
        C::TSharedPtr<Core::SkeletalAssetResource> asset;
        S::RigSplitReport assembled;
        RIG_CHECK(S::AssembleRigSplitV1(cpu, {&registry, T::Thread::GetCurrentThreadId()}, asset, assembled));
        auto clip = asset->GetClip(C::StringView(_T("Walk")));
        RIG_CHECK(clip);
        A::SkeletalPoseSnapshot pose;
        RIG_CHECK(A::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *clip, *asset->GetMesh(), 1,
                                                      F::MeshMatrix(*asset->GetMesh()), pose));
        const auto vertex =
            A::SkeletalAnimationSampler::SkinVertex(asset->GetMesh()->GetVertices()[2], pose.BonePalette);
        RIG_CHECK(std::abs(vertex.Position.x + 5) < 1e-5 && std::abs(vertex.Position.y - 2) < 1e-5);
        const auto* stable = cpu.GetData();
        auto limited = plan;
        limited.MaxTotalPackageBytes = total - 1;
        RIG_CHECK(!R::LoadRigSplitForWorker(limited, cpu, report) && cpu.GetData() == stable &&
                  report.PackageReadStatus == Core::Asset::AssetReadStatus::SizeTooLarge);
        limited.MaxTotalPackageBytes = total;
        RIG_CHECK(R::LoadRigSplitForWorker(limited, cpu, report));
        limited = plan;
        limited.MaxPackageBytes = 1;
        RIG_CHECK(!R::LoadRigSplitForWorker(limited, cpu, report) && report.PackageBytesRead == 0);
        auto bad = X::LoadPlan(f, F::Replace(json, "\"joint_count\":2", "\"joint_count\":1"));
        RIG_CHECK(!R::LoadRigSplitForWorker(bad, cpu, report) &&
                  report.Status == R::RigSplitLoadStatus::MetadataMismatch);
        auto missing = plan;
        missing.BankPaths[0] = "Animations/Missing.nvclip";
        RIG_CHECK(!R::LoadRigSplitForWorker(missing, cpu, report) &&
                  report.Status == R::RigSplitLoadStatus::ResolveRejected);
        auto duplicate = plan;
        duplicate.BankPaths[1] = duplicate.BankPaths[0];
        RIG_CHECK(!R::LoadRigSplitForWorker(duplicate, cpu, report) &&
                  report.Status == R::RigSplitLoadStatus::InvalidRequest);
        Core::Asset::AssetManifest manifest;
        RIG_CHECK(!manifest.LoadFromJsonText(C::String(F::Replace(json, ",\"profile\":1", "").c_str())));
        RIG_CHECK(!manifest.LoadFromJsonText(
            C::String(F::Replace(json, "\"kind\":\"skeleton\"", "\"kind\":\"model\"").c_str())));
        const char* output = std::getenv("NORVES_RIG_SPLIT_V1_OUTPUT");
        if (output && *output)
        {
            std::filesystem::create_directories(output);
            char measured[512]{};
            std::snprintf(
                measured, sizeof(measured),
                "{\"schema\":1,\"clips\":%llu,\"child_palette_y\":%.9g,\"child_model_y\":%.9g,\"vertex_x\":%.9g,\"vertex_y\":%.9g,\"materials_render_staged\":%s}",
                static_cast<unsigned long long>(asset->GetClipCount()), double(pose.BonePalette[0].m31),
                double(pose.JointModelMatrices[0].m31), double(vertex.Position.x), double(vertex.Position.y),
                cooked.bMaterialsRenderStaged ? "true" : "false");
            const F::Text value(measured);
            F::Bytes bytes;
            bytes.insert(bytes.end(), value.begin(), value.end());
            F::WriteBytes(std::filesystem::path(output) / "split-pose.json", bytes);
        }
        asset.reset();
        clip.reset();
        RIG_CHECK(registry.GetResourceCount() == 0);
        registry.Shutdown();
        f.SetBuffer(f.Binary);
        std::printf("RIG_SPLIT_CASE package_snapshot_cpu_pose result=pass\n");
    }
} // namespace
int main()
{
    F::Fixture fixture;
    Load(fixture);
    std::printf(
        "RIG_SPLIT_LOADER result=pass real_packages_one_snapshot_ordered_banks_preallocation_budget_owner_pose_no_runtime_publish\n");
    return 0;
}
