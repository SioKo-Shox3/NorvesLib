#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletonResource.h"
#include "Math/Matrix4x4.h"
#include "Resource/SkinnedMeshResource.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core;
namespace Animation = NorvesLib::Core::Animation;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;
namespace Skeletal = NorvesLib::Core::Skeletal;

namespace
{
    constexpr float Epsilon = 0.0001f;

    void AssertNear(float actual, float expected)
    {
        assert(std::fabs(actual - expected) <= Epsilon);
    }

    void SetIdentity(Container::FixedArray<float, 16>& matrix)
    {
        matrix.fill(0.0f);
        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
    }

    void SeedSkeleton(SkeletonResource& skeleton)
    {
        Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        joints[0].ParentIndex = -1;
        SetIdentity(joints[0].InverseBindMatrix);
        joints[0].InverseBindMatrix[12] = -2.0f;
        skeleton.SetJoints(std::move(joints));
        assert(skeleton.Load());
    }

    Skeletal::SkeletalAnimationSample MakeSample(float time, float x, float y, float z, float w)
    {
        Skeletal::SkeletalAnimationSample sample;
        sample.TimeSeconds = time;
        sample.Value = {x, y, z, w};
        return sample;
    }

    void SeedClip(AnimationClipResource& clip)
    {
        Skeletal::SkeletalAnimationClip data;
        data.Name = "MoveAndTurn";
        data.DurationSeconds = 1.0f;

        Skeletal::SkeletalAnimationChannel translation;
        translation.JointIndex = 0;
        translation.Path = Skeletal::SkeletalAnimationPath::Translation;
        translation.Interpolation = Skeletal::SkeletalAnimationInterpolation::Linear;
        translation.Samples.push_back(MakeSample(0.0f, 12.0f, 0.0f, 0.0f, 0.0f));
        translation.Samples.push_back(MakeSample(1.0f, 14.0f, 0.0f, 0.0f, 0.0f));

        Skeletal::SkeletalAnimationChannel rotation;
        rotation.JointIndex = 0;
        rotation.Path = Skeletal::SkeletalAnimationPath::Rotation;
        rotation.Interpolation = Skeletal::SkeletalAnimationInterpolation::Linear;
        rotation.Samples.push_back(MakeSample(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
        rotation.Samples.push_back(MakeSample(1.0f, 0.0f, 0.0f, -1.0f, 0.0f));

        data.Channels.push_back(std::move(translation));
        data.Channels.push_back(std::move(rotation));
        clip.SetClip(std::move(data));
        assert(clip.Load());
    }

    void SeedStepClip(AnimationClipResource& clip)
    {
        Skeletal::SkeletalAnimationClip data;
        data.Name = "StepBoundary";
        data.DurationSeconds = 1.0f;

        Skeletal::SkeletalAnimationChannel translation;
        translation.JointIndex = 0;
        translation.Path = Skeletal::SkeletalAnimationPath::Translation;
        translation.Interpolation = Skeletal::SkeletalAnimationInterpolation::Step;
        translation.Samples.push_back(MakeSample(0.0f, 12.0f, 0.0f, 0.0f, 0.0f));
        translation.Samples.push_back(MakeSample(0.5f, 22.0f, 0.0f, 0.0f, 0.0f));
        translation.Samples.push_back(MakeSample(1.0f, 32.0f, 0.0f, 0.0f, 0.0f));

        data.Channels.push_back(std::move(translation));
        clip.SetClip(std::move(data));
        assert(clip.Load());
    }

    void SeedMesh(SkinnedMeshResource& mesh)
    {
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(2);
        vertices[0].Position = {1.0f, 0.0f, 0.0f};
        vertices[0].Normal = {1.0f, 0.0f, 0.0f};
        vertices[0].JointIndices[0] = 0;
        vertices[0].JointWeights[0] = 1.0f;
        vertices[1].Position = {-1.0f, 0.0f, 0.0f};
        vertices[1].Normal = {1.0f, 0.0f, 0.0f};
        vertices[1].JointIndices[0] = 0;
        vertices[1].JointWeights[0] = 1.0f;
        Container::VariableArray<uint32_t> indices = {0, 1, 0};
        mesh.SetVertices(std::move(vertices));
        mesh.SetIndices(std::move(indices));
        assert(mesh.Load());
    }

    void SeedHierarchySkeleton(SkeletonResource& skeleton)
    {
        Container::VariableArray<Skeletal::SkeletalJoint> joints(2);
        joints[0].Name = "Parent";
        joints[0].ParentIndex = -1;
        SetIdentity(joints[0].InverseBindMatrix);
        joints[1].Name = "Child";
        joints[1].ParentIndex = 0;
        SetIdentity(joints[1].InverseBindMatrix);
        skeleton.SetJoints(std::move(joints));
        assert(skeleton.Load());
    }

    void SeedHierarchyClip(AnimationClipResource& clip)
    {
        Skeletal::SkeletalAnimationClip data;
        data.Name = "NonCommutativeHierarchy";
        data.DurationSeconds = 1.0f;

        Skeletal::SkeletalAnimationChannel parentTranslation;
        parentTranslation.JointIndex = 0;
        parentTranslation.Path = Skeletal::SkeletalAnimationPath::Translation;
        parentTranslation.Samples.push_back(MakeSample(0.0f, 10.0f, 0.0f, 0.0f, 0.0f));

        Skeletal::SkeletalAnimationChannel parentRotation;
        parentRotation.JointIndex = 0;
        parentRotation.Path = Skeletal::SkeletalAnimationPath::Rotation;
        parentRotation.Samples.push_back(MakeSample(0.0f, 0.0f, 0.0f, -0.70710678f, 0.70710678f));

        Skeletal::SkeletalAnimationChannel childTranslation;
        childTranslation.JointIndex = 1;
        childTranslation.Path = Skeletal::SkeletalAnimationPath::Translation;
        childTranslation.Samples.push_back(MakeSample(0.0f, 2.0f, 0.0f, 0.0f, 0.0f));

        Skeletal::SkeletalAnimationChannel childScale;
        childScale.JointIndex = 1;
        childScale.Path = Skeletal::SkeletalAnimationPath::Scale;
        childScale.Samples.push_back(MakeSample(0.0f, 2.0f, 1.0f, 1.0f, 0.0f));

        data.Channels.push_back(std::move(parentTranslation));
        data.Channels.push_back(std::move(parentRotation));
        data.Channels.push_back(std::move(childTranslation));
        data.Channels.push_back(std::move(childScale));
        clip.SetClip(std::move(data));
        assert(clip.Load());
    }

    void SeedHierarchyMesh(SkinnedMeshResource& mesh)
    {
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
        vertices[0].Position = {1.0f, 1.0f, 0.0f};
        vertices[0].Normal = {1.0f, 1.0f, 0.0f};
        vertices[0].JointIndices[0] = 1;
        vertices[0].JointWeights[0] = 1.0f;
        Container::VariableArray<uint32_t> indices = {0, 0, 0};
        mesh.SetVertices(std::move(vertices));
        mesh.SetIndices(std::move(indices));
        assert(mesh.Load());
    }

    Animation::SkeletalPoseSnapshot SampleAt(const SkeletonResource& skeleton,
                                             const AnimationClipResource& clip,
                                             const SkinnedMeshResource& mesh,
                                             float time)
    {
        Math::Matrix4x4 meshNodeGlobal = Math::Matrix4x4::Identity;
        meshNodeGlobal.SetTranslationRow(Math::Vector3(10.0f, 0.0f, 0.0f));
        Animation::SkeletalPoseSnapshot pose;
        assert(Animation::SkeletalAnimationSampler::Sample(
            skeleton, clip, mesh, time, meshNodeGlobal, pose));
        assert(pose.BonePalette.size() == 1);
        assert(pose.bHasAnimatedBounds);
        return pose;
    }

    void AssertInvalidHierarchyRejected(const Container::VariableArray<int32_t>& parentIndices,
                                        const AnimationClipResource& clip,
                                        const SkinnedMeshResource& mesh)
    {
        SkeletonResource invalidSkeleton;
        invalidSkeleton.Initialize();
        Container::VariableArray<Skeletal::SkeletalJoint> joints(parentIndices.size());
        for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
        {
            joints[jointIndex].Name = "Invalid";
            joints[jointIndex].ParentIndex = parentIndices[jointIndex];
            SetIdentity(joints[jointIndex].InverseBindMatrix);
        }
        invalidSkeleton.SetJoints(std::move(joints));
        assert(invalidSkeleton.Load());

        Animation::SkeletalPoseSnapshot pose;
        pose.BonePalette.push_back(Math::Matrix4x4::Identity);
        pose.bHasAnimatedBounds = true;
        assert(!Animation::SkeletalAnimationSampler::Sample(
            invalidSkeleton, clip, mesh, 0.0f, Math::Matrix4x4::Identity, pose));
        assert(pose.BonePalette.empty());
        assert(!pose.bHasAnimatedBounds);
    }

    void AssertVertex(const Skeletal::SkeletalVertex& source,
                      const Animation::SkeletalPoseSnapshot& pose,
                      float positionX,
                      float positionY,
                      float normalX,
                      float normalY)
    {
        const Animation::SkinnedVertexSample skinned =
            Animation::SkeletalAnimationSampler::SkinVertex(source, pose.BonePalette);
        AssertNear(skinned.Position.x, positionX);
        AssertNear(skinned.Position.y, positionY);
        AssertNear(skinned.Position.z, 0.0f);
        AssertNear(skinned.Normal.x, normalX);
        AssertNear(skinned.Normal.y, normalY);
        AssertNear(skinned.Normal.z, 0.0f);
    }
} // namespace

int main()
{
    std::cout << "SkeletalAnimationSamplingTest start\n";

    SkeletonResource skeleton;
    AnimationClipResource clip;
    SkinnedMeshResource mesh;
    skeleton.Initialize();
    clip.Initialize();
    mesh.Initialize();
    SeedSkeleton(skeleton);
    SeedClip(clip);
    SeedMesh(mesh);

    AssertInvalidHierarchyRejected(Container::VariableArray<int32_t>{-2}, clip, mesh);
    AssertInvalidHierarchyRejected(Container::VariableArray<int32_t>{1}, clip, mesh);
    AssertInvalidHierarchyRejected(Container::VariableArray<int32_t>{1, 0}, clip, mesh);

    const auto& vertices = mesh.GetVertices();
    {
        const Animation::SkeletalPoseSnapshot pose = SampleAt(skeleton, clip, mesh, 0.0f);
        AssertNear(pose.BonePalette[0].GetTranslationRow().x, 0.0f);
        AssertVertex(vertices[0], pose, 1.0f, 0.0f, 1.0f, 0.0f);
        AssertNear(pose.AnimatedBounds.Min.x, -1.0f);
        AssertNear(pose.AnimatedBounds.Max.x, 1.0f);
    }

    {
        const Animation::SkeletalPoseSnapshot pose = SampleAt(skeleton, clip, mesh, 0.5f);
        AssertNear(pose.BonePalette[0].GetTranslationRow().x, 3.0f);
        AssertNear(pose.BonePalette[0].GetTranslationRow().y, -2.0f);
        AssertVertex(vertices[0], pose, 3.0f, -1.0f, 0.0f, 1.0f);
        AssertVertex(vertices[1], pose, 3.0f, -3.0f, 0.0f, 1.0f);
        AssertNear(pose.AnimatedBounds.Min.x, 3.0f);
        AssertNear(pose.AnimatedBounds.Min.y, -3.0f);
        AssertNear(pose.AnimatedBounds.Max.x, 3.0f);
        AssertNear(pose.AnimatedBounds.Max.y, -1.0f);
    }

    {
        const Animation::SkeletalPoseSnapshot pose = SampleAt(skeleton, clip, mesh, 1.0f);
        AssertNear(pose.BonePalette[0].GetTranslationRow().x, 6.0f);
        AssertVertex(vertices[0], pose, 5.0f, 0.0f, -1.0f, 0.0f);
        AssertVertex(vertices[1], pose, 7.0f, 0.0f, -1.0f, 0.0f);
        AssertNear(pose.AnimatedBounds.Min.x, 5.0f);
        AssertNear(pose.AnimatedBounds.Max.x, 7.0f);
    }

    {
        AnimationClipResource stepClip;
        stepClip.Initialize();
        SeedStepClip(stepClip);

        const Animation::SkeletalPoseSnapshot keyPose = SampleAt(skeleton, stepClip, mesh, 0.5f);
        AssertNear(keyPose.BonePalette[0].GetTranslationRow().x, 10.0f);

        const Animation::SkeletalPoseSnapshot intervalPose = SampleAt(skeleton, stepClip, mesh, 0.75f);
        AssertNear(intervalPose.BonePalette[0].GetTranslationRow().x, 10.0f);
    }

    {
        SkeletonResource hierarchySkeleton;
        AnimationClipResource hierarchyClip;
        SkinnedMeshResource hierarchyMesh;
        hierarchySkeleton.Initialize();
        hierarchyClip.Initialize();
        hierarchyMesh.Initialize();
        SeedHierarchySkeleton(hierarchySkeleton);
        SeedHierarchyClip(hierarchyClip);
        SeedHierarchyMesh(hierarchyMesh);

        Animation::SkeletalPoseSnapshot pose;
        assert(Animation::SkeletalAnimationSampler::Sample(
            hierarchySkeleton,
            hierarchyClip,
            hierarchyMesh,
            0.0f,
            Math::Matrix4x4::Identity,
            pose));
        assert(pose.BonePalette.size() == 2);
        AssertNear(pose.BonePalette[1].GetTranslationRow().x, 10.0f);
        AssertNear(pose.BonePalette[1].GetTranslationRow().y, 2.0f);

        const Animation::SkinnedVertexSample skinned =
            Animation::SkeletalAnimationSampler::SkinVertex(
                hierarchyMesh.GetVertices()[0], pose.BonePalette);
        AssertNear(skinned.Position.x, 9.0f);
        AssertNear(skinned.Position.y, 4.0f);
        AssertNear(skinned.Position.z, 0.0f);
        AssertNear(skinned.Normal.x, -0.89442719f);
        AssertNear(skinned.Normal.y, 0.44721359f);
        AssertNear(skinned.Normal.z, 0.0f);
        AssertNear(pose.AnimatedBounds.Min.x, 9.0f);
        AssertNear(pose.AnimatedBounds.Min.y, 4.0f);
        AssertNear(pose.AnimatedBounds.Max.x, 9.0f);
        AssertNear(pose.AnimatedBounds.Max.y, 4.0f);
    }

    std::cout << "SkeletalAnimationSamplingTest passed\n";
    return 0;
}
