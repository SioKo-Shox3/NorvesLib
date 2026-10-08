// GR12の独立oracle・再利用領域・境界・実測を既存Sampler実行ファイルへまとめる。
#include "LegacyPoseOracle.h"
#include "Animation/SkeletalPoseBuilder.h"
#include "Animation/SkeletonResource.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/RigBoundClipProof.h"
#include "Resource/SkinnedMeshResource.h"
#include "Math/MatrixUtils.h"
#include "Test/Core/Asset/ClipBankV1Fixture.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace PoseEvaluationTest
{
    namespace C = NorvesLib::Core::Container;
    namespace A = NorvesLib::Core::Animation;
    namespace S = NorvesLib::Core::Skeletal;
    namespace M = NorvesLib::Math;
    using NorvesLib::Core::AnimationClipResource;
    using NorvesLib::Core::SkeletonResource;
    using NorvesLib::Core::SkinnedMeshResource;
    namespace F = NorvesLib::Tests::RigV1Fixture;
#define POSE_CHECK(x)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "GR12 %s:%d %s\n", __FILE__, __LINE__, #x);                                           \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

    C::FixedArray<float, 16> Values(const M::Matrix4x4& matrix)
    {
        C::FixedArray<float, 16> values(0.0f);
        for (size_t i = 0; i < 16; ++i)
        {
            values[i] = matrix.values[i];
        }
        return values;
    }
    void Near(float actual, float expected)
    {
        POSE_CHECK(std::isfinite(actual) && std::isfinite(expected));
        POSE_CHECK(std::fabs(actual - expected) <= 1e-4f * std::fmax(1.0f, std::fabs(expected)));
    }
    void Compare(const SkeletonResource& skeleton, const AnimationClipResource& clip, const SkinnedMeshResource& mesh,
                 float time, const M::Matrix4x4& transform)
    {
        A::SkeletalPoseSnapshot oldPose, pose;
        const bool oldOk = A::LegacyPoseOracle::Sample(skeleton, clip, mesh, time, transform, oldPose);
        const bool ok = A::SkeletalAnimationSampler::Sample(skeleton, clip, mesh, time, transform, pose);
        POSE_CHECK(ok == oldOk);
        if (!ok)
        {
            POSE_CHECK(pose.BonePalette.empty() && pose.JointModelMatrices.empty() && !pose.bHasAnimatedBounds);
            return;
        }
        POSE_CHECK(pose.BonePalette.size() == oldPose.BonePalette.size());
        POSE_CHECK(pose.JointModelMatrices.size() == oldPose.JointModelMatrices.size());
        for (size_t i = 0; i < pose.BonePalette.size(); ++i)
        {
            for (size_t k = 0; k < 16; ++k)
            {
                Near(pose.BonePalette[i].values[k], oldPose.BonePalette[i].values[k]);
                Near(pose.JointModelMatrices[i].values[k], oldPose.JointModelMatrices[i].values[k]);
            }
        }
        POSE_CHECK(pose.bHasAnimatedBounds == oldPose.bHasAnimatedBounds);
        if (pose.bHasAnimatedBounds)
        {
            POSE_CHECK(pose.AnimatedBounds.Min.x <= oldPose.AnimatedBounds.Min.x);
            POSE_CHECK(pose.AnimatedBounds.Min.y <= oldPose.AnimatedBounds.Min.y);
            POSE_CHECK(pose.AnimatedBounds.Min.z <= oldPose.AnimatedBounds.Min.z);
            POSE_CHECK(pose.AnimatedBounds.Max.x >= oldPose.AnimatedBounds.Max.x);
            POSE_CHECK(pose.AnimatedBounds.Max.y >= oldPose.AnimatedBounds.Max.y);
            POSE_CHECK(pose.AnimatedBounds.Max.z >= oldPose.AnimatedBounds.Max.z);
        }
    }
    uint32_t Random(uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    void Seed(SkeletonResource& skeleton, AnimationClipResource& clip, SkinnedMeshResource& mesh, size_t count,
              size_t vertexCount, bool dense = false)
    {
        C::VariableArray<S::SkeletalJoint> joints(count);
        for (size_t logical = 0; logical < count; ++logical)
        {
            const size_t index = count - 1 - logical;
            joints[index].ParentIndex = logical % 7 == 0 ? -1 : static_cast<int32_t>(index + 1);
            auto inverse = M::Matrix4x4::Identity;
            inverse.m30 = -float(logical) * 0.125f;
            inverse.m31 = -float(logical % 3) * 0.25f;
            joints[index].InverseBindMatrix = Values(inverse);
        }
        skeleton.SetJoints(std::move(joints));
        S::SkeletalAnimationClip data;
        data.Name = _T("Synthetic");
        data.DurationSeconds = 4;
        uint32_t random = 1234567;
        for (uint32_t joint = 0; joint < count; ++joint)
        {
            if (!dense && joint % 3)
            {
                continue;
            }
            for (uint32_t path = 0; path < 3; ++path)
            {
                S::SkeletalAnimationChannel channel;
                channel.JointIndex = joint;
                channel.Path = static_cast<S::SkeletalAnimationPath>(path);
                channel.Interpolation =
                    joint % 2 ? S::SkeletalAnimationInterpolation::Step : S::SkeletalAnimationInterpolation::Linear;
                for (float time : {-0.25f, 1.75f, 4.0f})
                {
                    const float value = float(Random(random) % 1000) / 1000;
                    S::SkeletalAnimationSample sample;
                    sample.TimeSeconds = time;
                    sample.Value = path == 0   ? S::SkeletalValue{value, time * 0.1f, -value, 0}
                                   : path == 1 ? S::SkeletalValue{0, 0, value * 0.25f, 1}
                                               : S::SkeletalValue{0.75f + value, 1.25f, 1, 0};
                    channel.Samples.push_back(sample);
                }
                data.Channels.push_back(std::move(channel));
            }
        }
        // 同じ関節・成分では最後の非空が優先。さらに空のchannelが続いても消さない。
        S::SkeletalAnimationChannel duplicate;
        duplicate.JointIndex = 0;
        duplicate.Samples.push_back({0, {2, 3, 1, 0}});
        data.Channels.push_back(duplicate);
        duplicate.Samples.clear();
        data.Channels.push_back(std::move(duplicate));
        S::SkeletalAnimationChannel huge;
        huge.JointIndex = UINT32_MAX;
        huge.Samples.push_back({0, {1, 2, 3, 0}});
        data.Channels.push_back(std::move(huge));
        clip.SetClip(std::move(data));
        C::VariableArray<S::SkeletalVertex> vertices(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i)
        {
            auto& vertex = vertices[i];
            vertex.Position = {float(int(Random(random) % 200) - 100) * 0.01f,
                               float(int(Random(random) % 200) - 100) * 0.01f,
                               float(int(Random(random) % 200) - 100) * 0.01f};
            vertex.Normal = {0, 1, 0};
            for (size_t influence = 0; influence < 4; ++influence)
            {
                vertex.JointIndices[influence] = uint32_t((i + influence) % count);
                vertex.JointWeights[influence] = float(4 - influence) * 0.1f;
            }
        }
        mesh.SetVertices(std::move(vertices));
    }
    void LegacyEquivalence()
    {
        for (size_t count : {size_t{1}, size_t{2}, size_t{7}, size_t{31}, size_t{52}, size_t{128}})
        {
            SkeletonResource skeleton;
            AnimationClipResource clip;
            SkinnedMeshResource mesh;
            skeleton.Initialize();
            clip.Initialize();
            mesh.Initialize();
            Seed(skeleton, clip, mesh, count, 64, count % 2 == 0);
            const auto transform =
                M::MatrixUtils::CreateWorldRowVector({3, -2, 1}, {0, 0, 0.1f, 0.9949874f}, {1.25f, 0.75f, 1.5f});
            for (float time : {-1.0f, 0.0f, 0.5f, 1.75f, 2.4f, 4.0f, 8.0f})
            {
                Compare(skeleton, clip, mesh, time, transform);
            }
            Compare(skeleton, clip, mesh, std::numeric_limits<float>::quiet_NaN(), transform);
            auto badTransform = transform;
            badTransform.m00 = std::numeric_limits<float>::infinity();
            Compare(skeleton, clip, mesh, 0, badTransform);
            badTransform = M::Matrix4x4::Identity;
            badTransform.m00 = 0;
            Compare(skeleton, clip, mesh, 0, badTransform);
            auto badClip = clip.GetClip();
            badClip.Channels[0].Samples[1].TimeSeconds = badClip.Channels[0].Samples[0].TimeSeconds;
            clip.SetClip(std::move(badClip));
            Compare(skeleton, clip, mesh, 1, transform);
        }
    }
    void BoundsCases()
    {
        SkeletonResource skeleton;
        AnimationClipResource clip;
        SkinnedMeshResource mesh;
        skeleton.Initialize();
        clip.Initialize();
        mesh.Initialize();
        C::VariableArray<S::SkeletalJoint> joints(1);
        joints[0].InverseBindMatrix = Values(M::Matrix4x4::Identity);
        skeleton.SetJoints(std::move(joints));
        // デフォルトの空clipはlegacyで有効。ロード状態による新しい拒否を足さない。
        for (float x : {3.0f, 3072.0f, 1e-38f, 1e-37f})
        {
            C::VariableArray<S::SkeletalVertex> vertices(1);
            vertices[0].Position = {x, 0, 0};
            vertices[0].JointIndices = {0, 0, 0, 0};
            vertices[0].JointWeights = x > 1 ? C::FixedArray<float, 4>{0.1f, 0.2f, 0.4f, 0.3f}
                                             : C::FixedArray<float, 4>{M::Constants::EPSILON * 1.125f, 0, 0, 0};
            mesh.SetVertices(std::move(vertices));
            Compare(skeleton, clip, mesh, 0, M::Matrix4x4::Identity);
        }
        for (unsigned mode = 0; mode < 5; ++mode)
        {
            C::VariableArray<S::SkeletalVertex> vertices(2);
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                auto& vertex = vertices[i];
                vertex.Position = {float(i) * 2, 0, 0};
                vertex.JointIndices = {0, UINT32_MAX, 10, 0};
                vertex.JointWeights = {1, 0, 0, 0};
                if (mode == 0)
                {
                    vertex.JointWeights = {0, 1, 0, 0};
                }
                if (mode == 1)
                {
                    vertex.JointWeights = {M::Constants::EPSILON * 0.5f, 1, 0, 0};
                }
                if (mode == 2)
                {
                    vertex.JointWeights = {1, std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0};
                }
                if (mode == 3)
                {
                    vertex.JointWeights = {1, std::numeric_limits<float>::quiet_NaN(), -1, 0};
                }
                if (mode == 4)
                {
                    vertex.JointWeights = {0, 0, 0, 0};
                }
            }
            mesh.SetVertices(std::move(vertices));
            Compare(skeleton, clip, mesh, 0, M::Matrix4x4::Identity);
            A::SkeletalPoseContext context;
            A::PoseScratch scratch;
            A::SkeletalPoseSnapshot pose;
            POSE_CHECK(
                A::SkeletalPoseBuilder::Prepare(skeleton, clip, mesh, M::Matrix4x4::Identity, context, {0, 0.5f}));
            POSE_CHECK(A::SkeletalPoseBuilder::Sample(context, clip, 0, scratch, pose));
            POSE_CHECK(pose.AnimatedBounds.Min.x <= -1 && pose.AnimatedBounds.Max.x >= 3);
        }
    }
    void ReuseAndRevision()
    {
        SkeletonResource skeleton;
        AnimationClipResource clip;
        SkinnedMeshResource mesh;
        skeleton.Initialize();
        clip.Initialize();
        mesh.Initialize();
        Seed(skeleton, clip, mesh, 52, 256, true);
        A::SkeletalPoseContext context;
        A::PoseScratch scratch;
        A::SkeletalPoseSnapshot out;
        const auto transform = M::Matrix4x4::Identity;
        POSE_CHECK(A::SkeletalPoseBuilder::Prepare(skeleton, clip, mesh, transform, context));
        POSE_CHECK(A::SkeletalPoseBuilder::Sample(context, clip, 0, scratch, out));
        const void* pointers[] = {scratch.Local.data(), scratch.LocalMatrices.data(), scratch.GlobalMatrices.data(),
                                  out.BonePalette.data(), out.JointModelMatrices.data()};
        const size_t capacities[] = {scratch.Local.capacity(), scratch.LocalMatrices.capacity(),
                                     scratch.GlobalMatrices.capacity(), out.BonePalette.capacity(),
                                     out.JointModelMatrices.capacity()};
        for (size_t frame = 0; frame < 1000; ++frame)
        {
            POSE_CHECK(A::SkeletalPoseBuilder::Sample(context, clip, float(frame % 241) / 60.0f, scratch, out));
            const void* current[] = {scratch.Local.data(), scratch.LocalMatrices.data(), scratch.GlobalMatrices.data(),
                                     out.BonePalette.data(), out.JointModelMatrices.data()};
            const size_t sizes[] = {scratch.Local.capacity(), scratch.LocalMatrices.capacity(),
                                    scratch.GlobalMatrices.capacity(), out.BonePalette.capacity(),
                                    out.JointModelMatrices.capacity()};
            for (size_t i = 0; i < 5; ++i)
            {
                POSE_CHECK(pointers[i] == current[i] && capacities[i] == sizes[i]);
            }
        }
        auto changed = clip.GetClip();
        changed.DurationSeconds = 5;
        clip.SetClip(std::move(changed));
        POSE_CHECK(!A::SkeletalPoseBuilder::IsPreparedFor(context, skeleton, clip, mesh, transform));
        POSE_CHECK(!A::SkeletalPoseBuilder::Sample(context, clip, 1, scratch, out));
        POSE_CHECK(out.BonePalette.empty());
    }
    void SplitEquivalence()
    {
        F::Fixture files;
        for (auto profile : {S::RigImportProfile::DirectTrs128, S::RigImportProfile::StaticRootFrame128,
                             S::RigImportProfile::StaticRootFrame256})
        {
            auto json = files.Json;
            if (S::IsStaticRootFrameProfile(profile))
            {
                json = F::Replace(json, "\"nodes\":[0,2]", "\"nodes\":[3,2]");
                json =
                    F::Replace(json, "\"skin\":0}],\"buffers\":",
                               "\"skin\":0},{\"name\":\"Armature\",\"children\":[0],\"translation\":[2,3,1],\"scale\":["
                               "2,2,2]}],\"buffers\":");
                json = F::Replace(json, "\"skeleton\":0,", "");
            }
            S::RigV1Limits limits;
            limits.MaxJoints = S::RigProfileMaximumJoints(profile);
            S::RigAuthoringCpu rig;
            S::RigV1Report report;
            POSE_CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/rig.gltf", profile, rig,
                                                                  report, limits));
            S::SkeletonV1 skeletonValue;
            S::SkinMeshV1 meshValue;
            S::ClipBankV1 bank;
            S::SkinMaterialV1 material;
            POSE_CHECK(S::BuildSkeletonV1(rig, skeletonValue, report, limits, profile));
            POSE_CHECK(S::BuildSkinMeshV1(rig, skeletonValue, "Models/rig.nvskel", {&material, 1}, meshValue, report,
                                          limits, profile));
            POSE_CHECK(S::BuildClipBankV1({&rig, 1}, bank, report, limits, profile));
            S::CookedRigSplitCpuAsset cpu;
            S::RigSplitReport binding;
            POSE_CHECK(S::BindRigSplitV1(skeletonValue, meshValue, {&bank, 1}, {}, cpu, binding, limits, profile));
            SkeletonResource skeleton;
            SkinnedMeshResource mesh;
            AnimationClipResource clip;
            skeleton.Initialize();
            mesh.Initialize();
            clip.Initialize();
            POSE_CHECK(skeleton.SetSplitSkeleton(skeletonValue) && skeleton.Load());
            POSE_CHECK(mesh.SetSplitMesh(meshValue) && mesh.Load());
            if (S::IsStaticRootFrameProfile(profile))
            {
                POSE_CHECK(S::RigBoundClipAccess::SetValidated(clip, cpu, 0));
            }
            else
            {
                clip.SetClip(S::SkeletalAnimationClip(cpu.GetData()->Clips[0]));
            }
            POSE_CHECK(clip.Load());
            for (float time : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f})
            {
                Compare(skeleton, clip, mesh, time, M::Matrix4x4::Identity);
            }
            if (S::IsStaticRootFrameProfile(profile))
            {
                clip.SetClip(S::SkeletalAnimationClip(clip.GetClip()));
                Compare(skeleton, clip, mesh, 0, M::Matrix4x4::Identity);
            }
        }
    }
    void Benchmark()
    {
        if (!std::getenv("NORVES_POSE_BENCHMARK"))
        {
            return;
        }
        SkeletonResource skeleton;
        AnimationClipResource clip;
        SkinnedMeshResource mesh;
        skeleton.Initialize();
        clip.Initialize();
        mesh.Initialize();
        Seed(skeleton, clip, mesh, 52, 180000, true);
        A::SkeletalPoseContext context;
        A::PoseScratch scratch;
        A::SkeletalPoseSnapshot oldPose, pose;
        POSE_CHECK(A::SkeletalPoseBuilder::Prepare(skeleton, clip, mesh, M::Matrix4x4::Identity, context));
        POSE_CHECK(A::SkeletalPoseBuilder::Sample(context, clip, 0, scratch, pose));
        const auto start = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < 240; ++frame)
        {
            POSE_CHECK(
                A::LegacyPoseOracle::Sample(skeleton, clip, mesh, float(frame) / 60, M::Matrix4x4::Identity, oldPose));
        }
        const auto middle = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < 240; ++frame)
        {
            POSE_CHECK(A::SkeletalPoseBuilder::Sample(context, clip, float(frame) / 60, scratch, pose));
        }
        const auto finish = std::chrono::steady_clock::now();
        std::printf("POSE_BENCHMARK joints=52 triangles=60000 frames=240 clip_seconds=4 old_ms=%.3f cached_ms=%.3f\n",
                    std::chrono::duration<double, std::milli>(middle - start).count(),
                    std::chrono::duration<double, std::milli>(finish - middle).count());
    }
} // namespace PoseEvaluationTest
void TestPoseEvaluation()
{
    PoseEvaluationTest::LegacyEquivalence();
    PoseEvaluationTest::BoundsCases();
    PoseEvaluationTest::ReuseAndRevision();
    PoseEvaluationTest::SplitEquivalence();
    PoseEvaluationTest::Benchmark();
    std::puts("GR12 pose oracle, sparse channels, conservative bounds and scratch reuse PASS");
}
