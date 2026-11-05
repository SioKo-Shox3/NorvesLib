// 合成の四足で cook の接地、足の印、根の移動、時間補正と拒否時の不変性を反証する。
#include "RigSplitTestFixture.h"
#include "Tools/AssetCook/RigMotionCleanup.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifdef NORVES_MOTION_SAMPLE_REGRESSION_TEST
#include "Animation/SkeletalClipRetarget.h"
#include "Animation/SkeletalClipSampling.h"
#endif

#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "動きの後処理の検査に失敗 %s:%d %s\n", __FILE__, __LINE__, #x);                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    namespace Core = NorvesLib::Core;
    namespace A = Core::Animation;
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    namespace F = NorvesLib::Tests::RigV1Fixture;
    namespace Cook = NorvesLib::Tools::AssetCook::Detail;
    constexpr double Fps = 200, Speed = 1.25, Stance = .6;
    constexpr double Touchdown[] = {.07, .23, .49, .71};
    constexpr const char* Feet[] = {"front_L_paw", "front_R_paw", "hind_L_paw", "hind_R_paw"};
    constexpr const char* Legs[] = {"LF", "RF", "LH", "RH"};

    C::AnsiString ProfileJson(const C::AnsiString& processing, bool bFeet = true)
    {
        C::AnsiString text =
            R"({"version":1,"vocabulary":"quadruped_v1","axes":{"up":"+Y","forward":"+Z","handedness":"right"},"units":{"position_scale":1},"position_convention":"additive","time":{"mode":"header_frame_time"},"source_roles":{"root":["Root"] )";
        if (bFeet)
        {
            for (const char* foot : Feet)
            {
                text += C::AnsiString(",\"") + foot + "\":[\"" + foot + "\"]";
            }
        }
        text += R"(},"target_roles":{"root":[{"joint":"Root","C":[1,0,0,0,1,0,0,0,1]}])";
        if (bFeet)
        {
            for (const char* foot : Feet)
            {
                text += C::AnsiString(",\"") + foot + "\":[{\"joint\":\"" + foot + "\",\"C\":[1,0,0,0,1,0,0,0,1]}]";
            }
        }
        return text + "},\"processing\":{" + processing + "}}";
    }
    A::SkeletalRoleProfile Profile(const C::AnsiString& extra = "", bool bFeet = true)
    {
        A::SkeletalRoleProfile profile;
        const auto text = ProfileJson(
            C::AnsiString(
                R"("output_fps":200,"loop_mode":"none","analyze_contacts":true,"generate_foot_markers":true,"derive_root_motion":true)") +
                extra,
            bFeet);
        const auto result = A::ParseSkeletalRoleProfile(F::View(text), {}, profile);
        if (!result.Succeeded())
        {
            std::fprintf(stderr, "合成の役割設定が拒否された: %s\n", result.Field.c_str());
        }
        CHECK(result.Succeeded());
        return profile;
    }
    S::RigAuthoringCpu Skeleton(F::Fixture& fixture)
    {
        auto rig = fixture.Import(fixture.Json);
        // デコーダが可変として確保した、この試験だけが所有するデータに合成骨格を置く。
        // 不正な階層も同じ入口へ渡し、デコーダ側の拒否を後処理の拒否と取り違えない。
        auto& data = *const_cast<S::RigAuthoringData*>(rig.GetData());
        data.Geometry.Joints.clear();
        data.LocalRest.clear();
        data.RootFrame = S::IdentityRigRootFrame();
        for (size_t i = 0; i < 5; ++i)
        {
            S::SkeletalJoint joint;
            joint.Name = C::String(i ? Feet[i - 1] : "Root");
            joint.ParentIndex = i ? 0 : -1;
            data.Geometry.Joints.push_back(joint);
            data.LocalRest.push_back({});
        }
        return rig;
    }
    S::SkeletalAnimationClip Clip(bool bLoop = true)
    {
        S::SkeletalAnimationClip clip;
        clip.Name = "SyntheticQuadruped";
        clip.DurationSeconds = 1;
        clip.Metadata.Loop = {bLoop, 0, bLoop ? 1.f : 0.f};
        for (size_t foot = 0; foot < 4; ++foot)
        {
            S::SkeletalAnimationChannel channel;
            channel.JointIndex = uint32_t(foot + 1);
            channel.Path = S::SkeletalAnimationPath::Translation;
            for (size_t i = 0; i <= size_t(Fps); ++i)
            {
                const double time = i / Fps;
                const double phase = time - Touchdown[foot] - std::floor(time - Touchdown[foot]);
                double height = 0, z = -Speed * phase;
                if (phase > Stance)
                {
                    const double swing = (phase - Stance) / (1 - Stance);
                    height = .3 * std::sin(3.14159265358979323846 * swing);
                    z = -Speed * Stance * (1 - swing);
                }
                channel.Samples.push_back({float(time), {0, float(height), float(z), 0}});
            }
            clip.Channels.push_back(std::move(channel));
        }
        return clip;
    }
    bool Cleanup(const S::RigAuthoringCpu& rig, const A::SkeletalRoleProfile& profile, S::SkeletalAnimationClip& clip,
                 Core::JsonDocument& report, C::AnsiString& error)
    {
        Core::JsonWriter writer;
        writer.BeginObject();
        const bool bSuccess = Cook::ApplyRigMotionCleanup(rig, profile, clip, writer, error);
        writer.EndObject();
        CHECK(writer.IsComplete());
        CHECK(Core::JsonDocument::TryParse(writer.ToString(), report));
        return bSuccess;
    }
    Core::JsonDocument Run(const S::RigAuthoringCpu& rig, const A::SkeletalRoleProfile& profile,
                           S::SkeletalAnimationClip& clip)
    {
        Core::JsonDocument report;
        C::AnsiString error;
        const bool bSuccess = Cleanup(rig, profile, clip, report, error);
        if (!bSuccess)
        {
            std::fprintf(stderr, "後処理が拒否された: %s\n", error.c_str());
        }
        CHECK(bSuccess);
        return report;
    }
    struct Interval
    {
        double Start, End;
    };
    C::VariableArray<Interval> Expected(size_t foot)
    {
        C::VariableArray<Interval> intervals;
        // 接地時刻と接地率から区間を解析的に切り出す。標本や製品の閾値には依存しない。
        for (int cycle = -1; cycle <= 0; ++cycle)
        {
            const double start = std::max(0., Touchdown[foot] + cycle);
            const double end = std::min(1., Touchdown[foot] + cycle + Stance);
            if (end > start)
            {
                intervals.push_back({start, end});
            }
        }
        return intervals;
    }
    void CheckContacts(const S::SkeletalAnimationClip& clip, const Core::JsonDocument& report)
    {
        const auto contacts = report.GetRoot().FindMember("contacts");
        CHECK(contacts.IsArray());
        for (const auto& event : clip.Metadata.Events)
        {
            CHECK(event.Name.GetView().substr(0, 5) != C::StringView(_T("Foot.")));
        }
        size_t expectedCount = 0;
        for (size_t foot = 0; foot < 4; ++foot)
        {
            const auto expected = Expected(foot);
            expectedCount += expected.size();
            C::VariableArray<Interval> actual;
            for (size_t i = 0; i < contacts.GetArraySize(); ++i)
            {
                const auto contact = contacts.GetArrayElement(i);
                if (contact.FindMember("foot").AsString() != C::String(Feet[foot]))
                {
                    continue;
                }
                const double start = contact.FindMember("start").AsNumber();
                const double end = contact.FindMember("end").AsNumber();
                CHECK(end > start);
                actual.push_back({start, end});
            }
            CHECK(actual.size() == expected.size());
            double intersection = 0, expectedLength = 0, actualLength = 0;
            for (const auto& a : actual)
            {
                actualLength += a.End - a.Start;
                for (const auto& e : expected)
                {
                    intersection += std::max(0., std::min(a.End, e.End) - std::max(a.Start, e.Start));
                }
            }
            for (const auto& e : expected)
            {
                expectedLength += e.End - e.Start;
            }
            const double iou = intersection / (expectedLength + actualLength - intersection);
            std::printf("CONTACT foot=%s iou=%.6f\n", Feet[foot], iou);
            CHECK(iou >= .9);
        }
        CHECK(contacts.GetArraySize() == expectedCount);
    }
    void CheckMarkers(const S::SkeletalAnimationClip& clip)
    {
        CHECK(clip.Metadata.Markers.size() == 8);
        for (size_t foot = 0; foot < 4; ++foot)
        {
            for (bool bDown : {true, false})
            {
                const auto name = C::AnsiString("Foot.") + Legs[foot] + (bDown ? ".Down" : ".Up");
                const double time = bDown ? Touchdown[foot] : std::fmod(Touchdown[foot] + Stance, 1.);
                size_t matches = 0;
                for (const auto& marker : clip.Metadata.Markers)
                {
                    if (marker.Name == Core::Identity(name.c_str()))
                    {
                        CHECK(std::abs(marker.Time - time) <= 1 / Fps + 1e-6);
                        ++matches;
                    }
                }
                CHECK(matches == 1);
            }
        }
    }
    void TestMotion(const S::RigAuthoringCpu& rig)
    {
        const auto profile = Profile();
        auto clip = Clip();
        const auto report = Run(rig, profile, clip);
        CheckContacts(clip, report);
        CheckMarkers(clip);
        CHECK(clip.Metadata.Root.Mode == A::RootMotionMode::Extract);
        CHECK(!clip.RootMotion.empty());
        CHECK(std::abs(std::hypot(clip.RootMotion.back().TranslationX, clip.RootMotion.back().TranslationZ) - Speed) <=
              Speed * .02);
        CHECK(std::abs(clip.Metadata.Root.NominalSpeed - Speed) <= Speed * .02);
        CHECK(std::abs(report.GetRoot().FindMember("natural_ground_speed").AsNumber() - Speed) <= Speed * .02);
        CHECK(report.GetRoot().FindMember("derived_root_motion").AsBool());
        CHECK(!report.GetRoot().FindMember("needs_review").AsBool());
        auto onceForRetime = Clip(false);
        Run(rig, profile, onceForRetime);
        CHECK(!onceForRetime.Metadata.Events.empty());
        for (const char* extra : {",\"desired_ground_speed\":0.625", ",\"desired_ground_speed\":2.5"})
        {
            const auto retimeProfile = Profile(extra);
            auto retimed = Clip();
            const auto retimedReport = Run(rig, retimeProfile, retimed);
            const double ratio = Speed / retimeProfile.Processing.DesiredGroundSpeed;
            CHECK(std::abs(retimed.DurationSeconds - clip.DurationSeconds * ratio) <= ratio * .01);
            CHECK(std::abs(retimedReport.GetRoot().FindMember("ground_speed_retime").AsNumber() - ratio) <=
                  ratio * .01);
            CHECK(retimed.Metadata.Markers.size() == clip.Metadata.Markers.size());
            CHECK(retimed.Metadata.Events.size() == clip.Metadata.Events.size());
            for (size_t i = 0; i < clip.Metadata.Markers.size(); ++i)
            {
                CHECK(retimed.Metadata.Markers[i].Name == clip.Metadata.Markers[i].Name);
                CHECK(std::abs(retimed.Metadata.Markers[i].Time - clip.Metadata.Markers[i].Time * ratio) < 1e-5);
            }
            auto retimedOnce = Clip(false);
            Run(rig, retimeProfile, retimedOnce);
            CHECK(retimedOnce.Metadata.Events.size() == onceForRetime.Metadata.Events.size());
            for (size_t i = 0; i < onceForRetime.Metadata.Events.size(); ++i)
            {
                CHECK(retimedOnce.Metadata.Events[i].Name == onceForRetime.Metadata.Events[i].Name);
                CHECK(std::abs(retimedOnce.Metadata.Events[i].Time - onceForRetime.Metadata.Events[i].Time * ratio) <
                      1e-5);
                CHECK(onceForRetime.Metadata.Events[i].EndTime == -1);
                CHECK(retimedOnce.Metadata.Events[i].EndTime == -1);
            }
        }
        auto existing = Clip();
        existing.Metadata.Markers.push_back({Core::Identity("authored"), .37f});
        const auto existingReport = Run(rig, profile, existing);
        CHECK(existing.Metadata.Markers.size() == 1);
        CHECK(existing.Metadata.Markers[0].Name == Core::Identity("authored"));
        CHECK(existing.Metadata.Markers[0].Time == .37f);
        CheckContacts(existing, existingReport);
        auto noFeet = Clip();
        const auto noFeetBefore = noFeet;
        const auto noFeetReport = Run(rig, Profile("", false), noFeet);
        CHECK(noFeetReport.GetRoot().FindMember("needs_review").AsBool());
        CHECK(noFeet.RootMotion.empty());
        CHECK(A::SameClipMetadata(noFeet.Metadata, noFeetBefore.Metadata));
        // 明示された処理区間もループとして継ぎ目を結ぶ。
        auto rangeProfile = profile;
        rangeProfile.Processing.Loop = A::SkeletalLoopSelection::Range;
        rangeProfile.Processing.RangeEnd = 1;
        auto range = Clip(false);
        Run(rig, rangeProfile, range);
        CheckMarkers(range);
        auto autoProfile = profile;
        autoProfile.Processing.Loop = A::SkeletalLoopSelection::Auto;
        auto automatic = Clip(false);
        Run(rig, autoProfile, automatic);
        CheckMarkers(automatic);
        // 非ループの端点は独立の接地のまま扱う。
        auto onceProfile = profile;
        for (const auto role : {A::SkeletalRole::FrontLPaw, A::SkeletalRole::FrontRPaw, A::SkeletalRole::HindLPaw})
        {
            onceProfile.Target[size_t(role)].clear();
        }
        auto once = Clip(false);
        Run(rig, onceProfile, once);
        CHECK(once.Metadata.Markers.empty());
        size_t matches = 0;
        for (const auto& event : once.Metadata.Events)
        {
            if (event.Name == Core::Identity("Foot.RH.Down") && event.Time == 0)
            {
                CHECK(event.EndTime == -1);
                ++matches;
            }
        }
        CHECK(matches == 1);
    }
    void CheckSame(const S::SkeletalAnimationClip& a, const S::SkeletalAnimationClip& b)
    {
        CHECK(a.Name == b.Name && a.DurationSeconds == b.DurationSeconds);
        CHECK(A::SameClipMetadata(a.Metadata, b.Metadata));
        CHECK(a.RootMotionJoint == b.RootMotionJoint && a.RootMotion.size() == b.RootMotion.size());
        for (size_t i = 0; i < a.RootMotion.size(); ++i)
        {
            const auto& x = a.RootMotion[i];
            const auto& y = b.RootMotion[i];
            CHECK(x.TimeSeconds == y.TimeSeconds && x.TranslationX == y.TranslationX &&
                  x.TranslationZ == y.TranslationZ && x.YawRadians == y.YawRadians);
        }
        CHECK(a.Channels.size() == b.Channels.size());
        for (size_t i = 0; i < a.Channels.size(); ++i)
        {
            const auto& x = a.Channels[i];
            const auto& y = b.Channels[i];
            CHECK(x.JointIndex == y.JointIndex && x.Path == y.Path && x.Interpolation == y.Interpolation);
            CHECK(x.Samples.size() == y.Samples.size());
            for (size_t j = 0; j < x.Samples.size(); ++j)
            {
                CHECK(x.Samples[j].TimeSeconds == y.Samples[j].TimeSeconds);
                const auto& u = x.Samples[j].Value;
                const auto& v = y.Samples[j].Value;
                CHECK(u.X == v.X && u.Y == v.Y && u.Z == v.Z && u.W == v.W);
            }
        }
    }
    void TestRejection(F::Fixture& fixture)
    {
        for (const char* errorName : {"cleanup_cycle", "cleanup_root", "cleanup_sample_count", "cleanup_parent"})
        {
            auto rig = Skeleton(fixture);
            auto& joints = const_cast<S::RigAuthoringData*>(rig.GetData())->Geometry.Joints;
            auto profile = Profile();
            auto clip = Clip();
            clip.Metadata.Events.push_back({Core::Identity("authored_event"), .2f, .4f});
            clip.Metadata.Markers.push_back({Core::Identity("authored_marker"), .3f});
            clip.Metadata.Root = {A::RootMotionMode::InPlace, 0, false, true, false, 2};
            clip.RootMotionJoint = 0;
            clip.RootMotion.push_back({0, 0, 0, .1});
            clip.RootMotion.push_back({1, .2, .3, .4});
            if (C::AnsiString(errorName) == "cleanup_cycle")
            {
                joints[1].ParentIndex = 2;
                joints[2].ParentIndex = 1;
            }
            else if (C::AnsiString(errorName) == "cleanup_root")
            {
                joints[0].ParentIndex = 1;
            }
            else if (C::AnsiString(errorName) == "cleanup_parent")
            {
                joints[1].ParentIndex = 99;
            }
            else
            {
                profile.Processing.OutputFps = 1000;
                clip.DurationSeconds = 66;
                clip.Metadata.Loop.End = 66;
            }
            const auto before = clip;
            Core::JsonDocument report;
            C::AnsiString error;
            CHECK(!Cleanup(rig, profile, clip, report, error));
            CHECK(error == C::AnsiString(errorName));
            CheckSame(clip, before);
        }
    }
    void TestProfileRanges()
    {
        struct Invalid
        {
            const char* Key;
            const char* Value;
        };
        for (const auto& invalid :
             {Invalid{"spike_threshold_degrees", "181"}, Invalid{"spike_window", "0"}, Invalid{"spike_window", "17"},
              Invalid{"smoothing_radius", "17"}, Invalid{"time_scale", "0"}, Invalid{"time_scale", "-1"},
              Invalid{"desired_ground_speed", "-1"}})
        {
            const auto text = ProfileJson(C::AnsiString("\"") + invalid.Key + "\":" + invalid.Value);
            A::SkeletalRoleProfile profile;
            const auto result = A::ParseSkeletalRoleProfile(F::View(text), {}, profile);
            CHECK(!result.Succeeded());
            CHECK(result.Field == C::AnsiString("processing.") + invalid.Key);
        }
    }
} // namespace

