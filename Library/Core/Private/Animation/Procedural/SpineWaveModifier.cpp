// 同期位相または内部位相を使い、背骨の各関節へ波の回転を加える。
#include "Animation/Procedural/SpineWaveModifier.h"

#include "Animation/Procedural/AngleDistribution.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
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
            const double length = std::sqrt(double(values[0]) * values[0] + double(values[1]) * values[1] +
                                            double(values[2]) * values[2]);
            if (!std::isfinite(length) || length <= Math::Constants::EPSILON)
            {
                return Fail(report, Error::OutOfRange, path);
            }
            out = {float(values[0] / length), float(values[1] / length), float(values[2] / length)};
            return true;
        }

        // メッシュノードの行スケールを除き、列規約の回転に戻す。
        bool TryRotation(const Math::Matrix4x4& matrix, Math::Quaternion& out)
        {
            Math::Matrix4x4 rotation;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double x = matrix.m[axis][0];
                const double y = matrix.m[axis][1];
                const double z = matrix.m[axis][2];
                const double length = std::sqrt(x * x + y * y + z * z);
                if (!std::isfinite(length) || length == 0.0)
                {
                    return false;
                }
                rotation.m[axis][0] = float(x / length);
                rotation.m[axis][1] = float(y / length);
                rotation.m[axis][2] = float(z / length);
            }
            const auto q = Math::QuaternionUtils::Conjugate(Math::QuaternionUtils::FromRotationMatrix(rotation));
            const double length = std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z +
                                            double(q.w) * q.w);
            if (!std::isfinite(length) || length <= Math::Constants::EPSILON)
            {
                return false;
            }
            out = {float(q.x / length), float(q.y / length), float(q.z / length), float(q.w / length)};
            return true;
        }

        bool ReadAmplitude(JsonValue value, SpineWaveJoint& joint, const Container::String& path,
                           PoseModifierConfigReport& report)
        {
            if (!value.IsValid())
            {
                return true;
            }
            constexpr float maximum = std::numeric_limits<float>::max();
            if (value.IsNumber())
            {
                return ReadNumber(value, -maximum, maximum, false, joint.Amplitude, path, report);
            }
            if (!value.IsArray())
            {
                return Fail(report, Error::TypeMismatch, path);
            }
            if (value.GetArraySize() == 0)
            {
                return Fail(report, Error::OutOfRange, path);
            }
            const char* const allowed[] = {"speed", "value"};
            joint.AmplitudeCurve.reserve(value.GetArraySize());
            for (size_t i = 0; i < value.GetArraySize(); ++i)
            {
                const auto item = value.GetArrayElement(i);
                const auto itemPath = ElementPath(path, i);
                PoseModifierConfigReport itemReport;
                if (!CheckPoseModifierKeys(item, allowed, itemReport))
                {
                    return Fail(report, itemReport.Error,
                                itemReport.Path.empty() ? itemPath : MemberPath(itemPath, itemReport.Path.c_str()));
                }
                const auto speed = item.FindMember("speed");
                const auto amplitude = item.FindMember("value");
                if (!speed.IsValid())
                {
                    return Fail(report, Error::MissingKey, MemberPath(itemPath, "speed"));
                }
                if (!amplitude.IsValid())
                {
                    return Fail(report, Error::MissingKey, MemberPath(itemPath, "value"));
                }
                if (!speed.IsNumber())
                {
                    return Fail(report, Error::TypeMismatch, MemberPath(itemPath, "speed"));
                }
                Math::CurveKeyframe key;
                key.Time = speed.AsNumber();
                if (!std::isfinite(key.Time) || key.Time < 0.0 ||
                    (!joint.AmplitudeCurve.empty() && key.Time <= joint.AmplitudeCurve.back().Time))
                {
                    return Fail(report, Error::OutOfRange, MemberPath(itemPath, "speed"));
                }
                if (!ReadNumber(amplitude, -maximum, maximum, false, key.Value, MemberPath(itemPath, "value"), report))
                {
                    return false;
                }
                joint.AmplitudeCurve.push_back(key);
            }
            return true;
        }

        bool TryAngle(const SpineWaveJoint& joint, double phase, float frequency, double speed, double weight,
                      float& out)
        {
            double amplitude = joint.Amplitude;
            if (!joint.AmplitudeCurve.empty() &&
                !Math::TryEvaluatePiecewiseLinear<Math::CurveKeyframe>(joint.AmplitudeCurve, speed, amplitude))
            {
                return false;
            }
            constexpr double twoPi = 6.283185307179586476925286766559;
            const double cycles = std::remainder(double(frequency) * phase +
                                                 std::remainder(double(joint.PhaseOffset), 1.0), 1.0);
            // 整数・半周期の理論値 0 を保ち、姿勢の不要な書き換えを避ける。
            if (cycles == 0.0 || std::abs(cycles) == 0.5)
            {
                out = 0.0f;
                return true;
            }
            const double angle = amplitude * std::sin(twoPi * cycles) * weight;
            if (!std::isfinite(angle) || std::abs(angle) > double(std::numeric_limits<float>::max()))
            {
                return false;
            }
            out = float(angle);
            return true;
        }
    } // namespace

    SpineWaveModifier::SpineWaveModifier(SpineWaveSettings settings) : m_Settings(std::move(settings))
    {
    }

    int32_t SpineWaveModifier::Order() const
    {
        return PoseModifierDefaultOrder::Spine;
    }

    bool SpineWaveModifier::Apply(LocalPose& pose, PoseModifierContext& context)
    {
        const float dt = context.DeltaSeconds;
        if (!std::isfinite(dt) || dt < 0.0f || !std::isfinite(context.Weight))
        {
            return false;
        }
        double phase = m_Phase;
        bool bSynchronized = false;
        if (m_Settings.SyncGroup.IsValid())
        {
            if (const auto* owner = context.GetOwner())
            {
                if (const auto* animator = owner->GetComponent<Component::AnimatorComponent>())
                {
                    const uint32_t index = animator->FindSyncGroup(m_Settings.SyncGroup);
                    if (index < context.SyncPhases.size() && context.SyncPhases.data())
                    {
                        phase = context.SyncPhases[index];
                        bSynchronized = true;
                    }
                }
            }
        }
        if (!bSynchronized)
        {
            const double advance = std::fmod(double(m_Settings.FallbackHz) * double(dt), 1.0);
            phase = std::fmod(m_Phase + advance, 1.0);
        }
        if (!std::isfinite(phase))
        {
            return false;
        }
        const double weight = double(m_Settings.Strength) * context.Weight;
        if (weight == 0.0)
        {
            if (!bSynchronized)
            {
                m_Phase = phase;
            }
            return true;
        }

        const double speed = context.HasDriveSignals() ? double(context.GetDriveSignals().Speed) : 0.0;
        bool bChangesPose = false;
        for (const auto& joint : m_Settings.Joints)
        {
            float angle;
            if (!TryAngle(joint, phase, m_Settings.Frequency, speed, weight, angle))
            {
                return false;
            }
            if (angle != 0.0f)
            {
                Math::Quaternion global;
                if (!TryComputeJointGlobalRotation(pose, context.Skeleton, joint.Joint, global))
                {
                    return false;
                }
                bChangesPose = true;
            }
        }
        if (bChangesPose)
        {
            Math::Quaternion meshRotation;
            if (!TryRotation(context.MeshNodeTransform, meshRotation))
            {
                return false;
            }
            const auto axis = meshRotation * (m_Settings.bPitch ? m_Settings.RightAxis : m_Settings.UpAxis);
            for (const auto& joint : m_Settings.Joints)
            {
                float angle;
                if (!TryAngle(joint, phase, m_Settings.Frequency, speed, weight, angle))
                {
                    return false;
                }
                if (angle == 0.0f)
                {
                    continue;
                }
                Math::Quaternion global;
                if (!TryComputeJointGlobalRotation(pose, context.Skeleton, joint.Joint, global) ||
                    !TrySetJointGlobalRotation(pose, context.Skeleton, joint.Joint,
                                               Math::Quaternion(axis, angle) * global))
                {
                    context.Invalidate();
                    return false;
                }
            }
            context.Invalidate();
        }
        if (!bSynchronized)
        {
            m_Phase = phase;
        }
        return true;
    }

    bool CreateSpineWaveModifier(const PoseModifierCreateArgs& args, Container::TSharedPtr<IPoseModifier>& out,
                                 PoseModifierConfigReport& report)
    {
        const char* const allowed[] = {"joints", "axis", "upAxis", "rightAxis", "frequency", "syncGroup",
                                       "fallbackHz", "strength"};
        if (args.Settings.IsValid() && !CheckPoseModifierKeys(args.Settings, allowed, report))
        {
            return false;
        }
        SpineWaveSettings settings;
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
        const char* const jointKeys[] = {"joint", "phaseOffset", "amplitude"};
        settings.Joints.reserve(joints.GetArraySize());
        constexpr float maximum = std::numeric_limits<float>::max();
        for (size_t i = 0; i < joints.GetArraySize(); ++i)
        {
            const auto item = joints.GetArrayElement(i);
            const auto path = ElementPath(Text("joints"), i);
            PoseModifierConfigReport itemReport;
            if (!CheckPoseModifierKeys(item, jointKeys, itemReport))
            {
                return Fail(report, itemReport.Error,
                            itemReport.Path.empty() ? path : MemberPath(path, itemReport.Path.c_str()));
            }
            const auto name = item.FindMember("joint");
            if (!name.IsValid())
            {
                return Fail(report, Error::MissingKey, MemberPath(path, "joint"));
            }
            SpineWaveJoint joint;
            PoseModifierConfigReport jointReport;
            if (!ResolvePoseModifierJoint(name, args.Skeleton, joint.Joint, jointReport))
            {
                return Fail(report, jointReport.Error, MemberPath(path, "joint"));
            }
            for (const auto& previous : settings.Joints)
            {
                if (previous.Joint == joint.Joint)
                {
                    return Fail(report, Error::OutOfRange, MemberPath(path, "joint"));
                }
            }
            if (!ReadNumber(item.FindMember("phaseOffset"), -maximum, maximum, false, joint.PhaseOffset,
                            MemberPath(path, "phaseOffset"), report) ||
                !ReadAmplitude(item.FindMember("amplitude"), joint, MemberPath(path, "amplitude"), report))
            {
                return false;
            }
            settings.Joints.push_back(std::move(joint));
        }
        const auto axis = args.Settings.FindMember("axis");
        if (axis.IsValid())
        {
            if (!axis.IsString())
            {
                return Fail(report, Error::TypeMismatch, Text("axis"));
            }
            if (axis.AsString() == Text("pitch"))
            {
                settings.bPitch = true;
            }
            else if (axis.AsString() != Text("yaw"))
            {
                return Fail(report, Error::OutOfRange, Text("axis"));
            }
        }
        const auto syncGroup = args.Settings.FindMember("syncGroup");
        if (syncGroup.IsValid())
        {
            if (!syncGroup.IsString())
            {
                return Fail(report, Error::TypeMismatch, Text("syncGroup"));
            }
            if (syncGroup.AsString().empty())
            {
                return Fail(report, Error::OutOfRange, Text("syncGroup"));
            }
            settings.SyncGroup = Identity(syncGroup.AsString());
        }
        if (!ReadAxis(args.Settings.FindMember("upAxis"), settings.UpAxis, Text("upAxis"), report) ||
            !ReadAxis(args.Settings.FindMember("rightAxis"), settings.RightAxis, Text("rightAxis"), report) ||
            !ReadNumber(args.Settings.FindMember("frequency"), 0.0f, 16.0f, true, settings.Frequency,
                        Text("frequency"), report) ||
            !ReadNumber(args.Settings.FindMember("fallbackHz"), 0.0f, 20.0f, false, settings.FallbackHz,
                        Text("fallbackHz"), report) ||
            !ReadNumber(args.Settings.FindMember("strength"), 0.0f, 1.0f, false, settings.Strength,
                        Text("strength"), report))
        {
            return false;
        }
        out = Container::MakeShared<SpineWaveModifier>(std::move(settings));
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
