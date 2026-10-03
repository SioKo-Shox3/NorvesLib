#pragma once

#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    struct CubicInterval
    {
        double Lower = 0;
        double Upper = 0;
    };
    struct CubicPoint
    {
        double Values[4] = {};
    };
    struct CubicFloatPoint
    {
        float Values[4] = {};
    };
    struct CubicBoundedPoint
    {
        CubicInterval Values[4];
    };
    struct CubicBezierBounds
    {
        uint32_t Dimensions = 3;
        CubicBoundedPoint Controls[4];
    };
    enum class CubicBoundsStatus
    {
        Success, InvalidInput, UnsupportedArithmetic, Uncertified
    };
    struct CubicChordBound
    {
        CubicBoundsStatus Status = CubicBoundsStatus::InvalidInput;
        double ErrorUpper = 0;
        double MinimumCurveNorm = 0;
    };
    // IEEE binary64、最近接丸め、subnormal有効、fast-math無効が必要。
    // 基本演算とsqrtの外向き区間で、double入力を実数としたHermite曲線を包む。
    // 全出力は成功時のみ置換。raw quaternion/tangentの符号は変更しない。
    [[nodiscard]] CubicBoundsStatus BuildHermiteBezierBounds(const CubicPoint& start, const CubicPoint& end,
        const CubicPoint& outgoing, const CubicPoint& incoming, double duration, uint32_t dimensions,
        CubicBezierBounds& out) noexcept;
    [[nodiscard]] CubicBoundsStatus EvaluateCubicBounds(const CubicBezierBounds& curve, double parameter,
        CubicBoundedPoint& out) noexcept;
    // parameterは実際の保存float時刻から算出した値を使い、丸め前の0.5と混同しないこと。
    [[nodiscard]] CubicBoundsStatus SplitCubicBounds(const CubicBezierBounds& curve, double parameter,
        CubicBezierBounds& left, CubicBezierBounds& right) noexcept;
    // 理想LINEARと、保存float端点を実数として比較する全区間L2上界。単位は入力と同じ。
    [[nodiscard]] CubicChordBound BoundVectorChord(const CubicBezierBounds& curve,
        const CubicFloatPoint& start, const CubicFloatPoint& end) noexcept;
    // 正規化した原cubicと理想short-path SLERP/NLERPのSO(3)角度上界(rad)。
    // 同一半球と非ゼロを証明できなければUncertified。実runtimeのfloat算術誤差は含めない。
    [[nodiscard]] CubicChordBound BoundRotationChord(const CubicBezierBounds& curve,
        const CubicFloatPoint& start, const CubicFloatPoint& end) noexcept;
} // namespace NorvesLib::Core::Skeletal
