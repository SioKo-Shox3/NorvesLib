// 共有化前の実Samplerを実行し、paddingを含めない固定bit列を採取する。
#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletonResource.h"
#include "Resource/SkinnedMeshResource.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
    using namespace NorvesLib::Core;
    namespace Math = NorvesLib::Math;
    using Path = Skeletal::SkeletalAnimationPath;
    constexpr uint32_t CaseCount = 30;
    constexpr const char* CaseNames[CaseCount] = {"identity_bind",
                                                  "scaled_rotated_bind",
                                                  "partial_translation",
                                                  "linear_before",
                                                  "linear_middle",
                                                  "linear_after",
                                                  "step_before",
                                                  "step_key",
                                                  "step_after",
                                                  "noncommutative_hierarchy",
                                                  "empty_mesh",
                                                  "nan_time",
                                                  "parent_cycle",
                                                  "duplicate_key_time",
                                                  "singular_ibm",
                                                  "singular_mesh",
                                                  "fk_overflow",
                                                  "parent_after_child",
                                                  "multiple_roots",
                                                  "empty_and_out_of_range_channels",
                                                  "duplicate_channels",
                                                  "sheared_bind",
                                                  "reflected_bind",
                                                  "extreme_bind",
                                                  "extreme_bind_with_overrides",
                                                  "extreme_ibm_and_clip",
                                                  "nonfinite_ibm",
                                                  "nonfinite_mesh",
                                                  "empty_skeleton",
                                                  "success_after_failures"};

    struct Fixture
    {
        Container::VariableArray<Skeletal::SkeletalJoint> Joints;
        Skeletal::SkeletalAnimationClip Clip;
        Container::VariableArray<Skeletal::SkeletalVertex> Vertices;
        Math::Matrix4x4 MeshGlobal = Math::Matrix4x4::Identity;
        float Time = 0.5f;
        // -1は旧実装のboolを観察するだけで、新しい成功条件を導入しない。
        int Expected = 1;
    };

    Fixture Basic(size_t count = 1)
    {
        Fixture f;
        f.Joints.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto& j = f.Joints[i];
            j.Name = i == 0 ? "Root" : "Child";
            j.ParentIndex = i == 0 ? -1 : 0;
            j.InverseBindMatrix.fill(0.0f);
            j.InverseBindMatrix[0] = j.InverseBindMatrix[5] = j.InverseBindMatrix[10] = j.InverseBindMatrix[15] = 1.0f;
        }
        f.Clip.Name = "Baseline";
        f.Clip.DurationSeconds = 1.0f;
        f.Vertices.resize(2);
        for (size_t i = 0; i < f.Vertices.size(); ++i)
        {
            auto& v = f.Vertices[i];
            v.Position = {i == 0 ? 1.0f : -1.0f, 1.0f, 0.0f};
            v.Normal = {1.0f, 1.0f, 0.0f};
            v.JointIndices.fill(0);
            v.JointWeights.fill(0.0f);
            v.JointIndices[0] = static_cast<uint32_t>(count - 1);
            v.JointWeights[0] = 1.0f;
        }
        return f;
    }

    void Channel(Fixture& f, uint32_t joint, Path path, Skeletal::SkeletalValue first, Skeletal::SkeletalValue last,
                 bool bTwoKeys = false)
    {
        Skeletal::SkeletalAnimationChannel channel;
        channel.JointIndex = joint;
        channel.Path = path;
        channel.Samples.push_back({0.0f, first});
        if (bTwoKeys)
        {
            channel.Samples.push_back({1.0f, last});
        }
        f.Clip.Channels.push_back(std::move(channel));
    }

    Fixture MakeFixture(uint32_t id)
    {
        Fixture f = Basic(id == 10 || id == 13 || id == 17 || id == 18 || id == 19 ? 2 : 1);
        if (id == 2 || id == 3)
        {
            auto& m = f.Joints[0].InverseBindMatrix;
            m[0] = 0.0f;
            m[1] = -1.0f;
            m[4] = 0.5f;
            m[5] = 0.0f;
            if (id == 3)
            {
                Channel(f, 0, Path::Translation, {3, 4, 5, 0}, {});
            }
        }
        if (id >= 4 && id <= 9)
        {
            f.Joints[0].InverseBindMatrix[12] = -2.0f;
            f.MeshGlobal.m30 = 10.0f;
            Channel(f, 0, Path::Translation, {12, 0, 0, 0}, {14, 0, 0, 0}, true);
            if (id <= 6)
            {
                Channel(f, 0, Path::Rotation, {0, 0, 0, 1}, {0, 0, 1, 0}, true);
                f.Time = id == 4 ? -1.0f : id == 5 ? 0.5f : 2.0f;
            }
            else
            {
                auto& c = f.Clip.Channels[0];
                c.Interpolation = Skeletal::SkeletalAnimationInterpolation::Step;
                c.Samples[1] = {0.5f, {22, 0, 0, 0}};
                c.Samples.push_back({1.0f, {32, 0, 0, 0}});
                f.Time = id == 7 ? 0.49999f : id == 8 ? 0.5f : 0.75f;
            }
        }
        switch (id)
        {
        case 10:
        case 18:
        case 19:
        {
            const uint32_t parent = id == 18 ? 1 : 0;
            const uint32_t child = 1 - parent;
            f.Joints[parent].ParentIndex = -1;
            f.Joints[child].ParentIndex = id == 19 ? -1 : static_cast<int32_t>(parent);
            for (auto& vertex : f.Vertices)
            {
                vertex.JointIndices[0] = child;
            }
            Channel(f, parent, Path::Translation, {10, 0, 0, 0}, {});
            Channel(f, parent, Path::Rotation, {0, 0, 0.70710678f, 0.70710678f}, {});
            Channel(f, child, Path::Translation, {2, 0, 0, 0}, {});
            Channel(f, child, Path::Scale, {2, 1, 1, 0}, {});
            break;
        }
        case 11:
            f.Vertices.clear();
            break;
        case 12:
            f.Time = std::numeric_limits<float>::quiet_NaN();
            f.Expected = 0;
            break;
        case 13:
            f.Joints[0].ParentIndex = 1;
            f.Expected = 0;
            break;
        case 14:
            Channel(f, 0, Path::Translation, {}, {}, true);
            f.Clip.Channels[0].Samples[1].TimeSeconds = 0.0f;
            f.Expected = 0;
            break;
        case 15:
            f.Joints[0].InverseBindMatrix.fill(0.0f);
            f.Expected = 0;
            break;
        case 16:
            f.MeshGlobal.m00 = 0.0f;
            f.Expected = 0;
            break;
        case 17:
            for (uint32_t i = 0; i < 2; ++i)
            {
                const float huge = std::numeric_limits<float>::max();
                Channel(f, i, Path::Scale, {huge, huge, huge, 0}, {});
            }
            f.Expected = 0;
            break;
        case 20:
            Channel(f, 0, Path::Translation, {}, {});
            f.Clip.Channels[0].Samples.clear();
            Channel(f, 99, Path::Translation, {30, 40, 50, 0}, {});
            break;
        case 21:
            Channel(f, 0, Path::Translation, {1, 2, 3, 0}, {});
            Channel(f, 0, Path::Translation, {4, 5, 6, 0}, {});
            break;
        case 22:
            f.Joints[0].InverseBindMatrix[1] = 0.25f;
            f.Expected = -1;
            break;
        case 23:
            f.Joints[0].InverseBindMatrix[0] = -1.0f;
            f.Expected = -1;
            break;
        case 24:
        case 25:
            f.MeshGlobal.m00 = 1e20f;
            f.MeshGlobal.m11 = 1e-20f;
            f.Vertices.clear();
            f.Expected = -1;
            if (id == 25)
            {
                Channel(f, 0, Path::Scale, {1, 1, 1, 0}, {});
                Channel(f, 0, Path::Rotation, {0, 0, 0, 1}, {});
            }
            break;
        case 26:
            f.Joints[0].InverseBindMatrix[0] = 1e20f;
            f.Joints[0].InverseBindMatrix[5] = 1e-20f;
            Channel(f, 0, Path::Scale, {1e20f, 1e20f, 1e20f, 0}, {});
            Channel(f, 0, Path::Rotation, {0, 0, 0, 1}, {});
            f.Vertices.clear();
            f.Expected = -1;
            break;
        case 27:
            f.Joints[0].InverseBindMatrix[0] = std::numeric_limits<float>::infinity();
            f.Expected = 0;
            break;
        case 28:
            f.MeshGlobal.m00 = std::numeric_limits<float>::infinity();
            f.Expected = 0;
            break;
        case 29:
            f.Joints.clear();
            f.Expected = 0;
            break;
        default:
            break;
        }
        return f;
    }

    class SnapshotWriter
    {
      public:
        Container::VariableArray<uint8_t> m_Bytes;
        bool m_bValid = true;
        void U32(uint32_t value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8)
            {
                m_Bytes.push_back(static_cast<uint8_t>(value >> shift));
            }
        }
        void Float(float value)
        {
            m_bValid = m_bValid && std::isfinite(value);
            U32(std::bit_cast<uint32_t>(value));
        }
        void Vector(const Math::Vector3& value)
        {
            Float(value.x);
            Float(value.y);
            Float(value.z);
        }
        void Matrices(const Container::VariableArray<Math::Matrix4x4>& values)
        {
            U32(static_cast<uint32_t>(values.size()));
            for (const auto& matrix : values)
            {
                for (float value : matrix.values)
                {
                    Float(value);
                }
            }
        }
    };

    bool IsCleared(const Animation::SkeletalPoseSnapshot& pose)
    {
        const auto& b = pose.AnimatedBounds;
        return pose.BonePalette.empty() && pose.JointModelMatrices.empty() && !pose.bHasAnimatedBounds &&
               b.Min.x == 0 && b.Min.y == 0 && b.Min.z == 0 && b.Max.x == 0 && b.Max.y == 0 && b.Max.z == 0;
    }
} // namespace

