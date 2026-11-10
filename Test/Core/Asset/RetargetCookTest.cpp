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

    struct GroundedWalkInput
    {
        using Q = QuadrupedInput::Quaternion;
        static constexpr size_t JointCount = 17, LegCount = 4;
        static constexpr uint32_t FrameCount = 61;
        static constexpr double Pi = QuadrupedInput::Pi, Speed = 1.35;
        struct Joint
        {
            const char* Role;
            int Parent;
            B::Vector3d Offset;
        };
        const Joint Joints[JointCount] = {
            {"root", -1, {0, 0, 0}}, {"pelvis", 0, {0, 1.4, 0}}, {"spine", 1, {0, 0, 1}},
            {"front_L_upper", 2, {.5, 0, .4}}, {"front_L_lower", 3, {0, -.7, 0}},
            {"front_L_paw", 4, {0, -.7, 0}},
            {"front_R_upper", 2, {-.5, 0, .4}}, {"front_R_lower", 6, {0, -.7, 0}},
            {"front_R_paw", 7, {0, -.7, 0}},
            {"hind_L_thigh", 1, {.5, 0, -.3}}, {"hind_L_shin", 9, {0, -.55, 0}},
            {"hind_L_hock", 10, {0, -.55, 0}}, {"hind_L_paw", 11, {0, -.3, 0}},
            {"hind_R_thigh", 1, {-.5, 0, -.3}}, {"hind_R_shin", 13, {0, -.55, 0}},
            {"hind_R_hock", 14, {0, -.55, 0}}, {"hind_R_paw", 15, {0, -.3, 0}},
        };
        const size_t Upper[LegCount] = {3, 6, 9, 13}, Paw[LegCount] = {5, 8, 12, 16};
        F::Fixture Files;
        F::Text Bvh, Target, Profile;
        struct Measurement
        {
            B::Vector3d Positions[FrameCount][JointCount]{};
            Q Locals[FrameCount][JointCount]{};
            double Slide = 0, HeightError = 0, SwingSpeed = 0, Drop = 0, Failed = 0, RootScale = 0;
        };
        static uint32_t Phase(uint32_t frame, size_t leg)
        {
            return (frame + 20 + uint32_t(leg) * 2) % 30;
        }
        static bool IsStance(uint32_t frame, size_t leg)
        {
            return Phase(frame, leg) <= 18;
        }
        void AppendJoint(size_t i)
        {
            char text[256];
            const auto& joint = Joints[i];
            std::snprintf(text, sizeof(text), "%s S%zu { OFFSET %.17g %.17g %.17g CHANNELS %s\n",
                          i ? "JOINT" : "ROOT", i, joint.Offset.X, joint.Offset.Y, joint.Offset.Z,
                          i ? "3 Zrotation Xrotation Yrotation" : "6 Xposition Yposition Zposition Zrotation Xrotation Yrotation");
            Bvh += text;
            bool bChild = false;
            for (size_t child = i + 1; child < JointCount; ++child)
            {
                if (Joints[child].Parent == int(i))
                {
                    bChild = true;
                    AppendJoint(child);
                }
            }
            Bvh += bChild ? "}\n" : "End Site { OFFSET 0 -.1 0 } }\n";
        }
        GroundedWalkInput()
        {
            Bvh = "HIERARCHY\n";
            AppendJoint(0);
            Bvh += "MOTION\nFrames: 61\nFrame Time: 0.033333333333333333\n";
            char text[512];
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                // rest は直立で接地し、歩行中は膝を曲げる余裕を作る。
                std::snprintf(text, sizeof(text), "0 -.2 %.17g ", Speed * frame / 30);
                Bvh += text;
                double angles[JointCount]{};
                for (size_t leg = 0; leg < LegCount; ++leg)
                {
                    const double phase = Phase(frame, leg);
                    double z = Speed * (.3 - phase / 30), y = 0;
                    if (phase > 18)
                    {
                        const double t = (phase - 18) / 12;
                        // ワールドの足先速度を両端でゼロにする五次補間と、鉛直の弧。
                        const double smooth = t * t * t * (10 + t * (-15 + 6 * t));
                        z += Speed * smooth;
                        y = .15 * std::pow(std::sin(Pi * t), 4);
                    }
                    const double length = leg < 2 ? .7 : .55;
                    const double down = 1.2 - y - (leg < 2 ? 0 : .3);
                    const double distance = std::hypot(down, z);
                    const double bend = std::acos(std::clamp(distance / (2 * length), -1.0, 1.0));
                    // X 回転で (0,-L,0) を矢状面内へ回す解析的な二関節 IK。
                    const double upper = std::atan2(-z, down) - bend, lower = 2 * bend;
                    angles[Upper[leg]] = upper;
                    angles[Upper[leg] + 1] = lower;
                    angles[Upper[leg] + 2] = -upper - lower;
                }
                for (double angle : angles)
                {
                    std::snprintf(text, sizeof(text), "0 %.17g 0 ", angle * 180 / Pi);
                    Bvh += text;
                }
                Bvh += "\n";
            }
            Target = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,17]}],"nodes":[)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                auto offset = Joints[i].Offset;
                if (i == 1)
                {
                    offset.Y = 1.54;
                }
                if (i == 3 || i == 6)
                {
                    offset.Y = .28;
                }
                if (i == 4 || i == 5 || i == 7 || i == 8)
                {
                    offset.Y *= 1.3;
                }
                if (i == 10 || i == 11 || i == 12 || i == 14 || i == 15 || i == 16)
                {
                    offset.Y *= 1.1;
                }
                std::snprintf(text, sizeof(text), R"({"name":"T%zu","translation":[%.17g,%.17g,%.17g])",
                              i, offset.X, offset.Y, offset.Z);
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
            const auto buffers = Files.Json.find("\"buffers\":");
            const auto skins = Files.Json.find("\"skins\":");
            Target += Files.Json.substr(buffers, skins - buffers);
            Target += R"("skins":[{"skeleton":0,"joints":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]}]})";
            Profile = R"({"version":2,"vocabulary":"quadruped_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":["S%zu"])", i ? "," : "", Joints[i].Role, i);
                Profile += text;
            }
            Profile += "},\"target_roles\":{";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":[{"joint":"T%zu"}])", i ? "," : "", Joints[i].Role, i);
                Profile += text;
            }
            Profile += R"(},"rest_pose":{"mode":"align_bones","roles":{)";
            for (size_t i = 3; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":{"up_hint":[0,0,1]})", i == 3 ? "" : ",", Joints[i].Role);
                Profile += text;
            }
            Profile += R"(}},"proportions":{"root_scale":"auto"},"processing":{"loop_mode":"none","extract_root_motion":false,"output_fps":30},"ground":{"height":0},"foot_planting":{"enabled":true,"ik":{"leg_roles":["front_L_paw","front_R_paw","hind_L_paw","hind_R_paw"]}},"report":{"max_foot_slide_m":0.005,"max_ik_failed_frames":0}})";
            QuadrupedInput::Write("RigV1Fixture/grounded.bvh", Bvh);
            QuadrupedInput::Write("RigV1Fixture/target.gltf", Target);
        }
        void Measure(Measurement& out) const
        {
            for (size_t leg = 0; leg < LegCount; ++leg)
            {
                B::Vector3d anchor{};
                for (uint32_t frame = 0; frame < FrameCount; ++frame)
                {
                    if (IsStance(frame, leg))
                    {
                        const auto paw = out.Positions[frame][Paw[leg]];
                        if (!frame || !IsStance(frame - 1, leg))
                        {
                            anchor = paw;
                        }
                        out.Slide = std::max(out.Slide, std::hypot(paw.X - anchor.X, paw.Z - anchor.Z));
                        out.HeightError = std::max(out.HeightError, std::abs(paw.Y));
                    }
                }
            }
            for (size_t leg = 0; leg < LegCount; ++leg)
            {
                for (uint32_t frame = 1; frame < FrameCount; ++frame)
                {
                    // 各脚について、その脚のどの接地区間にも属さない隣接コマを比べる。
                    if (IsStance(frame, leg) || IsStance(frame - 1, leg))
                    {
                        continue;
                    }
                    for (size_t joint = Upper[leg]; joint <= Paw[leg]; ++joint)
                    {
                        const auto delta = out.Locals[frame - 1][joint].Inverse() * out.Locals[frame][joint];
                        const double speed = 60 * std::atan2(std::hypot(delta.X, delta.Y, delta.Z), std::abs(delta.W));
                        out.SwingSpeed = std::max(out.SwingSpeed, speed);
                    }
                }
            }
        }

        double MaximumGoalError(const Measurement& planted, const Measurement& control) const
        {
            double maximum = 0;
            for (size_t leg = 0; leg < LegCount; ++leg)
            {
                B::Vector3d anchor{};
                for (uint32_t frame = 0; frame < FrameCount; ++frame)
                {
                    if (IsStance(frame, leg))
                    {
                        if (!frame || !IsStance(frame - 1, leg))
                        {
                            // 再接地前の対象 FK の接地開始位置が固定目標の水平座標。
                            anchor = control.Positions[frame][Paw[leg]];
                        }
                        const auto paw = planted.Positions[frame][Paw[leg]];
                        maximum = std::max(maximum, std::hypot(paw.X - anchor.X, paw.Z - anchor.Z));
                    }
                }
            }
            return maximum;
        }
        bool CheckSource() const
        {
            B::BvhDocument document;
            RETARGET_REQUIRE(B::DecodeBvh(F::View(Bvh), {}, document).Succeeded());
            Measurement source;
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                B::BvhPose pose;
                RETARGET_REQUIRE(B::EvaluateBvhFrame(document, frame, B::TranslationConvention::OffsetPlusChannels, pose).Succeeded());
                RETARGET_REQUIRE(pose.Joints.size() == JointCount);
                for (size_t joint = 0; joint < JointCount; ++joint)
                {
                    source.Positions[frame][joint] = pose.Joints[joint].World.Translation;
                }
            }
            Measure(source);
            std::printf("RETARGET_COOK_METRICS grounded_source slide_m=%.12g height_error_m=%.12g\n", source.Slide, source.HeightError);
            RETARGET_REQUIRE(source.Slide < .001 && source.HeightError < .001);
            return true;
        }
        bool CookAndMeasure(const F::Text& profileText, bool bPlanting, Measurement& out) const
        {
            QuadrupedInput::Write("RigV1Fixture/roles.json", profileText);
            Cook::SingleAssetCookRequest request;
            request.InputPath = "RigV1Fixture/grounded.bvh";
            request.RetargetSkeletonPath = "RigV1Fixture/target.gltf";
            request.RetargetProfilePath = "RigV1Fixture/roles.json";
            request.RetargetReportPath = "grounded-report.json";
            request.RetargetClipName = "Walk";
            request.LogicalPath = "Animations/Walk";
            request.Kind = "animation";
            request.Format = "nvskel.v1.clips";
            Cook::CookDependencySnapshot dependencies;
            F::Text error;
            F::Bytes payload;
            RETARGET_REQUIRE(Cook::CaptureCookDependencySnapshot(request, 1, dependencies, error));
            const bool bCooked = Cook::Detail::CookRigRetargetBank(request, F::View(Bvh), dependencies, payload, error);
            if (!bCooked)
            {
                std::fprintf(stderr, "接地歩行の cook に失敗: %s\n", error.c_str());
                const auto report = F::ReadBytes(request.RetargetReportPath);
                std::fwrite(report.data(), 1, report.size(), stderr);
            }
            RETARGET_REQUIRE(bCooked);
            Core::JsonDocument report;
            RETARGET_REQUIRE(Core::JsonDocument::TryParseUtf8(F::View(F::ReadBytes(request.RetargetReportPath)), report));
            RETARGET_REQUIRE(report.GetRoot().FindMember("result").FindMember("status").AsString() == "within");
            const auto entry = report.GetRoot().FindMember("clips").GetArrayElement(0);
            RETARGET_REQUIRE(entry.FindMember("root_scale").IsNumber());
            out.RootScale = entry.FindMember("root_scale").AsNumber();
            if (bPlanting)
            {
                const auto failed = entry.FindMember("ik_failed_frames"), drop = entry.FindMember("pelvis_drop_m");
                RETARGET_REQUIRE(failed.IsNumber() && drop.IsNumber());
                out.Failed = failed.AsNumber();
                out.Drop = drop.AsNumber();
                const auto contacts = entry.FindMember("foot_contacts");
                RETARGET_REQUIRE(contacts.IsArray() && contacts.GetArraySize() == LegCount);
                bool bSeen[LegCount]{};
                for (size_t index = 0; index < LegCount; ++index)
                {
                    const auto contact = contacts.GetArrayElement(index);
                    size_t leg = 0;
                    while (leg < LegCount && contact.FindMember("leg").AsString() != Joints[Paw[leg]].Role)
                    {
                        ++leg;
                    }
                    RETARGET_REQUIRE(leg < LegCount && !bSeen[leg]);
                    bSeen[leg] = true;
                    bool bReported[FrameCount]{};
                    const auto ranges = contact.FindMember("frames");
                    RETARGET_REQUIRE(ranges.IsArray() && ranges.GetArraySize() > 0);
                    for (size_t range = 0; range < ranges.GetArraySize(); ++range)
                    {
                        const auto pair = ranges.GetArrayElement(range);
                        RETARGET_REQUIRE(pair.IsArray() && pair.GetArraySize() == 2);
                        const double begin = pair.GetArrayElement(0).AsNumber(), end = pair.GetArrayElement(1).AsNumber();
                        RETARGET_REQUIRE(begin >= 0 && end <= FrameCount && begin < end && begin == std::floor(begin) && end == std::floor(end));
                        for (uint32_t frame = uint32_t(begin); frame < uint32_t(end); ++frame)
                        {
                            RETARGET_REQUIRE(!bReported[frame]);
                            bReported[frame] = true;
                        }
                    }
                    size_t disagreement = 0;
                    for (uint32_t frame = 0; frame < FrameCount; ++frame)
                    {
                        disagreement += bReported[frame] != IsStance(frame, leg);
                    }
                    // 検出器の境界付近の最大 2 コマの差を各周期で許す。
                    RETARGET_REQUIRE(disagreement <= 8);
                }
            }
            S::ClipBankV1 bank;
            S::RigV1Report rigReport;
            S::RigV1Limits limits;
            limits.MaxJoints = 256;
            constexpr auto profile = S::RigImportProfile::StaticRootFrame256;
            RETARGET_REQUIRE(S::ParseClipBankV1(F::View(payload), bank, rigReport, limits, profile));
            S::RigAuthoringCpu target;
            S::RigClipSourceSelection selection;
            selection.bAllowEmptyAnimations = true;
            RETARGET_REQUIRE(S::DecodeRigAuthoringWithProfileNativePath(F::View(Target), request.RetargetSkeletonPath,
                                                                       profile, target, rigReport, limits, nullptr, nullptr, nullptr, &selection));
            const auto* author = target.GetData();
            RETARGET_REQUIRE(author && bank.GetData() && bank.GetData()->Clips.size() == 1);
            const auto& clip = bank.GetData()->Clips[0];
            RETARGET_REQUIRE(clip.RootMotion.empty() && std::abs(clip.DurationSeconds - 2) < 1e-6);
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
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
                Q rotations[JointCount];
                for (size_t joint = 0; joint < JointCount; ++joint)
                {
                    RETARGET_REQUIRE(local[joint].Scale.X == 1 && local[joint].Scale.Y == 1 && local[joint].Scale.Z == 1);
                    RETARGET_REQUIRE(author->Geometry.Joints[joint].ParentIndex == Joints[joint].Parent);
                    const auto q = Q::FromValue(local[joint].Rotation);
                    RETARGET_REQUIRE(std::isfinite(q.X) && std::isfinite(q.Y) && std::isfinite(q.Z) && std::isfinite(q.W));
                    out.Locals[frame][joint] = q;
                    const auto parent = Joints[joint].Parent;
                    const B::Vector3d t{local[joint].Translation.X, local[joint].Translation.Y, local[joint].Translation.Z};
                    rotations[joint] = parent < 0 ? q : rotations[parent] * q;
                    const auto position = parent < 0 ? t : QuadrupedInput::Add(out.Positions[frame][parent], rotations[parent].Rotate(t));
                    RETARGET_REQUIRE(std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z));
                    out.Positions[frame][joint] = position;
                }
            }
            Measure(out);
            return true;
        }
    };

    bool V2GroundedWalkReplanting()
    {
        GroundedWalkInput input;
        RETARGET_REQUIRE(input.CheckSource());
        GroundedWalkInput::Measurement planted, control;
        auto disabled = F::Replace(input.Profile, R"("enabled":true)", R"("enabled":false)");
        disabled = F::Replace(disabled, R"(,"report":{"max_foot_slide_m":0.005,"max_ik_failed_frames":0})", "");
        RETARGET_REQUIRE(input.CookAndMeasure(disabled, false, control));
        RETARGET_REQUIRE(input.CookAndMeasure(input.Profile, true, planted));
        RETARGET_REQUIRE(control.SwingSpeed > 0);
        const double speedRatio = planted.SwingSpeed / control.SwingSpeed;
        const double goalError = input.MaximumGoalError(planted, control);
        std::printf("RETARGET_COOK_METRICS grounded_replant slide_on_m=%.12g slide_off_m=%.12g height_error_m=%.12g swing_on_rad_s=%.12g swing_off_rad_s=%.12g angular_speed_ratio=%.12g pelvis_drop_m=%.12g ik_failed_frames=%.0f goal_horizontal_error_m=%.12g\n",
                    planted.Slide, control.Slide, planted.HeightError, planted.SwingSpeed, control.SwingSpeed, speedRatio, planted.Drop, planted.Failed, goalError);
        // 5 mm・3 mm・1.5 倍・負の対照 5 cm は、この合成歩行に対する仮の値。
        RETARGET_REQUIRE(planted.Slide <= .005 && planted.HeightError <= .003 && planted.Failed == 0);
        RETARGET_REQUIRE(control.Slide >= .05 && speedRatio <= 1.5 && goalError <= .005);
        std::puts("RETARGET_COOK_CASE grounded_walk_replanting result=pass");
        return true;
    }

    bool V2GroundedWalkRatioTwo()
    {
        GroundedWalkInput input;
        RETARGET_REQUIRE(input.CheckSource());
        const auto profile = F::Replace(input.Profile, R"("root_scale":"auto")", R"("root_scale":"auto","leg_length_ratio":2.0)");
        GroundedWalkInput::Measurement planted, control;
        auto disabled = F::Replace(profile, R"("enabled":true)", R"("enabled":false)");
        disabled = F::Replace(disabled, R"(,"report":{"max_foot_slide_m":0.005,"max_ik_failed_frames":0})", "");
        RETARGET_REQUIRE(input.CookAndMeasure(disabled, false, control));
        RETARGET_REQUIRE(input.CookAndMeasure(profile, true, planted));
        const double goalError = input.MaximumGoalError(planted, control);
        std::printf("RETARGET_COOK_METRICS grounded_ratio_two slide_m=%.12g height_error_m=%.12g pelvis_drop_m=%.12g ik_failed_frames=%.0f goal_horizontal_error_m=%.12g root_scale=%.12g\n",
                    planted.Slide, planted.HeightError, planted.Drop, planted.Failed, goalError, planted.RootScale);
        // 接地精度の 5 mm・3 mm は、この合成歩行に対する仮の値。
        RETARGET_REQUIRE(planted.RootScale == 2 && planted.Drop > 0 && planted.Failed == 0);
        RETARGET_REQUIRE(planted.Slide <= .005 && goalError <= .005 && planted.HeightError <= .003);
        std::puts("RETARGET_COOK_CASE grounded_walk_ratio_two result=pass");
        return true;
    }

    struct HumanoidWalkInput
    {
        using Q = QuadrupedInput::Quaternion;
        static constexpr size_t JointCount = 22, LegCount = 2;
        static constexpr uint32_t FrameCount = 61;
        static constexpr double Pi = QuadrupedInput::Pi, Speed = 1.8, ExpectedRootScale = 1.2;
        struct Joint
        {
            const char* Role;
            int Parent;
            B::Vector3d Offset;
        };
        const Joint Joints[JointCount] = {
            {"hips", -1, {0, 1.4, 0}},
            {"spine1", 0, {0, .18, 0}}, {"spine2", 1, {0, .18, 0}},
            {"spine3", 2, {0, .18, 0}}, {"neck", 3, {0, .18, 0}}, {"head", 4, {0, .18, 0}},
            {"L_shoulder", 3, {.15, .12, 0}}, {"L_arm", 6, {.2, 0, 0}},
            {"L_forearm", 7, {.32, 0, 0}}, {"L_hand", 8, {.28, 0, 0}},
            {"R_shoulder", 3, {-.15, .12, 0}}, {"R_arm", 10, {-.2, 0, 0}},
            {"R_forearm", 11, {-.32, 0, 0}}, {"R_hand", 12, {-.28, 0, 0}},
            {"L_upleg", 0, {.16, 0, 0}}, {"L_leg", 14, {0, -.7, 0}},
            {"L_foot", 15, {0, -.7, 0}}, {"L_toe", 16, {0, 0, .22}},
            {"R_upleg", 0, {-.16, 0, 0}}, {"R_leg", 18, {0, -.7, 0}},
            {"R_foot", 19, {0, -.7, 0}}, {"R_toe", 20, {0, 0, .22}},
        };
        const size_t Upper[LegCount] = {14, 18}, Foot[LegCount] = {16, 20};
        F::Fixture Files;
        F::Text Bvh, Target, Profile;
        struct Measurement
        {
            B::Vector3d Positions[FrameCount][JointCount]{};
            Q Locals[FrameCount][JointCount]{};
            double Slide = 0, HeightError = 0, SwingSpeed = 0, Drop = 0, Failed = 0;
            double RootScale = 0, RotationError = 0, DirectionError = 0;
            size_t SwingLeg = 0, SwingJoint = 0;
            uint32_t SwingFrame = 0;
        };
        static uint32_t Phase(uint32_t frame, size_t leg)
        {
            return (frame + 20 + uint32_t(leg) * 15) % 30;
        }
        static bool IsStance(uint32_t frame, size_t leg)
        {
            return Phase(frame, leg) <= 18;
        }
        void AppendJoint(size_t i)
        {
            char text[256];
            const auto& joint = Joints[i];
            std::snprintf(text, sizeof(text), "%s S%zu { OFFSET %.17g %.17g %.17g CHANNELS %s\n",
                          i ? "JOINT" : "ROOT", i, joint.Offset.X, joint.Offset.Y, joint.Offset.Z,
                          i ? "3 Zrotation Xrotation Yrotation" : "6 Xposition Yposition Zposition Zrotation Xrotation Yrotation");
            Bvh += text;
            bool bChild = false;
            for (size_t child = i + 1; child < JointCount; ++child)
            {
                if (Joints[child].Parent == int(i))
                {
                    bChild = true;
                    AppendJoint(child);
                }
            }
            Bvh += bChild ? "}\n" : "End Site { OFFSET 0 -.1 0 } }\n";
        }
        HumanoidWalkInput()
        {
            Bvh = "HIERARCHY\n";
            AppendJoint(0);
            Bvh += "MOTION\nFrames: 61\nFrame Time: 0.033333333333333333\n";
            char text[512];
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                // rest の足首は地面にあり、歩行中は腰を下げて膝を曲げる。
                std::snprintf(text, sizeof(text), "0 -.2 %.17g ", Speed * frame / 30);
                Bvh += text;
                double angles[JointCount]{};
                for (size_t i = 1; i < 14; ++i)
                {
                    angles[i] = .04 * std::sin(2 * Pi * frame / 30 + .37 * i);
                }
                for (size_t leg = 0; leg < LegCount; ++leg)
                {
                    const double phase = Phase(frame, leg);
                    double z = Speed * (.3 - phase / 30), y = 0;
                    if (phase > 18)
                    {
                        const double t = (phase - 18) / 12;
                        const double smooth = t * t * t * (10 + t * (-15 + 6 * t));
                        z += Speed * smooth;
                        y = .15 * std::pow(std::sin(Pi * t), 4);
                    }
                    const double down = 1.2 - y;
                    const double distance = std::hypot(down, z);
                    const double bend = std::acos(std::clamp(distance / 1.4, -1.0, 1.0));
                    // X 軸の解析的二関節 IK で、足首の目標を矢状面内に置く。
                    const double upper = std::atan2(-z, down) - bend, lower = 2 * bend;
                    angles[Upper[leg]] = upper;
                    angles[Upper[leg] + 1] = lower;
                    angles[Foot[leg]] = -upper - lower;
                }
                for (double angle : angles)
                {
                    std::snprintf(text, sizeof(text), "0 %.17g 0 ", angle * 180 / Pi);
                    Bvh += text;
                }
                Bvh += "\n";
            }
            Target = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,22]}],"nodes":[)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                auto offset = Joints[i].Offset;
                if (i == 0)
                {
                    offset.Y *= ExpectedRootScale;
                }
                else if (i <= 5)
                {
                    offset.Y *= .9;
                }
                else if (i < 14)
                {
                    offset.X *= 1.1;
                    offset.Y *= 1.1;
                }
                // 左右は同じ脚長にし、太腿と下腿の比を変えて接地中の滑りを作る。
                else if (i == 15 || i == 19)
                {
                    offset.Y *= 1.4;
                }
                std::snprintf(text, sizeof(text), R"({"name":"T%zu","translation":[%.17g,%.17g,%.17g])",
                              i, offset.X, offset.Y, offset.Z);
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
            const auto buffers = Files.Json.find("\"buffers\":");
            const auto skins = Files.Json.find("\"skins\":");
            Target += Files.Json.substr(buffers, skins - buffers);
            Target += R"("skins":[{"skeleton":0,"joints":[)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), "%s%zu", i ? "," : "", i);
                Target += text;
            }
            Target += "]}]}";
            Profile = R"({"version":2,"vocabulary":"humanoid_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":["S%zu"])", i ? "," : "", Joints[i].Role, i);
                Profile += text;
            }
            Profile += "},\"target_roles\":{";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":[{"joint":"T%zu"}])", i ? "," : "", Joints[i].Role, i);
                Profile += text;
            }
            Profile += R"(},"rest_pose":{"mode":"align_bones","roles":{)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                const bool bForward = i == 16 || i == 17 || i == 20 || i == 21;
                std::snprintf(text, sizeof(text), R"(%s"%s":{"up_hint":[0,%d,%d]})",
                              i ? "," : "", Joints[i].Role, bForward ? 1 : 0, bForward ? 0 : 1);
                Profile += text;
            }
            Profile += R"(}},"proportions":{"root_scale":"auto"},"processing":{"loop_mode":"none","extract_root_motion":false,"output_fps":30},"ground":{"height":0},"foot_planting":{"enabled":true,"ik":{"leg_roles":["L_foot","R_foot"]}},"report":{"max_joint_error_deg":0.1,"max_foot_slide_m":0.005,"max_ik_failed_frames":0}})";
            QuadrupedInput::Write("RigV1Fixture/humanoid.bvh", Bvh);
            QuadrupedInput::Write("RigV1Fixture/target.gltf", Target);
        }
        void Measure(Measurement& out) const
        {
            for (size_t leg = 0; leg < LegCount; ++leg)
            {
                B::Vector3d anchor{};
                for (uint32_t frame = 0; frame < FrameCount; ++frame)
                {
                    if (IsStance(frame, leg))
                    {
                        const auto foot = out.Positions[frame][Foot[leg]];
                        if (!frame || !IsStance(frame - 1, leg))
                        {
                            anchor = foot;
                        }
                        out.Slide = std::max(out.Slide, std::hypot(foot.X - anchor.X, foot.Z - anchor.Z));
                        out.HeightError = std::max(out.HeightError, std::abs(foot.Y));
                    }
                }
                for (uint32_t frame = 1; frame < FrameCount; ++frame)
                {
                    if (IsStance(frame, leg) || IsStance(frame - 1, leg))
                    {
                        continue;
                    }
                    for (size_t joint = Upper[leg]; joint <= Foot[leg]; ++joint)
                    {
                        const auto delta = out.Locals[frame - 1][joint].Inverse() * out.Locals[frame][joint];
                        const double speed = 60 * std::atan2(std::hypot(delta.X, delta.Y, delta.Z), std::abs(delta.W));
                        if (speed > out.SwingSpeed)
                        {
                            out.SwingSpeed = speed;
                            out.SwingLeg = leg;
                            out.SwingJoint = joint;
                            out.SwingFrame = frame;
                        }
                    }
                }
            }
        }
        double MaximumGoalError(const Measurement& planted, const Measurement& control) const
        {
            double maximum = 0;
            for (size_t leg = 0; leg < LegCount; ++leg)
            {
                B::Vector3d anchor{};
                for (uint32_t frame = 0; frame < FrameCount; ++frame)
                {
                    if (IsStance(frame, leg))
                    {
                        if (!frame || !IsStance(frame - 1, leg))
                        {
                            anchor = control.Positions[frame][Foot[leg]];
                        }
                        const auto foot = planted.Positions[frame][Foot[leg]];
                        maximum = std::max(maximum, std::hypot(foot.X - anchor.X, foot.Z - anchor.Z));
                    }
                }
            }
            return maximum;
        }
        bool CheckSource() const
        {
            B::BvhDocument document;
            RETARGET_REQUIRE(B::DecodeBvh(F::View(Bvh), {}, document).Succeeded());
            Measurement source;
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                B::BvhPose pose;
                RETARGET_REQUIRE(B::EvaluateBvhFrame(document, frame, B::TranslationConvention::OffsetPlusChannels, pose).Succeeded());
                RETARGET_REQUIRE(pose.Joints.size() == JointCount);
                for (size_t joint = 0; joint < JointCount; ++joint)
                {
                    source.Positions[frame][joint] = pose.Joints[joint].World.Translation;
                }
            }
            Measure(source);
            std::printf("RETARGET_COOK_METRICS humanoid_source slide_m=%.12g height_error_m=%.12g\n", source.Slide, source.HeightError);
            RETARGET_REQUIRE(source.Slide < .001 && source.HeightError < .001);
            return true;
        }
        bool CookAndMeasure(const F::Text& profileText, bool bPlanting, Measurement& out) const
        {
            QuadrupedInput::Write("RigV1Fixture/roles.json", profileText);
            Cook::SingleAssetCookRequest request;
            request.InputPath = "RigV1Fixture/humanoid.bvh";
            request.RetargetSkeletonPath = "RigV1Fixture/target.gltf";
            request.RetargetProfilePath = "RigV1Fixture/roles.json";
            request.RetargetReportPath = "humanoid-report.json";
            request.RetargetClipName = "Walk";
            request.LogicalPath = "Animations/Walk";
            request.Kind = "animation";
            request.Format = "nvskel.v1.clips";
            Cook::CookDependencySnapshot dependencies;
            F::Text error;
            F::Bytes payload;
            RETARGET_REQUIRE(Cook::CaptureCookDependencySnapshot(request, 1, dependencies, error));
            const bool bCooked = Cook::Detail::CookRigRetargetBank(request, F::View(Bvh), dependencies, payload, error);
            if (!bCooked)
            {
                std::fprintf(stderr, "人型歩行の cook に失敗: %s\n", error.c_str());
                if (std::filesystem::exists(request.RetargetReportPath))
                {
                    const auto report = F::ReadBytes(request.RetargetReportPath);
                    std::fwrite(report.data(), 1, report.size(), stderr);
                }
            }
            RETARGET_REQUIRE(bCooked);
            Core::JsonDocument report;
            RETARGET_REQUIRE(Core::JsonDocument::TryParseUtf8(F::View(F::ReadBytes(request.RetargetReportPath)), report));
            RETARGET_REQUIRE(report.GetRoot().FindMember("result").FindMember("status").AsString() == "within");
            const auto clips = report.GetRoot().FindMember("clips");
            RETARGET_REQUIRE(clips.IsArray() && clips.GetArraySize() == 1);
            const auto entry = clips.GetArrayElement(0);
            const auto scale = entry.FindMember("root_scale");
            const auto rotation = entry.FindMember("rotation_retarget_error_deg").FindMember("max");
            RETARGET_REQUIRE(scale.IsNumber() && rotation.IsNumber());
            out.RootScale = scale.AsNumber();
            out.RotationError = rotation.AsNumber();
            if (bPlanting)
            {
                const auto failed = entry.FindMember("ik_failed_frames"), drop = entry.FindMember("pelvis_drop_m");
                RETARGET_REQUIRE(failed.IsNumber() && drop.IsNumber());
                out.Failed = failed.AsNumber();
                out.Drop = drop.AsNumber();
                const auto contacts = entry.FindMember("foot_contacts");
                RETARGET_REQUIRE(contacts.IsArray() && contacts.GetArraySize() == LegCount);
                bool bSeen[LegCount]{};
                for (size_t index = 0; index < LegCount; ++index)
                {
                    const auto contact = contacts.GetArrayElement(index);
                    size_t leg = 0;
                    while (leg < LegCount && contact.FindMember("leg").AsString() != Joints[Foot[leg]].Role)
                    {
                        ++leg;
                    }
                    RETARGET_REQUIRE(leg < LegCount && !bSeen[leg]);
                    bSeen[leg] = true;
                    bool bReported[FrameCount]{};
                    const auto ranges = contact.FindMember("frames");
                    RETARGET_REQUIRE(ranges.IsArray() && ranges.GetArraySize() > 0);
                    for (size_t range = 0; range < ranges.GetArraySize(); ++range)
                    {
                        const auto pair = ranges.GetArrayElement(range);
                        RETARGET_REQUIRE(pair.IsArray() && pair.GetArraySize() == 2);
                        RETARGET_REQUIRE(pair.GetArrayElement(0).IsNumber() && pair.GetArrayElement(1).IsNumber());
                        const double begin = pair.GetArrayElement(0).AsNumber(), end = pair.GetArrayElement(1).AsNumber();
                        RETARGET_REQUIRE(begin >= 0 && end <= FrameCount && begin < end && begin == std::floor(begin) && end == std::floor(end));
                        for (uint32_t frame = uint32_t(begin); frame < uint32_t(end); ++frame)
                        {
                            RETARGET_REQUIRE(!bReported[frame]);
                            bReported[frame] = true;
                        }
                    }
                    size_t disagreement = 0;
                    for (uint32_t frame = 0; frame < FrameCount; ++frame)
                    {
                        disagreement += bReported[frame] != IsStance(frame, leg);
                    }
                    // 両端の接地と遊脚のどちらも、既知の姿勢に一致することを確かめる。
                    RETARGET_REQUIRE(bReported[0] == IsStance(0, leg));
                    RETARGET_REQUIRE(bReported[FrameCount - 1] == IsStance(FrameCount - 1, leg));
                    // 既存の歩行試験と同じく境界の差を各周期で最大 2 コマ許す。
                    RETARGET_REQUIRE(disagreement <= 8);
                }
            }
            S::ClipBankV1 bank;
            S::RigV1Report rigReport;
            S::RigV1Limits limits;
            limits.MaxJoints = 256;
            constexpr auto profile = S::RigImportProfile::StaticRootFrame256;
            RETARGET_REQUIRE(S::ParseClipBankV1(F::View(payload), bank, rigReport, limits, profile));
            S::RigAuthoringCpu target;
            S::RigClipSourceSelection selection;
            selection.bAllowEmptyAnimations = true;
            RETARGET_REQUIRE(S::DecodeRigAuthoringWithProfileNativePath(F::View(Target), request.RetargetSkeletonPath,
                                                                       profile, target, rigReport, limits, nullptr, nullptr, nullptr, &selection));
            const auto* author = target.GetData();
            RETARGET_REQUIRE(author && bank.GetData() && bank.GetData()->Clips.size() == 1);
            const auto& clip = bank.GetData()->Clips[0];
            RETARGET_REQUIRE(clip.RootMotion.empty() && std::abs(clip.DurationSeconds - 2) < 1e-6);
            B::BvhDocument document;
            RETARGET_REQUIRE(B::DecodeBvh(F::View(Bvh), {}, document).Succeeded());
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                B::BvhPose source;
                RETARGET_REQUIRE(B::EvaluateBvhFrame(document, frame, B::TranslationConvention::OffsetPlusChannels, source).Succeeded());
                RETARGET_REQUIRE(source.Joints.size() == JointCount);
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
                Q rotations[JointCount];
                for (size_t joint = 0; joint < JointCount; ++joint)
                {
                    RETARGET_REQUIRE(local[joint].Scale.X == 1 && local[joint].Scale.Y == 1 && local[joint].Scale.Z == 1);
                    RETARGET_REQUIRE(author->Geometry.Joints[joint].ParentIndex == Joints[joint].Parent);
                    const auto q = Q::FromValue(local[joint].Rotation);
                    RETARGET_REQUIRE(std::isfinite(q.X) && std::isfinite(q.Y) && std::isfinite(q.Z) && std::isfinite(q.W));
                    out.Locals[frame][joint] = q;
                    const auto parent = Joints[joint].Parent;
                    const B::Vector3d t{local[joint].Translation.X, local[joint].Translation.Y, local[joint].Translation.Z};
                    rotations[joint] = parent < 0 ? q : rotations[parent] * q;
                    const auto position = parent < 0 ? t : QuadrupedInput::Add(out.Positions[frame][parent], rotations[parent].Rotate(t));
                    RETARGET_REQUIRE(std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z));
                    out.Positions[frame][joint] = position;
                    if (parent >= 0 && !bPlanting)
                    {
                        const auto a = QuadrupedInput::Subtract(position, out.Positions[frame][parent]);
                        const auto b = QuadrupedInput::Subtract(source.Joints[joint].World.Translation, source.Joints[parent].World.Translation);
                        RETARGET_REQUIRE(QuadrupedInput::Length(a) > 1e-8 && QuadrupedInput::Length(b) > 1e-8);
                        const B::Vector3d cross{a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
                        const double degrees = std::atan2(QuadrupedInput::Length(cross), a.X * b.X + a.Y * b.Y + a.Z * b.Z) * 180 / Pi;
                        RETARGET_REQUIRE(std::isfinite(degrees));
                        out.DirectionError = std::max(out.DirectionError, degrees);
                    }
                }
            }
            Measure(out);
            return true;
        }
    };

    bool V2HumanoidProportionsAndReplanting()
    {
        HumanoidWalkInput input;
        RETARGET_REQUIRE(input.CheckSource());
        HumanoidWalkInput::Measurement planted, control;
        auto disabled = F::Replace(input.Profile, R"("enabled":true)", R"("enabled":false)");
        disabled = F::Replace(disabled, R"(,"max_foot_slide_m":0.005,"max_ik_failed_frames":0)", "");
        RETARGET_REQUIRE(input.CookAndMeasure(disabled, false, control));
        RETARGET_REQUIRE(input.CookAndMeasure(input.Profile, true, planted));
        RETARGET_REQUIRE(control.SwingSpeed > 0);
        const double speedRatio = planted.SwingSpeed / control.SwingSpeed;
        const double goalError = input.MaximumGoalError(planted, control);
        std::printf("RETARGET_COOK_METRICS humanoid rotation_max_deg=%.12g direction_max_deg=%.12g root_scale=%.12g slide_on_m=%.12g slide_off_m=%.12g height_error_m=%.12g swing_on_rad_s=%.12g swing_off_rad_s=%.12g angular_speed_ratio=%.12g pelvis_drop_m=%.12g ik_failed_frames=%.0f goal_horizontal_error_m=%.12g\n",
                    planted.RotationError, control.DirectionError, planted.RootScale, planted.Slide, control.Slide,
                    planted.HeightError, planted.SwingSpeed, control.SwingSpeed, speedRatio, planted.Drop, planted.Failed, goalError);
        std::printf("RETARGET_COOK_METRICS humanoid_swing on_leg=%s on_joint=%s on_frame=%u off_leg=%s off_joint=%s off_frame=%u\n",
                    input.Joints[input.Foot[planted.SwingLeg]].Role, input.Joints[planted.SwingJoint].Role, planted.SwingFrame,
                    input.Joints[input.Foot[control.SwingLeg]].Role, input.Joints[control.SwingJoint].Role, control.SwingFrame);
        RETARGET_REQUIRE(std::isfinite(planted.RotationError) && planted.RotationError >= 0 && planted.RotationError <= .1);
        RETARGET_REQUIRE(std::isfinite(control.RotationError) && control.RotationError >= 0 && control.RotationError <= .1);
        RETARGET_REQUIRE(control.DirectionError <= .1);
        RETARGET_REQUIRE(std::isfinite(planted.RootScale) && std::abs(planted.RootScale - HumanoidWalkInput::ExpectedRootScale) <= 1e-3);
        RETARGET_REQUIRE(std::isfinite(control.RootScale) && std::abs(control.RootScale - HumanoidWalkInput::ExpectedRootScale) <= 1e-3);
        RETARGET_REQUIRE(planted.Slide <= .005 && planted.HeightError <= .003 && planted.Failed == 0);
        RETARGET_REQUIRE(control.Slide >= .05 && speedRatio <= 1.5 && goalError <= .005);
        std::puts("RETARGET_COOK_CASE humanoid_proportions_replanting result=pass");
        return true;
    }

} // namespace

int main()
{
    const bool bV1 = V1ProfileOutputIsPinned();
    const bool bV2 = V2QuadrupedProportionsAndReport();
    const bool bReplanting = V2GroundedWalkReplanting();
    const bool bRatioTwo = V2GroundedWalkRatioTwo();
    const bool bHumanoid = V2HumanoidProportionsAndReplanting();
    return bV1 && bV2 && bReplanting && bRatioTwo && bHumanoid ? 0 : 1;
}