#ifndef NORVES_MOTION_SAMPLE_REGRESSION_TEST
int main()
{
    TestProfileRanges();
    F::Fixture fixture;
    const auto rig = Skeleton(fixture);
    TestMotion(rig);
    TestRejection(fixture);
    std::puts("RIG_MOTION_CLEANUP result=pass");
    return 0;
}

#else
namespace
{
    constexpr double Degrees = 180 / 3.14159265358979323846;
    // V01fullの再帰誤差は6.76403533度、周期内の動きは16.6897544度で比は0.4052807。
    // 誤差の約1.25倍を8.5度に丸め、比は余裕を持たせて0.5とする。
    constexpr double SampleLoopRmsThresholdDegrees = 8.5;
    constexpr double SampleLoopRecurrenceRatio = 0.5;

    C::AnsiString SampleJointName(const Core::Bvh::Joint& joint)
    {
        return C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(joint.Name.data()), joint.Name.size()));
    }

    C::AnsiString SampleTarget(F::Fixture& f, const Core::Bvh::BvhDocument& bvh)
    {
        auto binary = f.Binary;
        binary.resize(224 + bvh.Joints.size() * 64, 0);
        C::VariableArray<Core::Bvh::Vector3d> positions(bvh.Joints.size());
        Core::JsonWriter json;
        json.BeginObject();
        json.BeginArray("nodes");
        for (size_t i = 0; i < bvh.Joints.size(); ++i)
        {
            const auto& joint = bvh.Joints[i];
            auto world = joint.Offset;
            if (joint.Parent != UINT32_MAX)
            {
                CHECK(joint.Parent < i);
                world.X += positions[joint.Parent].X;
                world.Y += positions[joint.Parent].Y;
                world.Z += positions[joint.Parent].Z;
            }
            positions[i] = world;
            // OFFSETだけのrestなので、逆バインド行列は累積平行移動の逆になる。
            for (size_t k = 0; k < 16; ++k)
                F::Float(binary, 224 + i * 64 + k * 4, k % 5 == 0 ? 1.f : 0.f);
            F::Float(binary, 224 + i * 64 + 48, float(-world.X));
            F::Float(binary, 224 + i * 64 + 52, float(-world.Y));
            F::Float(binary, 224 + i * 64 + 56, float(-world.Z));
            json.BeginObject();
            json.WriteString("name", SampleJointName(joint).c_str());
            json.BeginArray("translation");
            for (double v : {joint.Offset.X, joint.Offset.Y, joint.Offset.Z})
                json.WriteNumberElement(v);
            json.EndArray();
            json.BeginArray("children");
            for (size_t child = i + 1; child < bvh.Joints.size(); ++child)
                if (bvh.Joints[child].Parent == i)
                    json.WriteNumberElement(double(child));
            json.EndArray();
            json.EndObject();
        }
        json.BeginObject();
        json.WriteString("name", "SampleMesh");
        json.WriteUInt64("mesh", 0);
        json.WriteUInt64("skin", 0);
        json.EndObject();
        json.EndArray();
        json.BeginArray("skins");
        json.BeginObject();
        json.WriteUInt64("inverseBindMatrices", 9);
        json.WriteUInt64("skeleton", 0);
        json.BeginArray("joints");
        for (size_t i = 0; i < bvh.Joints.size(); ++i)
            json.WriteNumberElement(double(i));
        json.EndArray();
        json.EndObject();
        json.EndArray();
        json.WriteUInt64("scene", 0);
        json.BeginArray("scenes");
        json.BeginObject();
        json.BeginArray("nodes");
        json.WriteNumberElement(0);
        json.WriteNumberElement(double(bvh.Joints.size()));
        json.EndArray();
        json.EndObject();
        json.EndArray();
        json.EndObject();
        CHECK(json.IsComplete());
        // 合成三角形の頂点データだけを再利用し、骨格とIBMとsceneはBVHから生成する。
        const auto begin = f.Json.find(C::AnsiStringView("\"buffers\":"));
        const auto end = f.Json.find(C::AnsiStringView("\"skins\":"));
        CHECK(begin != C::AnsiString::npos && end > begin);
        char length[64], matrices[64], count[64];
        std::snprintf(length, sizeof(length), "\"byteLength\":%zu", binary.size());
        std::snprintf(matrices, sizeof(matrices), "\"byteLength\":%zu", bvh.Joints.size() * 64);
        std::snprintf(count, sizeof(count), "\"count\":%zu,\"type\":\"MAT4\"", bvh.Joints.size());
        auto resources = F::Replace(f.Json.substr(begin, end - begin), "\"byteLength\":416", length);
        resources = F::Replace(resources, "\"byteLength\":128", matrices);
        resources = F::Replace(resources, "\"count\":2,\"type\":\"MAT4\"", count);
        const C::AnsiString generated(json.ToString().c_str());
        auto target = C::AnsiString("{\"asset\":{\"version\":\"2.0\"},") + resources + generated.substr(1);
        f.SetBuffer(binary);
        F::WriteBytes(f.Root / "RigV1Fixture/target.gltf", F::Bytes(target.begin(), target.end()));
        return target;
    }

    C::AnsiString SampleProfile(const Core::Bvh::BvhDocument& bvh, bool loop, bool spikes)
    {
        Core::JsonWriter json;
        json.BeginObject();
        json.WriteUInt64("version", 1);
        json.WriteString("vocabulary", "quadruped_v1");
        json.BeginObject("axes");
        json.WriteString("up", "+Y");
        json.WriteString("forward", "+Z");
        json.WriteString("handedness", "right");
        json.EndObject();
        json.BeginObject("units");
        json.WriteNumber("position_scale", 1);
        json.EndObject();
        json.WriteString("position_convention", "additive");
        json.BeginObject("time");
        json.WriteString("mode", "header_frame_time");
        json.EndObject();
        // 同一骨格の恒等写像。可変長のspine枠で、root以外も省略せず全関節を写す。
        // 接地の役割解析はこの回帰の対象外なので有効にしない。
        for (bool target : {false, true})
        {
            json.BeginObject(target ? "target_roles" : "source_roles");
            for (bool root : {true, false})
            {
                json.BeginArray(root ? "root" : "spine");
                for (size_t i = root ? 0 : 1; i < (root ? 1 : bvh.Joints.size()); ++i)
                {
                    const auto name = SampleJointName(bvh.Joints[i]);
                    if (target)
                    {
                        json.BeginObject();
                        json.WriteString("joint", name.c_str());
                        json.BeginArray("C");
                        for (size_t k = 0; k < 9; ++k)
                            json.WriteNumberElement(k % 4 == 0 ? 1 : 0);
                        json.EndArray();
                        json.EndObject();
                    }
                    else
                        json.WriteStringElement(C::String(name.c_str()));
                }
                json.EndArray();
            }
            json.EndObject();
        }
        json.BeginObject("processing");
        // ヘッダの丸められたFrame Timeを逆数にせず、30 Hzの出力格子を使う。
        // 逆数だとfloatのDurationと最終キーが同時刻に丸まり、処理が拒否される。
        json.WriteNumber("output_fps", 30);
        json.WriteString("loop_mode", loop ? "auto" : "none");
        if (loop)
        {
            json.WriteNumber("loop_rms_threshold_degrees", SampleLoopRmsThresholdDegrees);
            json.WriteNumber("loop_recurrence_ratio", SampleLoopRecurrenceRatio);
        }
        json.WriteBool("average_cycles", loop);
        json.WriteUInt64("smoothing_radius", 0);
        // 判定量はコマ間の回転量ではなく、前後のslerp中点からの補間誤差。
        // 15度は21コマ目の23.34度より小さく、近傍の統計による閾値約4.9度より大きい。
        json.WriteNumber("spike_threshold_degrees", spikes ? 15 : 0);
        json.WriteUInt64("spike_window", 2);
        json.EndObject();
        json.EndObject();
        CHECK(json.IsComplete());
        return C::AnsiString(json.ToString().c_str());
    }

    S::SkeletalValue SampleRotation(const S::SkeletalAnimationClip& clip, uint32_t joint, double seconds)
    {
        for (const auto& channel : clip.Channels)
        {
            if (channel.JointIndex != joint || channel.Path != S::SkeletalAnimationPath::Rotation)
                continue;
            CHECK(!channel.Samples.empty());
            const float time = float(seconds);
            if (time <= channel.Samples.front().TimeSeconds)
                return channel.Samples.front().Value;
            if (time >= channel.Samples.back().TimeSeconds)
                return channel.Samples.back().Value;
            const auto next = std::lower_bound(channel.Samples.begin(), channel.Samples.end(), time,
                                               [](const auto& sample, float t) { return sample.TimeSeconds < t; });
            return A::Detail::SampleSkeletalChannelInterval(channel, *(next - 1), *next, time);
        }
        CHECK(false);
        return {};
    }

    double SampleAngle(const S::SkeletalValue& a, const S::SkeletalValue& b)
    {
        const double dot = double(a.X) * b.X + double(a.Y) * b.Y + double(a.Z) * b.Z + double(a.W) * b.W;
        const double aa = double(a.X) * a.X + double(a.Y) * a.Y + double(a.Z) * a.Z + double(a.W) * a.W;
        const double bb = double(b.X) * b.X + double(b.Y) * b.Y + double(b.Z) * b.Z + double(b.W) * b.W;
        CHECK(aa > 0 && bb > 0);
        return 2 * std::acos(std::clamp(std::abs(dot) / std::sqrt(aa * bb), 0., 1.)) * Degrees;
    }

    // スパイク除去後の未ループ化クリップから、周期内の重み付き回転量を推定する。
    double SampleCycleMotionDegrees(const S::RigAuthoringCpu& target, const S::SkeletalAnimationClip& clip, double period)
    {
        const auto& data = *target.GetData();
        const size_t count = data.Geometry.Joints.size();
        C::VariableArray<Core::Bvh::Vector3d> positions(count);
        C::VariableArray<double> weights(count);
        for (size_t i = 0; i < count; ++i)
        {
            const auto& t = data.LocalRest[i].Translation;
            positions[i] = {t.X, t.Y, t.Z};
            const auto parent = data.Geometry.Joints[i].ParentIndex;
            if (parent >= 0)
            {
                positions[i].X += positions[size_t(parent)].X;
                positions[i].Y += positions[size_t(parent)].Y;
                positions[i].Z += positions[size_t(parent)].Z;
            }
        }
        double totalWeight = 0;
        for (size_t i = 0; i < count; ++i)
        {
            double length = 0;
            for (size_t child = 0; child < count; ++child)
            {
                auto parent = data.Geometry.Joints[child].ParentIndex;
                while (parent >= 0 && size_t(parent) != i)
                {
                    parent = data.Geometry.Joints[size_t(parent)].ParentIndex;
                }
                if (parent >= 0)
                {
                    const auto& a = positions[i];
                    const auto& b = positions[child];
                    length = std::max(length, std::hypot(b.X - a.X, b.Y - a.Y, b.Z - a.Z));
                }
            }
            const auto parent = data.Geometry.Joints[i].ParentIndex;
            if (length < 1e-8 && parent >= 0)
            {
                const auto& a = positions[i];
                const auto& b = positions[size_t(parent)];
                length = std::hypot(b.X - a.X, b.Y - a.Y, b.Z - a.Z);
            }
            weights[i] = std::max(length, 1e-6);
            totalWeight += weights[i];
        }
        double motion = 0;
        for (uint32_t sample = 1; sample <= 16; ++sample)
        {
            double sum = 0;
            for (uint32_t joint = 0; joint < count; ++joint)
            {
                const double angle = SampleAngle(SampleRotation(clip, joint, 0),
                                                 SampleRotation(clip, joint, period * sample / 16));
                sum += weights[joint] * angle * angle;
            }
            motion = std::max(motion, std::sqrt(sum / totalWeight));
        }
        return motion;
    }

    double SampleInterpolationError(const S::SkeletalAnimationClip& clip, uint32_t joint, double time, double interval)
    {
        const auto previous = SampleRotation(clip, joint, time - interval);
        const auto next = SampleRotation(clip, joint, time + interval);
        const auto midpoint = A::Detail::Slerp({previous.X, previous.Y, previous.Z, previous.W},
                                              {next.X, next.Y, next.Z, next.W}, .5f);
        return SampleAngle(SampleRotation(clip, joint, time), {midpoint.x, midpoint.y, midpoint.z, midpoint.w});
    }

    struct SampleResult
    {
        S::SkeletalAnimationClip Clip;
        A::SkeletalClipRetargetReport Report;
    };
    SampleResult ProcessSample(const A::SkeletalRetargetClipSource& source, const S::RigAuthoringCpu& target,
                               const A::SkeletalRoleProfile& profile)
    {
        A::SkeletalClipRetargetSettings settings;
        settings.Processing = profile.Processing;
        SampleResult result;
        C::AnsiString error;
        const bool success = A::RetargetSkeletalClip(source, target, profile, settings, result.Clip, result.Report, error);
        if (!success)
            std::fprintf(stderr, "実物の写像が拒否された: %s\n", error.c_str());
        CHECK(success);
        CHECK(result.Report.UnmappedSource == 0 && result.Report.UnmappedTarget == 0);
        Run(target, profile, result.Clip);
        return result;
    }
}