bool CaptureSkeletalSamplerBaseline(const char* path)
{
    SnapshotWriter writer;
    writer.U32(0x4250534e);
    writer.U32(1);
    writer.U32(CaseCount);
    Animation::SkeletalPoseSnapshot pose;
    for (uint32_t id = 1; id <= CaseCount; ++id)
    {
        Fixture f = MakeFixture(id);
        SkeletonResource skeleton;
        AnimationClipResource clip;
        SkinnedMeshResource mesh;
        skeleton.Initialize();
        clip.Initialize();
        mesh.Initialize();
        skeleton.SetJoints(std::move(f.Joints));
        clip.SetClip(std::move(f.Clip));
        mesh.SetVertices(std::move(f.Vertices));
        if (!skeleton.Load() || !clip.Load())
        {
            return false;
        }
        // 全caseで再利用済みposeを汚し、早期失敗と後段失敗のClearを同じ条件で検査する。
        pose.BonePalette.assign(1, Math::Matrix4x4::Identity);
        pose.JointModelMatrices.assign(1, Math::Matrix4x4::Identity);
        pose.AnimatedBounds.Min = Math::Vector3(7, 8, 9);
        pose.AnimatedBounds.Max = Math::Vector3(10, 11, 12);
        pose.bHasAnimatedBounds = true;
        const bool bSuccess =
            Animation::SkeletalAnimationSampler::Sample(skeleton, clip, mesh, f.Time, f.MeshGlobal, pose);
        if ((f.Expected >= 0 && bSuccess != (f.Expected == 1)) || (!bSuccess && !IsCleared(pose)))
        {
            std::fprintf(stderr, "Sampler baseline contract failed: %s\n", CaseNames[id - 1]);
            return false;
        }
        writer.U32(id);
        const char* name = CaseNames[id - 1];
        writer.U32(static_cast<uint32_t>(std::strlen(name)));
        for (const char* c = name; *c; ++c)
        {
            writer.m_Bytes.push_back(static_cast<uint8_t>(*c));
        }
        writer.U32(bSuccess ? 1 : 0);
        writer.Matrices(pose.BonePalette);
        writer.Matrices(pose.JointModelMatrices);
        writer.U32(static_cast<uint32_t>(mesh.GetVertices().size()));
        for (const auto& vertex : mesh.GetVertices())
        {
            const auto skinned = Animation::SkeletalAnimationSampler::SkinVertex(vertex, pose.BonePalette);
            writer.Vector(skinned.Position);
            writer.Vector(skinned.Normal);
        }
        writer.U32(pose.bHasAnimatedBounds ? 1 : 0);
        writer.Vector(pose.AnimatedBounds.Min);
        writer.Vector(pose.AnimatedBounds.Max);
    }
    writer.U32(0x454e4442);
    if (!writer.m_bValid)
    {
        return false;
    }
    if (path != nullptr)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") != 0 || file == nullptr)
        {
            return false;
        }
        const bool bWritten =
            std::fwrite(writer.m_Bytes.data(), 1, writer.m_Bytes.size(), file) == writer.m_Bytes.size();
        const bool bClosed = std::fclose(file) == 0;
        if (!bWritten || !bClosed)
        {
            return false;
        }
    }
    std::puts(
        "SKELETAL_SAMPLER_BASELINE result=pass cases=30 explicit_float_bits_clear_all_fields_real_sampler_no_refactor");
    return true;
}
