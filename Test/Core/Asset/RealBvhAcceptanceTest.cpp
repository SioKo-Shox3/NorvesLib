// 環境変数で指定された実物の BVH の取り込みと写像結果を確認する。
#include "ClipBankV1Fixture.h"
#include "Animation/SkeletalClipRetarget.h"
#include "Text/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>

#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, Utf8(u8"実物の BVH の検査に失敗: %s:%d %s\n"), __FILE__, __LINE__, #x);              \
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
    constexpr double Degrees = 180 / 3.14159265358979323846;

    // 実行環境のコードページに依存せず、日本語のエラーを UTF-8 で出す。
    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    C::AnsiString SampleJointName(const Core::Bvh::Joint& joint)
    {
        return C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(joint.Name.data()), joint.Name.size()));
    }

    C::AnsiString SampleTarget(F::Fixture& fixture, const Core::Bvh::BvhDocument& bvh)
    {
        auto binary = fixture.Binary;
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
            // OFFSET だけの rest なので、逆バインド行列は累積平行移動の逆になる。
            for (size_t k = 0; k < 16; ++k)
            {
                F::Float(binary, 224 + i * 64 + k * 4, k % 5 == 0 ? 1.f : 0.f);
            }
            F::Float(binary, 224 + i * 64 + 48, float(-world.X));
            F::Float(binary, 224 + i * 64 + 52, float(-world.Y));
            F::Float(binary, 224 + i * 64 + 56, float(-world.Z));
            json.BeginObject();
            json.WriteString("name", SampleJointName(joint).c_str());
            json.BeginArray("translation");
            for (double value : {joint.Offset.X, joint.Offset.Y, joint.Offset.Z})
            {
                json.WriteNumberElement(value);
            }
            json.EndArray();
            json.BeginArray("children");
            for (size_t child = i + 1; child < bvh.Joints.size(); ++child)
            {
                if (bvh.Joints[child].Parent == i)
                {
                    json.WriteNumberElement(double(child));
                }
            }
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
        {
            json.WriteNumberElement(double(i));
        }
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

        // 合成三角形の頂点データを再利用し、骨格と逆バインド行列を BVH から生成する。
        const auto begin = fixture.Json.find(C::AnsiStringView("\"buffers\":"));
        const auto end = fixture.Json.find(C::AnsiStringView("\"skins\":"));
        CHECK(begin != C::AnsiString::npos && end > begin);
        char length[64], matrices[64], count[64];
        std::snprintf(length, sizeof(length), "\"byteLength\":%zu", binary.size());
        std::snprintf(matrices, sizeof(matrices), "\"byteLength\":%zu", bvh.Joints.size() * 64);
        std::snprintf(count, sizeof(count), "\"count\":%zu,\"type\":\"MAT4\"", bvh.Joints.size());
        auto resources = F::Replace(fixture.Json.substr(begin, end - begin), "\"byteLength\":416", length);
        resources = F::Replace(resources, "\"byteLength\":128", matrices);
        resources = F::Replace(resources, "\"count\":2,\"type\":\"MAT4\"", count);
        const C::AnsiString generated(json.ToString().c_str());
        auto target = C::AnsiString("{\"asset\":{\"version\":\"2.0\"},") + resources + generated.substr(1);
        fixture.SetBuffer(binary);
        F::WriteBytes(fixture.Root / "RigV1Fixture/target.gltf", F::Bytes(target.begin(), target.end()));
        return target;
    }

    C::AnsiString SampleProfile(const Core::Bvh::BvhDocument& bvh)
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
        json.WriteString("mode", "override_fps");
        json.WriteNumber("source_fps", 16);
        json.EndObject();
        // root と可変長の spine で全関節を恒等写像する。
        for (bool bTarget : {false, true})
        {
            json.BeginObject(bTarget ? "target_roles" : "source_roles");
            for (bool bRoot : {true, false})
            {
                json.BeginArray(bRoot ? "root" : "spine");
                for (size_t i = bRoot ? 0 : 1; i < (bRoot ? 1 : bvh.Joints.size()); ++i)
                {
                    const auto name = SampleJointName(bvh.Joints[i]);
                    if (bTarget)
                    {
                        json.BeginObject();
                        json.WriteString("joint", name.c_str());
                        json.BeginArray("C");
                        for (size_t k = 0; k < 9; ++k)
                        {
                            json.WriteNumberElement(k % 4 == 0 ? 1 : 0);
                        }
                        json.EndArray();
                        json.EndObject();
                    }
                    else
                    {
                        json.WriteStringElement(C::String(name.c_str()));
                    }
                }
                json.EndArray();
            }
            json.EndObject();
        }
        json.BeginObject("processing");
        json.WriteNumber("output_fps", 16);
        json.WriteString("loop_mode", "auto");
        json.WriteNumber("loop_rms_threshold_degrees", 8.5);
        json.WriteNumber("loop_recurrence_ratio", 0.5);
        json.WriteBool("average_cycles", false);
        json.WriteUInt64("smoothing_radius", 0);
        json.WriteNumber("spike_threshold_degrees", 0);
        json.WriteUInt64("spike_window", 2);
        json.EndObject();
        json.EndObject();
        CHECK(json.IsComplete());
        return C::AnsiString(json.ToString().c_str());
    }
}

