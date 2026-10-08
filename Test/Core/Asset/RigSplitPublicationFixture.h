#pragma once
// 実三role package、順序付き3Bank、別Mesh、legacyを同じsnapshotへ配置する。
#include "RigSplitTestFixture.h"
#include "SkeletalLoaderFixture.h"
#include "Resource/RigSplitPublicationIdentity.h"
#include "Resource/SkeletalAssetPublication.h"
#include "Animation/SkeletalAnimationSampler.h"
#include <cmath>
namespace NorvesLib::Tests::RigSplitPublicationFixture
{
    namespace F = RigV1Fixture;
    namespace X = RigSplitFixture;
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    namespace R = Core::ResourceIO;
    namespace A = Core::Asset;
    inline F::Text Record(const F::Text& json, const F::Text& path)
    {
        const F::Text token = F::Text("{\"logical_path\":\"") + path + "\"";
        const auto start = json.find(C::AnsiStringView(token.data(), token.size()));
        RIG_CHECK(start != F::Text::npos);
        bool quoted = false, escaped = false;
        int depth = 0;
        for (size_t i = start; i < json.size(); ++i)
        {
            const char ch = json[i];
            if (quoted)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (ch == '\\')
                {
                    escaped = true;
                }
                else if (ch == '"')
                {
                    quoted = false;
                }
            }
            else if (ch == '"')
            {
                quoted = true;
            }
            else if (ch == '{')
            {
                ++depth;
            }
            else if (ch == '}' && --depth == 0)
            {
                return json.substr(start, i - start + 1);
            }
        }
        RIG_CHECK(false);
        return {};
    }
    struct Fixture
    {
        F::Fixture Files;
        F::Text Json;
        C::TSharedPtr<A::AssetSystem> Snapshot;
        S::RigSplitRequest Request;
        explicit Fixture(bool mismatch = false)
        {
            const auto paths = X::Request();
            Request.SkeletonPath = paths.SkeletonPath;
            Request.MeshPath = paths.MeshPath;
            const auto target = mismatch ? F::ChildTrs(Files.Json, "\"translation\":[0,2,0]") : Files.Json;
            const auto cooked = X::CookSource(target);
            X::WritePackages(cooked);
            Json = "{\"version\":1,\"assets\":[";
            Json += Record(cooked.ManifestJson, paths.SkeletonPath) + "," + Record(cooked.ManifestJson, paths.MeshPath);
            for (const char* name : {"Wave", "Walk", "Run"})
            {
                auto request = paths;
                request.BankPath = F::Text("Animations/") + name + ".nvclip";
                const auto bank = X::CookSource(
                    F::Replace(Files.Json, "\"name\":\"Wave\"", F::Text("\"name\":\"") + name + "\""), request);
                X::WriteEntry(bank.Bank);
                Json += "," + Record(bank.ManifestJson, request.BankPath);
                Request.BankPaths.push_back(request.BankPath);
            }
            auto other = paths;
            other.MeshPath = "Models/Mesh2.nvskel";
            const auto second =
                X::CookSource(F::Replace(target, "\"translation\":[5,0,0]", "\"translation\":[6,0,0]"), other);
            X::WriteEntry(second.Mesh);
            Json += "," + Record(second.ManifestJson, other.MeshPath);
            // 旧統合modeも同じruntimeのready budgetを使う。
            const auto payload = SkeletalLoaderFixture::BuildThreeClips();
            uint64_t hash = 0;
            const auto package =
                SkeletalLoaderFixture::Package(payload, A::MakeAssetPackageFourCC('S', 'k', 'l', '0'), hash);
            F::WriteBytes("legacy.nvpkg", package);
            Json +=
                ",{\"logical_path\":\"Actors/Legacy\",\"kind\":\"model\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"nvskel.v0.skinned.pnujiw.u32\",\"cooked_package\":\"legacy.nvpkg\",\"entry_name\":\"a\",\"entry_type\":\"Skl0\",\"cooked_hash\":\"";
            Json += A::FormatAssetHashHex(hash) + "\",\"cooked_version\":0}]}";
            Snapshot = MakeSnapshot(Json);
        }
        C::TSharedPtr<A::AssetSystem> MakeSnapshot(const F::Text& json) const
        {
            auto assets = C::MakeShared<A::AssetSystem>(C::AnsiString(Files.Root.generic_string().c_str()));
            RIG_CHECK(assets->LoadManifestFromJsonText(SkeletalLoaderFixture::CoreText(json)));
            return assets;
        }
    };
    inline R::SkeletalAssetCreateContext Context(Core::ResourceRegistry& registry)
    {
        return {&registry, Thread::Thread::GetCurrentThreadId()};
    }
    inline R::RigSplitRequestIdentity Identity(const Fixture& f, const R::SkeletalCacheDomain& domain,
                                               const S::RigSplitRequest& request, uint64_t generation = 1)
    {
        R::RigSplitRequestIdentity result;
        RIG_CHECK(R::BuildRigSplitRequestIdentity(request, f.Snapshot, domain.Session, domain.Ordinal, generation,
                                                  R::RigSplitMaximumKeyBytes,
                                                  result) == R::RigSplitIdentityStatus::Success);
        return result;
    }
    inline C::TSharedPtr<const R::RigSplitPublicationReceipt> Load(const R::RigSplitRequestIdentity& identity)
    {
        C::TSharedPtr<const R::RigSplitPublicationReceipt> receipt;
        Core::RigSplitAssetDiagnostics diagnostics;
        RIG_CHECK(R::LoadRigSplitForPublication(identity, receipt, diagnostics));
        RIG_CHECK(receipt && receipt->GetEvidence().Entries.size() == identity.GetData()->References.size());
        return receipt;
    }
    inline R::SkeletalPublicationLimits Limits()
    {
        R::SkeletalPublicationLimits result;
        result.MaxKeyBytes = R::RigSplitMaximumKeyBytes;
        return result;
    }
    inline void Pose(const C::TSharedPtr<Core::SkeletalAssetResource>& asset, float expectedX = -5)
    {
        RIG_CHECK(asset && asset->IsLoaded() && asset->GetClipCount() == 3 && !asset->GetMesh()->GetRenderAssetLease());
        const auto clip = asset->GetClip(C::StringView(_T("Walk")));
        RIG_CHECK(clip);
        Core::Animation::SkeletalPoseSnapshot pose;
        RIG_CHECK(Core::Animation::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *clip, *asset->GetMesh(), 1,
                                                                    F::MeshMatrix(*asset->GetMesh()), pose));
        const auto vertex =
            Core::Animation::SkeletalAnimationSampler::SkinVertex(asset->GetMesh()->GetVertices()[2], pose.BonePalette);
        RIG_CHECK(std::abs(vertex.Position.x - expectedX) < 1e-5 && std::abs(vertex.Position.y - 2) < 1e-5);
    }
    inline void Handles(Core::ResourceRegistry& registry, const R::SkeletalPublishedAsset& value)
    {
        RIG_CHECK(registry.Resolve(value.AggregateHandle) == value.Asset);
        RIG_CHECK(registry.Resolve(value.MeshHandle) == value.Asset->GetMesh());
        RIG_CHECK(registry.Resolve(value.SkeletonHandle) == value.Asset->GetSkeleton());
        RIG_CHECK(value.ClipHandles.size() == 3);
        for (size_t i = 0; i < 3; ++i)
        {
            RIG_CHECK(registry.Resolve(value.ClipHandles[i]) == value.Asset->GetClip(i));
        }
    }
} // namespace NorvesLib::Tests::RigSplitPublicationFixture
