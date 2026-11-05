// アニメーション姿勢から測った視線差を平滑化し、首と頭へ分配する。
#include "Animation/Procedural/LookAtModifier.h"

#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/HoldSlotComponent.h"
#include "Math/CriticalDamping.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Animation::Procedural
{
    namespace
    {
        using Error = PoseModifierConfigError;

        Container::String Text(const char* text)
        {
            Container::String result;
            for (const char* p = text; *p; ++p)
            {
                result.push_back(TCHAR(*p));
            }
            return result;
        }

        Container::String MemberPath(const Container::String& base, const char* member)
        {
            auto result = base;
            result.push_back(TCHAR('.'));
            result += Text(member);
            return result;
        }

        Container::String ElementPath(const Container::String& base, size_t index)
        {
            auto result = base;
            result.push_back(TCHAR('['));
            TCHAR digits[3 * sizeof(size_t)];
            size_t count = 0;
            do
            {
                digits[count++] = TCHAR('0' + index % 10);
                index /= 10;
            } while (index);
            while (count)
            {
                result.push_back(digits[--count]);
            }
            result.push_back(TCHAR(']'));
            return result;
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

        bool ReadNumber(JsonValue value, float minimum, float maximum, bool bExclusiveMinimum, float& out,
                        const Container::String& path, PoseModifierConfigReport& report)
        {
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsNumber())
            {
                return Fail(report, Error::TypeMismatch, path);
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || number > double(maximum) ||
                (bExclusiveMinimum ? number <= double(minimum) : number < double(minimum)))
            {
                return Fail(report, Error::OutOfRange, path);
            }
            const float converted = float(number);
            if (!std::isfinite(converted) || (bExclusiveMinimum && converted <= minimum))
            {
                return Fail(report, Error::OutOfRange, path);
            }
            out = converted;
            return true;
        }

        bool NormalizeAxis(Math::Vector3& value)
        {
            const double length = std::sqrt(double(value.x) * value.x + double(value.y) * value.y +
                                            double(value.z) * value.z);
            if (!std::isfinite(length) || length <= Math::Constants::EPSILON)
            {
                return false;
            }
            value = {float(value.x / length), float(value.y / length), float(value.z / length)};
            return true;
        }

        bool ReadAxis(JsonValue value, Math::Vector3& out, const Container::String& path,
                      PoseModifierConfigReport& report)
        {
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsArray())
            {
                return Fail(report, Error::TypeMismatch, path);
            }
            if (value.GetArraySize() != 3)
            {
                return Fail(report, Error::OutOfRange, path);
            }
            float values[3] = {};
            constexpr float maximum = std::numeric_limits<float>::max();
            for (size_t i = 0; i < 3; ++i)
            {
                if (!ReadNumber(value.GetArrayElement(i), -maximum, maximum, false, values[i],
                                ElementPath(path, i), report))
                {
                    return false;
                }
            }
            out = {values[0], values[1], values[2]};
            return NormalizeAxis(out) || Fail(report, Error::OutOfRange, path);
        }

        bool IsDescendant(const SkeletonResource& skeleton, int32_t joint, int32_t ancestor)
        {
            const auto& parents = skeleton.GetPoseRuntime().Parents;
            if (joint == ancestor)
            {
                return false;
            }
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

        constexpr float RadiansPerDegree = Math::Constants::PI / 180.0f;

        bool TryRotation(const Math::Matrix4x4& matrix, Math::Quaternion& out)
        {
            const auto scale = Math::MatrixUtils::ExtractScale(matrix);
            if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) ||
                scale.x <= Math::Constants::EPSILON || scale.y <= Math::Constants::EPSILON ||
                scale.z <= Math::Constants::EPSILON)
            {
                return false;
            }
            const auto rotation = Math::MatrixUtils::ExtractRotationRowVector(matrix, scale);
            out = Math::QuaternionUtils::Normalize(
                Math::QuaternionUtils::Conjugate(Math::QuaternionUtils::FromRotationMatrix(rotation)));
            return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z) && std::isfinite(out.w);
        }

        // 頭までの経路だけをスタック上で合成し、FK 用の配列確保を避ける。
        bool TryHeadFrame(const LocalPose& pose, const SkeletonPoseRuntime& skeleton, int32_t head,
                          const Math::Matrix4x4& inverseMesh, Math::Vector3& position,
                          Math::Quaternion& rotation)
        {
            auto matrix = Math::Matrix4x4::Identity;
            int32_t joint = head;
            for (size_t visited = 0; joint != -1; ++visited)
            {
                if (visited >= skeleton.Parents.size() || joint < 0 ||
                    static_cast<size_t>(joint) >= pose.size() ||
                    static_cast<size_t>(joint) >= skeleton.Parents.size())
                {
                    return false;
                }
                matrix = matrix * ToRowMatrix(pose[static_cast<size_t>(joint)]);
                joint = skeleton.Parents[static_cast<size_t>(joint)];
            }
            if (skeleton.bSplit)
            {
                matrix = matrix * skeleton.RootFrame;
            }
            matrix = matrix * inverseMesh;
            position = matrix.GetTranslationRow();
            return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
                   TryRotation(matrix, rotation);
        }

        void Measure(const Math::Vector3& direction, const LookAtSettings& settings, float& yaw, float& pitch)
        {
            const auto right = Math::Vector3::Cross(settings.UpAxis, settings.ForwardAxis);
            const float x = Math::Vector3::Dot(direction, right);
            const float y = Math::Vector3::Dot(direction, settings.UpAxis);
            const float z = Math::Vector3::Dot(direction, settings.ForwardAxis);
            yaw = std::atan2(x, z);
            pitch = std::atan2(y, std::hypot(x, z));
        }

        Math::Vector3 Direction(float yaw, float pitch, const LookAtSettings& settings)
        {
            const auto right = Math::Vector3::Cross(settings.UpAxis, settings.ForwardAxis);
            return (settings.ForwardAxis * std::cos(yaw) + right * std::sin(yaw)) * std::cos(pitch) +
                   settings.UpAxis * std::sin(pitch);
        }

        bool Damp(float& position, float& velocity, float target, const LookAtSettings& settings, float dt)
        {
            const float previousPosition = position;
            const float previousVelocity = velocity;
            if (!Math::TryCriticalDamp(position, velocity, target, settings.HalfLife, dt))
            {
                return false;
            }
            const double limit = double(settings.MaxAngularAccelDeg) * RadiansPerDegree * dt;
            const double change = double(velocity) - previousVelocity;
            if (std::abs(change) > limit)
            {
                velocity = float(double(previousVelocity) + std::clamp(change, -limit, limit));
                position = float(double(previousPosition) + double(velocity) * dt);
            }
            return std::isfinite(position) && std::isfinite(velocity);
        }
        bool RotateToward(LocalPose& pose, PoseModifierContext& context, const LookAtSettings& settings,
                          const Math::Quaternion& meshRotation, const Math::Vector3& forward,
                          const Math::Vector3& desired)
        {
            auto axis = Math::Vector3::Cross(forward, desired);
            const double sine = std::sqrt(double(axis.x) * axis.x + double(axis.y) * axis.y + double(axis.z) * axis.z);
            const double cosine = std::clamp(double(Math::Vector3::Dot(forward, desired)), -1.0, 1.0);
            const double angle = std::atan2(sine, cosine);
            if (angle > 1e-7)
            {
                if (!NormalizeAxis(axis))
                {
                    axis = Math::Vector3::Cross(forward, settings.UpAxis);
                    if (!NormalizeAxis(axis))
                    {
                        axis = Math::Vector3::Cross(forward, settings.ForwardAxis);
                        if (!NormalizeAxis(axis))
                        {
                            return false;
                        }
                    }
                }
                const auto rotation = Math::Quaternion(meshRotation * axis, float(angle));
                if (!TryDistributeModelRotation(pose, context.Skeleton, settings.Chain, rotation))
                {
                    return false;
                }
                context.Invalidate();
            }
            return true;
        }
    } // 無名名前空間

    LookAtModifier::LookAtModifier(LookAtSettings settings) : m_Settings(std::move(settings))
    {
    }

    int32_t LookAtModifier::Order() const
    {
        return PoseModifierDefaultOrder::Look;
    }

    bool LookAtModifier::Apply(LocalPose& pose, PoseModifierContext& context)
    {
        const float dt = context.DeltaSeconds;
        if (!std::isfinite(dt) || dt < 0.0f || !std::isfinite(context.Weight))
        {
            return false;
        }
        Math::Quaternion meshRotation;
        Math::Quaternion headRotation;
        Math::Vector3 headPosition;
        const auto inverseMesh = Math::MatrixUtils::Inverse(context.MeshNodeTransform);
        const float meshDeterminant = Math::MatrixUtils::Determinant(context.MeshNodeTransform);
        if (!std::isfinite(meshDeterminant) || std::abs(meshDeterminant) < Math::Constants::EPSILON ||
            !TryRotation(context.MeshNodeTransform, meshRotation) ||
            !TryHeadFrame(pose, context.Skeleton, m_Settings.Head,
                          inverseMesh, headPosition, headRotation))
        {
            return false;
        }
        auto forward = headRotation * m_Settings.LookAxis;
        if (!NormalizeAxis(forward))
        {
            return false;
        }
        float forwardYaw, forwardPitch;
        Measure(forward, m_Settings, forwardYaw, forwardPitch);
        float targetYaw = 0.0f;
        float targetPitch = 0.0f;
        bool bTargetInCone = false;
        Math::Vector3 targetPosition;
        Math::Vector3 originalTargetDirection;
        float weight = m_Settings.Strength * context.Weight;
        if (const auto* owner = context.GetOwner())
        {
            if (const auto* animator = owner->GetComponent<Component::AnimatorComponent>())
            {
                Math::Vector3 worldTarget;
                if (animator->TryResolveLookTarget(worldTarget))
                {
                    const float determinant = Math::MatrixUtils::Determinant(context.MeshToWorld);
                    if (!std::isfinite(determinant) || std::abs(determinant) < Math::Constants::EPSILON)
                    {
                        return false;
                    }
                    targetPosition = Math::MatrixUtils::TransformPointRowVector(
                        Math::MatrixUtils::Inverse(context.MeshToWorld), worldTarget);
                    auto direction = targetPosition - headPosition;
                    if (NormalizeAxis(direction))
                    {
                        originalTargetDirection = direction;
                        Measure(direction, m_Settings, targetYaw, targetPitch);
                        targetYaw = std::remainder(targetYaw - forwardYaw, 2.0f * Math::Constants::PI);
                        targetPitch -= forwardPitch;
                        const double radius = std::hypot(double(targetYaw) / (m_Settings.MaxYawDeg * RadiansPerDegree),
                                                         double(targetPitch) / (m_Settings.MaxPitchDeg * RadiansPerDegree));
                        bTargetInCone = radius <= 1.0;
                        if (radius > 1.0)
                        {
                            targetYaw = float(targetYaw / radius);
                            targetPitch = float(targetPitch / radius);
                        }
                    }
                }
            }
            if (m_Settings.HoldSlot.IsValid())
            {
                if (const auto* slots = owner->GetComponent<Component::HoldSlotComponent>())
                {
                    if (slots->GetOccupancy(m_Settings.HoldSlot) > 0)
                    {
                        weight *= m_Settings.HoldWeightScale;
                    }
                }
            }
        }
        float yaw = m_Yaw, pitch = m_Pitch;
        float yawVelocity = m_YawVelocity, pitchVelocity = m_PitchVelocity;
        if (!Damp(yaw, yawVelocity, targetYaw, m_Settings, dt) ||
            !Damp(pitch, pitchVelocity, targetPitch, m_Settings, dt))
        {
            return false;
        }
        const auto desired = Direction(forwardYaw + yaw * weight, forwardPitch + pitch * weight, m_Settings);
        if (weight != 0.0f)
        {
            if (!RotateToward(pose, context, m_Settings, meshRotation, forward, desired))
            {
                return false;
            }
            if (bTargetInCone)
            {
                Math::Vector3 movedPosition;
                Math::Quaternion movedRotation;
                if (!TryHeadFrame(pose, context.Skeleton, m_Settings.Head, inverseMesh, movedPosition, movedRotation))
                {
                    return false;
                }
                auto movedDirection = targetPosition - movedPosition;
                if (NormalizeAxis(movedDirection))
                {
                    // 平滑化の遅れには触れず、頭の位置移動による視差だけを一度補正する。
                    const float parallaxCosine = Math::Vector3::Dot(originalTargetDirection, movedDirection);
                    if (parallaxCosine < std::cos(RadiansPerDegree))
                    {
                        float movedYaw, movedPitch;
                        Measure(movedDirection, m_Settings, movedYaw, movedPitch);
                        float correctedYaw = yaw + std::remainder(movedYaw - forwardYaw - targetYaw,
                                                                  2.0f * Math::Constants::PI);
                        float correctedPitch = pitch + movedPitch - forwardPitch - targetPitch;
                        const double radius = std::hypot(double(correctedYaw) / (m_Settings.MaxYawDeg * RadiansPerDegree),
                                                         double(correctedPitch) / (m_Settings.MaxPitchDeg * RadiansPerDegree));
                        if (radius > 1.0)
                        {
                            correctedYaw = float(correctedYaw / radius);
                            correctedPitch = float(correctedPitch / radius);
                        }
                        const auto corrected = Direction(forwardYaw + correctedYaw * weight,
                                                          forwardPitch + correctedPitch * weight, m_Settings);
                        const auto movedForward = movedRotation * m_Settings.LookAxis;
                        if (!RotateToward(pose, context, m_Settings, meshRotation, movedForward, corrected))
                        {
                            return false;
                        }
                    }
                }
            }
        }
        m_Yaw = yaw;
        m_Pitch = pitch;
        m_YawVelocity = yawVelocity;
        m_PitchVelocity = pitchVelocity;
        return true;
    }

    bool CreateLookAtModifier(const PoseModifierCreateArgs& args, Container::TSharedPtr<IPoseModifier>& out,
                              PoseModifierConfigReport& report)
    {
        const char* const allowed[] = {"chain", "head", "forwardAxis", "upAxis", "lookAxis", "maxYawDeg",
                                       "maxPitchDeg", "halfLife", "maxAngularAccelDeg", "holdSlot",
                                       "holdWeightScale", "strength"};
        if (args.Settings.IsValid() && !CheckPoseModifierKeys(args.Settings, allowed, report))
        {
            return false;
        }
        LookAtSettings settings;
        const auto chain = args.Settings.FindMember("chain");
        if (!chain.IsValid())
        {
            return Fail(report, Error::MissingKey, Text("chain"));
        }
        if (!chain.IsArray())
        {
            return Fail(report, Error::TypeMismatch, Text("chain"));
        }
        if (chain.GetArraySize() == 0)
        {
            return Fail(report, Error::OutOfRange, Text("chain"));
        }
        settings.Chain.reserve(chain.GetArraySize());
        double totalWeight = 0.0;
        const char* const chainKeys[] = {"joint", "weight"};
        for (size_t i = 0; i < chain.GetArraySize(); ++i)
        {
            const auto item = chain.GetArrayElement(i);
            const auto path = ElementPath(Text("chain"), i);
            PoseModifierConfigReport itemReport;
            if (!CheckPoseModifierKeys(item, chainKeys, itemReport))
            {
                return Fail(report, itemReport.Error,
                            itemReport.Path.empty() ? path : MemberPath(path, itemReport.Path.c_str()));
            }
            const auto joint = item.FindMember("joint");
            const auto weight = item.FindMember("weight");
            if (!joint.IsValid())
            {
                return Fail(report, Error::MissingKey, MemberPath(path, "joint"));
            }
            if (!weight.IsValid())
            {
                return Fail(report, Error::MissingKey, MemberPath(path, "weight"));
            }
            JointRotationShare share;
            PoseModifierConfigReport jointReport;
            if (!ResolvePoseModifierJoint(joint, args.Skeleton, share.Joint, jointReport))
            {
                return Fail(report, jointReport.Error, MemberPath(path, "joint"));
            }
            if (!settings.Chain.empty() &&
                !IsDescendant(args.Skeleton, share.Joint, settings.Chain.back().Joint))
            {
                return Fail(report, Error::OutOfRange, MemberPath(path, "joint"));
            }
            if (!ReadNumber(weight, 0.0f, std::numeric_limits<float>::max(), false, share.Weight,
                            MemberPath(path, "weight"), report))
            {
                return false;
            }
            totalWeight += double(share.Weight);
            settings.Chain.push_back(share);
        }
        const auto& parents = args.Skeleton.GetPoseRuntime().Parents;
        int32_t ancestor = settings.Chain.back().Joint;
        size_t visited = 0;
        while (ancestor != -1)
        {
            if (ancestor < 0 || static_cast<size_t>(ancestor) >= parents.size() || visited++ >= parents.size())
            {
                return Fail(report, Error::OutOfRange, Text("chain"));
            }
            ancestor = parents[static_cast<size_t>(ancestor)];
        }
        if (std::abs(totalWeight - 1.0) > 1e-4)
        {
            return Fail(report, Error::OutOfRange, Text("chain"));
        }


        settings.Head = settings.Chain.back().Joint;
        const auto head = args.Settings.FindMember("head");
        if (head.IsValid() && !ResolvePoseModifierJoint(head, args.Skeleton, settings.Head, report))
        {
            report.Path = Text("head");
            return false;
        }
        if (settings.Head != settings.Chain.back().Joint &&
            !IsDescendant(args.Skeleton, settings.Head, settings.Chain.back().Joint))
        {
            return Fail(report, Error::OutOfRange, Text("head"));
        }
        if (!ReadAxis(args.Settings.FindMember("forwardAxis"), settings.ForwardAxis, Text("forwardAxis"), report) ||
            !ReadAxis(args.Settings.FindMember("upAxis"), settings.UpAxis, Text("upAxis"), report))
        {
            return false;
        }
        const float dot = Math::Vector3::Dot(settings.ForwardAxis, settings.UpAxis);
        if (std::abs(dot) >= 0.9999f)
        {
            return Fail(report, Error::OutOfRange, Text("upAxis"));
        }
        // 前を保った直交基底にして、角度の測定と方向の再構成を互いの逆にする。
        settings.UpAxis = settings.UpAxis - settings.ForwardAxis * dot;
        if (!NormalizeAxis(settings.UpAxis))
        {
            return Fail(report, Error::OutOfRange, Text("upAxis"));
        }
        const auto lookAxis = args.Settings.FindMember("lookAxis");
        if (lookAxis.IsValid())
        {
            if (!ReadAxis(lookAxis, settings.LookAxis, Text("lookAxis"), report))
            {
                return false;
            }
        }
        else
        {
            const auto& runtime = args.Skeleton.GetPoseRuntime();
            Math::Quaternion bindRotation;
            if (runtime.bSplit)
            {
                if (!TryComputeJointGlobalRotation(runtime.AuthorRestLocal, runtime, settings.Head, bindRotation))
                {
                    return Fail(report, Error::OutOfRange, Text("head"));
                }
            }
            else if (static_cast<size_t>(settings.Head) >= runtime.LegacyBindModel.size() ||
                     !TryRotation(runtime.LegacyBindModel[static_cast<size_t>(settings.Head)], bindRotation))
            {
                return Fail(report, Error::OutOfRange, Text("head"));
            }
            settings.LookAxis = Math::QuaternionUtils::Conjugate(bindRotation) * settings.ForwardAxis;
            if (!NormalizeAxis(settings.LookAxis))
            {
                return Fail(report, Error::OutOfRange, Text("lookAxis"));
            }
        }
        constexpr float maximum = std::numeric_limits<float>::max();
        if (!ReadNumber(args.Settings.FindMember("maxYawDeg"), 0.0f, 180.0f, true, settings.MaxYawDeg,
                        Text("maxYawDeg"), report) ||
            !ReadNumber(args.Settings.FindMember("maxPitchDeg"), 0.0f, 90.0f, true, settings.MaxPitchDeg,
                        Text("maxPitchDeg"), report) ||
            !ReadNumber(args.Settings.FindMember("halfLife"), 0.0f, maximum, true, settings.HalfLife,
                        Text("halfLife"), report) ||
            !ReadNumber(args.Settings.FindMember("maxAngularAccelDeg"), 0.0f, maximum, true,
                        settings.MaxAngularAccelDeg, Text("maxAngularAccelDeg"), report) ||
            !ReadNumber(args.Settings.FindMember("holdWeightScale"), 0.0f, 1.0f, false, settings.HoldWeightScale,
                        Text("holdWeightScale"), report) ||
            !ReadNumber(args.Settings.FindMember("strength"), 0.0f, 1.0f, false, settings.Strength,
                        Text("strength"), report))
        {
            return false;
        }
        const auto holdSlot = args.Settings.FindMember("holdSlot");
        if (holdSlot.IsValid())
        {
            if (!holdSlot.IsString())
            {
                return Fail(report, Error::TypeMismatch, Text("holdSlot"));
            }
            if (holdSlot.AsString().empty())
            {
                return Fail(report, Error::OutOfRange, Text("holdSlot"));
            }
            settings.HoldSlot = Identity(holdSlot.AsString());
        }
        out = Container::MakeShared<LookAtModifier>(std::move(settings));
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
