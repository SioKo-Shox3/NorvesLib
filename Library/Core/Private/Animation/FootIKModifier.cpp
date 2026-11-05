// 足の接地補正設定を検証し、休止姿勢から脚の向きを準備する。
#include "Animation/FootIKModifier.h"
#include "Animation/Procedural/PoseModifierConfig.h"
#include "Animation/SkeletonResource.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Error = Procedural::PoseModifierConfigError;
        using Report = Procedural::PoseModifierConfigReport;

        Container::String Text(const char* text)
        {
            Container::String result;
            for (const char* p = text; *p; ++p)
            {
                result.push_back(TCHAR(*p));
            }
            return result;
        }

        Container::String MemberPath(const Container::String& base, const Container::String& member)
        {
            auto path = base;
            if (!member.empty())
            {
                if (!path.empty() && member[0] != TCHAR('['))
                {
                    path.push_back(TCHAR('.'));
                }
                path += member;
            }
            return path;
        }

        Container::String ElementPath(const Container::String& base, size_t index)
        {
            auto path = base;
            path.push_back(TCHAR('['));
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

        bool Fail(Report& report, Error error, const Container::String& path)
        {
            if (report.Error == Error::None)
            {
                report.Error = error;
                report.Path = path;
            }
            return false;
        }

        bool Keys(JsonValue object, Container::Span<const char* const> allowed,
                  const Container::String& path, Report& report)
        {
            if (!Procedural::CheckPoseModifierKeys(object, allowed, report))
            {
                report.Path = MemberPath(path, report.Path);
                return false;
            }
            return true;
        }

        bool ReadJoint(JsonValue value, const SkeletonResource& skeleton, int32_t& out,
                       const Container::String& path, Report& report)
        {
            if (!value.IsValid())
            {
                return Fail(report, Error::MissingKey, path);
            }
            if (!Procedural::ResolvePoseModifierJoint(value, skeleton, out, report))
            {
                report.Path = MemberPath(path, report.Path);
                return false;
            }
            return true;
        }

        bool ReadNumber(JsonValue object, const char* key, double minimum, double maximum, bool bExclusiveMinimum,
                        float& out, const Container::String& path, Report& report)
        {
            const auto value = object.FindMember(key);
            if (!value.IsValid())
            {
                return true;
            }
            const auto valuePath = MemberPath(path, Text(key));
            if (!value.IsNumber())
            {
                return Fail(report, Error::TypeMismatch, valuePath);
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || number < minimum || number > maximum ||
                (bExclusiveMinimum && (number <= minimum || float(number) <= float(minimum))))
            {
                return Fail(report, Error::OutOfRange, valuePath);
            }
            out = float(number);
            return true;
        }

        bool IsDescendant(Container::Span<const int32_t> parents, int32_t joint, int32_t ancestor)
        {
            size_t visited = 0;
            while (joint >= 0 && static_cast<size_t>(joint) < parents.size() && visited++ < parents.size())
            {
                joint = parents[static_cast<size_t>(joint)];
                if (joint == ancestor)
                {
                    return true;
                }
            }
            return false;
        }

        bool ReadPole(JsonValue value, FootIKLegSettings& leg, const Container::String& path, Report& report)
        {
            if (!value.IsValid())
            {
                return true;
            }
            if (value.IsString())
            {
                return value.AsString() == Text("auto") || Fail(report, Error::OutOfRange, path);
            }
            if (!value.IsArray())
            {
                return Fail(report, Error::TypeMismatch, path);
            }
            if (value.GetArraySize() != 3)
            {
                return Fail(report, Error::OutOfRange, path);
            }
            double components[3];
            double largest = 0.0;
            for (size_t i = 0; i < 3; ++i)
            {
                const auto component = value.GetArrayElement(i);
                const auto componentPath = ElementPath(path, i);
                if (!component.IsNumber())
                {
                    return Fail(report, Error::TypeMismatch, componentPath);
                }
                components[i] = component.AsNumber();
                if (!std::isfinite(components[i]))
                {
                    return Fail(report, Error::OutOfRange, componentPath);
                }
                largest = std::max(largest, std::abs(components[i]));
            }
            if (largest == 0.0)
            {
                return Fail(report, Error::OutOfRange, path);
            }
            const double x = components[0] / largest;
            const double y = components[1] / largest;
            const double z = components[2] / largest;
            const double scaledLength = std::sqrt(x * x + y * y + z * z);
            if (largest < 1e-6 && largest * scaledLength < 1e-6)
            {
                return Fail(report, Error::OutOfRange, path);
            }
            leg.bAutoPole = false;
            leg.Pole = {float(x / scaledLength), float(y / scaledLength), float(z / scaledLength)};
            return true;
        }

        // 行列は位置とスケール、四元数は純粋な回転の合成に使う。
        bool RestGlobal(const SkeletonResource& skeleton, int32_t joint, Math::Vector3& position,
                        Math::Quaternion& rotation)
        {
            const auto& runtime = skeleton.GetPoseRuntime();
            const auto& parents = runtime.Parents;
            const auto& rest = skeleton.GetAuthorRestPose();
            const bool bHasJoint = joint != -1;
            if (rest.size() != parents.size())
            {
                return false;
            }
            Math::Matrix4x4 matrix = Math::Matrix4x4::Identity;
            rotation = Math::Quaternion::Identity;
            size_t visited = 0;
            while (joint != -1)
            {
                if (joint < 0 || static_cast<size_t>(joint) >= parents.size() || visited++ >= parents.size())
                {
                    return false;
                }
                const size_t index = static_cast<size_t>(joint);
                const auto& source = rest[index];
                if (!Skeletal::IsValidSkeletalRestTransform(source))
                {
                    return false;
                }
                const auto& q = source.Rotation;
                const double length = std::sqrt(double(q.X) * q.X + double(q.Y) * q.Y +
                                                double(q.Z) * q.Z + double(q.W) * q.W);
                JointTransform local;
                local.Translation = {source.Translation.X, source.Translation.Y, source.Translation.Z};
                local.Rotation = {float(q.X / length), float(q.Y / length), float(q.Z / length), float(q.W / length)};
                local.Scale = {source.Scale.X, source.Scale.Y, source.Scale.Z};
                matrix = matrix * ToRowMatrix(local);
                rotation = local.Rotation * rotation;
                joint = parents[index];
            }
            // split 骨格の静的親を合成する。メッシュ変換は設定読み込み時には取得できない。
            if (bHasJoint && runtime.bSplit)
            {
                matrix = matrix * runtime.RootFrame;
                rotation = FromRowMatrix(runtime.RootFrame).Rotation * rotation;
            }
            for (float value : matrix.values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            const double length = std::sqrt(double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                                            double(rotation.z) * rotation.z + double(rotation.w) * rotation.w);
            if (!std::isfinite(length) || length == 0.0)
            {
                return false;
            }
            rotation = {float(rotation.x / length), float(rotation.y / length), float(rotation.z / length),
                        float(rotation.w / length)};
            position = matrix.GetTranslationRow();
            return true;
        }

        bool AutoPole(const SkeletonResource& skeleton, FootIKLegSettings& leg)
        {
            Math::Vector3 root, mid, ankle, parentPosition;
            Math::Quaternion unused, parentRotation;
            const auto& parents = skeleton.GetPoseRuntime().Parents;
            const int32_t parent = parents[static_cast<size_t>(leg.Joints[0])];
            if (!RestGlobal(skeleton, leg.Joints[0], root, unused) ||
                !RestGlobal(skeleton, leg.Joints[1], mid, unused) ||
                !RestGlobal(skeleton, leg.Joints[2], ankle, unused) ||
                !RestGlobal(skeleton, parent, parentPosition, parentRotation))
            {
                return false;
            }
            const double axis[3] = {double(ankle.x) - root.x, double(ankle.y) - root.y, double(ankle.z) - root.z};
            const double offset[3] = {double(mid.x) - root.x, double(mid.y) - root.y, double(mid.z) - root.z};
            const double axisSquared = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
            if (!(axisSquared > 0.0) || !std::isfinite(axisSquared))
            {
                return false;
            }
            const double projection = (offset[0] * axis[0] + offset[1] * axis[1] + offset[2] * axis[2]) / axisSquared;
            const double x = offset[0] - axis[0] * projection;
            const double y = offset[1] - axis[1] * projection;
            const double z = offset[2] - axis[2] * projection;
            const double length = std::sqrt(x * x + y * y + z * z);
            if (!std::isfinite(length) || length < 1e-5)
            {
                return false;
            }
            const Math::Vector3 direction(float(x / length), float(y / length), float(z / length));
            const Math::Quaternion inverse(-parentRotation.x, -parentRotation.y, -parentRotation.z, parentRotation.w);
            leg.PoleInParent = (inverse * direction).Normalized();
            return true;
        }

        bool ReadLeg(JsonValue value, const SkeletonResource& skeleton, FootIKSettings& settings,
                     const Container::String& path, Report& report)
        {
            const char* const keys[] = {"name", "chain", "pole", "sole", "maxStretch"};
            if (!Keys(value, keys, path, report))
            {
                return false;
            }
            const auto name = value.FindMember("name");
            const auto namePath = MemberPath(path, Text("name"));
            if (!name.IsValid())
            {
                return Fail(report, Error::MissingKey, namePath);
            }
            if (!name.IsString())
            {
                return Fail(report, Error::TypeMismatch, namePath);
            }
            FootIKLegSettings leg;
            leg.Name = Identity(name.AsString());
            for (const auto& previous : settings.Legs)
            {
                if (previous.Name == leg.Name)
                {
                    return Fail(report, Error::DuplicateKey, namePath);
                }
            }
            const auto chain = value.FindMember("chain");
            const auto chainPath = MemberPath(path, Text("chain"));
            if (!chain.IsValid())
            {
                return Fail(report, Error::MissingKey, chainPath);
            }
            if (!chain.IsArray())
            {
                return Fail(report, Error::TypeMismatch, chainPath);
            }
            if (chain.GetArraySize() < 3 || chain.GetArraySize() > 4)
            {
                return Fail(report, Error::OutOfRange, chainPath);
            }
            const auto& parents = skeleton.GetPoseRuntime().Parents;
            leg.JointCount = static_cast<uint32_t>(chain.GetArraySize());
            for (uint32_t i = 0; i < leg.JointCount; ++i)
            {
                const auto jointPath = ElementPath(chainPath, i);
                if (!ReadJoint(chain.GetArrayElement(i), skeleton, leg.Joints[i], jointPath, report))
                {
                    return false;
                }
                if (leg.Joints[i] < 0 || static_cast<size_t>(leg.Joints[i]) >= parents.size())
                {
                    return Fail(report, Error::OutOfRange, jointPath);
                }
                for (uint32_t j = 0; j < i; ++j)
                {
                    if (leg.Joints[j] == leg.Joints[i])
                    {
                        return Fail(report, Error::OutOfRange, jointPath);
                    }
                }
                if (i > 0 && !IsDescendant(parents, leg.Joints[i], leg.Joints[i - 1]))
                {
                    return Fail(report, Error::OutOfRange, jointPath);
                }
            }
            const auto polePath = MemberPath(path, Text("pole"));
            if (!ReadPole(value.FindMember("pole"), leg, polePath, report) ||
                !ReadNumber(value, "sole", 0, 0.5, false, leg.Sole, path, report) ||
                !ReadNumber(value, "maxStretch", 1, 1.5, false, leg.MaxStretch, path, report))
            {
                return false;
            }
            if (leg.bAutoPole && !AutoPole(skeleton, leg))
            {
                return Fail(report, Error::OutOfRange, polePath);
            }
            settings.Legs.push_back(leg);
            return true;
        }

        bool ReadBody(JsonValue value, const SkeletonResource& skeleton, FootIKBodySettings& body, Report& report)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const auto path = Text("body");
            const char* const keys[] = {"pelvis", "chest", "maxDrop", "maxRaise", "smoothTime"};
            if (!Keys(value, keys, path, report) ||
                !ReadJoint(value.FindMember("pelvis"), skeleton, body.Pelvis, MemberPath(path, Text("pelvis")), report) ||
                !ReadJoint(value.FindMember("chest"), skeleton, body.Chest, MemberPath(path, Text("chest")), report))
            {
                return false;
            }
            const auto& parents = skeleton.GetPoseRuntime().Parents;
            if (body.Pelvis < 0 || body.Chest < 0 || static_cast<size_t>(body.Pelvis) >= parents.size() ||
                static_cast<size_t>(body.Chest) >= parents.size() ||
                (body.Chest != body.Pelvis && !IsDescendant(parents, body.Chest, body.Pelvis)))
            {
                return Fail(report, Error::OutOfRange, MemberPath(path, Text("chest")));
            }
            if (!ReadNumber(value, "maxDrop", 0, 2, false, body.Balance.MaxDrop, path, report) ||
                !ReadNumber(value, "maxRaise", 0, 2, false, body.Balance.MaxRaise, path, report) ||
                !ReadNumber(value, "smoothTime", 0, 5, false, body.Balance.SmoothTime, path, report))
            {
                return false;
            }
            body.bEnabled = true;
            return true;
        }

        bool ReadGround(JsonValue value, FootIKGroundSettings& ground, Report& report)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const auto path = Text("ground");
            const char* const keys[] = {"rayUp", "rayDown", "maxSlopeDeg", "layerMask"};
            if (!Keys(value, keys, path, report) ||
                !ReadNumber(value, "rayUp", 0, 10, false, ground.RayUp, path, report) ||
                !ReadNumber(value, "rayDown", 0, 20, true, ground.RayDown, path, report) ||
                !ReadNumber(value, "maxSlopeDeg", 0, 89, false, ground.MaxSlopeDeg, path, report))
            {
                return false;
            }
            const auto mask = value.FindMember("layerMask");
            if (mask.IsValid())
            {
                const auto maskPath = MemberPath(path, Text("layerMask"));
                if (!mask.IsIntegerLiteral())
                {
                    return Fail(report, Error::TypeMismatch, maskPath);
                }
                const double number = mask.AsNumber();
                if (!std::isfinite(number) || number < 0 || number > double(UINT32_MAX))
                {
                    return Fail(report, Error::OutOfRange, maskPath);
                }
                ground.LayerMask = mask.AsUInt32();
            }
            return true;
        }
    } // namespace

    FootIKModifier::FootIKModifier(FootIKSettings settings) : m_Settings(std::move(settings))
    {
    }

    int32_t FootIKModifier::Order() const
    {
        return Procedural::PoseModifierDefaultOrder::FootIK;
    }

    bool FootIKModifier::Apply(LocalPose&, PoseModifierContext&)
    {
        return true;
    }

    void FootIKModifier::SetGroundQuery(const IGroundQuery* query)
    {
        m_GroundQuery = query;
    }

    const IGroundQuery* FootIKModifier::GetGroundQuery() const
    {
        return m_GroundQuery;
    }

    const FootIKSettings& FootIKModifier::GetSettings() const
    {
        return m_Settings;
    }

    void FootIKModifier::Reset()
    {
    }

    bool TryParseFootIKSettings(JsonValue settings, const SkeletonResource& skeleton, FootIKSettings& out,
                                Procedural::PoseModifierConfigReport& report)
    {
        if (report.Error != Error::None)
        {
            return false;
        }
        if (!settings.IsValid())
        {
            return Fail(report, Error::MissingKey, Text("legs"));
        }
        const char* const keys[] = {"legs", "body", "ground"};
        if (!Keys(settings, keys, {}, report))
        {
            return false;
        }
        const auto legs = settings.FindMember("legs");
        const auto legsPath = Text("legs");
        if (!legs.IsValid())
        {
            return Fail(report, Error::MissingKey, legsPath);
        }
        if (!legs.IsArray())
        {
            return Fail(report, Error::TypeMismatch, legsPath);
        }
        if (legs.GetArraySize() < 1 || legs.GetArraySize() > 8)
        {
            return Fail(report, Error::OutOfRange, legsPath);
        }
        FootIKSettings candidate;
        candidate.Legs.reserve(legs.GetArraySize());
        for (size_t i = 0; i < legs.GetArraySize(); ++i)
        {
            if (!ReadLeg(legs.GetArrayElement(i), skeleton, candidate, ElementPath(legsPath, i), report))
            {
                return false;
            }
        }
        if (!ReadBody(settings.FindMember("body"), skeleton, candidate.Body, report) ||
            !ReadGround(settings.FindMember("ground"), candidate.Ground, report))
        {
            return false;
        }
        if (candidate.Body.bEnabled && candidate.Body.Chest != candidate.Body.Pelvis)
        {
            const auto& parents = skeleton.GetPoseRuntime().Parents;
            for (auto& leg : candidate.Legs)
            {
                leg.bFront = IsDescendant(parents, leg.Joints[0], candidate.Body.Chest);
            }
        }
        out = std::move(candidate);
        return true;
    }

    bool CreateFootIKModifier(const Procedural::PoseModifierCreateArgs& args,
                              Container::TSharedPtr<IPoseModifier>& out, Procedural::PoseModifierConfigReport& report)
    {
        FootIKSettings settings;
        if (!TryParseFootIKSettings(args.Settings, args.Skeleton, settings, report))
        {
            return false;
        }
        auto modifier = Container::MakeShared<FootIKModifier>(std::move(settings));
        const auto& body = modifier->m_Settings.Body;
        if (body.bEnabled)
        {
            const auto& parents = args.Skeleton.GetPoseRuntime().Parents;
            for (int32_t joint = body.Chest; joint != body.Pelvis; joint = parents[static_cast<size_t>(joint)])
            {
                modifier->m_PitchJoints.push_back(joint);
            }
            std::reverse(modifier->m_PitchJoints.begin(), modifier->m_PitchJoints.end());
        }
        out = std::move(modifier);
        return true;
    }

    PhysicsGroundQuerySettings MakePhysicsGroundQuerySettings(const FootIKGroundSettings& ground)
    {
        PhysicsGroundQuerySettings result;
        result.LayerMask = ground.LayerMask;
        result.MaxSlopeDeg = ground.MaxSlopeDeg;
        return result;
    }
} // namespace NorvesLib::Core::Animation
