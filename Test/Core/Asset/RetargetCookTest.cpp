// v1 の出力固定と、異なる rest・脚長への v2 リターゲットの cook 契約を確かめる。
#include "ClipBankV1Fixture.h"
#include "Tools/AssetCook/RigRetargetCook.h"
#include "Asset/AssetPackageFormat.h"
#include "Animation/ClipChannelSampler.h"
#include "Resource/BvhEvaluate.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>

namespace F = NorvesLib::Tests::RigV1Fixture;
namespace Cook = NorvesLib::Tools::AssetCook;
namespace Core = NorvesLib::Core;
namespace S = Core::Skeletal;
namespace B = Core::Bvh;

#define RETARGET_REQUIRE(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::fprintf(stderr, "リターゲットの検証失敗: 行 %d: %s\n", __LINE__, #condition); \
            return false; \
        } \
    } while (false)

namespace
{
    bool V1ProfileOutputIsPinned()
    {
        // Fixture が一意な一時ディレクトリへ移動し、終了時に元へ戻して入力ごと削除する。
        // payload の source label を保つため、その中で従来の相対パスを使う。
        F::Fixture fixture;
        const auto write = [](const std::filesystem::path& path, const F::Text& text)
        {
            F::WriteBytes(path, F::Bytes(text.begin(), text.end()));
        };
        write("RigV1Fixture/target.gltf", fixture.Json);
        const auto motion = F::Replace(
            F::RootTrs(fixture.Json, R"("rotation":[0,0.7071067811865475,0,0.7071067811865475],)"),
            R"(,{"sampler":1,"target":{"node":0,"path":"rotation"}})", "");
        write("RigV1Fixture/motion.gltf", motion);
        write("RigV1Fixture/run.bvh", R"bvh(HIERARCHY
ROOT Root { OFFSET 0 0 0 CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
 JOINT Child { OFFSET 0 1 0 CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation End Site { OFFSET 0 1 0 } }
}
MOTION
Frames: 3
Frame Time: 0.5
0 0 0 0 0 45 10 0 0 0 0 0
1 .25 0 0 0 60 20 0 0 30 0 0
2 0 0 0 0 75 30 0 0 0 0 0
)bvh");
        write("RigV1Fixture/roles.json",
              R"json({"version":1,"vocabulary":"quadruped_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{"root":["Root"],"spine":["Child"]},"target_roles":{"root":[{"joint":"Root"}],"spine":[{"joint":"Child"}]},"rest_pose":{"mode":"match"},"processing":{"loop_mode":"none","output_fps":30}})json");

