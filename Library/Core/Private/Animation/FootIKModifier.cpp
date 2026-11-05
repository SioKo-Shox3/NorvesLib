// 接地設定を検証し、地面に合わせて体と脚の姿勢を補正する。
#include "Animation/FootIKModifier.h"
#include "Animation/Procedural/PoseModifierConfig.h"
#include "Animation/SkeletonResource.h"
#include "Animation/TwoBoneIK.h"
#include "Logging/LogMacros.h"
#include "Math/CriticalDamping.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Error = Procedural::PoseModifierConfigError;
        using Report = Procedural::PoseModifierConfigReport;

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(const Math::Matrix4x4& matrix)
        {
            for (float value : matrix.values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            return true;
        }

        bool TryNormalize(Math::Quaternion& rotation)
        {
            const double length = std::sqrt(double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                                            double(rotation.z) * rotation.z + double(rotation.w) * rotation.w);
            if (!std::isfinite(length) || length <= Math::Constants::EPSILON)
            {
                return false;
            }
            rotation = {float(rotation.x / length), float(rotation.y / length), float(rotation.z / length),
                        float(rotation.w / length)};
            return true;
        }

        bool TryNormalize(Math::Vector3& direction)
        {
            const double length = std::sqrt(double(direction.x) * direction.x + double(direction.y) * direction.y +
                                            double(direction.z) * direction.z);
            if (!std::isfinite(length) || length <= Math::Constants::EPSILON)
            {
                return false;
            }
            direction = {float(direction.x / length), float(direction.y / length), float(direction.z / length)};
            return true;
        }

        bool TryInverse(const Math::Matrix4x4& matrix, Math::Matrix4x4& inverse)
        {
            const float determinant = Math::MatrixUtils::Determinant(matrix);
            if (!IsFinite(matrix) || !std::isfinite(determinant) ||
                std::abs(determinant) < Math::Constants::EPSILON)
            {
                return false;
            }
            inverse = Math::MatrixUtils::Inverse(matrix);
            return IsFinite(inverse);
        }

        bool TryRotation(const Math::Matrix4x4& matrix, Math::Quaternion& rotation)
        {
            const auto scale = Math::MatrixUtils::ExtractScale(matrix);
            if (!IsFinite(matrix) || !IsFinite(scale) || scale.x <= Math::Constants::EPSILON ||
                scale.y <= Math::Constants::EPSILON || scale.z <= Math::Constants::EPSILON)
            {
                return false;
            }
            const auto pureRotation = Math::MatrixUtils::ExtractRotationRowVector(matrix, scale);
            rotation = FromRowMatrix(pureRotation).Rotation;
            return TryNormalize(rotation);
        }

        bool IsValidTransform(const JointTransform& transform)
        {
            auto rotation = transform.Rotation;
            return IsFinite(transform.Translation) && IsFinite(transform.Scale) && TryNormalize(rotation) &&
                   IsFinite(ToRowMatrix(transform));
        }

        // 根でも同じ式を使い、RootFrame とメッシュノードの逆変換を親の座標系へ含める。
        bool TryParentFrame(const JointTransform& local, const Math::Matrix4x4& model, Math::Matrix4x4& frame)
        {
            Math::Matrix4x4 inverse;
            if (!IsValidTransform(local) || !TryInverse(ToRowMatrix(local), inverse))
            {
                return false;
            }
            frame = inverse * model;
            return IsFinite(frame);
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
        m_LegStates.resize(m_Settings.Legs.size());
        m_Probes.resize(m_Settings.Legs.size());
        m_BodySamples.resize(m_Settings.Legs.size());
    }

    int32_t FootIKModifier::Order() const
    {
        return Procedural::PoseModifierDefaultOrder::FootIK;
    }

    bool FootIKModifier::Apply(LocalPose& pose, PoseModifierContext& context)
    {
        const float w = context.Weight;
        const float dt = context.DeltaSeconds;
        const auto& ground = m_Settings.Ground;
        const auto& body = m_Settings.Body;
        if (!std::isfinite(w) || w < 0.0f || w > 1.0f || !std::isfinite(dt) || dt < 0.0f ||
            !std::isfinite(body.Balance.SmoothTime) || body.Balance.SmoothTime < 0.0f ||
            !std::isfinite(ground.RayUp) || ground.RayUp < 0.0f || !std::isfinite(ground.RayDown) ||
            ground.RayDown <= 0.0f || !std::isfinite(ground.RayUp + ground.RayDown) ||
            !std::isfinite(ground.MaxSlopeDeg) || ground.MaxSlopeDeg < 0.0f || ground.MaxSlopeDeg >= 90.0f ||
            pose.size() != context.Skeleton.Parents.size())
        {
            return false;
        }
        auto validJoint = [&](int32_t joint)
        {
            return joint >= 0 && static_cast<size_t>(joint) < pose.size();
        };
        for (const auto& leg : m_Settings.Legs)
        {
            if (leg.JointCount < 3 || leg.JointCount > 4 || !std::isfinite(leg.Sole) || leg.Sole < 0.0f ||
                !std::isfinite(leg.MaxStretch) || leg.MaxStretch < 1.0f)
            {
                return false;
            }
            for (uint32_t j = 0; j < leg.JointCount; ++j)
            {
                if (!validJoint(leg.Joints[j]))
                {
                    return false;
                }
            }
        }
        if (body.bEnabled && (!validJoint(body.Pelvis) || !validJoint(body.Chest)))
        {
            return false;
        }
        const auto* models = context.GetJointModelMatrices();
        Math::Matrix4x4 worldToMesh;
        if (!models || models->size() != pose.size() || !TryInverse(context.MeshToWorld, worldToMesh))
        {
            return false;
        }
        auto up = Math::MatrixUtils::TransformVectorRowVector(worldToMesh, Math::Vector3::UnitY);
        const float originY = context.MeshToWorld.GetTranslationRow().y;
        if (!TryNormalize(up) || !std::isfinite(originY))
        {
            return false;
        }

        // 全脚の問い合わせを一度に行う。配列は構築時に確保して使い回す。
        for (size_t i = 0; i < m_Settings.Legs.size(); ++i)
        {
            const auto& leg = m_Settings.Legs[i];
            auto& state = m_LegStates[i];
            state.bUsable = true;
            for (uint32_t j = 0; j < leg.JointCount; ++j)
            {
                const auto scale = Math::MatrixUtils::ExtractScale((*models)[leg.Joints[j]]);
                const float minimum = std::min({scale.x, scale.y, scale.z});
                const float maximum = std::max({scale.x, scale.y, scale.z});
                if (!IsFinite(scale) || minimum <= Math::Constants::EPSILON)
                {
                    state.bUsable = false;
                }
                else if (maximum / minimum - 1.0f > 1e-3f)
                {
                    state.bUsable = false;
                    if (!state.bWarnedScale)
                    {
                        NORVES_LOG_WARNING("Animation", "脚 %zu は非一様スケールのため接地 IK を省略します。", i);
                        state.bWarnedScale = true;
                    }
                }
            }
            state.FootWorld = Math::MatrixUtils::TransformPointRowVector(
                context.MeshToWorld, (*models)[leg.Joints[leg.JointCount - 1]].GetTranslationRow());
            auto& probe = m_Probes[i];
            probe.OriginAbove = state.FootWorld + Math::Vector3(0.0f, ground.RayUp, 0.0f);
            probe.MaxDown = ground.RayUp + ground.RayDown;
            probe.Result = {};
            probe.Result.Status = EGroundQueryStatus::NotReady;
        }
        if (m_GroundQuery)
        {
            m_GroundQuery->QueryBatch(m_Probes);
        }
        const double minimumNormalY = std::cos(double(ground.MaxSlopeDeg) * double(Math::Constants::PI) / 180.0);
        for (size_t i = 0; i < m_Settings.Legs.size(); ++i)
        {
            const auto& leg = m_Settings.Legs[i];
            auto& state = m_LegStates[i];
            auto hit = m_Probes[i].Result;
            if (!IsFinite(state.FootWorld) || !IsFinite(m_Probes[i].OriginAbove))
            {
                state.bUsable = false;
                hit.Status = EGroundQueryStatus::Unavailable;
            }
            if (hit.Status == EGroundQueryStatus::Hit)
            {
                if (!IsFinite(hit.Point) || !TryNormalize(hit.Normal))
                {
                    hit.Status = EGroundQueryStatus::Unavailable;
                }
                else if (double(hit.Normal.y) < minimumNormalY)
                {
                    hit.Status = EGroundQueryStatus::TooSteep;
                }
            }
            state.Status = hit.Status;
            bool bSuccess = state.bUsable && hit.Status == EGroundQueryStatus::Hit;
            if (bSuccess)
            {
                state.LastHit = hit;
                state.bHasHit = true;
            }
            state.ToeTargetWorld = state.FootWorld;
            if (state.bHasHit)
            {
                const float animLift = state.FootWorld.y - originY;
                state.ToeTargetWorld.y = state.LastHit.Point.y + std::max(animLift, leg.Sole);
                if (!IsFinite(state.ToeTargetWorld) ||
                    !std::isfinite(state.ToeTargetWorld.y - state.FootWorld.y))
                {
                    state.Status = EGroundQueryStatus::Unavailable;
                    state.bUsable = false;
                    bSuccess = false;
                }
            }
            const float targetWeight = bSuccess ? 1.0f : 0.0f;
            if (body.Balance.SmoothTime == 0.0f)
            {
                state.Weight = targetWeight;
                state.Velocity = 0.0f;
            }
            else if (!Math::TryCriticalDamp(state.Weight, state.Velocity, targetWeight, body.Balance.SmoothTime, dt))
            {
                return false;
            }
            state.Weight = std::clamp(state.Weight, 0.0f, 1.0f);
            m_BodySamples[i] = {bSuccess ? state.ToeTargetWorld.y - state.FootWorld.y : 0.0f,
                                leg.bFront, bSuccess};
        }

        // 体を先に動かす。足先の目標は補正前のワールド位置を基準に固定する。
        if (body.bEnabled)
        {
            const auto bodyDirection = (*models)[body.Chest].GetTranslationRow() -
                                       (*models)[body.Pelvis].GetTranslationRow();
            const auto horizontal = bodyDirection - up * Math::Vector3::Dot(bodyDirection, up);
            const float horizontalDistance = horizontal.Length();
            BodyBalanceOutput output;
            if (!m_BodyBalance.Update(m_BodySamples, body.Balance, horizontalDistance, dt, output))
            {
                return false;
            }
            if (w > 0.0f)
            {
                auto pelvis = pose[body.Pelvis];
                Math::Matrix4x4 frame, inverseFrame;
                if (!TryParentFrame(pelvis, (*models)[body.Pelvis], frame) || !TryInverse(frame, inverseFrame))
                {
                    return false;
                }
                pelvis.Translation += Math::MatrixUtils::TransformVectorRowVector(
                    inverseFrame, up * (output.PelvisOffset * w));
                if (!IsValidTransform(pelvis))
                {
                    return false;
                }
                const auto originalPelvis = pose[body.Pelvis];
                auto pitch = Math::Quaternion::Identity;
                if (!m_PitchJoints.empty() && horizontalDistance >= 1e-4f)
                {
                    Math::Quaternion meshRotation;
                    if (!TryRotation(context.MeshNodeTransform, meshRotation))
                    {
                        return false;
                    }
                    const auto axis = Math::Vector3::Cross(horizontal / horizontalDistance, up);
                    const auto modelPitch = Math::QuaternionUtils::FromAxisAngle(axis, output.PitchRadians * w);
                    pitch = meshRotation * modelPitch * Math::QuaternionUtils::Conjugate(meshRotation);
                    if (!TryNormalize(pitch))
                    {
                        return false;
                    }
                }
                m_PitchShares.resize(m_PitchJoints.size());
                for (size_t i = 0; i < m_PitchJoints.size(); ++i)
                {
                    m_PitchShares[i] = {m_PitchJoints[i], 1.0f / float(m_PitchJoints.size())};
                }
                pose[body.Pelvis] = pelvis;
                if (!m_PitchShares.empty() &&
                    !Procedural::TryDistributeModelRotation(pose, context.Skeleton, m_PitchShares, pitch))
                {
                    pose[body.Pelvis] = originalPelvis;
                    return false;
                }
                context.Invalidate();
                models = context.GetJointModelMatrices();
                if (!models)
                {
                    return false;
                }
            }
        }

        for (size_t i = 0; i < m_Settings.Legs.size(); ++i)
        {
            const auto& leg = m_Settings.Legs[i];
            auto& state = m_LegStates[i];
            const float lw = state.Weight * w;
            if (!state.bUsable || !state.bHasHit || lw <= 0.0f)
            {
                continue;
            }
            models = context.GetJointModelMatrices();
            if (!models)
            {
                return false;
            }
            const auto root = (*models)[leg.Joints[0]].GetTranslationRow();
            const auto mid = (*models)[leg.Joints[1]].GetTranslationRow();
            const auto ankle = (*models)[leg.Joints[2]].GetTranslationRow();
            const auto toe = (*models)[leg.Joints[leg.JointCount - 1]].GetTranslationRow();
            const auto toeTarget = Math::MatrixUtils::TransformPointRowVector(worldToMesh, state.ToeTargetWorld);
            auto normal = Math::MatrixUtils::TransformVectorRowVector(worldToMesh, state.LastHit.Normal);
            if (!IsFinite(toeTarget) || !TryNormalize(normal))
            {
                continue;
            }
            auto align = Math::QuaternionUtils::Slerp(Math::Quaternion::Identity,
                Math::QuaternionUtils::FromToRotation(up, normal), lw);
            if (!TryNormalize(align))
            {
                continue;
            }
            const auto ankleTarget = leg.JointCount == 4 ? toeTarget - align * (toe - ankle) : toeTarget;
            TwoBoneIKInput input;
            input.Root = root;
            input.Mid = mid;
            input.End = ankle;
            input.Target = ankle + (ankleTarget - ankle) * lw;
            input.Pole = leg.Pole;
            if (leg.bAutoPole)
            {
                const int32_t parent = context.Skeleton.Parents[leg.Joints[0]];
                Math::Quaternion parentRotation = Math::Quaternion::Identity;
                if (parent != -1 && (!validJoint(parent) || !TryRotation((*models)[parent], parentRotation)))
                {
                    continue;
                }
                input.Pole = parentRotation * leg.PoleInParent;
            }
            input.FallbackPole = state.bHasPole ? state.PoleUsed : input.Pole;
            input.MaxStretch = leg.MaxStretch;
            input.SoftLimitWidth = 0.0f;
            if (!IsFinite(input.Target) || !IsFinite(input.Pole) || !IsFinite(input.FallbackPole))
            {
                continue;
            }
            const auto result = SolveTwoBoneIK(input);
            if (!std::isfinite(result.Stretch) || result.Stretch < 1.0f || result.Stretch > leg.MaxStretch ||
                !IsFinite(result.PoleUsed))
            {
                continue;
            }

            // 全関節を一時値で解く。途中の親の更新を反映し、検証後に脚全体を書き込む。
            JointTransform candidates[3];
            Math::Matrix4x4 oldModels[3], inverseModels[3], frames[3];
            Math::Quaternion globals[3];
            bool bValid = true;
            for (size_t j = 0; j < 3; ++j)
            {
                const size_t joint = static_cast<size_t>(leg.Joints[j]);
                candidates[j] = pose[joint];
                oldModels[j] = (*models)[joint];
                if (!TryParentFrame(candidates[j], oldModels[j], frames[j]) ||
                    !TryRotation(oldModels[j], globals[j]) || !TryInverse(oldModels[j], inverseModels[j]))
                {
                    bValid = false;
                    break;
                }
            }
            if (!bValid)
            {
                continue;
            }
            globals[0] = result.RootDelta * globals[0];
            globals[1] = result.MidDelta * globals[1];
            globals[2] = align * globals[2];
            Math::Matrix4x4 updatedModel;
            for (size_t j = 0; j < 3; ++j)
            {
                // 間に省略された骨があっても、旧親から更新済み先祖への相対変換を保存する。
                const auto parentFrame = j == 0 ? frames[j] : frames[j] * inverseModels[j - 1] * updatedModel;
                Math::Quaternion parentRotation;
                if (!TryRotation(parentFrame, parentRotation) || !TryNormalize(globals[j]))
                {
                    bValid = false;
                    break;
                }
                candidates[j].Rotation = Math::QuaternionUtils::Conjugate(parentRotation) * globals[j];
                if (j > 0 && result.Stretch > 1.0f)
                {
                    candidates[j].Translation *= result.Stretch;
                }
                if (!TryNormalize(candidates[j].Rotation) || !IsValidTransform(candidates[j]))
                {
                    bValid = false;
                    break;
                }
                updatedModel = ToRowMatrix(candidates[j]) * parentFrame;
                if (!IsFinite(updatedModel))
                {
                    bValid = false;
                    break;
                }
            }
            if (!bValid)
            {
                continue;
            }
            for (size_t j = 0; j < 3; ++j)
            {
                pose[leg.Joints[j]] = candidates[j];
            }
            state.PoleUsed = result.PoleUsed;
            state.bHasPole = true;
            context.Invalidate();
        }
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
        for (auto& state : m_LegStates)
        {
            state = {};
        }
        for (auto& probe : m_Probes)
        {
            probe = {};
        }
        for (auto& sample : m_BodySamples)
        {
            sample = {};
        }
        m_BodyBalance.Reset();
    }

    float FootIKModifier::GetLegWeight(size_t leg) const
    {
        return leg < m_LegStates.size() ? m_LegStates[leg].Weight : 0.0f;
    }

    EGroundQueryStatus FootIKModifier::GetLegStatus(size_t leg) const
    {
        return leg < m_LegStates.size() ? m_LegStates[leg].Status : EGroundQueryStatus::Unavailable;
    }

    const BodyBalanceOutput& FootIKModifier::GetBodyOutput() const
    {
        return m_BodyBalance.GetOutput();
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
