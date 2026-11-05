#include "Animation/Procedural/SpringBoneModifier.h"

#include "Animation/Procedural/AngleDistribution.h"
#include "Animation/SkeletonResource.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
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
            if (!result.empty())
            {
                result.push_back(TCHAR('.'));
            }
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

        bool CheckKeys(JsonValue object, Container::Span<const char* const> allowed,
                       const Container::String& path, PoseModifierConfigReport& report)
        {
            if (CheckPoseModifierKeys(object, allowed, report))
            {
                return true;
            }
            if (!path.empty())
            {
                auto fullPath = path;
                if (!report.Path.empty())
                {
                    fullPath.push_back(TCHAR('.'));
                    fullPath += report.Path;
                }
                report.Path = std::move(fullPath);
            }
            return false;
        }

        bool ReadNumber(JsonValue value, float minimum, float maximum, bool exclusiveMinimum, float& out,
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
                (exclusiveMinimum ? number <= double(minimum) : number < double(minimum)))
            {
                return Fail(report, Error::OutOfRange, path);
            }
            const float converted = float(number);
            if (!std::isfinite(converted) || (exclusiveMinimum && converted <= minimum))
            {
                return Fail(report, Error::OutOfRange, path);
            }
            out = converted;
            return true;
        }

        bool ReadGravity(JsonValue value, Math::Vector3& out, const Container::String& path,
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
            float components[3] = {};
            for (size_t i = 0; i < 3; ++i)
            {
                if (!ReadNumber(value.GetArrayElement(i), -100.0f, 100.0f, false, components[i],
                                ElementPath(path, i), report))
                {
                    return false;
                }
            }
            out = Math::Vector3(components[0], components[1], components[2]);
            return true;
        }

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(const Math::Quaternion& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
                   std::isfinite(value.w);
        }

        double Length(const Math::Vector3& value)
        {
            return std::sqrt(double(value.x) * value.x + double(value.y) * value.y + double(value.z) * value.z);
        }

        Math::Vector3 UnitDirection(const Math::Vector3& value, double length)
        {
            return Math::Vector3(float(double(value.x) / length), float(double(value.y) / length),
                                 float(double(value.z) / length));
        }
    } // namespace

    SpringBoneModifier::SpringBoneModifier(SpringBoneSettings settings) : m_Settings(std::move(settings))
    {
        m_Chains.resize(m_Settings.Chains.size());
        for (size_t i = 0; i < m_Chains.size(); ++i)
        {
            m_Chains[i].Particles.resize(m_Settings.Chains[i].Joints.size());
        }
    }

    int32_t SpringBoneModifier::Order() const
    {
        return PoseModifierDefaultOrder::SpringBone;
    }

    bool SpringBoneModifier::Apply(LocalPose& pose, PoseModifierContext& context)
    {
        // 早期 return でも評価の時点を記録し、飛びがあれば次の有効な評価までリセットを保持する。
        const auto* owner = context.GetOwner();
        const auto* world = owner ? owner->GetWorld() : nullptr;
        if (world)
        {
            const uint64_t tickSerial = world->GetTickSerial();
            if (m_bInitialized && m_bPreviousTickSerialValid && tickSerial != m_PreviousTickSerial &&
                tickSerial != m_PreviousTickSerial + 1)
            {
                m_bResetRequested = true;
            }
            m_PreviousTickSerial = tickSerial;
            m_bPreviousTickSerialValid = true;
        }
        else
        {
            m_bPreviousTickSerialValid = false;
        }

        if (!std::isfinite(context.DeltaSeconds) || context.DeltaSeconds < 0.0f ||
            pose.size() != context.Skeleton.Parents.size())
        {
            return false;
        }
        for (const auto& chain : m_Settings.Chains)
        {
            if (chain.Joints.size() < 2)
            {
                return false;
            }
            for (int32_t joint : chain.Joints)
            {
                if (joint < 0 || static_cast<size_t>(joint) >= context.Skeleton.Parents.size())
                {
                    return false;
                }
            }
        }

        const auto* model = context.GetJointModelMatrices();
        if (!model || model->size() != pose.size())
        {
            return false;
        }

        m_LastDroppedSeconds = 0.0f;
        auto resetNextTime = [&]() {
            m_bResetRequested = true;
            return true;
        };
        const auto ownerPosition = context.MeshToWorld.GetTranslationRow();
        if (!IsFinite(ownerPosition) || !IsFinite(m_WindAcceleration) || !std::isfinite(context.Weight) ||
            !std::isfinite(m_Settings.TeleportDistance) || m_Settings.TeleportDistance <= 0.0f)
        {
            return resetNextTime();
        }

        bool reset = !m_bInitialized || m_bResetRequested;
        if (m_bInitialized)
        {
            const double dx = double(ownerPosition.x) - m_PreviousOwnerPosition.x;
            const double dy = double(ownerPosition.y) - m_PreviousOwnerPosition.y;
            const double dz = double(ownerPosition.z) - m_PreviousOwnerPosition.z;
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            reset = reset || !IsFinite(m_PreviousOwnerPosition) || !std::isfinite(distance) ||
                    distance > double(m_Settings.TeleportDistance);
        }
        for (size_t c = 0; c < m_Chains.size(); ++c)
        {
            const auto& settings = m_Settings.Chains[c];
            if (!std::isfinite(settings.Stiffness) || !std::isfinite(settings.Damping) ||
                !std::isfinite(settings.Drag) || !std::isfinite(settings.RotationLimitDeg) ||
                !IsFinite(settings.Gravity))
            {
                return resetNextTime();
            }
            for (size_t i = 0; i < settings.Joints.size(); ++i)
            {
                auto& particle = m_Chains[c].Particles[i];
                const size_t joint = static_cast<size_t>(settings.Joints[i]);
                particle.AnimationWorld = ((*model)[joint] * context.MeshToWorld).GetTranslationRow();
                if (!IsFinite(particle.AnimationWorld))
                {
                    return resetNextTime();
                }
                reset = reset || !IsFinite(particle.Position) || !IsFinite(particle.Velocity);
            }
        }
        if (reset)
        {
            for (auto& chain : m_Chains)
            {
                for (auto& particle : chain.Particles)
                {
                    particle.Position = particle.AnimationWorld;
                    particle.Velocity = Math::Vector3::Zero;
                }
            }
            m_Accumulator.Reset();
            m_bInitialized = true;
            m_bResetRequested = false;
        }
        m_PreviousOwnerPosition = ownerPosition;

        int32_t substeps = 0;
        double dropped = 0.0;
        if (!m_Accumulator.Advance(context.DeltaSeconds, substeps, dropped))
        {
            return false;
        }
        m_LastDroppedSeconds = float(dropped);
        if (!std::isfinite(m_LastDroppedSeconds))
        {
            return resetNextTime();
        }

        constexpr float h = float(SpringSubstepSeconds);
        for (size_t c = 0; c < m_Chains.size(); ++c)
        {
            auto& particles = m_Chains[c].Particles;
            const auto& settings = m_Settings.Chains[c];
            particles[0].Position = particles[0].AnimationWorld;
            particles[0].Velocity = Math::Vector3::Zero;
            for (int32_t step = 0; step < substeps; ++step)
            {
                for (size_t i = 1; i < particles.size(); ++i)
                {
                    auto& particle = particles[i];
                    particle.PreviousPosition = particle.Position;
                    const auto acceleration = settings.Stiffness * (particle.AnimationWorld - particle.Position) -
                                              (settings.Damping + settings.Drag) * particle.Velocity +
                                              settings.Gravity + m_WindAcceleration;
                    if (!IsFinite(acceleration))
                    {
                        return resetNextTime();
                    }
                    particle.Velocity += acceleration * h;
                    particle.Position += particle.Velocity * h;
                    if (!IsFinite(particle.Position) || !IsFinite(particle.Velocity))
                    {
                        return resetNextTime();
                    }
                }
                for (size_t i = 1; i < particles.size(); ++i)
                {
                    auto& particle = particles[i];
                    const auto animatedDirection = particle.AnimationWorld - particles[i - 1].AnimationWorld;
                    auto direction = particle.Position - particles[i - 1].Position;
                    if (!IsFinite(animatedDirection) || !IsFinite(direction))
                    {
                        return resetNextTime();
                    }
                    const double boneLength = Length(animatedDirection);
                    double directionLength = Length(direction);
                    if (!std::isfinite(boneLength) || !std::isfinite(directionLength) ||
                        boneLength > std::numeric_limits<float>::max())
                    {
                        return resetNextTime();
                    }
                    if (directionLength <= Math::Constants::EPSILON)
                    {
                        direction = animatedDirection;
                        directionLength = boneLength;
                    }
                    particle.Position = directionLength > 0.0
                                            ? particles[i - 1].Position +
                                                  UnitDirection(direction, directionLength) * float(boneLength)
                                            : particles[i - 1].Position;
                    particle.Velocity = (particle.Position - particle.PreviousPosition) / h;
                    if (!IsFinite(particle.Position) || !IsFinite(particle.Velocity))
                    {
                        return resetNextTime();
                    }
                }
            }
        }

        const float weight = std::clamp(context.Weight, 0.0f, 1.0f);
        if (weight == 0.0f)
        {
            return true;
        }
        // 行ごとにスケールを除いてから逆行列を求め、小さな単位の変換でも行列式の下限を越えられるようにする。
        const auto axisX = Math::MatrixUtils::TransformVectorRowVector(context.MeshToWorld, Math::Vector3::UnitX);
        const auto axisY = Math::MatrixUtils::TransformVectorRowVector(context.MeshToWorld, Math::Vector3::UnitY);
        const auto axisZ = Math::MatrixUtils::TransformVectorRowVector(context.MeshToWorld, Math::Vector3::UnitZ);
        if (!IsFinite(axisX) || !IsFinite(axisY) || !IsFinite(axisZ))
        {
            return resetNextTime();
        }
        const double lengths[3] = {Length(axisX), Length(axisY), Length(axisZ)};
        float inverseLengths[3];
        for (size_t i = 0; i < 3; ++i)
        {
            if (!std::isfinite(lengths[i]) || lengths[i] == 0.0 ||
                1.0 / lengths[i] > std::numeric_limits<float>::max())
            {
                return resetNextTime();
            }
            inverseLengths[i] = float(1.0 / lengths[i]);
        }
        const auto normalization =
            Math::MatrixUtils::CreateScale(inverseLengths[0], inverseLengths[1], inverseLengths[2]);
        const auto normalizedMeshToWorld = normalization * context.MeshToWorld;
        const float determinant = Math::MatrixUtils::Determinant(normalizedMeshToWorld);
        if (!std::isfinite(determinant) || std::abs(determinant) < Math::Constants::EPSILON)
        {
            return resetNextTime();
        }
        const auto worldToMesh = Math::MatrixUtils::Inverse(normalizedMeshToWorld) * normalization;
        for (size_t c = 0; c < m_Chains.size(); ++c)
        {
            const auto& settings = m_Settings.Chains[c];
            auto& particles = m_Chains[c].Particles;
            for (size_t i = 0; i < particles.size(); ++i)
            {
                const size_t joint = static_cast<size_t>(settings.Joints[i]);
                auto& particle = particles[i];
                // モデル空間から、メッシュノードの逆変換を掛ける前の空間へ戻す。
                particle.AnimationAngleSpace = Math::MatrixUtils::TransformPointRowVector(
                    context.MeshNodeTransform, (*model)[joint].GetTranslationRow());
                const auto simulationMesh =
                    Math::MatrixUtils::TransformPointRowVector(worldToMesh, particle.Position);
                particle.SimulationAngleSpace =
                    Math::MatrixUtils::TransformPointRowVector(context.MeshNodeTransform, simulationMesh);
                if (!IsFinite(particle.AnimationAngleSpace) || !IsFinite(simulationMesh) ||
                    !IsFinite(particle.SimulationAngleSpace))
                {
                    return resetNextTime();
                }
                particle.OriginalLocal = pose[joint].Rotation;
                if (i + 1 < particles.size() &&
                    !TryComputeJointGlobalRotation(pose, context.Skeleton, settings.Joints[i],
                                                   particle.AnimationGlobal))
                {
                    return resetNextTime();
                }
            }
        }

        // 全関節の元の回転を先に控え、途中で失敗したときも姿勢を元に戻す。
        bool changed = false;
        auto restorePose = [&]() {
            if (changed)
            {
                for (size_t c = 0; c < m_Chains.size(); ++c)
                {
                    const auto& settings = m_Settings.Chains[c];
                    const auto& particles = m_Chains[c].Particles;
                    for (size_t i = 0; i + 1 < particles.size(); ++i)
                    {
                        pose[static_cast<size_t>(settings.Joints[i])].Rotation = particles[i].OriginalLocal;
                    }
                }
                context.Invalidate();
            }
            m_bResetRequested = true;
        };
        for (size_t c = 0; c < m_Chains.size(); ++c)
        {
            const auto& settings = m_Settings.Chains[c];
            const auto& particles = m_Chains[c].Particles;
            for (size_t i = 1; i < particles.size(); ++i)
            {
                const int32_t parent = settings.Joints[i - 1];
                model = context.GetJointModelMatrices();
                if (!model || model->size() != pose.size())
                {
                    restorePose();
                    return false;
                }
                Math::Quaternion currentGlobal;
                if (!TryComputeJointGlobalRotation(pose, context.Skeleton, parent, currentGlobal))
                {
                    restorePose();
                    return true;
                }
                const auto parentPosition = Math::MatrixUtils::TransformPointRowVector(
                    context.MeshNodeTransform, (*model)[static_cast<size_t>(parent)].GetTranslationRow());
                const auto animationDirection =
                    particles[i].AnimationAngleSpace - particles[i - 1].AnimationAngleSpace;
                // AngleDistribution と同じ列規約で、現在の回転差をアニメの向きへ左から掛ける。
                const auto currentDirection =
                    (currentGlobal * Math::QuaternionUtils::Conjugate(particles[i - 1].AnimationGlobal)) *
                    animationDirection;
                const auto targetDirection = particles[i].SimulationAngleSpace - parentPosition;
                if (!IsFinite(parentPosition) || !IsFinite(animationDirection) || !IsFinite(currentDirection) ||
                    !IsFinite(targetDirection))
                {
                    restorePose();
                    return true;
                }
                const double currentLength = Length(currentDirection);
                const double targetLength = Length(targetDirection);
                if (!std::isfinite(currentLength) || !std::isfinite(targetLength))
                {
                    restorePose();
                    return true;
                }
                if (currentLength <= Math::Constants::EPSILON || targetLength <= Math::Constants::EPSILON)
                {
                    continue;
                }
                auto rotation = Math::QuaternionUtils::FromToRotation(
                    UnitDirection(currentDirection, currentLength), UnitDirection(targetDirection, targetLength));
                if (!IsFinite(rotation))
                {
                    restorePose();
                    return true;
                }
                if (rotation.w < 0.0f)
                {
                    rotation = {-rotation.x, -rotation.y, -rotation.z, -rotation.w};
                }
                const double axisLength = std::sqrt(double(rotation.x) * rotation.x +
                                                    double(rotation.y) * rotation.y +
                                                    double(rotation.z) * rotation.z);
                const double angle = 2.0 * std::atan2(axisLength, double(rotation.w));
                const double limit = double(settings.RotationLimitDeg) * double(Math::Constants::PI) / 180.0;
                if (!std::isfinite(angle) || !std::isfinite(limit) || limit < 0.0)
                {
                    restorePose();
                    return true;
                }
                if (angle > limit)
                {
                    rotation = Math::QuaternionUtils::Slerp(Math::Quaternion::Identity, rotation, float(limit / angle));
                }
                const auto weighted = Math::QuaternionUtils::Slerp(Math::Quaternion::Identity, rotation, weight);
                const auto newGlobal = weighted * currentGlobal;
                if (!IsFinite(weighted) || !IsFinite(newGlobal) ||
                    !TrySetJointGlobalRotation(pose, context.Skeleton, parent, newGlobal))
                {
                    restorePose();
                    return true;
                }
                changed = true;
                context.Invalidate();
            }
        }
        return true;
    }

    void SpringBoneModifier::RequestReset()
    {
        m_bResetRequested = true;
    }

    void SpringBoneModifier::SetWindAcceleration(const Math::Vector3& acceleration)
    {
        m_WindAcceleration = acceleration;
    }

    float SpringBoneModifier::GetLastDroppedSeconds() const
    {
        return m_LastDroppedSeconds;
    }

    const SpringBoneSettings& SpringBoneModifier::GetSettings() const
    {
        return m_Settings;
    }

    bool SpringBoneModifier::TryGetParticleWorldPosition(size_t chain, size_t joint, Math::Vector3& out) const
    {
        if (!m_bInitialized || chain >= m_Chains.size() || joint >= m_Chains[chain].Particles.size() ||
            !IsFinite(m_Chains[chain].Particles[joint].Position))
        {
            return false;
        }
        out = m_Chains[chain].Particles[joint].Position;
        return true;
    }

    bool CreateSpringBoneModifier(const PoseModifierCreateArgs& args, Container::TSharedPtr<IPoseModifier>& out,
                                  PoseModifierConfigReport& report)
    {
        if (!args.Settings.IsValid())
        {
            return Fail(report, Error::MissingKey, Text("chains"));
        }
        const char* const settingsKeys[] = {"chains", "teleportDistance"};
        if (!CheckKeys(args.Settings, settingsKeys, {}, report))
        {
            return false;
        }
        const auto chains = args.Settings.FindMember("chains");
        const auto chainsPath = Text("chains");
        if (!chains.IsValid())
        {
            return Fail(report, Error::MissingKey, chainsPath);
        }
        if (!chains.IsArray())
        {
            return Fail(report, Error::TypeMismatch, chainsPath);
        }
        if (chains.GetArraySize() == 0)
        {
            return Fail(report, Error::OutOfRange, chainsPath);
        }
        SpringBoneSettings settings;
        if (!ReadNumber(args.Settings.FindMember("teleportDistance"), 0.0f, 1000.0f, true,
                        settings.TeleportDistance, Text("teleportDistance"), report))
        {
            return false;
        }
        const char* const chainKeys[] = {"name", "joints", "stiffness", "damping", "gravity", "drag", "radius",
                                         "rotationLimitDeg"};
        const auto& parents = args.Skeleton.GetPoseRuntime().Parents;
        for (size_t c = 0; c < chains.GetArraySize(); ++c)
        {
            const auto value = chains.GetArrayElement(c);
            const auto chainPath = ElementPath(chainsPath, c);
            if (!CheckKeys(value, chainKeys, chainPath, report))
            {
                return false;
            }
            const auto name = value.FindMember("name");
            const auto namePath = MemberPath(chainPath, "name");
            if (!name.IsValid())
            {
                return Fail(report, Error::MissingKey, namePath);
            }
            if (!name.IsString())
            {
                return Fail(report, Error::TypeMismatch, namePath);
            }
            SpringBoneChainSettings chain;
            chain.Name = Identity(name.AsString());
            for (const auto& previous : settings.Chains)
            {
                if (chain.Name == previous.Name)
                {
                    return Fail(report, Error::DuplicateKey, namePath);
                }
            }
            const auto joints = value.FindMember("joints");
            const auto jointsPath = MemberPath(chainPath, "joints");
            if (!joints.IsValid())
            {
                return Fail(report, Error::MissingKey, jointsPath);
            }
            if (!joints.IsArray())
            {
                return Fail(report, Error::TypeMismatch, jointsPath);
            }
            if (joints.GetArraySize() < 2)
            {
                return Fail(report, Error::OutOfRange, jointsPath);
            }
            for (size_t i = 0; i < joints.GetArraySize(); ++i)
            {
                const auto jointPath = ElementPath(jointsPath, i);
                int32_t joint = -1;
                if (!ResolvePoseModifierJoint(joints.GetArrayElement(i), args.Skeleton, joint, report))
                {
                    report.Path = jointPath;
                    return false;
                }
                if (joint < 0 || static_cast<size_t>(joint) >= parents.size() ||
                    (i > 0 && parents[static_cast<size_t>(joint)] != chain.Joints.back()))
                {
                    return Fail(report, Error::OutOfRange, jointPath);
                }
                for (const auto& previous : settings.Chains)
                {
                    for (int32_t previousJoint : previous.Joints)
                    {
                        if (joint == previousJoint)
                        {
                            return Fail(report, Error::OutOfRange, jointPath);
                        }
                    }
                }
                chain.Joints.push_back(joint);
            }
            if (!ReadNumber(value.FindMember("stiffness"), 0.0f, 10000.0f, true, chain.Stiffness,
                            MemberPath(chainPath, "stiffness"), report) ||
                !ReadNumber(value.FindMember("damping"), 0.0f, 100.0f, false, chain.Damping,
                            MemberPath(chainPath, "damping"), report) ||
                !ReadGravity(value.FindMember("gravity"), chain.Gravity, MemberPath(chainPath, "gravity"), report) ||
                !ReadNumber(value.FindMember("drag"), 0.0f, 100.0f, false, chain.Drag,
                            MemberPath(chainPath, "drag"), report) ||
                !ReadNumber(value.FindMember("radius"), 0.0f, 10.0f, false, chain.Radius,
                            MemberPath(chainPath, "radius"), report) ||
                !ReadNumber(value.FindMember("rotationLimitDeg"), 0.0f, 180.0f, true, chain.RotationLimitDeg,
                            MemberPath(chainPath, "rotationLimitDeg"), report))
            {
                return false;
            }
            settings.Chains.push_back(std::move(chain));
        }
        out = Container::MakeShared<SpringBoneModifier>(std::move(settings));
        return true;
    }
} // namespace NorvesLib::Core::Animation::Procedural