int main()
{
    const char* directory = std::getenv("NORVES_MOTION_SAMPLE_DIR");
    if (!directory || !*directory)
    {
        return 125;
    }

    const auto inputPath = std::filesystem::absolute(std::filesystem::path(directory) /
                                                   "Dog#V01full/Dog_Dog_rot6d_pred.bvh");
    {
        std::ifstream input(inputPath, std::ios::binary);
        if (!input)
        {
            std::fprintf(stderr, Utf8(u8"指定された実物の BVH を開けません。\n"));
            return 1;
        }
    }
    const auto bytes = F::ReadBytes(inputPath);
    Core::Bvh::BvhDocument bvh;
    const auto decode = Core::Bvh::DecodeBvh(bytes, {}, bvh);
    if (!decode.Succeeded())
    {
        std::fprintf(stderr, Utf8(u8"実物の BVH を読み取れません: 状態=%u 行=%zu 列=%zu\n"), unsigned(decode.Status),
                     decode.Line, decode.Column);
        return 1;
    }

    size_t endSites = 0;
    for (const auto& joint : bvh.Joints)
    {
        if (joint.bHasEndSite)
        {
            ++endSites;
        }
    }
    if (bvh.Joints.size() != 53 || endSites != 11 || bvh.Channels.size() != 162 ||
        bvh.Values.size() != 162 * 67 || bvh.FrameCount != 67 ||
        !(std::abs(bvh.FrameTimeSeconds - 0.033333) <= 1e-12))
    {
        std::fprintf(stderr,
                     Utf8(u8"実物の BVH は 2026 年 9 月の観測値と異なります: 関節数=%zu (期待値=53)、"
                          u8"End Site 数=%zu (期待値=11)、1 フレームの値の数=%zu (期待値=162)、"
                          u8"全値の数=%zu (期待値=10854)、フレーム数=%u (期待値=67)、"
                          u8"フレーム時間=%.17g (期待値=0.033333 秒)\n"),
                     bvh.Joints.size(), endSites, bvh.Channels.size(), bvh.Values.size(), unsigned(bvh.FrameCount),
                     bvh.FrameTimeSeconds);
        return 1;
    }

    F::Fixture fixture;
    const auto targetJson = SampleTarget(fixture, bvh);
    const auto target = fixture.Import(targetJson, "RigV1Fixture/target.gltf");
    const auto profileText = SampleProfile(bvh);
    A::SkeletalRoleProfile profile;
    const auto parse = A::ParseSkeletalRoleProfile(F::View(profileText), {}, profile);
    if (!parse.Succeeded())
    {
        std::fprintf(stderr, Utf8(u8"実物の BVH の写像設定を読み取れません: 項目=%s 状態=%u\n"), parse.Field.c_str(),
                     unsigned(parse.Status));
        return 1;
    }
    A::SkeletalRetargetClipSource source;
    C::AnsiString error;
    if (!A::MakeBvhRetargetClipSource(bvh, profile.Settings, C::String("MotionSample"), source, error))
    {
        std::fprintf(stderr, Utf8(u8"実物の BVH のクリップを作れません: %s\n"), error.c_str());
        return 1;
    }
    if (!(std::abs(double(source.Clip.DurationSeconds) - 4.125) <= 1e-6))
    {
        std::fprintf(stderr, Utf8(u8"16 fps のクリップの長さが異なります: %.9g 秒 (期待値=4.125 秒)\n"),
                     double(source.Clip.DurationSeconds));
        return 1;
    }

    A::SkeletalClipRetargetSettings settings;
    settings.Processing = profile.Processing;
    S::SkeletalAnimationClip clip;
    A::SkeletalClipRetargetReport report;
    if (!A::RetargetSkeletalClip(source, target, profile, settings, clip, report, error))
    {
        std::fprintf(stderr, Utf8(u8"実物の BVH の写像が拒否されました: %s\n"), error.c_str());
        return 1;
    }

    double beforeMax = 0, afterMax = 0, afterSum = 0;
    size_t directionMissing = 0;
    for (const auto& correction : report.Corrections)
    {
        beforeMax = std::max(beforeMax, correction.BeforeRadians * Degrees);
        afterMax = std::max(afterMax, correction.AfterRadians * Degrees);
        afterSum += correction.AfterRadians * Degrees;
        directionMissing += correction.bDirectionMissing ? 1 : 0;
    }
    const bool bNoCorrections = report.Corrections.empty();
    const double noMeasurement = std::numeric_limits<double>::quiet_NaN();
    const double afterMean = bNoCorrections ? noMeasurement : afterSum / double(report.Corrections.size());
    if (bNoCorrections)
    {
        beforeMax = noMeasurement;
        afterMax = noMeasurement;
    }
    const auto& processing = report.Processing;
    std::printf("REAL_BVH joints=%zu end_sites=%zu values_per_frame=%zu frames=%u frame_time_seconds=%.9g "
                "source_fps=16 duration_seconds=%.9g rest_error_before_max_degrees=%.9g "
                "rest_error_after_max_degrees=%.9g rest_error_after_mean_degrees=%.9g%s "
                "rest_direction_missing=%zu rotation_error_measured=%d rotation_error_max_degrees=%.9g "
                "loop_detected=%d period_seconds=%.9g period_frames=%.9g seam_before_degrees=%.9g "
                "seam_after_degrees=%.9g planar_distance_m=%.9g estimated_speed_m_s=%.9g "
                "source_root_distance_m=%.9g unmapped_source=%u unmapped_target=%u\n",
                bvh.Joints.size(), endSites, bvh.Channels.size(), unsigned(bvh.FrameCount), bvh.FrameTimeSeconds,
                double(source.Clip.DurationSeconds), beforeMax, afterMax, afterMean,
                bNoCorrections ? " rest_error_corrections=0" : "", directionMissing,
                report.bRotationErrorMeasured ? 1 : 0, report.RotationErrorMaxDegrees,
                processing.bLoopDetected ? 1 : 0, processing.PeriodSeconds, processing.PeriodSeconds * 16,
                processing.SeamBeforeRadians * Degrees, processing.SeamAfterRadians * Degrees,
                processing.PlanarDistanceMeters, processing.AverageSpeedMetersPerSecond, report.SourceRootDistanceMeters,
                unsigned(report.UnmappedSource), unsigned(report.UnmappedTarget));
    std::fflush(stdout);
    return 0;
}
