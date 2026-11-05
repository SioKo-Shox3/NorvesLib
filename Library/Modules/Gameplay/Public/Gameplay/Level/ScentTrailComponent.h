// 匂いの痕跡の配置データと制御点の補間を定義する。
#pragma once

#include "Component/Component.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/Vector3.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 子 Entity の並び順を点の順とする、匂いの痕跡の親。
    class ScentTrailComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(ScentTrailComponent, Core::Component::Component)

    public:
        ScentTrailComponent();

        /// 出力を空にし、直下の子のワールド位置と強度を子 Entity の並び順に集める。
        void GatherPoints(Core::Container::VariableArray<Math::Vector3>& outPositions,
                          Core::Container::VariableArray<float>& outIntensities) const;

        PROPERTY(Core::Container::String, TargetKey)
        PROPERTY(float, Width)
    };

    /// 匂いの痕跡の制御点。位置は所有 Entity のワールド位置を使う。
    class ScentTrailPointComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(ScentTrailPointComponent, Core::Component::Component)

    public:
        ScentTrailPointComponent();

        PROPERTY(float, Intensity)
    };

    /// 区間の通し番号 t を一様 Catmull-Rom で補間する。範囲外は端に固定し、端の隣は端点の複製で補う。
    /// 点が無ければ Zero、1 点ならその点を返し、整数の t では制御点を通る。
    Math::Vector3 EvaluateCatmullRom(Core::Container::Span<const Math::Vector3> points, float t);

    /// 強度を位置と同じ規則で補間する。点が無ければ 0 を返す。
    float EvaluateCatmullRom(Core::Container::Span<const float> points, float t);
} // namespace NorvesLib::Modules::Gameplay::Level
