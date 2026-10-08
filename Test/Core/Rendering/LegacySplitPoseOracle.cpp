// GR12変更前のsplit Sampler。作者rest/frameと拒否条件の比較に用いる。
#include "LegacyPoseOracle.h"
#include "Animation/RigBoundClipProof.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/SkeletalJointGlobalRowMath.h"
#include "Animation/SkeletalClipSampling.h"
#include "Animation/SkeletonResource.h"
#include "Animation/AnimationClipResource.h"
#include "Resource/SkinnedMeshResource.h"
#include "Asset/CookedSkinMeshV1.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Animation::LegacyPoseOracle
{
    using namespace NorvesLib::Core::Animation::Detail;
    namespace C = Container;
    namespace S = Skeletal;
    namespace
    {
        Math::Matrix4x4 Matrix(const C::FixedArray<float, 16>& v)
        {
            return {v[0], v[1], v[2],  v[3],  v[4],  v[5],  v[6],  v[7],
                    v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]};
        }
        bool ValidClip(const S::SkeletalAnimationClip& clip, size_t joints)
        {
            const S::RigV1Limits limits;
            if (clip.Name.empty() || !std::isfinite(clip.DurationSeconds) || clip.DurationSeconds < 0 ||
                clip.Channels.size() > limits.MaxChannels)
            {
                return false;
            }
            size_t samples = 0;
            for (size_t i = 0; i < clip.Channels.size(); ++i)
            {
                const auto& c = clip.Channels[i];
                if (c.JointIndex >= joints || uint32_t(c.Path) > 2 || uint32_t(c.Interpolation) > 1 ||
                    c.Samples.empty() || c.Samples.size() > limits.MaxSamples - samples)
                {
                    return false;
                }
                samples += c.Samples.size();
                for (size_t j = 0; j < i; ++j)
                {
                    if (clip.Channels[j].JointIndex == c.JointIndex && clip.Channels[j].Path == c.Path)
                    {
                        return false;
                    }
                }
                float previous = -1;
                for (const auto& s : c.Samples)
                {
                    const auto& v = s.Value;
                    if (!std::isfinite(s.TimeSeconds) || s.TimeSeconds < 0 || s.TimeSeconds <= previous ||
                        s.TimeSeconds > clip.DurationSeconds || !std::isfinite(v.X) || !std::isfinite(v.Y) ||
                        !std::isfinite(v.Z) || !std::isfinite(v.W))
                    {
                        return false;
                    }
                    if (c.Path == S::SkeletalAnimationPath::Rotation && !S::IsRepresentableSkeletalRotation(v))
                    {
                        return false;
                    }
                    if (c.Path == S::SkeletalAnimationPath::Scale && (v.X <= 0 || v.Y <= 0 || v.Z <= 0))
                    {
                        return false;
                    }
                    previous = s.TimeSeconds;
                }
            }
            return true;
        }
        S::SkeletalValue Sample(const S::SkeletalAnimationChannel& c, float time)
        {
            if (c.Samples.size() == 1 || time <= c.Samples.front().TimeSeconds)
            {
                return c.Samples.front().Value;
            }
            if (time >= c.Samples.back().TimeSeconds)
            {
                return c.Samples.back().Value;
            }
            for (size_t i = 1; i < c.Samples.size(); ++i)
            {
                if (time <= c.Samples[i].TimeSeconds)
                {
                    return SampleSkeletalChannelInterval(c, c.Samples[i - 1], c.Samples[i], time);
                }
            }
            return c.Samples.back().Value;
        }
    } // namespace
    bool SampleSplitV1(const SkeletonResource& skeleton, const AnimationClipResource& clip,
                       const SkinnedMeshResource& mesh, float timeSeconds, const Math::Matrix4x4& meshNodeGlobalRow,
                       SkeletalPoseSnapshot& out)
    {
        out.Clear();
        const auto* sk = skeleton.GetSplitSkeleton();
        const auto* sm = mesh.GetSplitMesh();
        if (!skeleton.IsSplitV1() || !mesh.IsSplitV1() || !skeleton.IsLoaded() || !mesh.IsLoaded() ||
            !clip.IsLoaded() || !sk || !sm || !std::isfinite(timeSeconds) ||
            !S::SameRigTopology(sk->Topology, sm->Topology) || sk->ContentHash != sm->SkeletonContentHash ||
            sk->CurrentRest.RestHash != sm->SkeletonRestHash || sk->RootHash != sm->SkeletonRootHash)
        {
            return false;
        }
        if (sk->Profile != sm->Profile ||
            (S::IsStaticRootFrameProfile(sk->Profile) && !S::RigBoundClipAccess::Matches(clip, sk)))
        {
            return false;
        }
        const size_t count = sk->Topology.Joints.size();
        const auto& data = clip.GetClip();
        if (!ValidClip(data, count))
        {
            return false;
        }
        Math::Matrix4x4 inverseMesh;
        if (!TryInverseMatrix(meshNodeGlobalRow, inverseMesh))
        {
            return false;
        }
        C::VariableArray<Detail::JointTransform> transforms(count);
        C::VariableArray<Math::Matrix4x4> locals(count), globals(count);
        for (size_t i = 0; i < count; ++i)
        {
            const auto& r = sk->CurrentRest.Rest[i];
            auto& t = transforms[i];
            t.Translation = {r.Translation.X, r.Translation.Y, r.Translation.Z};
            t.Rotation = SkeletalRotationFromColumn(r.Rotation.X, r.Rotation.Y, r.Rotation.Z, r.Rotation.W);
            t.Scale = {r.Scale.X, r.Scale.Y, r.Scale.Z};
        }
        const float time = std::fmax(0.0f, std::fmin(timeSeconds, data.DurationSeconds));
        for (const auto& c : data.Channels)
        {
            const auto v = Sample(c, time);
            auto& t = transforms[c.JointIndex];
            switch (c.Path)
            {
            case S::SkeletalAnimationPath::Translation:
                t.Translation = {v.X, v.Y, v.Z};
                break;
            case S::SkeletalAnimationPath::Rotation:
                t.Rotation = SkeletalRotationFromColumn(v.X, v.Y, v.Z, v.W);
                break;
            case S::SkeletalAnimationPath::Scale:
                t.Scale = {v.X, v.Y, v.Z};
                break;
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            locals[i] = ComposeSkeletalLocalRowTransform(transforms[i]);
            if (!IsFiniteMatrix(locals[i]))
            {
                return false;
            }
        }
        C::VariableArray<uint8_t> scratch(count, 0);
        const auto parent = [&](uint32_t i) noexcept -> int32_t { return sk->Topology.Joints[i].ParentIndex; };
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!BuildJointGlobalRow(i, parent, {locals.data(), locals.size()}, {globals.data(), globals.size()},
                                     {scratch.data(), scratch.size()}))
            {
                return false;
            }
        }
        SkeletalPoseSnapshot candidate;
        candidate.BonePalette.resize(count);
        candidate.JointModelMatrices.resize(count);
        const auto root = Matrix(sk->RootTransform);
        for (size_t i = 0; i < count; ++i)
        {
            const auto global = globals[i] * root;
            candidate.BonePalette[i] = Matrix(sm->InverseBindMatrices[i]) * global * inverseMesh;
            candidate.JointModelMatrices[i] = global * inverseMesh;
            if (!IsFiniteMatrix(candidate.BonePalette[i]) || !IsFiniteMatrix(candidate.JointModelMatrices[i]))
            {
                return false;
            }
        }
        candidate.AnimatedBounds = Math::AABB::CreateInvalid();
        for (const auto& v : sm->Vertices)
        {
            const auto skinned = SkinVertex(v, candidate.BonePalette);
            if (!std::isfinite(skinned.Position.x) || !std::isfinite(skinned.Position.y) ||
                !std::isfinite(skinned.Position.z))
            {
                return false;
            }
            candidate.AnimatedBounds.Expand(skinned.Position);
        }
        candidate.bHasAnimatedBounds = !sm->Vertices.empty();
        out = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::Animation::LegacyPoseOracle
