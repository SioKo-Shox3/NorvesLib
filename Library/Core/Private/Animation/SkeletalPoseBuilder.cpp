#include "Animation/SkeletalPoseBuilder.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletonResource.h"
#include "Animation/ClipChannelSampler.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/RigBoundClipProof.h"
#include "Asset/CookedSkinMeshV1.h"
#include "Resource/SkinnedMeshResource.h"
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        Math::Matrix4x4 Matrix(const Container::FixedArray<float, 16>& v)
        {
            return {v[0], v[1], v[2],  v[3],  v[4],  v[5],  v[6],  v[7],
                    v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]};
        }
    } // namespace

    bool SkeletalPoseBuilder::IsPreparedFor(const SkeletalPoseContext& context, const SkeletonResource& skeleton,
                                            const AnimationClipResource& clip, const SkinnedMeshResource& mesh,
                                            const Math::Matrix4x4& transform)
    {
        return context.bValid && context.Skeleton == &skeleton && context.Clip == &clip && context.Mesh == &mesh &&
               context.SkeletonRevision == skeleton.GetPoseRevision() &&
               context.ClipRevision == clip.GetPoseRevision() && context.MeshRevision == mesh.GetPoseRevision() &&
               std::memcmp(context.MeshTransform.values, transform.values, sizeof(transform.values)) == 0 &&
               (!context.bSplit || (skeleton.IsLoaded() && clip.IsLoaded() && mesh.IsLoaded()));
    }

    bool SkeletalPoseBuilder::Prepare(const SkeletonResource& skeleton, const AnimationClipResource& clip,
                                      const SkinnedMeshResource& mesh, const Math::Matrix4x4& transform,
                                      SkeletalPoseContext& out, const PoseBoundsSettings& boundsSettings)
    {
        out = {};
        const auto& runtime = skeleton.GetPoseRuntime();
        const auto& clipRuntime = clip.GetPoseRuntime();
        if (!runtime.bValid || !IsValidPoseBoundsSettings(boundsSettings) ||
            !Detail::TryInverseMatrix(transform, out.InverseMesh))
        {
            return false;
        }
        out.bSplit = skeleton.IsSplitV1() || mesh.IsSplitV1();
        const size_t count = runtime.Parents.size();
        if (out.bSplit)
        {
            const auto* sk = skeleton.GetSplitSkeleton();
            const auto* sm = mesh.GetSplitMesh();
            if (!skeleton.IsSplitV1() || !mesh.IsSplitV1() || !sk || !sm || !skeleton.IsLoaded() || !mesh.IsLoaded() ||
                !clip.IsLoaded() || !clipRuntime.bSplitValid || clipRuntime.RequiredJointCount > count ||
                !Skeletal::SameRigTopology(sk->Topology, sm->Topology) || sk->ContentHash != sm->SkeletonContentHash ||
                sk->CurrentRest.RestHash != sm->SkeletonRestHash || sk->RootHash != sm->SkeletonRootHash ||
                sk->Profile != sm->Profile ||
                (Skeletal::IsStaticRootFrameProfile(sk->Profile) && !Skeletal::RigBoundClipAccess::Matches(clip, sk)))
            {
                return false;
            }
            out.DefaultLocal = runtime.AuthorRestLocal;
            out.InverseBind.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                out.InverseBind[i] = Matrix(sm->InverseBindMatrices[i]);
            }
            out.RootFrame = runtime.RootFrame;
        }
        else
        {
            if (!clipRuntime.bLegacyValid)
            {
                return false;
            }
            out.InverseBind = runtime.LegacyInverseBind;
            Container::VariableArray<Math::Matrix4x4> globals(count);
            for (size_t i = 0; i < count; ++i)
            {
                // author restがある場合も旧Samplerの中間演算の有限性を検査する。
                globals[i] = runtime.LegacyBindModel[i] * transform;
                if (!Detail::IsFiniteMatrix(globals[i]))
                {
                    return false;
                }
            }
            if (!runtime.AuthorRestLocal.empty())
            {
                out.DefaultLocal = runtime.AuthorRestLocal;
            }
            else
            {
                out.DefaultLocal.resize(count);
                for (size_t i = 0; i < count; ++i)
                {
                    const int32_t parent = runtime.Parents[i];
                    Math::Matrix4x4 local;
                    if (!Detail::TryBuildBindLocalRow(globals[i], parent >= 0 ? &globals[parent] : nullptr, local))
                    {
                        return false;
                    }
                    out.DefaultLocal[i] = FromRowMatrix(local);
                }
            }
        }
        MeshPoseBounds approximate;
        const MeshPoseBounds* bounds = &mesh.GetPoseBounds();
        if (boundsSettings.WeightThreshold != 0)
        {
            BuildMeshPoseBounds(mesh.GetVertices(), boundsSettings.WeightThreshold, approximate);
            bounds = &approximate;
        }
        out.BoundsSettings = boundsSettings;
        out.MaximumWeightSum = bounds->MaximumWeightSum;
        out.bHasVertices = bounds->bHasVertices;
        out.bNeedsExactBounds = bounds->bNeedsExactFallback;
        for (const auto& joint : bounds->Joints)
        {
            if (joint.JointIndex < count)
            {
                out.JointBounds.push_back(joint);
            }
        }
        for (const auto& fallback : bounds->Fallbacks)
        {
            if (fallback.RequiredJointCount > count)
            {
                out.FallbackBounds.Merge(fallback.Bounds);
            }
        }
        out.Parents = runtime.Parents;
        out.Order = runtime.EvaluationOrder;
        out.MeshTransform = transform;
        out.Skeleton = &skeleton;
        out.Clip = &clip;
        out.Mesh = &mesh;
        out.SkeletonRevision = skeleton.GetPoseRevision();
        out.ClipRevision = clip.GetPoseRevision();
        out.MeshRevision = mesh.GetPoseRevision();
        out.bValid = true;
        return true;
    }

    bool SkeletalPoseBuilder::SampleClipToLocalPose(const SkeletalPoseContext& context,
                                                    const AnimationClipResource& clip, float timeSeconds,
                                                    LocalPose& out)
    {
        if (!context.bValid || context.Clip != &clip || !std::isfinite(timeSeconds) ||
            !IsPreparedFor(context, *context.Skeleton, clip, *context.Mesh, context.MeshTransform))
        {
            out.clear();
            return false;
        }
        out = context.DefaultLocal;
        const auto& data = clip.GetClip();
        const float time = std::fmax(0.0f, std::fmin(timeSeconds, data.DurationSeconds));
        for (const auto& selection : clip.GetPoseRuntime().Joints)
        {
            if (selection.JointIndex >= out.size())
            {
                continue;
            }
            auto& transform = out[selection.JointIndex];
            for (size_t path = 0; path < 3; ++path)
            {
                const size_t selected = selection.Channels[path];
                if (selected == SIZE_MAX)
                {
                    continue;
                }
                const auto value = ClipChannelSampler::Sample(data.Channels[selected], time);
                if (path == 0)
                {
                    transform.Translation = {value.X, value.Y, value.Z};
                }
                else if (path == 1)
                {
                    transform.Rotation = Detail::NormalizeQuaternion({value.X, value.Y, value.Z, value.W});
                }
                else
                {
                    transform.Scale = {value.X, value.Y, value.Z};
                }
            }
        }
        return true;
    }

    bool SkeletalPoseBuilder::BuildGlobalPose(const SkeletalPoseContext& context,const LocalPose& pose,PoseScratch& scratch)
    {
        if (!context.bValid || pose.size() != context.Parents.size() ||
            !IsPreparedFor(context, *context.Skeleton, *context.Clip, *context.Mesh, context.MeshTransform))
        {
            return false;
        }
        const size_t count = pose.size();
        scratch.LocalMatrices.resize(count);
        scratch.GlobalMatrices.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            scratch.LocalMatrices[i] = ToRowMatrix(pose[i]);
            if (!Detail::IsFiniteMatrix(scratch.LocalMatrices[i]))
            {
                return false;
            }
        }
        for (uint32_t i : context.Order)
        {
            const int32_t parent = context.Parents[i];
            scratch.GlobalMatrices[i] =
                parent >= 0 ? scratch.LocalMatrices[i] * scratch.GlobalMatrices[parent] : scratch.LocalMatrices[i];
            if (!Detail::IsFiniteMatrix(scratch.GlobalMatrices[i]))
            {
                return false;
            }
        }
        return true;
    }
    bool SkeletalPoseBuilder::BuildJointModelMatrices(const SkeletalPoseContext& context,const LocalPose& pose,
        PoseScratch& scratch,Container::VariableArray<Math::Matrix4x4>& out)
    {
        out.clear();
        if(!BuildGlobalPose(context,pose,scratch))return false;
        out.resize(pose.size());
        for(size_t i=0;i<pose.size();++i)
        {
            const auto global=context.bSplit?scratch.GlobalMatrices[i]*context.RootFrame:scratch.GlobalMatrices[i];
            out[i]=global*context.InverseMesh;
            if(!Detail::IsFiniteMatrix(out[i])){out.clear();return false;}
        }
        return true;
    }

    bool SkeletalPoseBuilder::BuildPose(const SkeletalPoseContext& context, const LocalPose& pose, PoseScratch& scratch,
                                        SkeletalPoseSnapshot& out)
    {
        out.Clear();
        if(!BuildGlobalPose(context,pose,scratch))return false;
        const size_t count=pose.size();
        out.BonePalette.resize(count);
        out.JointModelMatrices.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            const auto global =
                context.bSplit ? scratch.GlobalMatrices[i] * context.RootFrame : scratch.GlobalMatrices[i];
            out.BonePalette[i] = context.InverseBind[i] * global * context.InverseMesh;
            out.JointModelMatrices[i] = global * context.InverseMesh;
            if (!Detail::IsFiniteMatrix(out.BonePalette[i]) || !Detail::IsFiniteMatrix(out.JointModelMatrices[i]))
            {
                out.Clear();
                return false;
            }
        }
        if (context.bHasVertices)
        {
            const bool fast = !context.bNeedsExactBounds &&
                              TransformJointBounds(context.JointBounds, context.FallbackBounds, out.BonePalette,
                                                   context.BoundsSettings.RelativePadding, out.AnimatedBounds,
                                                   context.MaximumWeightSum);
            if (!fast && (!ComputeExactBounds(context.Mesh->GetVertices(), out.BonePalette, context.bSplit,
                                              out.AnimatedBounds) ||
                          !ApplyPoseBoundsPadding(context.BoundsSettings.RelativePadding, out.AnimatedBounds)))
            {
                out.Clear();
                return false;
            }
            out.bHasAnimatedBounds = true;
        }
        return true;
    }

    bool SkeletalPoseBuilder::Sample(const SkeletalPoseContext& context, const AnimationClipResource& clip,
                                     float timeSeconds, PoseScratch& scratch, SkeletalPoseSnapshot& out)
    {
        out.Clear();
        return SampleClipToLocalPose(context, clip, timeSeconds, scratch.Local) &&
               BuildPose(context, scratch.Local, scratch, out);
    }
} // namespace NorvesLib::Core::Animation
