// 明示profile2のroot推定/静的祖先と同read所有を反証する。
#include "RigStaticRootFrameFixture.h"
#include "Resource/RigGltfImportCapture.h"
#include "Resource/SkeletalGltfDecode.h"
#include <cmath>
namespace F = NorvesLib::Tests::RigV1Fixture;
namespace H = NorvesLib::Tests::RigStaticRootFrameFixture;
namespace Core = NorvesLib::Core;
namespace C = Core::Container;
namespace S = Core::Skeletal;
namespace
{
    F::Text JointCountSource(F::Fixture& f, uint32_t count)
    {
        const auto number = [](uint32_t value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%u", value);
            return F::Text(buffer);
        };
        F::Text joints = "0", children;
        F::Text nodes = "{\"name\":\"Root\",\"children\":[";
        for (uint32_t i = 1; i < count; ++i)
        {
            joints += "," + number(i);
            if (i > 1)
            {
                children += ",";
            }
            children += number(i);
        }
        nodes += children + "]}";
        for (uint32_t i = 1; i < count; ++i)
        {
            nodes += ",{\"name\":\"" + (i == 1 ? F::Text("Child") : "Bone" + number(i)) + "\",\"translation\":[0,1,0]}";
        }
        nodes += ",{\"name\":\"Mesh\",\"translation\":[5,0,0],\"mesh\":0,\"skin\":0}";
        auto json = F::Replace(
            f.Json,
            "{\"name\":\"Root\",\"children\":[1]},{\"name\":\"Child\",\"translation\":[0,1,0]},{\"name\":\"Mesh\",\"translation\":[5,0,0],\"mesh\":0,\"skin\":0}",
            nodes);
        json = F::Replace(json, "\"nodes\":[0,2]", "\"nodes\":[0," + number(count) + "]");
        json = F::Replace(json, "\"joints\":[0,1]", "\"joints\":[" + joints + "]");
        json = F::Replace(json, "\"byteLength\":416", "\"byteLength\":" + number(416 + count * 64));
        json = F::Replace(json, "\"byteOffset\":224,\"byteLength\":128",
                          "\"byteOffset\":416,\"byteLength\":" + number(count * 64));
        json = F::Replace(json, "\"bufferView\":9,\"componentType\":5126,\"count\":2",
                          "\"bufferView\":9,\"componentType\":5126,\"count\":" + number(count));
        auto binary = f.Binary;
        binary.resize(416 + count * 64, 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t offset = 416 + size_t(i) * 64;
            for (size_t j = 0; j < 4; ++j)
            {
                F::Float(binary, offset + j * 20, 1);
            }
            F::Float(binary, offset + 48, 5);
            F::Float(binary, offset + 52, i == 0 ? 0.0f : -1.0f);
        }
        f.SetBuffer(binary);
        return json;
    }
    void Roots(F::Fixture& f)
    {
        const auto json = H::Armature(f.Json);
        auto rig = H::Import(json);
        const auto* d = rig.GetData();
        RIG_CHECK(d->Profile == H::Profile && S::SameRigRootFrame(d->RootFrame, H::ExpectedFrame()));
        RIG_CHECK(d->Geometry.Joints[0].ParentIndex == -1 && d->Geometry.Joints[1].ParentIndex == 0 &&
                  d->LocalRest[1].Translation.Y == 1);
        // 元のnode添字を0→2、1→4、2→1、3→0、4→3へ移し、祖先をjointの前後に分ける。
        auto reorderedJson = F::Replace(
            json,
            "\"nodes\":[{\"name\":\"Root\",\"children\":[1]},{\"name\":\"Child\",\"translation\":[0,1,0]},{\"name\":\"Mesh\",\"translation\":[5,0,0],\"mesh\":0,\"skin\":0},{\"name\":\"Armature\",\"children\":[0],\"translation\":[2,3,0],\"rotation\":[0,0,1,0],\"scale\":[2,2,2]},{\"name\":\"WorldFrame\",\"children\":[3],\"translation\":[-1,4,1],\"rotation\":[1,0,0,0]}]",
            "\"nodes\":[{\"name\":\"Armature\",\"children\":[2],\"translation\":[2,3,0],\"rotation\":[0,0,1,0],\"scale\":[2,2,2]},{\"name\":\"Mesh\",\"translation\":[5,0,0],\"mesh\":0,\"skin\":0},{\"name\":\"Root\",\"children\":[4]},{\"name\":\"WorldFrame\",\"children\":[0],\"translation\":[-1,4,1],\"rotation\":[1,0,0,0]},{\"name\":\"Child\",\"translation\":[0,1,0]}]");
        reorderedJson = F::Replace(reorderedJson, "\"nodes\":[4,2]", "\"nodes\":[3,1]");
        reorderedJson = F::Replace(reorderedJson, "\"joints\":[0,1]", "\"joints\":[2,4]");
        reorderedJson = F::Replace(reorderedJson, "\"node\":1,\"path\":\"translation\"", "\"node\":4,\"path\":\"translation\"");
        reorderedJson = F::Replace(reorderedJson, "\"node\":0,\"path\":\"rotation\"", "\"node\":2,\"path\":\"rotation\"");
        const auto reordered = H::Import(reorderedJson);
        const auto* reorderedData = reordered.GetData();
        RIG_CHECK(S::SameRigRootFrame(reorderedData->RootFrame, H::ExpectedFrame()) &&
                  S::SameRigTopology(reorderedData->Topology, d->Topology) &&
                  reorderedData->LocalRest.size() == d->LocalRest.size());
        for (size_t i = 0; i < d->LocalRest.size(); ++i)
        {
            const auto& a = reorderedData->LocalRest[i];
            const auto& b = d->LocalRest[i];
            RIG_CHECK(a.Translation.X == b.Translation.X && a.Translation.Y == b.Translation.Y &&
                      a.Translation.Z == b.Translation.Z && a.Rotation.X == b.Rotation.X &&
                      a.Rotation.Y == b.Rotation.Y && a.Rotation.Z == b.Rotation.Z && a.Rotation.W == b.Rotation.W &&
                      a.Scale.X == b.Scale.X && a.Scale.Y == b.Scale.Y && a.Scale.Z == b.Scale.Z);
        }
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
            "ROOT_FRAME_IMPORT_CASE result=pass optional_skin_hint_static_chain_joint_order_node_array_order_scale_same_read_glb_qsign\n");
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
    void BlenderLayoutProfile3(F::Fixture& f)
    {
        f.SetBuffer(f.Binary);
        const auto json = F::BaseJson("BlenderArmature.gltf");
        S::RigAuthoringCpu out;
        S::RigV1Report report;
        S::RigV1Limits limits;
        limits.MaxJoints = 256;
        RIG_CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf",
                                                             S::RigImportProfile::StaticRootFrame256, out, report,
                                                             limits));
        const auto* data = out.GetData();
        RIG_CHECK(data && data->Profile == S::RigImportProfile::StaticRootFrame256);
        constexpr float expectedFrame[16] = {
            0.5f, 0, 0, 0, 0, 0, 0.5f, 0, 0, -0.5f, 0, 0, 1, 0, -2, 1};
        for (size_t i = 0; i < 16; ++i)
        {
            RIG_CHECK(std::fabs(data->RootFrame[i] - expectedFrame[i]) <= 0.000001f);
        }
        const auto& joints = data->Geometry.Joints;
        RIG_CHECK(joints.size() == 2 && data->LocalRest.size() == joints.size());
        size_t rootIndex = joints.size();
        size_t childIndex = joints.size();
        for (size_t i = 0; i < joints.size(); ++i)
        {
            if (joints[i].Name == "Bone")
            {
                rootIndex = i;
            }
            else if (joints[i].Name == "Bone.001")
            {
                childIndex = i;
            }
        }
        RIG_CHECK(rootIndex < joints.size() && childIndex < joints.size());
        RIG_CHECK(joints[rootIndex].ParentIndex == -1 &&
                  joints[childIndex].ParentIndex == static_cast<int32_t>(rootIndex));
        const auto& rootRest = data->LocalRest[rootIndex];
        RIG_CHECK(rootRest.Translation.X == 0 && rootRest.Translation.Y == 0 && rootRest.Translation.Z == 0 &&
                  rootRest.Rotation.X == 0 && rootRest.Rotation.Y == 0 && rootRest.Rotation.Z == 0 &&
                  rootRest.Rotation.W == 1 && rootRest.Scale.X == 1 && rootRest.Scale.Y == 1 && rootRest.Scale.Z == 1);
        RIG_CHECK(data->LocalRest[childIndex].Translation.Y == 1);
        std::printf("ROOT_FRAME_IMPORT_CASE result=pass blender_layout_profile3_armature_author_frame\n");
    }
    void JointCapacity(F::Fixture& f)
    {
        const auto json = JointCountSource(f, 128);
        const auto stable = H::Import(json);
        RIG_CHECK(stable.GetData()->Topology.Joints.size() == 128 && stable.GetData()->LocalRest.size() == 128);
        const auto tooMany = JointCountSource(f, 129);
        auto out = stable;
        S::RigV1Report report;
        RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(tooMany), "RigV1Fixture/rig.gltf", H::Profile,
                                                              out, report) &&
                  out.GetData() == stable.GetData());
        f.SetBuffer(f.Binary);
        std::printf("ROOT_FRAME_IMPORT_CASE result=pass profile2_joint_128_accepted_129_rejected_out_preserved\n");
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
        out = stable;
        limits.MaxNodes = 4;
        RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", H::Profile, out,
                                                              report, limits) &&
                  report.Status == S::RigV1Status::LimitExceeded && out.GetData() == stable.GetData());
        limits.MaxNodes = 5;
        limits.MaxSourceBytes = json.size() - 1;
        out = stable;
        RIG_CHECK(!S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", H::Profile, out,
                                                              report, limits) &&
                  out.GetData() == stable.GetData());
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
        std::printf("ROOT_FRAME_IMPORT_CASE result=pass exact_node_source_limits_out_preserved_frame_domain_old_profile_refusal\n");
    }
} // namespace
int main()
{
    F::Fixture f;
    Roots(f);
    Reject(f);
    Budgets(f);
    JointCapacity(f);
    BlenderLayoutProfile3(f);
    std::printf("ROOT_FRAME_IMPORT result=pass explicit_static_parent_author_frame_owned_limits\n");
    return 0;
}
