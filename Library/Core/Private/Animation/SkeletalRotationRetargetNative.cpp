#include "Animation/SkeletalRotationRetargetNative.h"
#include "Animation/SkeletalRotationRetargetMath.h"
#include "Animation/SkeletalFloatEnvironment.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/SkeletalJointGlobalRowMath.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Status = SkeletalRetargetStatus;
        using Side = SkeletalJointMappingSide;
        template <class T> bool Valid(Container::Span<T> values)
        {
            return Detail::RetargetValidSpan(values);
        }
        template <class T, class U> bool Overlap(Container::Span<T> a, Container::Span<U> b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data()), y = reinterpret_cast<uintptr_t>(b.data());
            return a.size() != 0 && b.size() != 0 && x < y + b.size() * sizeof(U) && y < x + a.size() * sizeof(T);
        }
        Math::Matrix4x4 Load(const Container::FixedArray<float, 16>& v)
        {
            return Math::Matrix4x4(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12],
                                   v[13], v[14], v[15]);
        }
        bool Affine(const Math::Matrix4x4& m)
        {
            return m.m03 == 0 && m.m13 == 0 && m.m23 == 0 &&
                   std::abs(static_cast<double>(m.m33) - 1) <= Detail::RetargetRotationTolerance;
        }
        Status Orientation(const Math::Matrix4x4& m, Bvh::Matrix3d& out)
        {
            if (!Detail::IsFiniteMatrix(m))
            {
                return Status::NonFiniteTransform;
            }
            if (!Affine(m))
            {
                return Status::UnsupportedAffine;
            }
            double lengths[3]{};
            for (size_t i = 0; i < 3; ++i)
            {
                lengths[i] = std::hypot(static_cast<double>(m.values[i * 4]), static_cast<double>(m.values[i * 4 + 1]),
                                        static_cast<double>(m.values[i * 4 + 2]));
            }
            const double minimum = std::min({lengths[0], lengths[1], lengths[2]}),
                         maximum = std::max({lengths[0], lengths[1], lengths[2]});
            if (minimum <= 0 || !std::isfinite(maximum))
            {
                return Status::DegenerateScale;
            }
            if (maximum - minimum > Detail::RetargetRotationTolerance * maximum)
            {
                return Status::NonUniformScale;
            }
            Bvh::Matrix3d column;
            for (size_t row = 0; row < 3; ++row)
            {
                for (size_t col = 0; col < 3; ++col)
                {
                    column.Values[col * 3 + row] = static_cast<double>(m.values[row * 4 + col]) / lengths[row];
                }
            }
            const auto& v = column.Values;
            const double determinant = v[0] * (v[4] * v[8] - v[5] * v[7]) - v[1] * (v[3] * v[8] - v[5] * v[6]) +
                                       v[2] * (v[3] * v[7] - v[4] * v[6]);
            if (determinant < 0)
            {
                return Status::Reflection;
            }
            if (!Detail::RetargetProjectRotation(column, out))
            {
                return Status::Shear;
            }
            return Status::Success;
        }
        SkeletalRetargetResult Failure(Status status, uint32_t joint = UINT32_MAX, double angle = 0)
        {
            return {status, Side::Target, joint, SIZE_MAX, angle};
        }
    } // namespace
    SkeletalRetargetResult RetargetSkeletalRotationFrame(const SkeletalRetargetNativeInput& input,
                                                         Container::Span<const Skeletal::SkeletalJoint> joints,
                                                         const Math::Matrix4x4& meshGlobal,
                                                         Container::Span<SkeletalRetargetRotationValue> out)
    {
        if (!Detail::SupportedSkeletalFloatEnvironment())
        {
            return Failure(Status::UnsupportedFloatEnvironment);
        }
        if (joints.empty() || input.Source.empty() || input.Mappings.empty() ||
            input.Corrections.size() != input.Mappings.size() || out.size() != input.Mappings.size())
        {
            return Failure(Status::InvalidInput);
        }
        if (joints.size() > SkeletalRetargetMaxJoints || input.Source.size() > SkeletalRetargetMaxJoints ||
            input.Mappings.size() > SkeletalRetargetMaxJoints)
        {
            return Failure(Status::LimitExceeded);
        }
        if (!Valid(joints) || !Valid(input.Source) || !Valid(input.Mappings) || !Valid(input.Corrections) ||
            !Valid(out))
        {
            return Failure(Status::InvalidInput);
        }
        if (Overlap(out, joints) || Overlap(out, input.Source) || Overlap(out, input.Mappings) ||
            Overlap(out, input.Corrections) || Overlap(out, Container::Span<const Math::Matrix4x4>(&meshGlobal, 1)))
        {
            return Failure(Status::InvalidInput);
        }
        if (input.Policy != SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations ||
            (input.SourceReuse != SkeletalSourceReusePolicy::Allow &&
             input.SourceReuse != SkeletalSourceReusePolicy::Reject))
        {
            return Failure(Status::InvalidPolicy);
        }
        if (!Detail::IsFiniteMatrix(meshGlobal))
        {
            return Failure(Status::NonFiniteTransform);
        }
        if (!Affine(meshGlobal))
        {
            return Failure(Status::UnsupportedAffine);
        }
        Math::Matrix4x4 inverseMesh;
        if (!Detail::TryInverseMatrix(meshGlobal, inverseMesh))
        {
            return Failure(Status::NonFiniteTransform);
        }
        const size_t count = joints.size();
        for (size_t i = 0; i < count; ++i)
        {
            const int32_t parent = joints[i].ParentIndex;
            if (parent < -1 || (parent >= 0 && static_cast<size_t>(parent) >= count))
            {
                return Failure(Status::InvalidHierarchy, static_cast<uint32_t>(i));
            }
            size_t depth = 0;
            int32_t cursor = static_cast<int32_t>(i);
            while (cursor >= 0)
            {
                if (++depth > count)
                {
                    return Failure(Status::InvalidHierarchy, static_cast<uint32_t>(i));
                }
                // 各親の範囲を、このwalkで読む前にも検査する。
                const int32_t next = joints[static_cast<size_t>(cursor)].ParentIndex;
                if (next < -1 || (next >= 0 && static_cast<size_t>(next) >= count))
                {
                    return Failure(Status::InvalidHierarchy, static_cast<uint32_t>(cursor));
                }
                cursor = next;
            }
        }
        Container::VariableArray<Math::Matrix4x4> inverseBinds(count), bindGlobals(count), locals(count),
            globals(count);
        Container::VariableArray<Detail::JointTransform> transforms(count);
        Container::VariableArray<SkeletalRetargetTargetRotation> targets(count);
        Container::VariableArray<SkeletalRetargetRotationWork> work(count);
        Container::VariableArray<SkeletalRetargetRotationValue> candidate(input.Mappings.size());
        Container::VariableArray<uint8_t> state(count, 0);
        const auto parentAt = [joints](uint32_t index) noexcept -> int32_t
        {
            return joints[index].ParentIndex;
        };
        for (size_t i = 0; i < count; ++i)
        {
            inverseBinds[i] = Load(joints[i].InverseBindMatrix);
            if (!Detail::IsFiniteMatrix(inverseBinds[i]))
            {
                return Failure(Status::NonFiniteTransform, static_cast<uint32_t>(i));
            }
            if (!Affine(inverseBinds[i]))
            {
                return Failure(Status::UnsupportedAffine, static_cast<uint32_t>(i));
            }
            if (!Detail::TryBuildBindGlobalRow(inverseBinds[i], meshGlobal, bindGlobals[i]))
            {
                return Failure(Status::NonFiniteTransform, static_cast<uint32_t>(i));
            }
            targets[i].ParentIndex = joints[i].ParentIndex;
            const auto status = Orientation(bindGlobals[i], targets[i].BindWorldRotation);
            if (status != Status::Success)
            {
                return Failure(status, static_cast<uint32_t>(i));
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            const auto parent = joints[i].ParentIndex;
            Math::Matrix4x4 bindLocal;
            if (!Detail::TryBuildBindLocalRow(
                    bindGlobals[i], parent < 0 ? nullptr : &bindGlobals[static_cast<size_t>(parent)], bindLocal))
            {
                return Failure(Status::NonFiniteTransform, static_cast<uint32_t>(i));
            }
            Bvh::Matrix3d rawLocal;
            auto status = Orientation(bindLocal, rawLocal);
            if (status != Status::Success)
            {
                return Failure(status, static_cast<uint32_t>(i));
            }
            transforms[i] = Detail::DecomposeRowTransform(bindLocal);
            locals[i] = Detail::ComposeSkeletalLocalRowTransform(transforms[i]);
            status = Orientation(locals[i], targets[i].BindLocalRotation);
            if (status != Status::Success)
            {
                return Failure(status, static_cast<uint32_t>(i));
            }
            const double angle = Detail::RetargetRotationAngle(rawLocal, targets[i].BindLocalRotation);
            if (!std::isfinite(angle) || angle > Detail::RetargetAngularTolerance)
            {
                return Failure(Status::BindReconstructionMismatch, static_cast<uint32_t>(i), angle);
            }
        }
        const Container::Span<const Math::Matrix4x4> localView(locals);
        const Container::Span<Math::Matrix4x4> globalView(globals);
        const Container::Span<uint8_t> stateView(state);
        for (size_t i = 0; i < count; ++i)
        {
            if (!Detail::BuildJointGlobalRow(static_cast<uint32_t>(i), parentAt, localView, globalView, stateView))
            {
                return Failure(Status::NonFiniteTransform, static_cast<uint32_t>(i));
            }
            Bvh::Matrix3d realized;
            const auto status = Orientation(globals[i], realized);
            if (status != Status::Success)
            {
                return Failure(status, static_cast<uint32_t>(i));
            }
            const double angle = Detail::RetargetRotationAngle(realized, targets[i].BindWorldRotation);
            if (!std::isfinite(angle) || angle > Detail::RetargetAngularTolerance)
            {
                return Failure(Status::BindReconstructionMismatch, static_cast<uint32_t>(i), angle);
            }
        }
        SkeletalRetargetRotationRequest request{
            input.Source,   Container::Span<const SkeletalRetargetTargetRotation>(targets),
            input.Mappings, input.Corrections,
            input.Root,     input.SourceReuse,
            input.Policy};
        const auto evaluated =
            EvaluateSkeletalRotationFrame(request, Container::Span<SkeletalRetargetRotationWork>(work),
                                          Container::Span<SkeletalRetargetRotationValue>(candidate));
        if (!evaluated.Succeeded())
        {
            return evaluated;
        }
        for (const auto& value : candidate)
        {
            transforms[value.TargetIndex].Rotation =
                Detail::SkeletalRotationFromColumn(value.X, value.Y, value.Z, value.W);
        }
        for (size_t i = 0; i < count; ++i)
        {
            locals[i] = Detail::ComposeSkeletalLocalRowTransform(transforms[i]);
            if (!Detail::IsFiniteMatrix(locals[i]))
            {
                return Failure(Status::NonFiniteTransform, static_cast<uint32_t>(i));
            }
            state[i] = 0;
        }
        double maximumError = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (!Detail::BuildJointGlobalRow(static_cast<uint32_t>(i), parentAt, localView, globalView, stateView))
            {
                return Failure(Status::NonFiniteTransform, static_cast<uint32_t>(i));
            }
            Bvh::Matrix3d realized;
            const auto status = Orientation(globals[i], realized);
            if (status != Status::Success)
            {
                return Failure(status, static_cast<uint32_t>(i));
            }
            const double angle = Detail::RetargetRotationAngle(realized, work[i].WorldRotation);
            if (!std::isfinite(angle) || angle > Detail::RetargetAngularTolerance)
            {
                return Failure(Status::FloatRealizationMismatch, static_cast<uint32_t>(i), angle);
            }
            maximumError = std::max(maximumError, angle);
            // Sampleの公開poseに入る後段の行列も、同じ左結合順で有限性を確認する。
            const auto jointModel = globals[i] * inverseMesh;
            const auto palette = inverseBinds[i] * globals[i] * inverseMesh;
            if (!Detail::IsFiniteMatrix(jointModel) || !Detail::IsFiniteMatrix(palette))
            {
                return Failure(Status::NonFiniteTransform, static_cast<uint32_t>(i));
            }
        }
        for (size_t i = 0; i < candidate.size(); ++i)
        {
            out[i] = candidate[i];
        }
        return {Status::Success, Side::None, UINT32_MAX, SIZE_MAX, maximumError, true};
    }
} // namespace NorvesLib::Core::Animation
