// 明示profile2のroot推定/静的祖先と同read所有を反証する。
#include "RigStaticRootFrameFixture.h"
#include "Resource/RigGltfImportCapture.h"
#include "Resource/SkeletalGltfDecode.h"
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace H = NorvesLib::Tests::RigStaticRootFrameFixture;
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace S = Core::Skeletal;
namespace
{
    void Roots(F::Fixture& f)
    {
        const auto json = H::Armature(f.Json);
        auto rig = H::Import(json);
        const auto* d = rig.GetData();
        RIG_CHECK(d->Profile == H::Profile && S::SameRigRootFrame(d->RootFrame, H::ExpectedFrame()));
        RIG_CHECK(d->Geometry.Joints[0].ParentIndex == -1 && d->Geometry.Joints[1].ParentIndex == 0 &&
                  d->LocalRest[1].Translation.Y == 1);
        for (const char* hint : {"0", "3", "4"})
        {
            const auto text =
                F::Replace(json, "\"joints\":[0,1]", F::Text("\"skeleton\":") + hint + ",\"joints\":[0,1]");
            const auto hinted = H::Import(text);
            RIG_CHECK(S::SameRigRootFrame(hinted.GetData()->RootFrame, d->RootFrame) &&
                      S::SameRigTopology(hinted.GetData()->Topology, d->Topology));
        }
        auto same = d;
        S::RigV1Report report;
        RIG_CHECK(!S::DecodeRigAuthoringNativePath(F::View(json), "RigV1Fixture/rig.gltf", rig, report) &&
                  rig.GetData() == same);
        const auto identity = H::Import(f.Json);
        RIG_CHECK(S::SameRigRootFrame(identity.GetData()->RootFrame, S::IdentityRigRootFrame()));
        // 関節配列の順序は変えても、全node親と静的frameは変わらない。
        f.SetBuffer(F::SwappedBuffer(f.Binary));
        auto swapped = H::Import(F::Replace(json, "\"joints\":[0,1]", "\"joints\":[1,0]"));
        RIG_CHECK(S::SameRigTopology(swapped.GetData()->Topology, d->Topology) &&
                  S::SameRigRootFrame(swapped.GetData()->RootFrame, d->RootFrame));
        f.SetBuffer(f.Binary);
        Core::AssetImport::LoadedImportSettings settings;
        settings.bPresent = true;
        settings.Settings.Scale = 2;
        const auto scaled = H::Import(json, &settings);
        RIG_CHECK(S::SameRigRootFrame(scaled.GetData()->RootFrame, H::ExpectedFrame(2)) &&
                  scaled.GetData()->LocalRest[1].Translation.Y == 2);
        auto glb = H::Glb(json, f.Binary);
        S::RigAuthoringCpu embedded;
        S::RigGltfImportCapture capture;
        RIG_CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(glb), "RigV1Fixture/rig.gltf", H::Profile,
                                                             embedded, report, {}, nullptr, nullptr, &capture));
        RIG_CHECK(S::SameRigRootFrame(embedded.GetData()->RootFrame, d->RootFrame) && capture.Buffers.GetCount() == 1);
        glb.clear();
        RIG_CHECK(S::SameRigRootFrame(embedded.GetData()->RootFrame, H::ExpectedFrame()));
        const auto sign = H::Import(F::Replace(F::Replace(json, "\"rotation\":[0,0,1,0]", "\"rotation\":[0,0,-1,0]"),
                                               "\"rotation\":[1,0,0,0]", "\"rotation\":[-1,0,0,0]"));
        RIG_CHECK(S::SameRigRootFrame(sign.GetData()->RootFrame, d->RootFrame));
        std::printf(
            "ROOT_FRAME_IMPORT_CASE result=pass optional_skin_hint_static_chain_joint_order_scale_same_read_glb_qsign\n");
    }
    void Reject(F::Fixture& f)
    {
        const auto json = H::Armature(f.Json);
        auto stable = H::Import(json);
        S::RigV1Report report;
        const auto bad = [&](const F::Text& text)
        {
            auto out = stable;
            RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(text), "RigV1Fixture/rig.gltf", H::Profile,
                                                                  out, report) &&
                      out.GetData() == stable.GetData());
        };
        for (const char* hint : {"null", "true", "2", "1", "99"})
        {
            bad(F::Replace(json, "\"joints\":[0,1]", F::Text("\"skeleton\":") + hint + ",\"joints\":[0,1]"));
        }
        for (const char* scale : {"[2,3,2]", "[-2,2,2]", "[0,0,0]", "[1e-40,1e-40,1e-40]", "[1e30,1e30,1e30]"})
        {
            bad(F::Replace(json, "\"scale\":[2,2,2]", F::Text("\"scale\":") + scale));
        }
        for (const char* rotation : {"[0,0,0,0]", "[0,0,0,1e-30]", "[0,0,0,1e30]"})
        {
            bad(F::Replace(json, "\"rotation\":[0,0,1,0]", F::Text("\"rotation\":") + rotation));
        }
        bad(F::Replace(json, "\"name\":\"Armature\",",
                       "\"name\":\"Armature\",\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"));
        bad(F::Replace(json, "\"children\":[3]", "\"children\":[3,0]"));
        bad(F::Replace(json, "\"children\":[0]", "\"children\":[4]"));
        bad(F::Replace(json, "\"nodes\":[4,2]", "\"nodes\":[2]"));
        bad(F::Replace(F::Replace(json, "\"children\":[1]", "\"children\":[]"), "\"children\":[0]",
                       "\"children\":[0,1]"));
        // rootとchildの間に非jointを置いた場合は許可する祖先chainではない。
        bad(F::Replace(F::Replace(F::Replace(json, "\"children\":[1]", "\"children\":[3]"), "\"children\":[0]",
                                  "\"children\":[1]"),
                       "\"children\":[3],\"translation\":[-1,4,1]", "\"children\":[0],\"translation\":[-1,4,1]"));
        for (const char* path : {"translation", "rotation", "scale", "weights"})
        {
            bad(F::Replace(json, "\"channels\":[",
                           F::Text("\"channels\":[{\"sampler\":0,\"target\":{\"node\":3,\"path\":\"") + path +
                               "\"}},"));
        }
        for (bool drop : {false, true})
        {
            auto text = F::Replace(json, "\"node\":1,\"path\":\"translation\"", "\"node\":3,\"path\":\"translation\"");
            S::SkeletalGltfDecodeOptions options;
            if (drop)
            {
                options.MorphPolicy = S::SkeletalMorphPolicy::Drop;
            }
            auto out = stable;
            RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(text), "RigV1Fixture/rig.gltf", H::Profile,
                                                                  out, report, {}, nullptr, &options));
        }
        std::printf(
            "ROOT_FRAME_IMPORT_CASE result=pass malformed_hint_static_domain_scene_cycle_interjoint_animated_parent_rejected\n");
    }
    void Budgets(F::Fixture& f)
    {
        const auto json = H::Armature(f.Json);
        auto stable = H::Import(json);
        S::RigV1Report report;
        S::RigV1Limits limits;
        limits.MaxNodes = 5;
        limits.MaxSourceBytes = json.size();
        S::RigAuthoringCpu out;
        RIG_CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", H::Profile, out,
                                                             report, limits));
        limits.MaxNodes = 4;
        RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", H::Profile, out,
                                                              report, limits) &&
                  report.Status == S::RigV1Status::LimitExceeded);
        limits.MaxNodes = 5;
        limits.MaxSourceBytes = json.size() - 1;
        RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", H::Profile, out,
                                                              report, limits));
        S::RigRootFrame frame = H::ExpectedFrame();
        frame[0] = -0.f;
        RIG_CHECK(!S::IsValidRigRootFrame(frame, H::Profile));
        frame = H::ExpectedFrame();
        frame[1] = 0.1f;
        RIG_CHECK(!S::IsValidRigRootFrame(frame, H::Profile));
        frame = H::ExpectedFrame();
        frame[0] = 2;
        RIG_CHECK(!S::IsValidRigRootFrame(frame, H::Profile));
        RIG_CHECK(!S::IsValidRigRootFrame(H::ExpectedFrame(), S::RigImportProfile::DirectTrs128));
        std::printf("ROOT_FRAME_IMPORT_CASE result=pass exact_node_source_limits_frame_domain_old_profile_refusal\n");
    }
} // namespace
int main()
{
    F::Fixture f;
    Roots(f);
    Reject(f);
    Budgets(f);
    std::printf("ROOT_FRAME_IMPORT result=pass explicit_static_parent_author_frame_owned_limits\n");
    return 0;
}