        struct PinnedOutput
        {
            const char* Input;
            const char* ClipName;
            uint64_t Hash;
            size_t Size;
        };
        constexpr PinnedOutput expected[] = {
            {"RigV1Fixture/run.bvh", "Run", 9676747322439301739ull, 5384},
            {"RigV1Fixture/motion.gltf", "", 2385898557403942170ull, 9464},
        };
        bool bMatches = true;
        for (const auto& pin : expected)
        {
            Cook::SingleAssetCookRequest request;
            request.InputPath = pin.Input;
            request.RetargetSkeletonPath = "RigV1Fixture/target.gltf";
            request.RetargetProfilePath = "RigV1Fixture/roles.json";
            request.RetargetClipName = pin.ClipName;
            request.LogicalPath = "Animations/Run";
            request.Kind = "animation";
            request.Format = "nvskel.v1.clips";
            Cook::CookDependencySnapshot dependencies;
            F::Text error;
            F::Bytes payload;
            const auto source = F::ReadBytes(request.InputPath);
            if (!Cook::CaptureCookDependencySnapshot(request, 1, dependencies, error) ||
                !Cook::Detail::CookRigRetargetBank(request, F::View(source), dependencies, payload, error))
            {
                std::fprintf(stderr, "リターゲットの cook に失敗: %s (%s)\n", pin.Input, error.c_str());
                return false;
            }
            const auto hash = NorvesLib::Core::Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size());
            if (hash != pin.Hash || payload.size() != pin.Size)
            {
                std::fprintf(stderr,
                             "v1 profile の出力が不一致: %s 期待 hash=%llu bytes=%zu 実際 hash=%llu bytes=%zu\n",
                             pin.Input, static_cast<unsigned long long>(pin.Hash), pin.Size,
                             static_cast<unsigned long long>(hash), payload.size());
                bMatches = false;
            }
        }
        if (bMatches)
        {
            std::puts("RETARGET_COOK_CASE v1_profile_output_pinned result=pass");
        }
        return bMatches;
    }

    struct QuadrupedInput
    {
        static constexpr size_t JointCount = 17;
        static constexpr uint32_t FrameCount = 61;
        static constexpr double Pi = 3.14159265358979323846;
        struct Joint
        {
            const char* Role;
            int Parent;
            B::Vector3d Offset;
            bool bLegSegment;
        };
        const Joint Joints[JointCount] = {
            {"root", -1, {0, 0, 0}, false},
            {"pelvis", 0, {0, 2, 0}, false},
            {"spine", 1, {0, 0, 1}, false},
            {"front_L_upper", 2, {.5, 0, .4}, false},
            {"front_L_lower", 3, {0, -.7, .08}, true},
            {"front_L_paw", 4, {0, -.6, .1}, true},
            {"front_R_upper", 2, {-.5, 0, .4}, false},
            {"front_R_lower", 6, {0, -.7, .08}, true},
            {"front_R_paw", 7, {0, -.6, .1}, true},
            {"hind_L_thigh", 1, {.5, 0, -.3}, false},
            {"hind_L_shin", 9, {0, -.6, -.1}, true},
            {"hind_L_hock", 10, {0, -.5, .15}, true},
            {"hind_L_paw", 11, {0, -.3, .15}, true},
            {"hind_R_thigh", 1, {-.5, 0, -.3}, false},
            {"hind_R_shin", 13, {0, -.6, -.1}, true},
            {"hind_R_hock", 14, {0, -.5, .15}, true},
            {"hind_R_paw", 15, {0, -.3, .15}, true},
        };
        struct Quaternion
        {
            double X = 0, Y = 0, Z = 0, W = 1;
            Quaternion Inverse() const
            {
                return {-X, -Y, -Z, W};
            }
            Quaternion operator*(const Quaternion& b) const
            {
                return {W * b.X + X * b.W + Y * b.Z - Z * b.Y,
                        W * b.Y - X * b.Z + Y * b.W + Z * b.X,
                        W * b.Z + X * b.Y - Y * b.X + Z * b.W,
                        W * b.W - X * b.X - Y * b.Y - Z * b.Z};
            }
            B::Vector3d Rotate(B::Vector3d v) const
            {
                const auto q = *this * Quaternion{v.X, v.Y, v.Z, 0} * Inverse();
                return {q.X, q.Y, q.Z};
            }
            static Quaternion FromValue(const S::SkeletalValue& v)
            {
                const double norm = std::hypot(std::hypot(double(v.X), double(v.Y)),
                                               std::hypot(double(v.Z), double(v.W)));
                return {v.X / norm, v.Y / norm, v.Z / norm, v.W / norm};
            }
        };
        F::Fixture Files;
        F::Text Bvh, Target, Profile;

        static B::Vector3d Add(B::Vector3d a, B::Vector3d b)
        {
            return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
        }
        static B::Vector3d Subtract(B::Vector3d a, B::Vector3d b)
        {
            return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
        }
        static double Length(B::Vector3d a)
        {
            return std::hypot(a.X, a.Y, a.Z);
        }
        static void Write(const std::filesystem::path& path, const F::Text& text)
        {
            F::WriteBytes(path, F::Bytes(text.begin(), text.end()));
        }
        void AppendBvhJoint(size_t i)
        {
            char text[256];
            const auto& j = Joints[i];
            std::snprintf(text, sizeof(text), "%s S%zu { OFFSET %.17g %.17g %.17g CHANNELS %s\n",
                          i == 0 ? "ROOT" : "JOINT", i, j.Offset.X, j.Offset.Y, j.Offset.Z,
                          i == 0 ? "6 Xposition Yposition Zposition Zrotation Xrotation Yrotation"
                                 : "3 Zrotation Xrotation Yrotation");
            Bvh += text;
            bool bChild = false;
            for (size_t child = i + 1; child < JointCount; ++child)
            {
                if (Joints[child].Parent == int(i))
                {
                    bChild = true;
                    AppendBvhJoint(child);
                }
            }
            if (!bChild)
            {
                Bvh += "End Site { OFFSET 0 -.1 0 }\n";
            }
            Bvh += "}\n";
        }
        QuadrupedInput()
        {
            Bvh = "HIERARCHY\n";
            AppendBvhJoint(0);
            Bvh += "MOTION\nFrames: 61\nFrame Time: 0.033333333333333333\n";
            char text[512];
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                std::snprintf(text, sizeof(text), "0 0 %.17g ", double(frame) / 30);
                Bvh += text;
                for (size_t i = 0; i < JointCount; ++i)
                {
                    // 宣言順の Z・X・Y 回転を混ぜ、30 コマ周期の非可換な歩行にする。
                    const double phase = 2 * Pi * frame / 30 + .37 * i;
                    const double amplitude = i < 3 ? 4 : 25;
                    std::snprintf(text, sizeof(text), "%.17g %.17g %.17g ",
                                  amplitude * std::sin(phase), amplitude * .7 * std::cos(phase),
                                  amplitude * .4 * std::sin(phase + .6));
                    Bvh += text;
                }
                Bvh += "\n";
            }
            Target = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,17]}],"nodes":[)";
            Quaternion worldRest[JointCount];
            for (size_t i = 0; i < JointCount; ++i)
            {
                B::Vector3d axis = i == 0 ? B::Vector3d{0, 1, 0} : Joints[i].Offset;
                for (size_t child = i + 1; child < JointCount; ++child)
                {
                    if (Joints[child].Parent == int(i))
                    {
                        axis = Joints[child].Offset;
                        break;
                    }
                }
                const double angle = (15 + 3 * i) * Pi / 180;
                const double factor = std::sin(angle / 2) / Length(axis);
                worldRest[i] = {axis.X * factor, axis.Y * factor, axis.Z * factor, std::cos(angle / 2)};
                const auto parent = Joints[i].Parent < 0 ? Quaternion{} : worldRest[Joints[i].Parent];
                const auto local = parent.Inverse() * worldRest[i];
                auto offset = Joints[i].Offset;
                if (Joints[i].bLegSegment)
                {
                    offset = {offset.X * 1.3, offset.Y * 1.3, offset.Z * 1.3};
                }
                // 親の rest 回転を逆に掛け、world の骨方向を元と同じに保つ。
                offset = parent.Inverse().Rotate(offset);
                std::snprintf(text, sizeof(text),
                              R"({"name":"T%zu","translation":[%.17g,%.17g,%.17g],"rotation":[%.17g,%.17g,%.17g,%.17g])",
                              i, offset.X, offset.Y, offset.Z, local.X, local.Y, local.Z, local.W);
                Target += text;
                bool bFirst = true;
                for (size_t child = i + 1; child < JointCount; ++child)
                {
                    if (Joints[child].Parent == int(i))
                    {
                        Target += bFirst ? ",\"children\":[" : ",";
                        std::snprintf(text, sizeof(text), "%zu", child);
                        Target += text;
                        bFirst = false;
                    }
                }
                Target += bFirst ? "}," : "]},";
            }
            Target += R"({"name":"Mesh","mesh":0,"skin":0}],)";
            // 最小スキンメッシュの buffer/accessor は既存 fixture を借りる。アニメと IBM は使わない。
            const auto buffers = Files.Json.find("\"buffers\":");
            const auto skins = Files.Json.find("\"skins\":");
            Target += Files.Json.substr(buffers, skins - buffers);
            Target += R"("skins":[{"skeleton":0,"joints":[)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), "%s%zu", i == 0 ? "" : ",", i);
                Target += text;
            }
            Target += "]}]}";
            Profile = R"({"version":2,"vocabulary":"quadruped_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":["S%zu"])", i == 0 ? "" : ",", Joints[i].Role, i);
                Profile += text;
            }
            Profile += "},\"target_roles\":{";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":[{"joint":"T%zu"}])", i == 0 ? "" : ",", Joints[i].Role, i);
                Profile += text;
            }
            Profile += R"(},"rest_pose":{"mode":"align_bones","roles":{)";
            for (size_t i = 3; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":{"up_hint":[0,0,1]})", i == 3 ? "" : ",", Joints[i].Role);
                Profile += text;
            }
            Profile += R"(}},"proportions":{"root_scale":"auto"},"report":{"max_joint_error_deg":0.1},"processing":{"loop_mode":"none","extract_root_motion":false,"output_fps":30}})";
            Write("RigV1Fixture/walk.bvh", Bvh);
            Write("RigV1Fixture/target.gltf", Target);
            Write("RigV1Fixture/roles.json", Profile);
        }
        bool CompareDirections(const S::ClipBankV1& bank, const S::RigAuthoringCpu& target,
                               double& maximumDegrees) const
        {
            B::BvhDocument document;
            RETARGET_REQUIRE(B::DecodeBvh(F::View(Bvh), {}, document).Succeeded());
            const auto* data = bank.GetData();
            const auto* author = target.GetData();
            RETARGET_REQUIRE(data && author && data->Clips.size() == 1 && document.Joints.size() == JointCount);
            RETARGET_REQUIRE(std::abs(data->Clips[0].DurationSeconds - 2) < 1e-6);
            const auto& clip = data->Clips[0];
            RETARGET_REQUIRE(clip.RootMotion.empty());
            // 名前順の wire 関節番号を作者順へ戻し、報告計算に依存しない FK を行う。
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                B::BvhPose source;
                RETARGET_REQUIRE(B::EvaluateBvhFrame(document, frame, B::TranslationConvention::OffsetPlusChannels,
                                                     source).Succeeded());
                auto local = author->LocalRest;
                for (const auto& channel : clip.Channels)
                {
                    RETARGET_REQUIRE(channel.JointIndex < author->Topology.CanonicalToSource.size());
                    const auto joint = author->Topology.CanonicalToSource[channel.JointIndex];
                    const auto value = Core::Animation::ClipChannelSampler::Sample(channel, float(frame) / 30);
                    if (channel.Path == S::SkeletalAnimationPath::Rotation)
                    {
                        local[joint].Rotation = value;
                    }
                    else if (channel.Path == S::SkeletalAnimationPath::Translation)
                    {
                        local[joint].Translation = {value.X, value.Y, value.Z};
                    }
                    else
                    {
                        local[joint].Scale = {value.X, value.Y, value.Z};
                    }
                }
                Quaternion rotations[JointCount];
                B::Vector3d positions[JointCount];
                for (size_t i = 0; i < JointCount; ++i)
                {
                    RETARGET_REQUIRE(local[i].Scale.X == 1 && local[i].Scale.Y == 1 && local[i].Scale.Z == 1);
                    const auto q = Quaternion::FromValue(local[i].Rotation);
                    const B::Vector3d t{local[i].Translation.X, local[i].Translation.Y, local[i].Translation.Z};
                    const auto parent = Joints[i].Parent;
                    RETARGET_REQUIRE(author->Geometry.Joints[i].ParentIndex == parent);
                    rotations[i] = parent < 0 ? q : rotations[parent] * q;
                    positions[i] = parent < 0 ? t : Add(positions[parent], rotations[parent].Rotate(t));
                    if (parent < 0)
                    {
                        continue;
                    }
                    const auto a = Subtract(positions[i], positions[parent]);
                    const auto b = Subtract(source.Joints[i].World.Translation, source.Joints[parent].World.Translation);
                    RETARGET_REQUIRE(Length(a) > 1e-8 && Length(b) > 1e-8);
                    const B::Vector3d cross{a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
                    const double degrees = std::atan2(Length(cross), a.X * b.X + a.Y * b.Y + a.Z * b.Z) * 180 / Pi;
                    RETARGET_REQUIRE(std::isfinite(degrees));
                    maximumDegrees = std::max(maximumDegrees, degrees);
                }
            }
            return true;
        }
    };

    bool V2QuadrupedProportionsAndReport()
    {
        QuadrupedInput input;
        Cook::SingleAssetCookRequest request;
        request.InputPath = "RigV1Fixture/walk.bvh";
        request.RetargetSkeletonPath = "RigV1Fixture/target.gltf";
        request.RetargetProfilePath = "RigV1Fixture/roles.json";
        request.RetargetReportPath = "within.json";
        request.RetargetClipName = "Walk";
        request.LogicalPath = "Animations/Walk";
        request.Kind = "animation";
        request.Format = "nvskel.v1.clips";
        Cook::CookDependencySnapshot dependencies;
        F::Text error;
        F::Bytes payload;
        RETARGET_REQUIRE(Cook::CaptureCookDependencySnapshot(request, 1, dependencies, error));
        if (!Cook::Detail::CookRigRetargetBank(request, F::View(input.Bvh), dependencies, payload, error))
        {
            std::fprintf(stderr, "四足の cook に失敗: %s\n", error.c_str());
            return false;
        }
        Core::JsonDocument report;
        RETARGET_REQUIRE(Core::JsonDocument::TryParseUtf8(F::View(F::ReadBytes(request.RetargetReportPath)), report));
        const auto root = report.GetRoot();
        RETARGET_REQUIRE(root.FindMember("result").FindMember("status").AsString() == "within");
        const auto clips = root.FindMember("clips");
        RETARGET_REQUIRE(clips.GetArraySize() == 1);
        const auto clip = clips.GetArrayElement(0);
        const auto errorValue = clip.FindMember("rotation_retarget_error_deg").FindMember("max");
        const auto scaleValue = clip.FindMember("root_scale");
        const auto distances = clip.FindMember("root_motion_distance_m");
        const auto sourceDistance = distances.FindMember("source");
        const auto targetDistance = distances.FindMember("target");
        RETARGET_REQUIRE(errorValue.IsNumber() && scaleValue.IsNumber() && sourceDistance.IsNumber() && targetDistance.IsNumber());
        const double maximumError = errorValue.AsNumber(), rootScale = scaleValue.AsNumber();
        RETARGET_REQUIRE(sourceDistance.AsNumber() > 0);
        const double distanceRatio = targetDistance.AsNumber() / sourceDistance.AsNumber();
        S::ClipBankV1 bank;
        S::RigV1Report rigReport;
        S::RigV1Limits limits;
        limits.MaxJoints = 256;
        constexpr auto profile = S::RigImportProfile::StaticRootFrame256;
        RETARGET_REQUIRE(S::ParseClipBankV1(F::View(payload), bank, rigReport, limits, profile));
        S::RigAuthoringCpu target;
        S::RigClipSourceSelection selection;
        selection.bAllowEmptyAnimations = true;
        RETARGET_REQUIRE(S::DecodeRigAuthoringWithProfileNativePath(F::View(input.Target), request.RetargetSkeletonPath,
                                                                   profile, target, rigReport, limits, nullptr,
                                                                   nullptr, nullptr, &selection));
        S::RigTopology topology;
        const auto& joints = target.GetData()->Geometry.Joints;
        RETARGET_REQUIRE(S::BuildRigTopology({joints.data(), joints.size()}, limits, topology) == S::RigV1Status::Success);
        RETARGET_REQUIRE(bank.GetData()->Topology.SkeletonId == topology.SkeletonId);
        char skeletonId[17];
        std::snprintf(skeletonId, sizeof(skeletonId), "%016llx", static_cast<unsigned long long>(topology.SkeletonId));
        RETARGET_REQUIRE(root.FindMember("target_skeleton_id").AsString() == skeletonId);
        double directionError = 0;
        RETARGET_REQUIRE(input.CompareDirections(bank, target, directionError));
        std::printf("RETARGET_COOK_METRICS quadruped_v2 rotation_max_deg=%.12g direction_max_deg=%.12g root_scale=%.12g distance_ratio=%.12g source_distance_m=%.12g target_distance_m=%.12g\n",
                    maximumError, directionError, rootScale, distanceRatio, sourceDistance.AsNumber(), targetDistance.AsNumber());
        RETARGET_REQUIRE(std::isfinite(maximumError) && maximumError >= 0 && maximumError <= .1);
        RETARGET_REQUIRE(directionError <= .1);
        RETARGET_REQUIRE(std::isfinite(rootScale) && std::abs(rootScale - 1.3) <= 1e-3);
        RETARGET_REQUIRE(std::isfinite(distanceRatio) && std::abs(distanceRatio - 1.3) <= 1e-3);
        // 周期回転へ大きな平滑化を掛け、0.1 度を十分に超える差を作る。
        const auto smoothed = F::Replace(input.Profile, R"("output_fps":30)", R"("output_fps":30,"smoothing_radius":5)");
        QuadrupedInput::Write(request.RetargetProfilePath, smoothed);
        request.RetargetReportPath = "exceeded.json";
        error.clear();
        RETARGET_REQUIRE(Cook::CaptureCookDependencySnapshot(request, 1, dependencies, error));
        RETARGET_REQUIRE(!Cook::Detail::CookRigRetargetBank(request, F::View(input.Bvh), dependencies, payload, error));
        RETARGET_REQUIRE(error.find("retarget_report_limit:") == 0);
        Core::JsonDocument exceeded;
        RETARGET_REQUIRE(Core::JsonDocument::TryParseUtf8(F::View(F::ReadBytes(request.RetargetReportPath)), exceeded));
        const auto result = exceeded.GetRoot().FindMember("result");
        RETARGET_REQUIRE(result.FindMember("status").AsString() == "joint_error_exceeded");
        RETARGET_REQUIRE(result.FindMember("value").IsNumber() && result.FindMember("value").AsNumber() > 1);
        std::printf("RETARGET_COOK_METRICS quadruped_v2_limit error=%s\n", error.c_str());
        std::puts("RETARGET_COOK_CASE quadruped_v2_proportions_report result=pass");
        return true;
    }
} // namespace

int main()
{
    const bool bV1 = V1ProfileOutputIsPinned();
    const bool bV2 = V2QuadrupedProportionsAndReport();
    return bV1 && bV2 ? 0 : 1;
}
