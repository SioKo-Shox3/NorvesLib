// 合成の四足で cook の接地、足の印、根の移動、時間補正と拒否時の不変性を反証する。
#include "RigSplitTestFixture.h"
#include "Tools/AssetCook/RigMotionCleanup.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
        CHECK(contacts.GetArraySize() == clip.Metadata.Events.size());
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
                size_t matches = 0;
                for (const auto& event : clip.Metadata.Events)
                {
                    if (event.Name == Core::Identity(Feet[foot]) && std::abs(event.Time - start) < 1e-6 &&
                        std::abs(event.EndTime - end) < 1e-6)
                    {
                        ++matches;
                    }
                }
                CHECK(matches == 1);
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
                const auto name = C::AnsiString(Feet[foot]) + (bDown ? "_down" : "_up");
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
            for (size_t i = 0; i < clip.Metadata.Events.size(); ++i)
            {
                CHECK(retimed.Metadata.Events[i].Name == clip.Metadata.Events[i].Name);
                CHECK(std::abs(retimed.Metadata.Events[i].Time - clip.Metadata.Events[i].Time * ratio) < 1e-5);
                CHECK(std::abs(retimed.Metadata.Events[i].EndTime - clip.Metadata.Events[i].EndTime * ratio) < 1e-5);
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
        size_t matches = 0;
        for (const auto& marker : once.Metadata.Markers)
        {
            if (marker.Name == Core::Identity("hind_R_paw_down"))
            {
                CHECK(marker.Time == 0);
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
