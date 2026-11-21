// 草の場から知覚用の透過率を取得する。
// 踏み倒し（GR44）は GPU 上の場にのみ記録され、CPU から読めないため、草の透過率に含めない。
#pragma once

#include "AI/Ports/IGrassFieldSource.h"

namespace NorvesLib::Core::Vegetation
{
    class Vegetation;
}

namespace NorvesLib::Modules::AI::Perception
{
    /// GameThread 専用。植生を借用し、借用中の寿命は呼び出し側が保証する。
    class EngineGrassFieldSource final : public IGrassFieldSource
    {
    public:
        explicit EngineGrassFieldSource(const Core::Vegetation::Vegetation* vegetation = nullptr);
        void SetVegetation(const Core::Vegetation::Vegetation* vegetation);
        float QueryTransmittance(const Math::Vector3& a, const Math::Vector3& b, float stepMeters) const override;

    private:
        const Core::Vegetation::Vegetation* m_pVegetation;
    };
}
