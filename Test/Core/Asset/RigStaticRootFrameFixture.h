#pragma once
// 二段の静的祖先を持つ権利独立の合成入力。旧literalや元fixtureは変更しない。
#include "RigSplitTestFixture.h"
#include "RigSplitPublicationFixture.h"
#include "Animation/RigRootFrame.h"
#include "Animation/RigBoundClipProof.h"
#include "Animation/SkeletalAnimationSampler.h"
namespace NorvesLib::Tests::RigStaticRootFrameFixture
{
    namespace F = RigV1Fixture;
    namespace X = RigSplitFixture;
    namespace P = RigSplitPublicationFixture;
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    namespace R = Core::ResourceIO;
    inline constexpr auto Profile = S::RigImportProfile::StaticRootFrame128;
    inline F::Text Armature(const F::Text& base)
    {
        auto json = F::Replace(base, "\"nodes\":[0,2]", "\"nodes\":[4,2]");
        json = F::Replace(
            json, "\"skin\":0}],\"buffers\":",
            "\"skin\":0},{\"name\":\"Armature\",\"children\":[0],\"translation\":[2,3,0],\"rotation\":[0,0,1,0],\"scale\":[2,2,2]},{\"name\":\"WorldFrame\",\"children\":[3],\"translation\":[-1,4,1],\"rotation\":[1,0,0,0]}],\"buffers\":");
        return F::Replace(json, "\"skeleton\":0,", "");
    }
    inline S::RigRootFrame ExpectedFrame(float scale = 1)
    {
        return {-2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -2, 0, scale, scale, scale, 1};
    }
    inline S::RigAuthoringCpu Import(const F::Text& json,
                                     const Core::AssetImport::LoadedImportSettings* settings = nullptr)
    {
        S::RigAuthoringCpu out;
        S::RigV1Report report;
        RIG_CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", Profile, out,
                                                             report, {}, settings));
        return out;
    }
    inline Tools::AssetCook::RigSplitCookRequest CookRequest()
    {
        auto q = X::Request();
        q.Profile = Profile;
        return q;
    }
    inline Tools::AssetCook::RigSplitCookResult Cook(const F::Text& json)
    {
        return X::CookSource(json, CookRequest());
    }
    inline R::RigSplitLoadPlan Plan(const F::Fixture& f, const F::Text& json)
    {
        auto q = X::LoadPlan(f, json);
        q.Profile = Profile;
        return q;
    }
    inline S::RigSplitRequest Request()
    {
        const auto r = CookRequest();
        S::RigSplitRequest q;
        q.SkeletonPath = r.SkeletonPath;
        q.MeshPath = r.MeshPath;
        q.BankPaths.push_back(r.BankPath);
        q.Profile = Profile;
        return q;
    }
    inline F::Bytes Glb(F::Text json, const F::Bytes& buffer)
    {
        json = F::Replace(json, "\"uri\":\"fixture.bin\",", "");
        while (json.size() % 4)
        {
            json += " ";
        }
        F::Bytes out;
        X::U32(out, 0x46546c67);
        X::U32(out, 2);
        X::U32(out, uint32_t(12 + 8 + json.size() + 8 + buffer.size()));
        X::U32(out, uint32_t(json.size()));
        X::U32(out, 0x4e4f534a);
        out.insert(out.end(), json.begin(), json.end());
        X::U32(out, uint32_t(buffer.size()));
        X::U32(out, 0x004e4942);
        out.insert(out.end(), buffer.begin(), buffer.end());
        return out;
    }
    inline Core::Animation::SkeletalPoseSnapshot Pose(const C::TSharedPtr<Core::SkeletalAssetResource>& asset,
                                                      float scale = 1)
    {
        RIG_CHECK(asset && asset->IsLoaded() && !asset->GetMesh()->GetRenderAssetLease());
        const auto clip = asset->GetClip(C::StringView(_T("Wave")));
        RIG_CHECK(clip);
        Core::Animation::SkeletalPoseSnapshot pose;
        RIG_CHECK(Core::Animation::SkeletalAnimationSampler::Sample(*asset->GetSkeleton(), *clip, *asset->GetMesh(), 1,
                                                                    F::MeshMatrix(*asset->GetMesh()), pose));
        const auto child = asset->GetSkeleton()->FindJointIndex(Core::Identity(_T("Child")));
        RIG_CHECK(child >= 0 && pose.BonePalette.size() == 2);
        const auto vertex =
            Core::Animation::SkeletalAnimationSampler::SkinVertex(asset->GetMesh()->GetVertices()[2], pose.BonePalette);
        RIG_CHECK(std::abs(vertex.Position.x + 4 * scale) < 1e-5 && std::abs(vertex.Position.y - 5 * scale) < 1e-5 &&
                  std::abs(vertex.Position.z - scale) < 1e-5);
        RIG_CHECK(std::abs(pose.BonePalette[size_t(child)].values[12] + 4 * scale) < 1e-5 &&
                  std::abs(pose.BonePalette[size_t(child)].values[13] - 3 * scale) < 1e-5 &&
                  std::abs(pose.BonePalette[size_t(child)].values[14] - scale) < 1e-5);
        return pose;
    }
} // namespace NorvesLib::Tests::RigStaticRootFrameFixture
