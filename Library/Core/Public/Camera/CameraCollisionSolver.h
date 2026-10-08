#pragma once
#include "Math/Vector3.h"
#include <cstdint>
namespace NorvesLib::Core::Camera
{
    struct CameraProbeRequest
    {
        Math::Vector3 Origin, Direction;
        float Distance = 0, Radius = .2f;
    };
    struct CameraProbeHit
    {
        float Distance = 0;
        bool bStartPenetrating = false;
    };
    enum class CameraProbeResult : uint8_t
    {
        Clear,
        Hit,
        Failed
    };
    class ICameraProbe
    {
      public:
        virtual ~ICameraProbe() = default;
        // 球中心の移動距離を返す。半径はDistanceに重ねて差し引かない。
        virtual CameraProbeResult SweepSphere(const CameraProbeRequest& request, CameraProbeHit& out) const = 0;
    };
    struct CameraCollisionSettings
    {
        float ProbeRadius = .2f, Margin = .05f, ExtendHalfLife = .3f;
        float MaximumExtendSpeed = 4;
    };
    struct CameraCollisionState
    {
        float EffectiveLength = 0, ExtendVelocity = 0;
        bool bInitialized = false;
    };
    struct CameraCollisionRequest
    {
        Math::Vector3 Pivot;
        Math::Vector3 Direction = Math::Vector3(0, 0, -1);
        // MinArm等の希望値制限は呼出側で適用。実効長は障害優先で0まで縮める。
        float DesiredLength = 3;
    };
    struct CameraCollisionOutput
    {
        Math::Vector3 Position;
        float EffectiveLength = 0, SafeLimit = 0;
        bool bObstructed = false;
    };
    enum class CameraCollisionResult : uint8_t
    {
        Success,
        Paused,
        InvalidArgument,
        ProbeFailed,
        StartOverlapping
    };
    class CameraCollisionSolver final
    {
      public:
        // dt0はprobeも状態も進めない。失敗/初期overlap/例外時はstate/outを保持する。
        // 初期overlapは長さだけでは解消できない。呼出側は以前のカメラ姿勢全体を保持し、別途解決する。
        // 同じ長さを新しいpivotへ適用しても安全にはならない。
        static CameraCollisionResult Solve(CameraCollisionState& state, const CameraCollisionSettings& settings,
                                           const CameraCollisionRequest& request, const ICameraProbe& probe, float dt,
                                           CameraCollisionOutput& out);
    };
} // namespace NorvesLib::Core::Camera
