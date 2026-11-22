#pragma once

#include "AI/Navigation/NavDynamicOverlay.h"
#include "Component/Component.h"
#include "Math/Vector2.h"

namespace NorvesLib::Modules::AI
{
    /// 所有 Entity の位置と向きに従う動的障害物。寸法はメートルで指定し、Entity の拡大率は掛けない。
    class NavObstacleComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(NavObstacleComponent, Core::Component::Component)

    public:
        NavObstacleComponent();
        explicit NavObstacleComponent(const Core::FieldInitializer* initializer);
        explicit NavObstacleComponent(const Core::IUnknown* sourceObject);
        ~NavObstacleComponent() override;

        NavObstacleComponent(const NavObstacleComponent&) = delete;
        NavObstacleComponent& operator=(const NavObstacleComponent&) = delete;

        PROPERTY(ENavObstacleShape, Shape)
        PROPERTY(float, Radius)              // 円・カプセルの半径。
        PROPERTY(float, HalfLength)          // カプセルの中心線の半分。
        PROPERTY(Math::Vector2, HalfExtents) // OBB の局所 X・Z 方向の半分。
        PROPERTY(float, MinHeight)           // 所有 Entity の Y から下端までの高さ。
        PROPERTY(float, MaxHeight)           // 所有 Entity の Y から上端までの高さ。
        PROPERTY(ENavObstacleMode, Mode)
        PROPERTY(float, CostMultiplier)

    public:
        void Initialize() override;
        void EndPlay() override;
        void Finalize() override;
        void OnPropertyApplied(const Core::ClassProperty& property) override;

        bool IsNavStamped() const;
        uint32_t GetStampedCellCount() const;
        bool IsRegisteredToModule() const { return m_bRegistered; }

    private:
        friend class NavObstacleSystem;
        void SetDefaults();
        void Unregister();

        // 浮動小数はビット列で控え、同じ NaN が残っていても変更と扱わない。
        struct StampProperties
        {
            ENavObstacleShape Shape;
            uint32_t RadiusBits;
            uint32_t HalfLengthBits;
            uint32_t HalfExtentXBits;
            uint32_t HalfExtentZBits;
            uint32_t MinHeightBits;
            uint32_t MaxHeightBits;
            ENavObstacleMode Mode;
            uint32_t CostMultiplierBits;

            bool operator==(const StampProperties&) const = default;
        };
        StampProperties GetStampProperties() const;

        StampProperties m_EvaluatedProperties{};
        NavDynamicStamp m_Stamp;
        const NavDynamicOverlay* m_Overlay = nullptr; // 押印先。登録解除時に参照を外す。
        Math::Vector3 m_StampedPosition;
        Math::Vector2 m_StampedAxis;
        double m_StampedTime = 0.0;
        Math::Vector3 m_ObservedPosition;
        uint64_t m_ObservedVersion = 0;
        double m_ObservedTime = 0.0;
        bool m_bHasObservation = false;
        bool m_bNeedsRestamp = true;      // 押印が無いため、現在の姿勢で押す。
        bool m_bNeedsReevaluate = false;  // 既存の押印を前回の姿勢で評価し直す。
        bool m_bWaitingForInterval = false;
        bool m_bRegistered = false;
    };
}
