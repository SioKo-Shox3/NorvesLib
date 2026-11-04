#pragma once

#include "Component/Component.h"

namespace NorvesLib::Modules::Gameplay::World
{
    /// 所有 Entity のシミュレーション変換を基準に領域へ参加するセンサー。
    class ZoneSensorComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(ZoneSensorComponent, Core::Component::Component)

    public:
        ZoneSensorComponent();
        void Initialize() override;
        void BeginPlay() override;
        void Finalize() override;

        /// 位置を移した後に呼ぶ。印は次の評価で消費される。
        void MarkTeleported() { m_bTeleported = true; }

        PROPERTY(Math::Vector3, Offset)
        PROPERTY(uint32_t, KindBits)

    private:
        friend class EncounterService;
        bool m_bTeleported = false;
    };
} // namespace NorvesLib::Modules::Gameplay::World
