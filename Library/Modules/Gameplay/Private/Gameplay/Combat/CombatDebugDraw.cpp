// 戦闘判定のデバッグ表示を扱う実装を置く。
#include "CombatDebugDraw.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "Gameplay/Combat/CombatGeometry.h"
#include "Math/Vector4.h"
#include "Rendering/DebugDrawQueue.h"

#include <cmath>

namespace NorvesLib::Modules::Gameplay::Combat
{
    namespace
    {
        using Math::Vector3;
        using Math::Vector4;
        using Core::Rendering::DebugDrawQueue;

        Vector3 WorldPoint(const CombatPose& pose, const Vector3& point)
        {
            return pose.Position + pose.Rotation * point;
        }

        bool IsFinitePoint(const Vector3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        void AddLine(DebugDrawQueue& queue, const Vector3& start, const Vector3& end, const Vector4& color)
        {
            if (IsFinitePoint(start) && IsFinitePoint(end))
            {
                queue.AddLine(start, end, color);
            }
        }

        void DrawCapsule(const CombatShape& shape, const CombatPose& pose, const Vector4& color,
                         DebugDrawQueue& queue)
        {
            const Vector3 pointA = WorldPoint(pose, shape.PointA);
            const Vector3 pointB = WorldPoint(pose, shape.PointB);
            queue.AddSphere(pointA, shape.Radius, color);
            queue.AddSphere(pointB, shape.Radius, color);

            // 差を double で求め、極端に大きい座標でも軸の正規化を保つ。
            const double dx = double(shape.PointB.x) - double(shape.PointA.x);
            const double dy = double(shape.PointB.y) - double(shape.PointA.y);
            const double dz = double(shape.PointB.z) - double(shape.PointA.z);
            const double length = std::hypot(dx, dy, dz);
            const Vector3 axis = length > 0.0
                ? Vector3(static_cast<float>(dx / length), static_cast<float>(dy / length),
                          static_cast<float>(dz / length))
                : Vector3::UnitY;
            const Vector3 reference = std::fabs(axis.y) < 0.9f ? Vector3::UnitY : Vector3::UnitX;
            const Vector3 localSide = Vector3::Cross(axis, reference).Normalized();
            const Vector3 localOther = Vector3::Cross(axis, localSide).Normalized();
            const Vector3 side = (pose.Rotation * localSide).Normalized() * shape.Radius;
            const Vector3 other = (pose.Rotation * localOther).Normalized() * shape.Radius;

            AddLine(queue, pointA + side, pointB + side, color);
            AddLine(queue, pointA - side, pointB - side, color);
            AddLine(queue, pointA + other, pointB + other, color);
            AddLine(queue, pointA - other, pointB - other, color);
        }

        void DrawBox(const CombatShape& shape, const CombatPose& pose, const Vector4& color,
                     DebugDrawQueue& queue)
        {
            const Vector3 center = WorldPoint(pose, shape.Center);
            const Vector3 x = (pose.Rotation * Vector3::UnitX).Normalized() * shape.HalfExtents.x;
            const Vector3 y = (pose.Rotation * Vector3::UnitY).Normalized() * shape.HalfExtents.y;
            const Vector3 z = (pose.Rotation * Vector3::UnitZ).Normalized() * shape.HalfExtents.z;
            Vector3 corners[8];
            for (unsigned int i = 0; i < 8; ++i)
            {
                corners[i] = center + x * ((i & 1u) ? 1.0f : -1.0f)
                                    + y * ((i & 2u) ? 1.0f : -1.0f)
                                    + z * ((i & 4u) ? 1.0f : -1.0f);
            }
            for (unsigned int i = 0; i < 8; ++i)
            {
                for (unsigned int axis = 1; axis <= 4; axis <<= 1)
                {
                    if ((i & axis) == 0)
                    {
                        AddLine(queue, corners[i], corners[i | axis], color);
                    }
                }
            }
        }

        void DrawShape(const CombatShape& shape, const CombatPose& pose, const Vector4& color,
                       DebugDrawQueue& queue)
        {
            switch (shape.Type)
            {
            case ECombatShapeType::Sphere:
                queue.AddSphere(WorldPoint(pose, shape.Center), shape.Radius, color);
                break;
            case ECombatShapeType::Capsule:
                DrawCapsule(shape, pose, color, queue);
                break;
            case ECombatShapeType::Box:
                DrawBox(shape, pose, color, queue);
                break;
            }
        }

        bool PoseChanged(const CombatPose& previous, const CombatPose& current)
        {
            const auto& a = previous.Rotation;
            const auto& b = current.Rotation;
            const bool bSameRotation = a == b ||
                (a.x == -b.x && a.y == -b.y && a.z == -b.z && a.w == -b.w);
            return previous.Position != current.Position || !bSameRotation;
        }

        void DrawMotion(const CombatShape& shape, const CombatPose& previous, const CombatPose& current,
                        const Vector4& color, DebugDrawQueue& queue)
        {
            if (shape.Type == ECombatShapeType::Capsule)
            {
                AddLine(queue, WorldPoint(previous, shape.PointA), WorldPoint(current, shape.PointA), color);
                AddLine(queue, WorldPoint(previous, shape.PointB), WorldPoint(current, shape.PointB), color);
            }
            else
            {
                AddLine(queue, WorldPoint(previous, shape.Center), WorldPoint(current, shape.Center), color);
            }
        }
    } // namespace

    void SubmitCombatDebugShapes(const Core::Container::VariableArray<CombatVolumeSnapshot>& volumes,
                                 Core::Rendering::DebugDrawQueue& queue)
    {
        for (const CombatVolumeSnapshot& snapshot : volumes)
        {
            const CombatSweepVolume& volume = snapshot.Volume;
            if (!IsValidCombatVolume(volume))
            {
                continue;
            }

            const Vector4 color = snapshot.bHitbox
                ? (snapshot.bWindowOpen ? Vector4(1.0f, 0.1f, 0.1f, 1.0f) : Vector4(0.35f, 0.02f, 0.02f, 1.0f))
                : Vector4(0.1f, 1.0f, 0.2f, 1.0f);
            // 判定と同じ姿勢の補間と正規化を使う。
            const CombatPose current = InterpolateCombatPose(volume.Previous, volume.Current, 1.0f);
            if (snapshot.bHitbox && PoseChanged(volume.Previous, volume.Current))
            {
                const CombatPose previous = InterpolateCombatPose(volume.Previous, volume.Current, 0.0f);
                // 線の描画はアルファ混合しないので、前の姿勢は RGB を白に寄せる。
                const Vector4 previousColor(color.x + (1.0f - color.x) * 0.55f,
                                            color.y + (1.0f - color.y) * 0.55f,
                                            color.z + (1.0f - color.z) * 0.55f, 1.0f);
                DrawShape(volume.Shape, previous, previousColor, queue);
                DrawMotion(volume.Shape, previous, current, previousColor, queue);
            }
            DrawShape(volume.Shape, current, color, queue);
        }
    }
} // namespace NorvesLib::Modules::Gameplay::Combat
#endif
