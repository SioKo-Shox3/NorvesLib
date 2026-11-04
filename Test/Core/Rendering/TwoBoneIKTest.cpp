// 二関節 IK の幾何と回転差分を独立した倍精度の計算で検証する。
#include "Animation/TwoBoneIK.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace
{
    namespace Animation = NorvesLib::Core::Animation;
    namespace Math = NorvesLib::Math;
    constexpr unsigned Seed = 0x2b01cafe;
    constexpr double Pi = 3.14159265358979323846;

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

    DVector DoubleVector(const Math::Vector3& v)
    {
        return {v.x, v.y, v.z};
    }
    Math::Vector3 FloatVector(DVector v)
    {
        return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    }
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
    DVector Unit(DVector v)
    {
        return v * (1.0 / Length(v));
    }

    double Random(std::mt19937& random, double lo, double hi)
    {
        return lo + (hi - lo) * (static_cast<double>(random()) / std::mt19937::max());
    }
    DVector RandomUnit(std::mt19937& random)
    {
        const double z = Random(random, -1.0, 1.0);
        const double angle = Random(random, -Pi, Pi);
        const double radius = std::sqrt(1.0 - z * z);
        return {radius * std::cos(angle), radius * std::sin(angle), z};
    }

    void PrintVector(const char* name, const Math::Vector3& v)
    {
        char text[192];
        std::snprintf(text, sizeof(text), "%s=(%.17g, %.17g, %.17g)\n", name,
            static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
        std::fputs(text, stderr);
    }
    void PrintQuaternion(const char* name, const Math::Quaternion& q)
    {
        char text[256];
        std::snprintf(text, sizeof(text), "%s=(%.17g, %.17g, %.17g, %.17g)\n", name,
            static_cast<double>(q.x), static_cast<double>(q.y), static_cast<double>(q.z), static_cast<double>(q.w));
        std::fputs(text, stderr);
    }
    void Check(bool bOk, const char* contract, int sample, double actual, double limit,
        const Animation::TwoBoneIKInput& input, const Animation::TwoBoneIKResult& result)
    {
        if (!bOk)
        {
            char text[320];
            std::snprintf(text, sizeof(text),
                "TwoBoneIKTest 検査失敗: %s seed=%u sample=%d actual=%.17g limit=%.17g\n",
                contract, Seed, sample, actual, limit);
            std::fputs(text, stderr);
            PrintVector("Root", input.Root);
            PrintVector("Mid", input.Mid);
            PrintVector("End", input.End);
            PrintVector("Target", input.Target);
            PrintVector("Pole", input.Pole);
            PrintVector("FallbackPole", input.FallbackPole);
            std::snprintf(text, sizeof(text), "MaxStretch=%.17g SoftLimitWidth=%.17g\n",
                static_cast<double>(input.MaxStretch), static_cast<double>(input.SoftLimitWidth));
            std::fputs(text, stderr);
            PrintVector("newMid", result.Mid);
            PrintVector("newEnd", result.End);
            PrintVector("PoleUsed", result.PoleUsed);
            PrintQuaternion("RootDelta", result.RootDelta);
            PrintQuaternion("MidDelta", result.MidDelta);
            std::abort();
        }
    }

    // q * (v, 0) * conjugate(q) を倍精度で展開し、正規化で出力の誤りを隠さない。
    DVector Rotate(const Math::Quaternion& q, DVector v)
    {
        const DVector u{q.x, q.y, q.z};
        const double w = q.w;
        return v * (w * w - Dot(u, u)) + u * (2.0 * Dot(u, v)) + Cross(u, v) * (2.0 * w);
    }

    // swing * twist の twist は四元数のベクトル部を元の軸に射影して得る。
    // 符号の同値性を除いた最小の角度を使い、ゼロ四元数への分解は不合格にする。
    double TwistAngle(const Math::Quaternion& q, DVector axis)
    {
        const double projection = Dot({q.x, q.y, q.z}, axis);
        const double w = q.w;
        if (projection == 0.0 && w == 0.0)
        {
            return Pi;
        }
        return 2.0 * std::atan2(std::fabs(projection), std::fabs(w));
    }
    void RecordMax(double& worst, double value)
    {
        if (value > worst)
        {
            worst = value;
        }
    }
    bool Finite(const Math::Vector3& v)
    {
        return std::isfinite(static_cast<double>(v.x)) && std::isfinite(static_cast<double>(v.y))
            && std::isfinite(static_cast<double>(v.z));
    }
    bool Finite(const Math::Quaternion& q)
    {
        return std::isfinite(static_cast<double>(q.x)) && std::isfinite(static_cast<double>(q.y))
            && std::isfinite(static_cast<double>(q.z)) && std::isfinite(static_cast<double>(q.w));
    }

    void TestRandomGeometry()
    {
        std::mt19937 random(Seed);
        double worstReach = 0.0, worstLength1 = 0.0, worstLength2 = 0.0, worstUnreachable = 0.0;
        double worstRootTwist = 0.0, worstMidTwist = 0.0, worstRootDirection = 0.0, worstMidDirection = 0.0;
        for (int sample = 0; sample < 10000; ++sample)
        {
            const double l1 = Random(random, 0.2, 0.6);
            const double l2 = Random(random, 0.2, 0.6);
            const DVector root{Random(random, -1.0, 1.0), Random(random, -1.0, 1.0), Random(random, -1.0, 1.0)};
            const DVector mid = root + RandomUnit(random) * l1;
            const DVector end = mid + RandomUnit(random) * l2;
            const DVector targetDirection = RandomUnit(random);
            const double distance = Random(random, std::fabs(l1 - l2) + 0.01, l1 + l2 - 0.01);
            Animation::TwoBoneIKInput input;
            input.Root = FloatVector(root);
            input.Mid = FloatVector(mid);
            input.End = FloatVector(end);
            input.Target = FloatVector(root + targetDirection * distance);
            input.Pole = FloatVector(RandomUnit(random));
            input.FallbackPole = FloatVector(RandomUnit(random));
            input.MaxStretch = 1.0f;
            input.SoftLimitWidth = 0.0f;
            const auto result = Animation::SolveTwoBoneIK(input);
            const DVector a = DoubleVector(input.Root);
            const DVector newB = DoubleVector(result.Mid);
            const DVector newC = DoubleVector(result.End);
            const double reach = Length(newC - DoubleVector(input.Target));
            const double length1 = std::fabs(Length(newB - a) - l1) / l1;
            const double length2 = std::fabs(Length(newC - newB) - l2) / l2;
            Check(reach <= 1e-4, "到達位置", sample, reach, 1e-4, input, result);
            Check(length1 <= 1e-5, "根の区間長の相対誤差", sample, length1, 1e-5, input, result);
            Check(length2 <= 1e-5, "端の区間長の相対誤差", sample, length2, 1e-5, input, result);
            RecordMax(worstReach, reach);
            RecordMax(worstLength1, length1);
            RecordMax(worstLength2, length2);

            const DVector rootAxis = Unit(DoubleVector(input.Mid) - a);
            const DVector midAxis = Unit(DoubleVector(input.End) - DoubleVector(input.Mid));
            const double rootTwist = TwistAngle(result.RootDelta, rootAxis);
            const double midTwist = TwistAngle(result.MidDelta, midAxis);
            const double rootDirection = Length(Rotate(result.RootDelta, rootAxis) - Unit(newB - a));
            const double midDirection = Length(Rotate(result.MidDelta, midAxis) - Unit(newC - newB));
            Check(rootTwist <= 1e-4, "根のねじれ角", sample, rootTwist, 1e-4, input, result);
            Check(midTwist <= 1e-4, "中間のねじれ角", sample, midTwist, 1e-4, input, result);
            Check(rootDirection <= 1e-4, "根の回転差分の作用方向", sample, rootDirection, 1e-4, input, result);
            Check(midDirection <= 1e-4, "中間の回転差分の作用方向", sample, midDirection, 1e-4, input, result);
            RecordMax(worstRootTwist, rootTwist);
            RecordMax(worstMidTwist, midTwist);
            RecordMax(worstRootDirection, rootDirection);
            RecordMax(worstMidDirection, midDirection);

            input.Target = FloatVector(a + targetDirection * ((l1 + l2) * Random(random, 1.1, 3.0)));
            const auto unreachable = Animation::SolveTwoBoneIK(input);
            const DVector expected = a + Unit(DoubleVector(input.Target) - a) * (l1 + l2);
            const double clampError = Length(DoubleVector(unreachable.End) - expected);
            Check(clampError <= 1e-4, "届かない目標の完全伸展", sample, clampError, 1e-4, input, unreachable);
            RecordMax(worstUnreachable, clampError);
        }
        std::printf("TwoBoneIKTest 最大誤差: reach_m=%.12g length1_relative=%.12g length2_relative=%.12g unreachable_m=%.12g\n",
            worstReach, worstLength1, worstLength2, worstUnreachable);
        std::printf("TwoBoneIKTest 最大回転誤差: root_twist_rad=%.12g mid_twist_rad=%.12g root_direction=%.12g mid_direction=%.12g\n",
            worstRootTwist, worstMidTwist, worstRootDirection, worstMidDirection);
    }

    void TestPoleSweep()
    {
        Animation::TwoBoneIKInput input;
        input.Root = {0.1f, -0.2f, 0.3f};
        input.Mid = {0.1f, 0.2f, 0.3f};
        input.End = {0.4f, 0.2f, 0.3f};
        input.Target = {0.5f, 0.1f, 0.6f};
        const DVector a = DoubleVector(input.Root);
        const DVector axis = Unit(DoubleVector(input.Target) - a);
        const DVector u = Unit(Cross(axis, {0.0, 1.0, 0.0}));
        const DVector v = Cross(axis, u);
        double minimumSide = 1.0;
        for (int step = 0; step <= 720; ++step)
        {
            const double angle = 2.0 * Pi * step / 720.0;
            input.Pole = FloatVector(u * std::cos(angle) + v * std::sin(angle));
            const auto result = Animation::SolveTwoBoneIK(input);
            const DVector b = DoubleVector(result.Mid) - a;
            const DVector pole = DoubleVector(input.Pole);
            const double side = Dot(b - axis * Dot(b, axis), pole - axis * Dot(pole, axis));
            Check(std::isfinite(side) && side > 0.0, "ポール側の膝", step, side, 0.0, input, result);
            if (side < minimumSide)
            {
                minimumSide = side;
            }
        }
        std::printf("TwoBoneIKTest ポール掃引: steps=720 minimum_side_dot=%.12g\n", minimumSide);
    }

    void TestDegenerateInputs()
    {
        Animation::TwoBoneIKInput base;
        base.Root = {0.0f, 0.0f, 0.0f};
        base.Mid = {0.3f, 0.2f, 0.0f};
        base.End = {0.6f, 0.0f, 0.0f};
        base.Target = {0.5f, 0.0f, 0.0f};
        base.Pole = {0.0f, 1.0f, 0.0f};
        Animation::TwoBoneIKInput cases[8];
        for (auto& input : cases)
        {
            input = base;
        }
        cases[0].Target = base.Root;
        cases[1].Pole = {1.0f, 0.0f, 0.0f};
        cases[1].FallbackPole = {0.0f, 0.0f, 1.0f};
        cases[2].Pole = {1.0f, 0.0f, 0.0f};
        cases[2].FallbackPole = {0.0f, 0.0f, 0.0f};
        cases[3].Mid = base.Root;
        cases[4].End = base.Mid;
        cases[5].Mid = base.Root;
        cases[5].End = base.Root;
        cases[6].Target = {1e30f, -1e30f, 1e30f};
        cases[7].Target = {1e-30f, -1e-30f, 1e-30f};
        for (int sample = 0; sample < 8; ++sample)
        {
            const auto& input = cases[sample];
            const auto result = Animation::SolveTwoBoneIK(input);
            const bool bFinite = Finite(result.Mid) && Finite(result.End) && Finite(result.RootDelta)
                && Finite(result.MidDelta) && Finite(result.PoleUsed);
            Check(bFinite, "退化入力での全出力成分の有限性", sample, bFinite ? 1.0 : 0.0, 1.0, input, result);
        }
        std::puts("TwoBoneIKTest 退化入力: 8 件の全成分が有限");
    }
} // namespace

void TestTwoBoneIK()
{
    TestRandomGeometry();
    TestPoleSweep();
    TestDegenerateInputs();
    std::puts("TwoBoneIKTest PASS");
}
