// 駆動信号またはパラメータから胴の傾きを求め、関節へ分配する。
#include "Animation/Procedural/BodyLeanModifier.h"

#include "Animation/SkeletonResource.h"
#include "Math/CriticalDamping.h"
#include "Math/QuaternionUtils.h"
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

        bool ReadParameterName(JsonValue settings, const char* key, Identity& out, PoseModifierConfigReport& report)
        {
            const auto value = settings.FindMember(key);
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsString())
            {
                return Fail(report, Error::TypeMismatch, Text(key));
            }
            if (value.AsString().empty())
            {
                return Fail(report, Error::OutOfRange, Text(key));
            }
            out = Identity(value.AsString());
            return true;
        }

        float ReadParameter(const AnimParamSet& params, Identity name)
        {
            const auto handle = params.Find(name);
            const auto* definition = params.Definition(handle);
            const auto* value = params.Get(handle);
            return definition && definition->Type == AnimParamType::Float && value ? value->Float : 0.0f;
        }

        // メッシュノードは行、所有者のワールド行列は列にスケールを持つ。
        // スケールを除き、行ベクトルから列規約の回転へ戻す。
        bool TryRotation(const Math::Matrix4x4& matrix, bool bColumnScale, Math::Quaternion& out)
        {
            Math::Matrix4x4 rotation;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                double values[3];
                for (size_t component = 0; component < 3; ++component)
                {
                    values[component] = bColumnScale ? matrix.m[component][axis] : matrix.m[axis][component];
                }
                const double length = std::sqrt(values[0] * values[0] + values[1] * values[1] +
                                                values[2] * values[2]);
                if (!std::isfinite(length) || length == 0.0)
                {
                    return false;
                }
                for (size_t component = 0; component < 3; ++component)
                {
                    if (bColumnScale)
                    {
                        rotation.m[component][axis] = float(values[component] / length);
                    }
                    else
                    {
                        rotation.m[axis][component] = float(values[component] / length);
                    }
                }
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
    } // namespace

    BodyLeanModifier::BodyLeanModifier(BodyLeanSettings settings) : m_Settings(std::move(settings))
    {
    }

    int32_t BodyLeanModifier::Order() const
    {
        return PoseModifierDefaultOrder::Lean;
    }

    bool BodyLeanModifier::Apply(LocalPose& pose, PoseModifierContext& context)
    {
        const float dt = context.DeltaSeconds;
        if (!std::isfinite(dt) || dt < 0.0f || !std::isfinite(context.Weight))
        {
            return false;
        }

        const auto& signals = context.GetDriveSignals();
        const float groundTarget = context.HasDriveSignals() && !signals.bGrounded ? 0.0f : 1.0f;
        float ground = m_bGroundInitialized ? m_GroundWeight : groundTarget;
        float groundVelocity = m_bGroundInitialized ? m_GroundVelocity : 0.0f;
        if (!Math::TryCriticalDamp(ground, groundVelocity, groundTarget, m_Settings.AirFadeHalfLife, dt))
        {
            return false;
        }

        float bank = m_Bank;
        float bankVelocity = m_BankVelocity;
        float pitch = m_Pitch;
        float pitchVelocity = m_PitchVelocity;
        if (m_Settings.bUseSignals)
        {
            double bankTarget = 0.0;
            double pitchTarget = 0.0;
            if (context.HasDriveSignals())
            {
                if (!std::isfinite(signals.Speed) || !std::isfinite(signals.TurnRate) ||
                    !std::isfinite(signals.Acceleration.x) || !std::isfinite(signals.Acceleration.y) ||
                    !std::isfinite(signals.Acceleration.z))
                {
                    return false;
                }
                bankTarget = std::atan(double(signals.Speed) * signals.TurnRate / m_Settings.Gravity);
                if (signals.Acceleration.x != 0.0f || signals.Acceleration.y != 0.0f ||
                    signals.Acceleration.z != 0.0f)
                {
                    Math::Quaternion worldRotation;
                    if (!TryRotation(context.MeshToWorld, true, worldRotation))
                    {
                        return false;
                    }
                    const auto forwardWorld = worldRotation * m_Settings.ForwardAxis;
                    pitchTarget = double(m_Settings.PitchPerAccel) *
                                  (double(signals.Acceleration.x) * forwardWorld.x +
                                   double(signals.Acceleration.y) * forwardWorld.y +
                                   double(signals.Acceleration.z) * forwardWorld.z);
                }
            }
            constexpr double radiansPerDegree = double(Math::Constants::PI) / 180.0;
            const double maxBank = double(m_Settings.MaxBankDeg) * radiansPerDegree;
            const double maxPitch = double(m_Settings.MaxPitchDeg) * radiansPerDegree;
            bankTarget = std::clamp(bankTarget, -maxBank, maxBank);
            pitchTarget = std::clamp(pitchTarget, -maxPitch, maxPitch);
            if (!Math::TryCriticalDamp(bank, bankVelocity, float(bankTarget), m_Settings.SmoothingHalfLife, dt) ||
                !Math::TryCriticalDamp(pitch, pitchVelocity, float(pitchTarget), m_Settings.SmoothingHalfLife, dt))
            {
                return false;
            }
        }
        else
        {
            bank = ReadParameter(context.Parameters, m_Settings.BankParam);
            pitch = ReadParameter(context.Parameters, m_Settings.PitchParam);
        }

        const float weight = std::clamp(ground, 0.0f, 1.0f) * m_Settings.Strength * context.Weight;
        const float weightedBank = bank * weight;
        const float weightedPitch = pitch * weight;
        if (!std::isfinite(weightedBank) || !std::isfinite(weightedPitch))
        {
            return false;
        }
        if (weightedBank != 0.0f || weightedPitch != 0.0f)
        {
            Math::Quaternion meshRotation;
            if (!TryRotation(context.MeshNodeTransform, false, meshRotation))
            {
                return false;
            }
            const auto forward = meshRotation * m_Settings.ForwardAxis;
            const auto right = meshRotation * m_Settings.RightAxis;
            const auto rotation = Math::Quaternion(forward, weightedBank) * Math::Quaternion(right, -weightedPitch);
            if (!TryDistributeModelRotation(pose, context.Skeleton, m_Settings.Chain, rotation))
            {
                return false;
            }
            context.Invalidate();
        }

        m_Bank = bank;
        m_BankVelocity = bankVelocity;
        m_Pitch = pitch;
        m_PitchVelocity = pitchVelocity;
        m_GroundWeight = ground;
        m_GroundVelocity = groundVelocity;
        m_bGroundInitialized = true;
        return true;
    }

    bool CreateBodyLeanModifier(const PoseModifierCreateArgs& args, Container::TSharedPtr<IPoseModifier>& out,
                                PoseModifierConfigReport& report)
    {
        const char* const allowed[] = {"source", "bankParam", "pitchParam", "chain", "forwardAxis", "rightAxis",
                                       "maxBankDeg", "maxPitchDeg", "pitchPerAccel", "gravity", "smoothingHalfLife",
                                       "airFadeHalfLife", "strength"};
        if (args.Settings.IsValid() && !CheckPoseModifierKeys(args.Settings, allowed, report))
        {
            return false;
        }

        BodyLeanSettings settings;
        const auto source = args.Settings.FindMember("source");
        if (source.IsValid())
        {
            if (!source.IsString())
            {
                return Fail(report, Error::TypeMismatch, Text("source"));
            }
            if (source.AsString() == Text("signals"))
            {
                settings.bUseSignals = true;
            }
            else if (source.AsString() != Text("params"))
            {
                return Fail(report, Error::OutOfRange, Text("source"));
            }
        }
        if (!ReadParameterName(args.Settings, "bankParam", settings.BankParam, report) ||
            !ReadParameterName(args.Settings, "pitchParam", settings.PitchParam, report))
        {
            return false;
        }

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

        if (!ReadAxis(args.Settings.FindMember("forwardAxis"), settings.ForwardAxis, Text("forwardAxis"), report) ||
            !ReadAxis(args.Settings.FindMember("rightAxis"), settings.RightAxis, Text("rightAxis"), report))
        {
            return false;
        }
        const double dot = double(settings.ForwardAxis.x) * settings.RightAxis.x +
                           double(settings.ForwardAxis.y) * settings.RightAxis.y +
                           double(settings.ForwardAxis.z) * settings.RightAxis.z;
        if (std::abs(dot) >= 0.9999)
        {
            return Fail(report, Error::OutOfRange, Text("rightAxis"));
        }

        constexpr float maximum = std::numeric_limits<float>::max();
        if (!ReadNumber(args.Settings.FindMember("maxBankDeg"), 0.0f, maximum, false, settings.MaxBankDeg,
                        Text("maxBankDeg"), report) ||
            !ReadNumber(args.Settings.FindMember("maxPitchDeg"), 0.0f, maximum, false, settings.MaxPitchDeg,
                        Text("maxPitchDeg"), report) ||
            !ReadNumber(args.Settings.FindMember("pitchPerAccel"), -maximum, maximum, false, settings.PitchPerAccel,
                        Text("pitchPerAccel"), report) ||
            !ReadNumber(args.Settings.FindMember("gravity"), 0.0f, maximum, true, settings.Gravity,
                        Text("gravity"), report) ||
            !ReadNumber(args.Settings.FindMember("smoothingHalfLife"), 0.0f, maximum, true, settings.SmoothingHalfLife,
                        Text("smoothingHalfLife"), report) ||
            !ReadNumber(args.Settings.FindMember("airFadeHalfLife"), 0.0f, maximum, true, settings.AirFadeHalfLife,
                        Text("airFadeHalfLife"), report) ||
            !ReadNumber(args.Settings.FindMember("strength"), 0.0f, 1.0f, false, settings.Strength,
                        Text("strength"), report))
        {
            return false;
        }

        out = Container::MakeShared<BodyLeanModifier>(std::move(settings));
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
