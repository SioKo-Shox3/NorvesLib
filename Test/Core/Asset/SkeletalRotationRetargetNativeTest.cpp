// 借用targetの1frame変換を、実Resourceと実Samplerで独立の期待値へ照合する。
#include "Animation/SkeletalRotationRetargetNative.h"
#include "Animation/SkeletalCoordinateConversion.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletonResource.h"
#include "Animation/AnimationClipResource.h"
#include "Resource/SkinnedMeshResource.h"
#include "Resource/BvhDecode.h"
#include "Resource/BvhEvaluate.h"
#include "Math/MatrixUtils.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include <initializer_list>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Native retarget check failed %s:%d %s\n", __FILE__, __LINE__, #x);                   \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace Core = NorvesLib::Core;
namespace A = Core::Animation;
namespace B = Core::Bvh;
namespace C = Core::Container;
namespace S = Core::Skeletal;
namespace M = NorvesLib::Math;
using Status = A::SkeletalRetargetStatus;
namespace
{
    B::Matrix3d Matrix(std::initializer_list<double> values)
    {
        CHECK(values.size() == 9);
        B::Matrix3d out;
        size_t i = 0;
        for (double v : values)
        {
            out.Values[i++] = v;
        }
        return out;
    }
    B::Matrix3d X()
    {
        return Matrix({1, 0, 0, 0, 0, -1, 0, 1, 0});
    }
    B::Matrix3d Y()
    {
        return Matrix({0, 0, 1, 0, 1, 0, -1, 0, 0});
    }
    B::Matrix3d Z()
    {
        return Matrix({0, -1, 0, 1, 0, 0, 0, 0, 1});
    }
    void Near(double actual, double expected, double epsilon = 5e-5)
    {
        CHECK(std::isfinite(actual));
        CHECK(std::abs(actual - expected) <= epsilon);
    }
    void Store(S::SkeletalJoint& joint, const M::Matrix4x4& m)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            joint.InverseBindMatrix[i] = m.values[i];
        }
    }
    C::VariableArray<S::SkeletalJoint> Joints(size_t count)
    {
        C::VariableArray<S::SkeletalJoint> result(count);
        for (size_t i = 0; i < count; ++i)
        {
            result[i].Name = i == 0 ? "Root" : "Child";
            result[i].ParentIndex = i == 0 ? -1 : 0;
            Store(result[i], M::Matrix4x4::Identity);
        }
        return result;
    }
    A::SkeletalRetargetNativeInput Input(C::Span<const A::SkeletalRetargetSourceRotation> source,
                                         C::Span<const A::SkeletalJointMappingPair> pairs,
                                         C::Span<const B::Matrix3d> corrections)
    {
        return {source,
                pairs,
                corrections,
                {0, 0},
                A::SkeletalSourceReusePolicy::Reject,
                A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations};
    }
    A::SkeletalPoseSnapshot Sample(const C::VariableArray<S::SkeletalJoint>& joints,
                                   C::Span<const A::SkeletalRetargetRotationValue> values,
                                   const M::Matrix4x4& meshGlobal)
    {
        Core::SkeletonResource skeleton;
        Core::AnimationClipResource clip;
        Core::SkinnedMeshResource mesh;
        skeleton.Initialize();
        clip.Initialize();
        mesh.Initialize();
        auto copy = joints;
        skeleton.SetJoints(std::move(copy));
        S::SkeletalAnimationClip data;
        data.Name = "RotationFrame";
        data.DurationSeconds = 0;
        for (const auto& value : values)
        {
            S::SkeletalAnimationChannel channel;
            channel.JointIndex = value.TargetIndex;
            channel.Path = S::SkeletalAnimationPath::Rotation;
            channel.Interpolation = S::SkeletalAnimationInterpolation::Step;
            channel.Samples.push_back({0, {value.X, value.Y, value.Z, value.W}});
            data.Channels.push_back(std::move(channel));
        }
        CHECK(data.Channels.size() == values.size());
        clip.SetClip(std::move(data));
        CHECK(skeleton.Load());
        CHECK(clip.Load());
        A::SkeletalPoseSnapshot pose;
        CHECK(A::SkeletalAnimationSampler::Sample(skeleton, clip, mesh, 0, meshGlobal, pose));
        CHECK(pose.JointModelMatrices.size() == joints.size());
        CHECK(pose.BonePalette.size() == joints.size());
        return pose;
    }
    void WorldNear(const M::Matrix4x4& actual, const B::Matrix3d& column, double scale = 1)
    {
        for (size_t row = 0; row < 3; ++row)
        {
            for (size_t col = 0; col < 3; ++col)
            {
                Near(actual.values[row * 4 + col], scale * column.Values[col * 3 + row]);
            }
        }
    }
    void TestBvhToActualSampler(bool bScaledMesh, bool bReflectedBasis = false)
    {
        const char text[] =
            "HIERARCHY\nROOT Root { OFFSET 0 0 0 CHANNELS 3 Zrotation Yrotation Xrotation JOINT Child { OFFSET 0 1 0 CHANNELS 3 Xrotation Yrotation Zrotation End Site { OFFSET 0 1 0 } } }\nMOTION\nFrames: 1\nFrame Time: 0.03333333333333333\n90 0 0 90 0 0\n";
        B::BvhDocument document;
        CHECK(B::DecodeBvh(C::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1),
                           B::BvhDecodeLimits{}, document)
                  .Succeeded());
        B::BvhPose sourcePose;
        CHECK(B::EvaluateBvhFrame(document, 0, B::TranslationConvention::OffsetPlusChannels, sourcePose).Succeeded());
        A::SkeletalCoordinateConversion conversion;
        using Axis = Core::AssetImport::SignedAxis;
        CHECK(A::BuildSkeletalCoordinateConversion(Axis::PositiveY, Axis::PositiveZ,
                                                   bReflectedBasis ? A::SkeletalSourceHandedness::Left
                                                                   : A::SkeletalSourceHandedness::Right,
                                                   1, conversion) == A::SkeletalCoordinateStatus::Success);
        A::SkeletalRetargetSourceRotation source[2];
        for (size_t i = 0; i < 2; ++i)
        {
            source[i].ParentIndex = i == 0 ? -1 : 0;
            CHECK(A::ConvertSkeletalMatrixBasis(conversion, sourcePose.Joints[i].World.Rotation,
                                                source[i].WorldRotation) == A::SkeletalCoordinateStatus::Success);
        }
        auto joints = Joints(2);
        auto child = M::Matrix4x4::Identity;
        child.m31 = -1;
        Store(joints[1], child);
        M::Matrix4x4 mesh = M::Matrix4x4::Identity;
        double scale = 1;
        if (bScaledMesh)
        {
            // joint globalsはscale2。Meshは別の90度回転と平行移動を持ち、IBMに相殺分を含める。
            mesh = M::Matrix4x4(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1);
            Store(joints[0], M::Matrix4x4(0, .5f, 0, 0, -.5f, 0, 0, 0, 0, 0, .5f, 0, 1.5f, 2, 2.5f, 1));
            Store(joints[1], M::Matrix4x4(0, .5f, 0, 0, -.5f, 0, 0, 0, 0, 0, .5f, 0, 1.5f, 1.5f, 2.5f, 1));
            scale = 2;
        }
        A::SkeletalJointMappingPair pairs[2]{{1, 1, 0}, {0, 0, 1}};
        B::Matrix3d correction[2];
        A::SkeletalRetargetRotationValue values[2];
        const auto input =
            Input(C::Span<const A::SkeletalRetargetSourceRotation>(source),
                  C::Span<const A::SkeletalJointMappingPair>(pairs), C::Span<const B::Matrix3d>(correction));
        const auto result = A::RetargetSkeletalRotationFrame(input, C::Span<const S::SkeletalJoint>(joints), mesh,
                                                             C::Span<A::SkeletalRetargetRotationValue>(values));
        CHECK(result.Succeeded());
        CHECK(result.bFloatRealizationChecked);
        CHECK(result.AngularErrorRadians <= 0.05 * 3.14159265358979323846 / 180);
        CHECK(values[0].TargetIndex == 1 && values[1].TargetIndex == 0);
        const auto pose = Sample(joints, C::Span<const A::SkeletalRetargetRotationValue>(values), mesh);
        const auto root = pose.JointModelMatrices[0] * mesh, tip = pose.JointModelMatrices[1] * mesh;
        WorldNear(root, bReflectedBasis ? Matrix({0, 1, 0, -1, 0, 0, 0, 0, 1}) : Z(), scale);
        WorldNear(tip, bReflectedBasis ? Matrix({0, 0, -1, -1, 0, 0, 0, 1, 0}) : Matrix({0, 0, 1, 1, 0, 0, 0, 1, 0}),
                  scale);
        Near(root.m30, 0);
        Near(root.m31, 0);
        Near(root.m32, 0);
        Near(tip.m30, bReflectedBasis ? 1 : -1);
        Near(tip.m31, 0);
        Near(tip.m32, 0);
        // 同じ長さの端点を比較する。scale2ならlocal端点を1/2にし、world長は1mにそろえる。
        const auto endpoint =
            M::MatrixUtils::TransformPointRowVector(tip, M::Vector3(0, static_cast<float>(1 / scale), 0));
        Near(endpoint.x, bReflectedBasis ? 1 : -1, 0.001);
        Near(endpoint.y, 0, 0.001);
        Near(endpoint.z, 1, 0.001);
        Near(endpoint.x, (bReflectedBasis ? -1 : 1) * sourcePose.Joints[1].EndSiteWorld.X, 0.001);
        Near(endpoint.y, sourcePose.Joints[1].EndSiteWorld.Y, 0.001);
        Near(endpoint.z, sourcePose.Joints[1].EndSiteWorld.Z, 0.001);
    }
    void TestCorrectionAndRest()
    {
        auto joints = Joints(1);
        Store(joints[0], M::Matrix4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1));
        A::SkeletalRetargetSourceRotation source[1]{{-1, X()}};
        A::SkeletalJointMappingPair pairs[1]{{0, 0, 0}};
        B::Matrix3d correction[1]{Z()};
        A::SkeletalRetargetRotationValue out[1];
        const auto input =
            Input(C::Span<const A::SkeletalRetargetSourceRotation>(source),
                  C::Span<const A::SkeletalJointMappingPair>(pairs), C::Span<const B::Matrix3d>(correction));
        CHECK(A::RetargetSkeletalRotationFrame(input, C::Span<const S::SkeletalJoint>(joints), M::Matrix4x4::Identity,
                                               C::Span<A::SkeletalRetargetRotationValue>(out))
                  .Succeeded());
        auto pose = Sample(joints, C::Span<const A::SkeletalRetargetRotationValue>(out), M::Matrix4x4::Identity);
        WorldNear(pose.JointModelMatrices[0], Matrix({-1, 0, 0, 0, 1, 0, 0, 0, -1}));
        source[0].WorldRotation = B::Matrix3d{};
        CHECK(A::RetargetSkeletalRotationFrame(input, C::Span<const S::SkeletalJoint>(joints), M::Matrix4x4::Identity,
                                               C::Span<A::SkeletalRetargetRotationValue>(out))
                  .Succeeded());
        pose = Sample(joints, C::Span<const A::SkeletalRetargetRotationValue>(out), M::Matrix4x4::Identity);
        WorldNear(pose.JointModelMatrices[0], Y());
    }
    void TestUnmappedAndForest()
    {
        auto joints = Joints(4);
        joints[2].ParentIndex = 1;
        joints[3].ParentIndex = -1;
        Store(joints[1], M::Matrix4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, -1, 0, 1));
        Store(joints[2], M::Matrix4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, -2, 0, 1));
        Store(joints[3], M::Matrix4x4(0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 7, 0, 1));
        A::SkeletalRetargetSourceRotation source[2]{{-1, Z()}, {0, X()}};
        A::SkeletalJointMappingPair pairs[2]{{0, 0, 0}, {1, 2, 1}};
        B::Matrix3d corrections[2];
        A::SkeletalRetargetRotationValue out[2];
        const auto input =
            Input(C::Span<const A::SkeletalRetargetSourceRotation>(source),
                  C::Span<const A::SkeletalJointMappingPair>(pairs), C::Span<const B::Matrix3d>(corrections));
        CHECK(A::RetargetSkeletalRotationFrame(input, C::Span<const S::SkeletalJoint>(joints), M::Matrix4x4::Identity,
                                               C::Span<A::SkeletalRetargetRotationValue>(out))
                  .Succeeded());
        CHECK(out[0].TargetIndex == 0 && out[1].TargetIndex == 2);
        const auto pose = Sample(joints, C::Span<const A::SkeletalRetargetRotationValue>(out), M::Matrix4x4::Identity);
        WorldNear(pose.JointModelMatrices[1], Matrix({0, -1, 0, 0, 0, 1, -1, 0, 0}));
        WorldNear(pose.JointModelMatrices[2], Matrix({0, 0, 1, 1, 0, 0, 0, 1, 0}));
        WorldNear(pose.JointModelMatrices[3], Z());
        Near(pose.JointModelMatrices[3].m30, 7);
        Near(pose.JointModelMatrices[3].m31, 0);
        Near(pose.JointModelMatrices[1].m30, -1);
        Near(pose.JointModelMatrices[2].m30, -2);
    }
    void TestProfileRefusalsAndLateOverflow()
    {
        auto joints = Joints(1);
        A::SkeletalRetargetSourceRotation source[1];
        A::SkeletalJointMappingPair pairs[1]{{0, 0, 0}};
        B::Matrix3d correction[1];
        A::SkeletalRetargetRotationValue out[1]{{77, 2, 3, 5, 7}};
        const auto saved = out[0];
        const auto input =
            Input(C::Span<const A::SkeletalRetargetSourceRotation>(source),
                  C::Span<const A::SkeletalJointMappingPair>(pairs), C::Span<const B::Matrix3d>(correction));
        auto run = [&](const M::Matrix4x4& mesh)
        {
            return A::RetargetSkeletalRotationFrame(input, C::Span<const S::SkeletalJoint>(joints), mesh,
                                                    C::Span<A::SkeletalRetargetRotationValue>(out));
        };
        auto refuse = [&](Status status)
        {
            CHECK(run(M::Matrix4x4::Identity).Status == status);
            CHECK(std::memcmp(&saved, out, sizeof(saved)) == 0);
        };
        auto ibm = M::Matrix4x4::Identity;
        ibm.m00 = .5f;
        Store(joints[0], ibm);
        refuse(Status::NonUniformScale);
        ibm = M::Matrix4x4::Identity;
        ibm.m00 = -1;
        Store(joints[0], ibm);
        refuse(Status::Reflection);
        ibm = M::Matrix4x4::Identity;
        ibm.m10 = -.75f;
        ibm.m11 = 1.25f;
        Store(joints[0], ibm);
        refuse(Status::Shear);
        ibm = M::Matrix4x4::Identity;
        ibm.m00 = std::numeric_limits<float>::infinity();
        Store(joints[0], ibm);
        refuse(Status::NonFiniteTransform);
        ibm = M::Matrix4x4::Identity;
        Store(joints[0], ibm);
        auto mesh = ibm;
        mesh.m03 = 1e-7f;
        CHECK(run(mesh).Status == Status::UnsupportedAffine);
        CHECK(std::memcmp(&saved, out, sizeof(saved)) == 0);
        joints[0].ParentIndex = 0;
        refuse(Status::InvalidHierarchy);
        joints[0].ParentIndex = -1;
        // restでは相殺される大きな位置が、rootの回転後に加算overflowする。double回転だけなら見逃す。
        joints = Joints(2);
        const float large = std::numeric_limits<float>::max() * .75f;
        ibm = M::Matrix4x4::Identity;
        ibm.m30 = -large;
        Store(joints[0], ibm);
        ibm.m31 = -large;
        Store(joints[1], ibm);
        source[0].WorldRotation = Matrix({0, 1, 0, -1, 0, 0, 0, 0, 1});
        refuse(Status::NonFiniteTransform);
    }
    void TestLegacyComposeRows()
    {
        A::Detail::JointTransform transform;
        transform.Translation = M::Vector3(3, 4, 5);
        transform.Scale = M::Vector3(2, 3, 4);
        const float half = std::sqrt(.5f);
        transform.Rotation = M::Quaternion(0, 0, -half, half);
        const auto matrix = A::Detail::ComposeSkeletalLocalRowTransform(transform);
        Near(matrix.m00, 0);
        Near(matrix.m01, 2);
        Near(matrix.m10, -3);
        Near(matrix.m11, 0);
        Near(matrix.m22, 4);
        Near(matrix.m30, 3);
        Near(matrix.m31, 4);
        Near(matrix.m32, 5);
    }
} // namespace
int main()
{
    TestLegacyComposeRows();
    TestBvhToActualSampler(false);
    TestBvhToActualSampler(true);
    TestBvhToActualSampler(false, true);
    TestBvhToActualSampler(true, true);
    TestCorrectionAndRest();
    TestUnmappedAndForest();
    TestProfileRefusalsAndLateOverflow();
    std::puts(
        "SKELETAL_ROTATION_NATIVE result=pass actual_bvh_basis_sampler_uniform_scaled_bind_mesh_unmapped_forest_correction_float_validation_atomic_no_gpu");
}
