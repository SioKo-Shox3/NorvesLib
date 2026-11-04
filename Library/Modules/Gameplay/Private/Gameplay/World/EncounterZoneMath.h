#pragma once

#include "Gameplay/World/EncounterZoneTypes.h"
#include "Math/Vector3.h"

namespace NorvesLib::Modules::Gameplay::World
{
    // 箱は各軸に沿い、円柱は Y 軸に沿う。Center はワールド座標。
    struct EncounterZoneShapeDesc
    {
        EEncounterZoneShape Shape = EEncounterZoneShape::Sphere;
        Math::Vector3 Center;
        Math::Vector3 HalfExtents;
        float Radius = 0.0f;
        float HalfHeight = 0.0f;
    };

    // 境界を含む。有限でない入力や未知の形状は false を返す。
    bool IsInsideInflated(const EncounterZoneShapeDesc& shape, const Math::Vector3& point, float margin);

    struct EncounterZoneTuning
    {
        float EnterMargin = 0.0f;
        float ExitMargin = 0.0f;
        float ResetDelaySeconds = 0.0f;

        // 退出判定にはこの値を使い、進入判定より小さい領域にならないようにする。
        float GetEffectiveExitMargin() const;
    };

    struct EncounterZoneEvents
    {
        bool bEntered = false;
        bool bExited = false;
        bool bReset = false;
        bool bCleared = false;
    };

    class EncounterZoneStateMachine
    {
    public:
        // 進入判定には EnterMargin、退出判定には GetEffectiveExitMargin() を使う。
        // 退出領域外の連続時間を計り、帯に戻った場合は計時をやり直す。
        EncounterZoneEvents Step(bool bAnySensorInsideEnter, bool bAllSensorsOutsideExit, bool bTeleported,
                                 float worldDeltaSeconds, const EncounterZoneTuning& tuning);
        EncounterZoneEvents MarkCleared();
        EEncounterZoneState GetState() const { return m_State; }

    private:
        EEncounterZoneState m_State = EEncounterZoneState::Outside;
        double m_ResetElapsedSeconds = 0.0;
    };
} // namespace NorvesLib::Modules::Gameplay::World
