#include "Animation/Procedural/FollowThroughModifier.h"

#include "Animation/Procedural/AngleDistribution.h"
#include "Animation/SkeletonResource.h"
#include "Component/HoldSlotComponent.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Animation::Procedural
{
    namespace
    {
        using Error = PoseModifierConfigError;

        bool IsValidRotation(const Math::Quaternion& rotation)
        {
            if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) ||
                !std::isfinite(rotation.w))
            {
                return false;
            }
            const double lengthSquared = double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                                         double(rotation.z) * rotation.z + double(rotation.w) * rotation.w;
            const double epsilon = Math::Constants::EPSILON;
            return lengthSquared > epsilon * epsilon;
        }

        bool IsValidSpring(const AngularSpring& spring)
        {
            const auto velocity = spring.GetAngularVelocity();
            return IsValidRotation(spring.GetRotation()) && std::isfinite(velocity.x) && std::isfinite(velocity.y) &&
                   std::isfinite(velocity.z) && std::isfinite(spring.GetRemainderSeconds());
        }

        Container::String Text(const char* text)
        {
            Container::String result;
            for (const char* p = text; *p; ++p)
            {
                result.push_back(TCHAR(*p));
            }
            return result;
        }

        Container::String JointPath(size_t index)
        {
            auto path = Text("joints[");
            TCHAR digits[3 * sizeof(size_t)];
            size_t count = 0;
            do
            {
                digits[count++] = TCHAR('0' + index % 10);
                index /= 10;
            } while (index);
            while (count)
            {
                path.push_back(digits[--count]);
            }
            path.push_back(TCHAR(']'));
            return path;
        }

        bool Fail(PoseModifierConfigReport& report, Error error, const Container::String& path)
        {
            if (report.Error == Error::None)
            {
                report.Error = error;
                report.Path = path;
            }
            return false;
        }

        bool ReadJoint(JsonValue value, const SkeletonResource& skeleton, int32_t& out,
                       const Container::String& path, PoseModifierConfigReport& report)
        {
            PoseModifierConfigReport jointReport;
            if (!ResolvePoseModifierJoint(value, skeleton, out, jointReport))
            {
                return Fail(report, jointReport.Error, path);
            }
            return true;
        }

        bool IsDescendant(const SkeletonResource& skeleton, int32_t joint, int32_t ancestor)
        {
            const auto& parents = skeleton.GetPoseRuntime().Parents;
            if (joint == ancestor)
            {
                return false;
            }
            // 不正な親添字と循環があっても、配列外を参照せず有限回で拒否する。
            for (size_t visited = 0; visited < parents.size(); ++visited)
            {
                if (joint < 0 || static_cast<size_t>(joint) >= parents.size())
                {
                    return false;
                }
                joint = parents[static_cast<size_t>(joint)];
                if (joint == ancestor)
                {
                    return true;
                }
            }
            return false;
        }

        bool ReadNumber(JsonValue settings, const char* key, float& out, float maximum, bool bAllowZero,
                        PoseModifierConfigReport& report)
        {
            const auto value = settings.FindMember(key);
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsNumber())
            {
                return Fail(report, Error::TypeMismatch, Text(key));
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || number > maximum || (bAllowZero ? number < 0.0 : number <= 0.0))
            {
                return Fail(report, Error::OutOfRange, Text(key));
            }
            const float parsed = static_cast<float>(number);
            if (!bAllowZero && parsed == 0.0f)
            {
                return Fail(report, Error::OutOfRange, Text(key));
            }
            out = parsed;
            return true;
        }
    } // namespace

    FollowThroughModifier::FollowThroughModifier(FollowThroughSettings settings) : m_Settings(std::move(settings))
    {
        const size_t count = m_Settings.Joints.size();
        m_Springs.resize(count);
        m_AnimationRotations.resize(count);
        m_OriginalRotations.resize(count);
    }

    int32_t FollowThroughModifier::Order() const
    {
        return PoseModifierDefaultOrder::FollowThrough;
    }

    bool FollowThroughModifier::Apply(LocalPose& pose, PoseModifierContext& context)
    {
        if (!std::isfinite(context.DeltaSeconds) || context.DeltaSeconds < 0.0f || m_Settings.Joints.empty() ||
            m_Settings.HeadJoint < -1)
        {
            return false;
        }

        // 親を書き換える前のアニメ回転と、失敗時にそのまま戻すローカル回転を保存する。
        for (size_t i = 0; i < m_Settings.Joints.size(); ++i)
        {
            const int32_t joint = m_Settings.Joints[i];
            if (!TryComputeJointGlobalRotation(pose, context.Skeleton, joint, m_AnimationRotations[i]))
            {
                return false;
            }
            m_OriginalRotations[i] = pose[static_cast<size_t>(joint)].Rotation;
        }
        Math::Quaternion headRotation;
        Math::Quaternion originalHeadRotation;
        if (m_Settings.HeadJoint >= 0)
        {
            if (!TryComputeJointGlobalRotation(pose, context.Skeleton, m_Settings.HeadJoint, headRotation))
            {
                return false;
            }
            originalHeadRotation = pose[static_cast<size_t>(m_Settings.HeadJoint)].Rotation;
        }

        m_LastMassScale = m_Settings.MassScale;
        if (m_Settings.bHasHeldSlot)
        {
            if (auto* owner = context.GetOwner())
            {
                if (auto* slots = owner->GetComponent<Component::HoldSlotComponent>())
                {
                    if (slots->GetOccupancy(m_Settings.HeldSlot) > 0)
                    {
                        m_LastMassScale *= m_Settings.HeldMassScale;
                    }
                }
            }
        }
        m_LastDroppedSeconds = 0.0f;
        if (!std::isfinite(m_LastMassScale) || m_LastMassScale <= 0.0f)
        {
            m_bNeedsReset = true;
            return true;
        }
        const SpringSettings springSettings{m_Settings.FrequencyHz / std::sqrt(m_LastMassScale),
                                            m_Settings.DampingRatio};
        for (const auto& spring : m_Springs)
        {
            if (!IsValidSpring(spring))
            {
                m_bNeedsReset = true;
                break;
            }
        }
        if (m_bNeedsReset)
        {
            for (size_t i = 0; i < m_Springs.size(); ++i)
            {
                if (!m_Springs[i].Reset(m_AnimationRotations[i]))
                {
                    return true;
                }
            }
            m_bNeedsReset = false;
        }
        for (size_t i = 0; i < m_Springs.size(); ++i)
        {
            float dropped = 0.0f;
            if (!m_Springs[i].Advance(m_AnimationRotations[i], context.DeltaSeconds, springSettings, dropped) ||
                !IsValidSpring(m_Springs[i]) || !std::isfinite(dropped))
            {
                m_bNeedsReset = true;
                return true;
            }
            if (i == 0)
            {
                m_LastDroppedSeconds = dropped;
            }
        }

        const float combinedWeight = m_Settings.Strength * context.Weight;
        if (!std::isfinite(combinedWeight))
        {
            m_bNeedsReset = true;
            return true;
        }
        const float weight = std::clamp(combinedWeight, 0.0f, 1.0f);
        if (weight == 0.0f)
        {
            return true;
        }

        const auto restorePose = [&]()
        {
            for (size_t i = 0; i < m_Settings.Joints.size(); ++i)
            {
                pose[static_cast<size_t>(m_Settings.Joints[i])].Rotation = m_OriginalRotations[i];
            }
            if (m_Settings.HeadJoint >= 0)
            {
                pose[static_cast<size_t>(m_Settings.HeadJoint)].Rotation = originalHeadRotation;
            }
            context.Invalidate();
            m_bNeedsReset = true;
            return true;
        };
        for (size_t i = 0; i < m_Settings.Joints.size(); ++i)
        {
            // AngleDistribution と同じ列規約で、グローバルの残差を左から重ねる。
            auto residual = m_Springs[i].GetRotation() * Math::QuaternionUtils::Conjugate(m_AnimationRotations[i]);
            if (!IsValidRotation(residual))
            {
                return restorePose();
            }
            residual = Math::QuaternionUtils::Normalize(residual);
            if (residual.w < 0.0f)
            {
                residual = {-residual.x, -residual.y, -residual.z, -residual.w};
            }
            const auto part = Math::QuaternionUtils::Slerp(Math::Quaternion::Identity, residual, weight);
            const auto global = part * m_AnimationRotations[i];
            if (!IsValidRotation(part) || !IsValidRotation(global) ||
                !TrySetJointGlobalRotation(pose, context.Skeleton, m_Settings.Joints[i], global))
            {
                return restorePose();
            }
            context.Invalidate();
        }
        if (m_Settings.HeadJoint >= 0)
        {
            if (!TrySetJointGlobalRotation(pose, context.Skeleton, m_Settings.HeadJoint, headRotation))
            {
                return restorePose();
            }
            context.Invalidate();
        }
        return true;
    }

    void FollowThroughModifier::RequestReset()
    {
        m_bNeedsReset = true;
    }

    float FollowThroughModifier::GetLastDroppedSeconds() const
    {
        return m_LastDroppedSeconds;
    }

    float FollowThroughModifier::GetLastMassScale() const
    {
        return m_LastMassScale;
    }

    const FollowThroughSettings& FollowThroughModifier::GetSettings() const
    {
        return m_Settings;
    }

    bool CreateFollowThroughModifier(const PoseModifierCreateArgs& args, Container::TSharedPtr<IPoseModifier>& out,
                                     PoseModifierConfigReport& report)
    {
        const char* const allowed[] = {"joints", "head", "frequencyHz", "dampingRatio", "massScale", "heldSlot",
                                      "heldMassScale", "strength"};
        if (args.Settings.IsValid() && !CheckPoseModifierKeys(args.Settings, allowed, report))
        {
            return false;
        }
        const auto joints = args.Settings.FindMember("joints");
        if (!joints.IsValid())
        {
            return Fail(report, Error::MissingKey, Text("joints"));
        }
        if (!joints.IsArray())
        {
            return Fail(report, Error::TypeMismatch, Text("joints"));
        }
        if (joints.GetArraySize() == 0)
        {
            return Fail(report, Error::OutOfRange, Text("joints"));
        }
        FollowThroughSettings settings;
        settings.Joints.reserve(joints.GetArraySize());
        for (size_t i = 0; i < joints.GetArraySize(); ++i)
        {
            int32_t joint;
            const auto path = JointPath(i);
            if (!ReadJoint(joints.GetArrayElement(i), args.Skeleton, joint, path, report))
            {
                return false;
            }
            for (int32_t previous : settings.Joints)
            {
                if (joint == previous)
                {
                    return Fail(report, Error::OutOfRange, path);
                }
            }
            if (i > 0 && !IsDescendant(args.Skeleton, joint, settings.Joints.back()))
            {
                return Fail(report, Error::OutOfRange, path);
            }
            settings.Joints.push_back(joint);
        }
        const auto head = args.Settings.FindMember("head");
        if (head.IsValid())
        {
            if (!ReadJoint(head, args.Skeleton, settings.HeadJoint, Text("head"), report))
            {
                return false;
            }
            if (!IsDescendant(args.Skeleton, settings.HeadJoint, settings.Joints.back()))
            {
                return Fail(report, Error::OutOfRange, Text("head"));
            }
        }
        if (!ReadNumber(args.Settings, "frequencyHz", settings.FrequencyHz, 30.0f, false, report) ||
            !ReadNumber(args.Settings, "dampingRatio", settings.DampingRatio, 1.0f, false, report) ||
            !ReadNumber(args.Settings, "massScale", settings.MassScale, 100.0f, false, report) ||
            !ReadNumber(args.Settings, "heldMassScale", settings.HeldMassScale, 100.0f, false, report) ||
            !ReadNumber(args.Settings, "strength", settings.Strength, 1.0f, true, report))
        {
            return false;
        }
        const auto heldSlot = args.Settings.FindMember("heldSlot");
        if (heldSlot.IsValid())
        {
            if (!heldSlot.IsString())
            {
                return Fail(report, Error::TypeMismatch, Text("heldSlot"));
            }
            if (heldSlot.AsString().empty())
            {
                return Fail(report, Error::OutOfRange, Text("heldSlot"));
            }
            settings.HeldSlot = Identity(heldSlot.AsString());
            settings.bHasHeldSlot = true;
        }
        out = Container::MakeShared<FollowThroughModifier>(std::move(settings));
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
