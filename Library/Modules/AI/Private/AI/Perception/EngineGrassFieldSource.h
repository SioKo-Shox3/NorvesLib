// 知覚用に視線上の草の透過率と先端の最大高さを取得する。
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
        GrassPathSample QueryGrassPath(const Math::Vector3& eye, const Math::Vector3& target,
                                       float stepMeters, float opacityScale) const override;

    private:
        const Core::Vegetation::Vegetation* m_pVegetation;
    };
}
