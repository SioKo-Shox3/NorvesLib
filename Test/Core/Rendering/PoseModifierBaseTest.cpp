// 手続き層と足 IK が共有する姿勢の後処理の土台を検証する。
#include "Animation/Procedural/AngleDistribution.h"
#include "Animation/Procedural/Spring.h"
#include "Animation/Procedural/PoseModifierConfig.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    namespace Animation = NorvesLib::Core::Animation;
    namespace Procedural = NorvesLib::Core::Animation::Procedural;
    namespace Container = NorvesLib::Core::Container;
    namespace Math = NorvesLib::Math;

    constexpr double Pi = 3.14159265358979323846;
    constexpr int JointCount = 5;

    struct DVector
    {
        double x, y, z;

        DVector operator+(DVector rhs) const
        {
            return {x + rhs.x, y + rhs.y, z + rhs.z};
        }

        DVector operator-(DVector rhs) const
        {
            return {x - rhs.x, y - rhs.y, z - rhs.z};
        }

        DVector operator*(double scale) const
        {
            return {x * scale, y * scale, z * scale};
        }
    };

    struct DQuaternion
    {
        double x, y, z, w;
    };

    double Dot(DVector a, DVector b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    DVector Cross(DVector a, DVector b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    double Length(DVector v)
    {
        return std::sqrt(Dot(v, v));
    }

    DQuaternion Normalize(DQuaternion q)
    {
        const double length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        return {q.x / length, q.y / length, q.z / length, q.w / length};
    }

    DQuaternion Multiply(DQuaternion a, DQuaternion b)
    {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    DQuaternion Conjugate(DQuaternion q)
    {
        return {-q.x, -q.y, -q.z, q.w};
    }

    DQuaternion AxisAngle(DVector axis, double angle)
    {
        const double scale = std::sin(angle * 0.5) / Length(axis);
        return {axis.x * scale, axis.y * scale, axis.z * scale, std::cos(angle * 0.5)};
    }

    DVector Rotate(DQuaternion q, DVector v)
    {
        q = Normalize(q);
        const DVector u{q.x, q.y, q.z};
        return v * (q.w * q.w - Dot(u, u)) + u * (2.0 * Dot(u, v)) + Cross(u, v) * (2.0 * q.w);
    }

    DVector ToDouble(const Math::Vector3& v)
    {
        return {v.x, v.y, v.z};
    }

    DQuaternion ToDouble(const Math::Quaternion& q)
    {
        return Normalize({q.x, q.y, q.z, q.w});
    }

    Math::Vector3 ToFloat(DVector v)
    {
        return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    }

    Math::Quaternion ToFloat(DQuaternion q)
    {
        return {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
    }

    double QuaternionError(DQuaternion a, DQuaternion b)
    {
        a = Normalize(a);
        b = Normalize(b);
        const double minus = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                             (a.z - b.z) * (a.z - b.z) + (a.w - b.w) * (a.w - b.w);
        const double plus = (a.x + b.x) * (a.x + b.x) + (a.y + b.y) * (a.y + b.y) +
                            (a.z + b.z) * (a.z + b.z) + (a.w + b.w) * (a.w + b.w);
        return std::sqrt(minus < plus ? minus : plus);
    }

    double RotationAngle(DQuaternion a, DQuaternion b)
    {
        const DQuaternion difference = Multiply(Normalize(a), Conjugate(Normalize(b)));
        return 2.0 * std::atan2(Length({difference.x, difference.y, difference.z}), std::fabs(difference.w));
    }

    void RecordMax(double& maximum, double value)
    {
        if (value > maximum)
        {
            maximum = value;
        }
    }

    void Check(bool bOk, const char* contract, int sample, double actual, double limit)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "PoseModifierBaseTest 検査失敗: %s sample=%d actual=%.17g limit=%.17g\n",
                         contract, sample, actual, limit);
            std::abort();
        }
    }

    void CheckWithin(double value, double limit, const char* contract, int sample)
    {
        Check(std::isfinite(value) && value <= limit, contract, sample, value, limit);
    }

    bool SameBits(float a, float b)
    {
        return std::memcmp(&a, &b, sizeof(a)) == 0;
    }

    bool SameBits(double a, double b)
    {
        return std::memcmp(&a, &b, sizeof(a)) == 0;
    }

    bool SameBits(const Math::Vector3& a, const Math::Vector3& b)
    {
        return SameBits(a.x, b.x) && SameBits(a.y, b.y) && SameBits(a.z, b.z);
    }

    bool SameBits(const Math::Quaternion& a, const Math::Quaternion& b)
    {
        return SameBits(a.x, b.x) && SameBits(a.y, b.y) && SameBits(a.z, b.z) && SameBits(a.w, b.w);
    }

    void BuildSkeleton(Animation::SkeletonPoseRuntime& skeleton)
    {
        skeleton.Parents.resize(JointCount);
        for (int i = 0; i < JointCount; ++i)
        {
            skeleton.Parents[i] = i - 1;
        }
        skeleton.bSplit = false;
    }

    void BuildPose(Animation::LocalPose& pose)
    {
        const DVector translations[JointCount] = {
            {0.31, -0.22, 0.13}, {0.12, 0.38, -0.07}, {-0.09, 0.44, 0.11},
            {0.08, 0.39, 0.05}, {-0.04, 0.28, 0.12}};
        const DVector axes[JointCount] = {
            {1.0, 2.0, -1.0}, {-0.4, 0.8, 1.0}, {0.7, 1.0, 0.3}, {1.0, -0.2, 0.5}, {-0.3, 0.4, 1.0}};
        const double angles[JointCount] = {0.4, -0.6, 0.35, 0.7, -0.25};
        pose.resize(JointCount);
        for (int i = 0; i < JointCount; ++i)
        {
            pose[i].Translation = ToFloat(translations[i]);
            pose[i].Rotation = ToFloat(AxisAngle(axes[i], angles[i]));
            pose[i].Scale = Math::Vector3::One;
        }
    }

    void ForwardKinematics(const Animation::LocalPose& pose, const Animation::SkeletonPoseRuntime& skeleton,
                           DVector (&positions)[JointCount], DQuaternion (&rotations)[JointCount])
    {
        for (int i = 0; i < JointCount; ++i)
        {
            const int parent = skeleton.Parents[i];
            const DVector parentPosition = parent < 0 ? DVector{0.0, 0.0, 0.0} : positions[parent];
            const DQuaternion parentRotation = parent < 0 ? DQuaternion{0.0, 0.0, 0.0, 1.0} : rotations[parent];
            positions[i] = parentPosition + Rotate(parentRotation, ToDouble(pose[i].Translation));
            rotations[i] = Normalize(Multiply(parentRotation, ToDouble(pose[i].Rotation)));
        }
    }

    void TestAxisIndependentDistribution()
    {
        // 局所軸の変更がモデル空間の配分結果を変えない契約を反証する。
        Animation::SkeletonPoseRuntime skeleton;
        BuildSkeleton(skeleton);
        Animation::LocalPose poseA;
        BuildPose(poseA);
        Animation::LocalPose poseB;
        poseB.resize(JointCount);
        const DVector basisAxes[JointCount] = {
            {-0.2, 1.0, 0.6}, {1.0, 0.4, -0.3}, {0.5, -0.7, 1.0}, {-0.8, 0.2, 0.6}, {0.3, 1.0, -0.5}};
        const double basisAngles[JointCount] = {0.75, -0.5, 0.9, -0.65, 0.45};
        DQuaternion basis[JointCount];
        for (int i = 0; i < JointCount; ++i)
        {
            basis[i] = AxisAngle(basisAxes[i], basisAngles[i]);
            const DQuaternion parentBasis = i == 0 ? DQuaternion{0.0, 0.0, 0.0, 1.0} : basis[i - 1];
            poseB[i].Rotation = ToFloat(Multiply(Multiply(Conjugate(parentBasis), ToDouble(poseA[i].Rotation)), basis[i]));
            poseB[i].Translation = ToFloat(Rotate(Conjugate(parentBasis), ToDouble(poseA[i].Translation)));
            poseB[i].Scale = Math::Vector3::One;
        }

        DVector beforePositionsA[JointCount], beforePositionsB[JointCount];
        DQuaternion beforeRotationsA[JointCount], beforeRotationsB[JointCount];
        ForwardKinematics(poseA, skeleton, beforePositionsA, beforeRotationsA);
        ForwardKinematics(poseB, skeleton, beforePositionsB, beforeRotationsB);
        double maxPositionError = 0.0;
        double maxBasisError = 0.0;
        for (int i = 0; i < JointCount; ++i)
        {
            const double positionError = Length(beforePositionsA[i] - beforePositionsB[i]);
            const double basisError = QuaternionError(beforeRotationsB[i], Multiply(beforeRotationsA[i], basis[i]));
            CheckWithin(positionError, 1e-5, "配分前の関節位置", i);
            CheckWithin(basisError, 1e-5, "配分前の局所軸", i);
            RecordMax(maxPositionError, positionError);
            RecordMax(maxBasisError, basisError);
        }

        unsigned char headBefore[sizeof(Animation::JointTransform)];
        std::memcpy(headBefore, &poseA[4], sizeof(headBefore));
        const Procedural::JointRotationShare shares[3] = {{1, 0.2f}, {2, 0.3f}, {3, 0.5f}};
        const Math::Quaternion modelRotation = ToFloat(AxisAngle({0.3, -0.7, 1.0}, 50.0 * Pi / 180.0));
        const Container::Span<const Procedural::JointRotationShare> chain(shares, 3);
        Check(Procedural::TryDistributeModelRotation(poseA, skeleton, chain, modelRotation),
              "骨格 A の配分", 0, 0.0, 1.0);
        Check(Procedural::TryDistributeModelRotation(poseB, skeleton, chain, modelRotation),
              "骨格 B の配分", 0, 0.0, 1.0);

        DVector positionsA[JointCount], positionsB[JointCount];
        DQuaternion rotationsA[JointCount], rotationsB[JointCount];
        ForwardKinematics(poseA, skeleton, positionsA, rotationsA);
        ForwardKinematics(poseB, skeleton, positionsB, rotationsB);
        for (int i = 0; i < JointCount; ++i)
        {
            const double error = Length(positionsA[i] - positionsB[i]);
            CheckWithin(error, 1e-5, "配分後の関節位置", i);
            RecordMax(maxPositionError, error);
        }
        const DQuaternion expectedTip = Multiply(ToDouble(modelRotation), beforeRotationsA[3]);
        const double tipError = QuaternionError(rotationsA[3], expectedTip);
        const double tipAngle = RotationAngle(rotationsA[3], expectedTip);
        CheckWithin(tipError, 1e-5, "先端のグローバル回転", 3);
        Check(std::memcmp(headBefore, &poseA[4], sizeof(headBefore)) == 0,
              "チェーン外の頭のローカル姿勢", 4, 1.0, 0.0);
        std::printf("PoseModifierBaseTest 軸に依らない配分: max_position_m=%.12g max_basis_quat=%.12g tip_quat=%.12g tip_angle_rad=%.12g\n",
                    maxPositionError, maxBasisError, tipError, tipAngle);
    }

    void TestRejectedDistributionPreservesPose()
    {
        // 不正な配分入力を拒否したときに姿勢の全ビットを保つ契約を反証する。
        Animation::SkeletonPoseRuntime skeleton;
        BuildSkeleton(skeleton);
        Animation::LocalPose original;
        BuildPose(original);
        const Math::Quaternion modelRotation = ToFloat(AxisAngle({0.3, -0.7, 1.0}, 50.0 * Pi / 180.0));
        size_t maximumChangedBytes = 0;
        for (int sample = 0; sample < 4; ++sample)
        {
            Animation::LocalPose pose = original;
            Procedural::JointRotationShare shares[3] = {{1, 0.2f}, {2, 0.3f}, {3, 0.5f}};
            Math::Quaternion rotation = modelRotation;
            if (sample == 0)
            {
                shares[2].Weight = 0.5002f;
            }
            else if (sample == 1)
            {
                shares[1].Weight = std::nanf("");
            }
            else if (sample == 2)
            {
                rotation.x = std::nanf("");
            }
            else
            {
                shares[0].Joint = 3;
                shares[2].Joint = 1;
            }
            unsigned char before[JointCount * sizeof(Animation::JointTransform)];
            std::memcpy(before, pose.data(), sizeof(before));
            const Container::Span<const Procedural::JointRotationShare> chain(shares, 3);
            const bool bAccepted = Procedural::TryDistributeModelRotation(pose, skeleton, chain, rotation);
            const auto* after = reinterpret_cast<const unsigned char*>(pose.data());
            size_t changedBytes = 0;
            for (size_t i = 0; i < sizeof(before); ++i)
            {
                changedBytes += before[i] != after[i] ? 1 : 0;
            }
            if (changedBytes > maximumChangedBytes)
            {
                maximumChangedBytes = changedBytes;
            }
            Check(!bAccepted, "不正な配分入力の拒否", sample, bAccepted ? 1.0 : 0.0, 0.0);
            Check(changedBytes == 0, "拒否時の姿勢の全ビット", sample, static_cast<double>(changedBytes), 0.0);
        }
        Animation::LocalPose boundary = original;
        const Procedural::JointRotationShare accepted[3] = {{1, 0.2f}, {2, 0.3f}, {3, 0.50005f}};
        const Container::Span<const Procedural::JointRotationShare> chain(accepted, 3);
        Check(Procedural::TryDistributeModelRotation(boundary, skeleton, chain, modelRotation),
              "許容範囲内の重みの和", 4, 0.0, 1.0);
        const double acceptedSum = double(accepted[0].Weight) + double(accepted[1].Weight) + double(accepted[2].Weight);
        std::printf("PoseModifierBaseTest 拒否時の姿勢: max_changed_bytes=%zu accepted_weight_sum=%.12g\n",
                    maximumChangedBytes, acceptedSum);
    }

    void TestCriticalSpringResponse()
    {
        // 臨界減衰の静止目標への応答が単調に近づき行き過ぎない契約を反証する。
        Procedural::ScalarSpring spring;
        Check(spring.Reset(0.0f), "臨界減衰の Reset", 0, 0.0, 1.0);
        const Procedural::SpringSettings settings{2.0f, 1.0f};
        double previous = 0.0;
        double maxOvershoot = 0.0;
        double maxDecrease = 0.0;
        for (int i = 0; i < 180; ++i)
        {
            float dropped = -1.0f;
            Check(spring.Advance(1.0f, 1.0f / 60.0f, settings, dropped),
                  "臨界減衰の Advance", i, 0.0, 1.0);
            const double position = spring.GetPosition();
            Check(std::isfinite(position) && position <= 1.0 + 1e-6,
                  "臨界減衰の行き過ぎ", i, position, 1.0 + 1e-6);
            Check(position >= previous, "臨界減衰の単調性", i, position - previous, 0.0);
            RecordMax(maxOvershoot, position - 1.0);
            RecordMax(maxDecrease, previous - position);
            previous = position;
        }
        Check(previous >= 0.999, "臨界減衰の収束", 180, previous, 0.999);
        std::printf("PoseModifierBaseTest 臨界減衰: max_overshoot=%.12g max_decrease=%.12g final_error=%.12g\n",
                    maxOvershoot, maxDecrease, std::fabs(1.0 - previous));
    }

    void TestOscillatingSpringTimePartition()
    {
        // 減衰振動の軌跡が同じ時刻で時間刻みの分割に依らない契約を反証する。
        Procedural::ScalarSpring scalarShort, scalarLong;
        Procedural::AngularSpring angularShort, angularLong;
        Check(scalarShort.Reset() && scalarLong.Reset() && angularShort.Reset() && angularLong.Reset(),
              "減衰振動の Reset", 0, 0.0, 1.0);
        const Procedural::SpringSettings settings{3.0f, 0.3f};
        const Math::Quaternion target = ToFloat(AxisAngle({0.3, -0.5, 0.8}, Pi * 0.5));
        double maxScalarError = 0.0;
        double maxAngularError = 0.0;
        double maximumPosition = 0.0;
        for (int i = 0; i < 120; ++i)
        {
            float dropped = -1.0f;
            for (int step = 0; step < 2; ++step)
            {
                Check(scalarShort.Advance(1.0f, 1.0f / 60.0f, settings, dropped),
                      "短い刻みのスカラー Advance", i * 2 + step, 0.0, 1.0);
                Check(angularShort.Advance(target, 1.0f / 60.0f, settings, dropped),
                      "短い刻みの回転 Advance", i * 2 + step, 0.0, 1.0);
                RecordMax(maximumPosition, scalarShort.GetPosition());
            }
            Check(scalarLong.Advance(1.0f, 1.0f / 30.0f, settings, dropped),
                  "長い刻みのスカラー Advance", i, 0.0, 1.0);
            Check(angularLong.Advance(target, 1.0f / 30.0f, settings, dropped),
                  "長い刻みの回転 Advance", i, 0.0, 1.0);
            const double scalarError = std::fabs(double(scalarShort.GetPosition()) - scalarLong.GetPosition());
            const double angularError = RotationAngle(ToDouble(angularShort.GetRotation()), ToDouble(angularLong.GetRotation()));
            CheckWithin(scalarError, 1e-5, "時間分割による位置差", i);
            CheckWithin(angularError, 1e-5, "時間分割による回転角差", i);
            RecordMax(maxScalarError, scalarError);
            RecordMax(maxAngularError, angularError);
        }
        Check(maximumPosition > 1.0, "減衰振動の行き過ぎ", 0, maximumPosition, 1.0);
        std::printf("PoseModifierBaseTest 時間分割: max_position_difference=%.12g max_angle_rad=%.12g max_position=%.12g\n",
                    maxScalarError, maxAngularError, maximumPosition);
    }

    void TestSpringDroppedTime()
    {
        // 固定刻みの上限を超えた時間を捨て、その秒数を返す契約を反証する。
        Procedural::ScalarSpring capped, stepped, boundary;
        Check(capped.Reset() && stepped.Reset() && boundary.Reset(), "時間上限の Reset", 0, 0.0, 1.0);
        const Procedural::SpringSettings settings{3.0f, 0.3f};
        float dropped = -1.0f;
        Check(capped.Advance(1.0f, 0.1f, settings, dropped), "時間上限の Advance", 0, 0.0, 1.0);
        const double droppedError = std::fabs(double(dropped) - (0.1 - 4.0 / 120.0));
        CheckWithin(droppedError, 1e-6, "捨てた秒数", 0);
        for (int i = 0; i < 4; ++i)
        {
            float stepDropped = -1.0f;
            Check(stepped.Advance(1.0f, 1.0f / 120.0f, settings, stepDropped),
                  "固定刻みの Advance", i, 0.0, 1.0);
        }
        const double positionError = std::fabs(double(capped.GetPosition()) - stepped.GetPosition());
        CheckWithin(positionError, 1e-6, "上限で進めた位置", 0);
        float boundaryDropped = -1.0f;
        Check(boundary.Advance(1.0f, 1.0f / 30.0f, settings, boundaryDropped),
              "上限ちょうどの Advance", 0, 0.0, 1.0);
        Check(boundaryDropped == 0.0f, "上限ちょうどで捨てた秒数", 0, boundaryDropped, 0.0);
        std::printf("PoseModifierBaseTest 時間上限: dropped_seconds=%.12g dropped_error_s=%.12g position_error=%.12g boundary_dropped_s=%.12g\n",
                    double(dropped), droppedError, positionError, double(boundaryDropped));
    }

    void TestSpringReset()
    {
        // Reset が位置・速度・余りを初期化し新しいばねと同じ軌跡に戻す契約を反証する。
        Procedural::ScalarSpring scalar, freshScalar;
        Procedural::AngularSpring angular, freshAngular;
        const Procedural::SpringSettings settings{3.0f, 0.3f};
        const Math::Quaternion target = ToFloat(AxisAngle({0.3, -0.5, 0.8}, Pi * 0.5));
        float dropped = -1.0f;
        for (int i = 0; i < 7; ++i)
        {
            Check(scalar.Advance(1.0f, 1.0f / 60.0f, settings, dropped), "初期化前のスカラー Advance", i, 0.0, 1.0);
            Check(angular.Advance(target, 1.0f / 60.0f, settings, dropped), "初期化前の回転 Advance", i, 0.0, 1.0);
        }
        Check(scalar.Advance(1.0f, 1.0f / 240.0f, settings, dropped), "スカラーの余りの準備", 0, 0.0, 1.0);
        Check(angular.Advance(target, 1.0f / 240.0f, settings, dropped), "回転の余りの準備", 0, 0.0, 1.0);
        Check(std::fabs(scalar.GetPosition()) > 1e-6f && std::fabs(scalar.GetVelocity()) > 1e-6f &&
                  scalar.GetRemainderSeconds() > 0.0,
              "初期化前のスカラー状態", 0, scalar.GetRemainderSeconds(), 0.0);
        Check(RotationAngle(ToDouble(angular.GetRotation()), DQuaternion{0.0, 0.0, 0.0, 1.0}) > 1e-6 &&
                  Length(ToDouble(angular.GetAngularVelocity())) > 1e-6 && angular.GetRemainderSeconds() > 0.0,
              "初期化前の回転状態", 0, angular.GetRemainderSeconds(), 0.0);
        Check(scalar.Reset() && angular.Reset(), "ばねの Reset", 0, 0.0, 1.0);
        Check(SameBits(scalar.GetPosition(), 0.0f) && SameBits(scalar.GetVelocity(), 0.0f) &&
                  SameBits(scalar.GetRemainderSeconds(), 0.0),
              "スカラーの初期状態", 0, 1.0, 0.0);
        Check(SameBits(angular.GetRotation(), Math::Quaternion::Identity) &&
                  SameBits(angular.GetAngularVelocity(), Math::Vector3::Zero) &&
                  SameBits(angular.GetRemainderSeconds(), 0.0),
              "回転の初期状態", 0, 1.0, 0.0);

        const float times[] = {1.0f / 60.0f, 1.0f / 240.0f, 1.0f / 120.0f, 1.0f / 30.0f, 0.1f, 1.0f / 60.0f};
        const float targets[] = {1.0f, 1.0f, -0.25f, 0.75f, -0.25f, 1.0f};
        double maxPositionError = 0.0, maxAngleError = 0.0, maxRemainderError = 0.0, maxDroppedError = 0.0;
        for (int i = 0; i < 6; ++i)
        {
            const Math::Quaternion rotationTarget = i % 2 == 0 ? target : Math::Quaternion::Identity;
            float scalarDropped = -1.0f, freshScalarDropped = -1.0f;
            float angularDropped = -1.0f, freshAngularDropped = -1.0f;
            Check(scalar.Advance(targets[i], times[i], settings, scalarDropped) &&
                      freshScalar.Advance(targets[i], times[i], settings, freshScalarDropped),
                  "Reset 後のスカラー Advance", i, 0.0, 1.0);
            Check(angular.Advance(rotationTarget, times[i], settings, angularDropped) &&
                      freshAngular.Advance(rotationTarget, times[i], settings, freshAngularDropped),
                  "Reset 後の回転 Advance", i, 0.0, 1.0);

            const double positionError = std::fabs(double(scalar.GetPosition()) - freshScalar.GetPosition());
            const double angleError = RotationAngle(ToDouble(angular.GetRotation()), ToDouble(freshAngular.GetRotation()));
            const double scalarRemainderError = std::fabs(scalar.GetRemainderSeconds() - freshScalar.GetRemainderSeconds());
            const double angularRemainderError = std::fabs(angular.GetRemainderSeconds() - freshAngular.GetRemainderSeconds());
            const double scalarDroppedError = std::fabs(double(scalarDropped) - freshScalarDropped);
            const double angularDroppedError = std::fabs(double(angularDropped) - freshAngularDropped);
            CheckWithin(positionError, 0.0, "Reset 後の位置差", i);
            CheckWithin(angleError, 1e-12, "Reset 後の回転角差", i);
            CheckWithin(scalarRemainderError, 0.0, "Reset 後のスカラーの余り", i);
            CheckWithin(angularRemainderError, 0.0, "Reset 後の回転の余り", i);
            CheckWithin(scalarDroppedError, 0.0, "Reset 後のスカラーの捨てた秒数", i);
            CheckWithin(angularDroppedError, 0.0, "Reset 後の回転の捨てた秒数", i);
            Check(SameBits(scalar.GetPosition(), freshScalar.GetPosition()) &&
                      SameBits(scalar.GetVelocity(), freshScalar.GetVelocity()) &&
                      SameBits(scalar.GetRemainderSeconds(), freshScalar.GetRemainderSeconds()) &&
                      SameBits(scalarDropped, freshScalarDropped),
                  "Reset 後のスカラー状態の全ビット", i, 1.0, 0.0);
            Check(SameBits(angular.GetRotation(), freshAngular.GetRotation()) &&
                      SameBits(angular.GetAngularVelocity(), freshAngular.GetAngularVelocity()) &&
                      SameBits(angular.GetRemainderSeconds(), freshAngular.GetRemainderSeconds()) &&
                      SameBits(angularDropped, freshAngularDropped),
                  "Reset 後の回転状態の全ビット", i, 1.0, 0.0);
            Check(std::isfinite(scalar.GetVelocity()) &&
                      std::isfinite(Length(ToDouble(angular.GetAngularVelocity()))),
                  "Reset 後の速度の有限性", i, 1.0, 0.0);
            RecordMax(maxPositionError, positionError);
            RecordMax(maxAngleError, angleError);
            RecordMax(maxRemainderError, scalarRemainderError);
            RecordMax(maxRemainderError, angularRemainderError);
            RecordMax(maxDroppedError, scalarDroppedError);
            RecordMax(maxDroppedError, angularDroppedError);
        }
        std::printf("PoseModifierBaseTest Reset: max_position_error=%.12g max_angle_rad=%.12g max_remainder_error_s=%.12g max_dropped_error_s=%.12g bit_mismatches=0\n",
                    maxPositionError, maxAngleError, maxRemainderError, maxDroppedError);
    }
    using NorvesLib::Core::AnimationClipResource;
    using NorvesLib::Core::AnimGraphResource;
    using NorvesLib::Core::Entity;
    using NorvesLib::Core::Identity;
    using NorvesLib::Core::ResourceRegistry;
    using NorvesLib::Core::SkeletalAssetResource;
    using NorvesLib::Core::SkeletonResource;
    using NorvesLib::Core::SkinnedMeshResource;
    using NorvesLib::Core::World;
    namespace Component = NorvesLib::Core::Component;
    namespace Skeletal = NorvesLib::Core::Skeletal;

    struct ModifierCall
    {
        int32_t Id;
        const Animation::IPoseModifier* Instance;
        float Weight;
        bool bHasDriveSignals;
        Animation::AnimDriveSignals Signals;
        Entity* Owner;
        int32_t ChildIndex;
        int32_t MissingIndex;
        Math::Matrix4x4 MeshToWorld;
        Math::Matrix4x4 MeshNodeTransform;
        Container::VariableArray<Math::Matrix4x4> JointModelMatrices;
    };

    Container::VariableArray<ModifierCall>* g_ModifierCalls = nullptr;

    class RecordingModifier final : public Animation::IPoseModifier
    {
      public:
        RecordingModifier(int32_t id, Container::VariableArray<ModifierCall>& calls)
            : m_Id(id), m_Calls(calls)
        {
        }

        int32_t Order() const override
        {
            return 1000;
        }

        bool Apply(Animation::LocalPose&, Animation::PoseModifierContext& context) override
        {
            const auto* models = context.GetJointModelMatrices();
            if (!models)
            {
                return false;
            }
            m_Calls.push_back({m_Id, this, context.Weight, context.HasDriveSignals(), context.GetDriveSignals(),
                               context.GetOwner(), context.FindJointIndex(Identity("Child")),
                               context.FindJointIndex(Identity("NoSuchJoint")), context.MeshToWorld,
                               context.MeshNodeTransform, *models});
            return true;
        }

      private:
        int32_t m_Id;
        Container::VariableArray<ModifierCall>& m_Calls;
    };

    template <int32_t Id>
    bool CreateRecordingModifier(const Procedural::PoseModifierCreateArgs& args,
                                 Container::TSharedPtr<Animation::IPoseModifier>& out,
                                 Procedural::PoseModifierConfigReport& report)
    {
        const char* const allowed[] = {"joint"};
        if (!Procedural::CheckPoseModifierKeys(args.Settings, allowed, report))
        {
            return false;
        }
        int32_t jointIndex = -1;
        if (!Procedural::ResolvePoseModifierJoint(args.Settings.FindMember("joint"), args.Skeleton, jointIndex, report))
        {
            report.Path = Container::String("joint");
            return false;
        }
        Check(g_ModifierCalls != nullptr, "試験用記録先", 0, g_ModifierCalls ? 1.0 : 0.0, 1.0);
        out = Container::MakeShared<RecordingModifier>(Id, *g_ModifierCalls);
        return true;
    }

    enum ModifierId : int32_t
    {
        First = 1,
        Second,
        Foot,
        Lean,
        Custom,
        Curve,
        Tail
    };

    const Procedural::PoseModifierTypeEntry ModifierTypes[] = {
        {"First", 0, CreateRecordingModifier<First>},
        {"Second", 0, CreateRecordingModifier<Second>},
        {"FootIK", 0, CreateRecordingModifier<Foot>},
        {"Lean", 0, CreateRecordingModifier<Lean>},
        {"Custom", 300, CreateRecordingModifier<Custom>},
        {"Curve", 0, CreateRecordingModifier<Curve>}
    };
    const Procedural::PoseModifierTypeTable ModifierTable{ModifierTypes};

    void CheckConfig(bool bOk, const char* contract)
    {
        Check(bOk, contract, 0, bOk ? 1.0 : 0.0, 1.0);
    }

    struct ModifierFixture : Animation::IClipResolver
    {
        ResourceRegistry Registry;
        Container::TSharedPtr<SkeletonResource> Skeleton;
        Container::TSharedPtr<SkinnedMeshResource> Mesh;
        Container::TSharedPtr<AnimationClipResource> Clip;
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        Container::TSharedPtr<AnimGraphResource> Graph;
        Container::VariableArray<ModifierCall> Calls;
        World TestWorld;
        Entity* Owner = nullptr;
        Component::SkinnedMeshComponent* MeshComponent = nullptr;
        Component::AnimatorComponent* Animator = nullptr;

        ModifierFixture()
        {
            CheckConfig(Registry.Initialize(), "試験用 Registry の初期化");
            Skeleton = Registry.CreateTransient<SkeletonResource>("ModifierSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("ModifierMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("ModifierClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("ModifierAsset");
            CheckConfig(Skeleton && Mesh && Clip && Asset, "試験用資源の作成");

            Container::VariableArray<Skeletal::SkeletalJoint> joints(2);
            joints[0].Name = "Root";
            joints[1].Name = "Child";
            joints[1].ParentIndex = 0;
            for (auto& joint : joints)
            {
                joint.InverseBindMatrix.fill(0);
                for (unsigned i = 0; i < 4; ++i)
                {
                    joint.InverseBindMatrix[i * 5] = 1;
                }
            }
            Skeleton->SetJoints(std::move(joints));
            CheckConfig(Skeleton->Load(), "試験用骨格の読み込み");

            Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointIndices[0] = 1;
            vertices[0].JointWeights[0] = 1;
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(Container::VariableArray<uint32_t>{0, 0, 0});
            CheckConfig(Mesh->Load(), "試験用メッシュの読み込み");

            Skeletal::SkeletalAnimationClip clip;
            clip.Name = "a";
            clip.DurationSeconds = 1;
            Skeletal::SkeletalAnimationChannel channel;
            channel.JointIndex = 0;
            channel.Path = Skeletal::SkeletalAnimationPath::Translation;
            channel.Samples.push_back({0, {0, 0, 0, 0}});
            channel.Samples.push_back({1, {2, 0, 0, 0}});
            clip.Channels.push_back(std::move(channel));
            Clip->SetClip(std::move(clip));
            CheckConfig(Clip->Load(), "試験用クリップの読み込み");
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            CheckConfig(Asset->Load(), "試験用アセットの読み込み");

            Graph = Registry.CreateTransient<AnimGraphResource>("ModifierGraph");
            Animation::AnimGraphReport graphReport;
            const char* graphJson =
                R"({"version":1,"params":[{"name":"Speed","type":"float","value":0.5}],"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})";
            CheckConfig(Graph && Graph->Compile(Container::String(graphJson), *Skeleton, *this, graphReport) &&
                            Graph->Load(),
                        "試験用グラフの読み込み");
            TestWorld.Initialize();
            Owner = TestWorld.SpawnEntity<Entity>();
            CheckConfig(Owner != nullptr, "試験用 Entity の作成");
            MeshComponent = TestWorld.CreateComponent<Component::SkinnedMeshComponent>(Owner);
            Animator = TestWorld.CreateComponent<Component::AnimatorComponent>(Owner);
            CheckConfig(MeshComponent && Animator, "試験用コンポーネントの作成");
            MeshComponent->SetSkeletalAsset(Asset);
            CheckConfig(Animator->SetGraph(Graph), "試験用グラフの接続");
            CheckConfig(g_ModifierCalls == nullptr, "試験用記録先の独立性");
            g_ModifierCalls = &Calls;
        }

        ~ModifierFixture()
        {
            TestWorld.Finalize();
            g_ModifierCalls = nullptr;
        }

        Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
        {
            return name == Container::StringView("a") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
        }

        void Tick()
        {
            Calls.clear();
            TestWorld.Tick(0.01f);
        }
    };

    const char* ConfigErrorName(Procedural::PoseModifierConfigError error)
    {
        using Error = Procedural::PoseModifierConfigError;
        switch (error)
        {
        case Error::UnknownType: return "UnknownType";
        case Error::UnknownKey: return "UnknownKey";
        case Error::DuplicateKey: return "DuplicateKey";
        case Error::TypeMismatch: return "TypeMismatch";
        case Error::OutOfRange: return "OutOfRange";
        case Error::UnknownJoint: return "UnknownJoint";
        case Error::UnknownParam: return "UnknownParam";
        case Error::FileReadFailed: return "FileReadFailed";
        default: return "Other";
        }
    }

    void PrintConfigError(const Procedural::PoseModifierConfigReport& report)
    {
        std::printf("PoseModifierBaseTest 拒否: error=%s path=", ConfigErrorName(report.Error));
        for (const auto character : report.Path)
        {
            std::putchar(static_cast<unsigned char>(character));
        }
        std::putchar('\n');
        std::fflush(stdout);
    }

    void TestConfigRejectionPreservesModifiers()
    {
        // 設定の拒否理由を区別し、失敗後も元の modifier 群をそのまま実行する契約を反証する。
        ModifierFixture fixture;
        Procedural::PoseModifierConfigReport report;
        const char* valid =
            R"({"version":1,"modifiers":[{"type":"First","order":400,"settings":{"joint":"Root"}},{"type":"Second","order":200,"settings":{"joint":"Child"}}]})";
        CheckConfig(fixture.Animator->ApplyPoseModifiersJson(Container::String(valid), ModifierTable, &report),
                    "初期 modifier 群の設定");
        fixture.Tick();
        CheckConfig(fixture.Calls.size() == 2 && fixture.Calls[0].Id == Second && fixture.Calls[1].Id == First,
                    "初期 modifier 群の順序と回数");
        const auto* previousFirst = fixture.Calls[0].Instance;
        const auto* previousSecond = fixture.Calls[1].Instance;

        using Error = Procedural::PoseModifierConfigError;
        const struct RejectionCase
        {
            const char* Json;
            Error Expected;
        } cases[] = {
            {R"({"version":1,"modifiers":[{"type":"First","settings":{"joint":"Root"}},{"type":"Missing"}]})", Error::UnknownType},
            {R"({"version":1,"modifiers":[{"type":"First","settings":{"joint":"Root"}},{"type":"Second","extra":1}]})", Error::UnknownKey},
            {R"({"version":1,"modifiers":[{"type":"First","settings":{"joint":"Root"}},{"type":"Second","order":2,"order":3}]})", Error::DuplicateKey},
            {R"({"version":1,"modifiers":[{"type":"First","settings":{"joint":"Root"}},{"type":"Second","order":"5"}]})", Error::TypeMismatch},
            {R"({"version":1,"modifiers":[{"type":"First","settings":{"joint":"Root"}},{"type":"Second","order":20000}]})", Error::OutOfRange},
            {R"({"version":1,"modifiers":[{"type":"First","settings":{"joint":"Root"}},{"type":"Second","settings":{"joint":"NoSuchJoint"}}]})", Error::UnknownJoint}
        };
        for (const auto& test : cases)
        {
            const bool bAccepted = fixture.Animator->ApplyPoseModifiersJson(Container::String(test.Json), ModifierTable, &report);
            PrintConfigError(report);
            CheckConfig(!bAccepted && report.Error == test.Expected && !report.Path.empty(), "設定の拒否理由とパス");
            fixture.Tick();
            CheckConfig(fixture.Calls.size() == 2 && fixture.Calls[0].Id == Second && fixture.Calls[1].Id == First &&
                            fixture.Calls[0].Instance == previousFirst && fixture.Calls[1].Instance == previousSecond,
                        "拒否後の modifier 群の保持");
        }
    }

    void TestConfigOrderAndFile()
    {
        // 既定順・同順の安定性・無効化と BOM 付きファイルの読み込みを反証する。
        ModifierFixture fixture;
        Procedural::PoseModifierConfigReport report;
        const char* json =
            R"({"version":1,"modifiers":[{"type":"FootIK","settings":{"joint":"Root"}},{"type":"Lean","settings":{"joint":"Root"}},{"type":"Custom","settings":{"joint":"Child"}},{"type":"Second","order":600,"settings":{"joint":"Root"}},{"type":"First","order":600,"settings":{"joint":"Root"}},{"type":"First","order":50,"enabled":false,"settings":{"joint":"Root"}}]})";
        CheckConfig(fixture.Animator->ApplyPoseModifiersJson(Container::String(json), ModifierTable, &report),
                    "順序の設定");
        const auto checkOrder = [&fixture]()
        {
            fixture.Tick();
            CheckConfig(fixture.Calls.size() == 5 && fixture.Calls[0].Id == Lean && fixture.Calls[1].Id == Custom &&
                            fixture.Calls[2].Id == Foot && fixture.Calls[3].Id == Second && fixture.Calls[4].Id == First,
                        "既定順・同順・無効化");
        };
        checkOrder();

        const char* boundaries = R"({
            "version": 1,
            "modifiers": [
                {"type":"FootIK","settings":{"joint":"Root"}},
                {"type":"Lean","settings":{"joint":"Root"}},
                {"type":"Custom","settings":{"joint":"Root"}},
                {"type":"First","order":99,"settings":{"joint":"Root"}},
                {"type":"Second","order":100,"settings":{"joint":"Root"}},
                {"type":"First","order":299,"settings":{"joint":"Root"}},
                {"type":"Second","order":300,"settings":{"joint":"Root"}},
                {"type":"First","order":499,"settings":{"joint":"Root"}},
                {"type":"Second","order":500,"settings":{"joint":"Root"}}
            ]
        })";
        CheckConfig(fixture.Animator->ApplyPoseModifiersJson(Container::String(boundaries), ModifierTable, &report),
                    "既定 order の境界設定");
        fixture.Tick();
        const int32_t expectedOrder[] = {First, Lean, Second, First, Custom, Second, First, Foot, Second};
        CheckConfig(fixture.Calls.size() == sizeof(expectedOrder) / sizeof(expectedOrder[0]),
                    "既定 order の境界での呼び出し回数");
        for (size_t i = 0; i < fixture.Calls.size(); ++i)
        {
            CheckConfig(fixture.Calls[i].Id == expectedOrder[i], "既定 order の境界での呼び出し順");
        }

        char path[128];
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::snprintf(path, sizeof(path), "PoseModifierBaseTest.%llu.posemod.json",
                      static_cast<unsigned long long>(stamp));
        std::FILE* file = nullptr;
        CheckConfig(fopen_s(&file, path, "wbx") == 0 && file != nullptr, "一時設定ファイルの作成");
        const unsigned char bom[] = {0xef, 0xbb, 0xbf};
        const bool bWritten = std::fwrite(bom, 1, sizeof(bom), file) == sizeof(bom) &&
                              std::fwrite(json, 1, std::strlen(json), file) == std::strlen(json);
        const bool bClosed = std::fclose(file) == 0;
        const bool bAccepted = bWritten && bClosed &&
            fixture.Animator->ApplyPoseModifiersFile(Container::String(path), ModifierTable, &report);
        const bool bRemoved = std::remove(path) == 0;
        CheckConfig(bWritten && bClosed && bRemoved, "一時設定ファイルの書き込みと削除");
        CheckConfig(bAccepted, "BOM 付き設定ファイルの読み込み");
        checkOrder();

        const bool bMissingAccepted = fixture.Animator->ApplyPoseModifiersFile(Container::String(path), ModifierTable, &report);
        PrintConfigError(report);
        CheckConfig(!bMissingAccepted && report.Error == Procedural::PoseModifierConfigError::FileReadFailed &&
                        !report.Path.empty(),
                    "存在しない設定ファイルの拒否");
    }

    void TestConfigWeightCurve()
    {
        // カーブの補間・範囲端・重み 0 の省略と、後続処理への重み復元を反証する。
        ModifierFixture fixture;
        Procedural::PoseModifierConfigReport report;
        const char* json =
            R"({"version":1,"modifiers":[{"type":"Curve","weight":{"param":"Speed","curve":[[0,0],[0.5,0.2],[1,1]]},"settings":{"joint":"Root"}}]})";
        CheckConfig(fixture.Animator->ApplyPoseModifiersJson(Container::String(json), ModifierTable, &report),
                    "重みカーブの設定");
        CheckConfig(fixture.Animator->AddModifier(Container::MakeShared<RecordingModifier>(Tail, fixture.Calls)),
                    "包まれていない後続 modifier の追加");
        const auto speed = fixture.Animator->FindParam(Identity("Speed"));
        CheckConfig(speed != Animation::InvalidAnimParam, "Speed パラメータの検索");
        const float values[] = {0.25f, 0.75f, 2.0f, 0.0f};
        const float expected[] = {0.1f, 0.6f, 1.0f, 0.0f};
        for (int i = 0; i < 4; ++i)
        {
            CheckConfig(fixture.Animator->SetFloat(speed, values[i]), "Speed の設定");
            fixture.Tick();
            const bool bZero = values[i] == 0.0f;
            CheckConfig(fixture.Calls.size() == (bZero ? 1 : 2), "カーブ modifier の呼び出し回数");
            if (!bZero)
            {
                CheckConfig(fixture.Calls[0].Id == Curve, "カーブ modifier の呼び出し順");
                CheckWithin(std::fabs(double(fixture.Calls[0].Weight) - expected[i]), 1e-6,
                            "カーブの区分線形補間", i);
            }
            const auto& tail = fixture.Calls[bZero ? 0 : 1];
            CheckConfig(tail.Id == Tail, "後続 modifier の呼び出し順");
            CheckWithin(std::fabs(double(tail.Weight) - 1.0), 1e-6, "後続 modifier の重みの復元", i);
        }
        const char* unknown =
            R"({"version":1,"modifiers":[{"type":"Curve","weight":{"param":"MissingSpeed","curve":[[0,0],[1,1]]},"settings":{"joint":"Root"}}]})";
        const bool bAccepted = fixture.Animator->ApplyPoseModifiersJson(Container::String(unknown), ModifierTable, &report);
        PrintConfigError(report);
        CheckConfig(!bAccepted && report.Error == Procedural::PoseModifierConfigError::UnknownParam && !report.Path.empty(),
                    "未知のカーブパラメータの拒否");
    }

    void TestModifierContextDelivery()
    {
        // 駆動信号・所有者・関節検索と、モデルからワールドおよびメッシュノードの変換が届く契約を反証する。
        ModifierFixture fixture;
        const Math::Transform ownerWorld({10, -3, 7}, ToFloat(AxisAngle({1, 2, -1}, 0.7)), {1.5f, 0.75f, 2.25f});
        fixture.Owner->SetWorldTransform(ownerWorld);
        Animation::AnimDriveSignals signals;
        signals.Speed = 2.5f;
        signals.Velocity = {1.25f, -2.5f, 3.75f};
        CheckConfig(fixture.Animator->SetDriveSignals(signals), "駆動信号の設定");
        Math::Matrix4x4 transform = Math::Matrix4x4::Identity;
        transform.m00 = 0;
        transform.m01 = 2;
        transform.m10 = -2;
        transform.m11 = 0;
        transform.m22 = 2;
        transform.m30 = 3;
        transform.m31 = -4;
        transform.m32 = 5;
        fixture.MeshComponent->SetMeshNodeGlobalTransform(transform);
        CheckConfig(fixture.Animator->AddModifier(Container::MakeShared<RecordingModifier>(First, fixture.Calls)),
                    "文脈記録 modifier の追加");
        fixture.Tick();
        CheckConfig(fixture.Calls.size() == 1, "文脈記録 modifier の呼び出し");
        const auto& call = fixture.Calls[0];
        CheckConfig(call.bHasDriveSignals && SameBits(call.Signals.Speed, signals.Speed) &&
                        SameBits(call.Signals.Velocity, signals.Velocity),
                    "駆動信号の受け渡し");
        CheckConfig(call.Owner == fixture.Owner, "所有 Entity の受け渡し");
        CheckConfig(call.ChildIndex == 1 && call.MissingIndex == -1, "骨格の関節名検索");
        const auto& meshTransform = fixture.MeshComponent->GetMeshNodeGlobalTransform();
        CheckConfig(call.JointModelMatrices.size() == fixture.Skeleton->GetPoseRuntime().Parents.size(),
                    "TestModifierContextDelivery: 全関節のモデル行列の記録");
        double maxMatrixElementError = 0;
        for (size_t joint = 0; joint < call.JointModelMatrices.size(); ++joint)
        {
            Math::Matrix4x4 expectedWorld;
            CheckConfig(fixture.MeshComponent->TryGetJointWorldMatrix(static_cast<uint32_t>(joint), expectedWorld),
                        "TestModifierContextDelivery: 関節のワールド行列の取得");
            const auto actualWorld = call.JointModelMatrices[joint] * call.MeshToWorld;
            for (int element = 0; element < 16; ++element)
            {
                const double expected = expectedWorld.values[element];
                const double error = std::fabs(static_cast<double>(actualWorld.values[element]) - expected);
                const double magnitude = std::fabs(expected);
                const double limit = 1e-5 * (magnitude > 1.0 ? magnitude : 1.0);
                RecordMax(maxMatrixElementError, error);
                CheckWithin(error, limit, "TestModifierContextDelivery: モデル行列 * MeshToWorld とワールド行列の一致",
                            static_cast<int>(joint * 16 + element));
            }
        }
        bool bDifferentTransforms = false;
        for (int i = 0; i < 16; ++i)
        {
            Check(SameBits(meshTransform.values[i], transform.values[i]) &&
                      SameBits(call.MeshNodeTransform.values[i], meshTransform.values[i]),
                  "メッシュノードのグローバル変換の受け渡し", i, call.MeshNodeTransform.values[i], meshTransform.values[i]);
            bDifferentTransforms |= !SameBits(call.MeshToWorld.values[i], meshTransform.values[i]);
        }
        CheckConfig(bDifferentTransforms, "モデルからワールドへの変換とメッシュノードの変換の区別");
        std::printf("TestModifierContextDelivery maxMatrixElementError=%.17g\n", maxMatrixElementError);
    }
} // 無名名前空間

void TestPoseModifierBase()
{
    TestAxisIndependentDistribution();
    TestRejectedDistributionPreservesPose();
    TestCriticalSpringResponse();
    TestOscillatingSpringTimePartition();
    TestSpringDroppedTime();
    TestSpringReset();
    TestConfigRejectionPreservesModifiers();
    TestConfigOrderAndFile();
    TestConfigWeightCurve();
    TestModifierContextDelivery();
    std::puts("PoseModifierBaseTest PASS");
}