int main()
{
    const char* directory = std::getenv("NORVES_MOTION_SAMPLE_DIR");
    if (!directory || !*directory)
        return 125;
    // 外部BVHは直接読むだけで、試験の一時領域にも複製しない。
    const auto inputPath = std::filesystem::absolute(std::filesystem::path(directory) /
                                                   "Dog#V01full/Dog_Dog_rot6d_pred.bvh");
    const auto bytes = F::ReadBytes(inputPath);
    Core::Bvh::BvhDocument bvh;
    CHECK(Core::Bvh::DecodeBvh(bytes, {}, bvh).Succeeded());
    CHECK(bvh.Joints.size() == 53 && bvh.FrameCount == 67 && bvh.FrameTimeSeconds > 0);
    F::Fixture f;
    const auto targetJson = SampleTarget(f, bvh);
    const auto target = f.Import(targetJson, "RigV1Fixture/target.gltf");
    const auto profileText = SampleProfile(bvh, true, true);
    A::SkeletalRoleProfile profile;
    CHECK(A::ParseSkeletalRoleProfile(F::View(profileText), {}, profile).Succeeded());
    A::SkeletalRetargetClipSource source;
    C::AnsiString error;
    CHECK(A::MakeBvhRetargetClipSource(bvh, profile.Settings, C::String("MotionSample"), source, error));
    const auto loop = ProcessSample(source, target, profile);

    // cookと同じ写像・後処理を通したクリップの全関節で閉じを測る。
    const auto& output = loop.Clip;
    double seamMean = 0, seamMax = 0;
    for (uint32_t joint = 0; joint < bvh.Joints.size(); ++joint)
    {
        const double angle = SampleAngle(SampleRotation(output, joint, 0),
                                         SampleRotation(output, joint, output.DurationSeconds));
        seamMean += angle;
        seamMax = std::max(seamMax, angle);
    }
    seamMean /= bvh.Joints.size();

    A::SkeletalRoleProfile spikeProfile, rawProfile;
    CHECK(A::ParseSkeletalRoleProfile(F::View(SampleProfile(bvh, false, true)), {}, spikeProfile).Succeeded());
    CHECK(A::ParseSkeletalRoleProfile(F::View(SampleProfile(bvh, false, false)), {}, rawProfile).Succeeded());
    const auto spike = ProcessSample(source, target, spikeProfile);
    const auto raw = ProcessSample(source, target, rawProfile);
    uint32_t finger = UINT32_MAX;
    for (uint32_t joint = 0; joint < target.GetData()->Geometry.Joints.size(); ++joint)
        if (target.GetData()->Geometry.Joints[joint].Name == C::String("Bip01_L_Finger0"))
            finger = joint;
    CHECK(finger != UINT32_MAX);
    const double time = 21 * bvh.FrameTimeSeconds;
    const double errorBefore = SampleInterpolationError(raw.Clip, finger, time, bvh.FrameTimeSeconds);
    const double errorAfter = SampleInterpolationError(spike.Clip, finger, time, bvh.FrameTimeSeconds);
    const auto& report = loop.Report.Processing;
    const double periodFrames = report.PeriodSeconds / bvh.FrameTimeSeconds;
    const double cycleMotion = SampleCycleMotionDegrees(target, spike.Clip, report.PeriodSeconds);
    const double recurrenceRatio = cycleMotion > 0 ? report.RecurrenceRmsRadians * Degrees / cycleMotion : 0;
    std::printf("MOTION_SAMPLE period_frames=%.9g recurrence_rms_degrees=%.9g cycle_motion_degrees_approx=%.9g "
                "recurrence_motion_ratio_approx=%.9g loop_rms_threshold_degrees=%.9g loop_recurrence_ratio=%.9g "
                "start_seconds=%.9g end_seconds=%.9g source_duration_seconds=%.9g "
                "seam_mean_degrees=%.9g seam_max_degrees=%.9g seam_before_degrees=%.9g seam_after_degrees=%.9g "
                "replaced_spikes=%u averaged_cycles=%u spike_replaced_spikes=%u spike_frame=21 "
                "spike_time_seconds=%.9g spike_error_before_degrees=%.9g spike_error_after_degrees=%.9g\n",
                periodFrames, report.RecurrenceRmsRadians * Degrees, cycleMotion, recurrenceRatio,
                profile.Processing.LoopRmsThresholdRadians * Degrees, profile.Processing.LoopRecurrenceRatio,
                report.StartSeconds, report.EndSeconds, double(source.Clip.DurationSeconds),
                seamMean, seamMax, report.SeamBeforeRadians * Degrees, report.SeamAfterRadians * Degrees,
                report.ReplacedSpikes, report.AveragedCycles, spike.Report.Processing.ReplacedSpikes, time,
                errorBefore, errorAfter);
    std::fflush(stdout);
    // 未整形の21コマ目が既知の補間誤差付近に無ければ、格子のずれとして止める。
    CHECK(std::abs(errorBefore - 23.34) <= 1);
    CHECK(report.bLoopDetected && report.AveragedCycles > 1);
    CHECK(periodFrames >= 12 && periodFrames <= 13);
    CHECK(seamMean <= 1 && seamMax <= 3);
    CHECK(seamMean < report.SeamBeforeRadians * Degrees);
    CHECK(report.SeamAfterRadians < report.SeamBeforeRadians);
    CHECK(spike.Report.Processing.ReplacedSpikes >= 1);
    CHECK(errorAfter < errorBefore && errorAfter < 15);
    return 0;
}
#endif
