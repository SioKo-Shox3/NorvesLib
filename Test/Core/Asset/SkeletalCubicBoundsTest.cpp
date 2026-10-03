#include "Resource/SkeletalCubicBounds.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::Skeletal;
namespace
{
    bool SameCurve(const CubicBezierBounds& a, const CubicBezierBounds& b)
    {
        if (a.Dimensions != b.Dimensions)
        {
            return false;
        }
        for (size_t control = 0; control < 4; ++control)
        {
            for (size_t component = 0; component < 4; ++component)
            {
                if (a.Controls[control].Values[component].Lower != b.Controls[control].Values[component].Lower ||
                    a.Controls[control].Values[component].Upper != b.Controls[control].Values[component].Upper)
                {
                    return false;
                }
            }
        }
        return true;
    }
    bool Contains(CubicInterval interval, long double value)
    {
        return static_cast<long double>(interval.Lower) <= value && value <= static_cast<long double>(interval.Upper);
    }
    // MSVCのlong doubleはbinary64。参照式側の丸めをkernelの包絡違反と誤認しない。
    // 拡張精度を使える場合は厳密比較を保つ。これはテストoracleだけの誤差予算。
    bool ContainsReference(CubicInterval interval, long double value, long double magnitude)
    {
        if constexpr (std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits)
        {
            return Contains(interval, value);
        }
        const long double allowance = 64 * std::numeric_limits<double>::epsilon() * std::max(1.0L, magnitude);
        return static_cast<long double>(interval.Lower) - allowance <= value &&
        value <= static_cast<long double>(interval.Upper) + allowance;
    }
    long double Hermite(double a, double z, double firstTangent, double lastTangent, double duration, long double u)
    {
        return (2*u*u*u-3*u*u+1)*a + (u*u*u-2*u*u+u)*duration*firstTangent +
        (-2*u*u*u+3*u*u)*z + (u*u*u-u*u)*duration*lastTangent;
    }
    CubicFloatPoint Stored(const CubicBezierBounds& curve, double u)
    {
        CubicBoundedPoint point;
        assert(EvaluateCubicBounds(curve, u, point) == CubicBoundsStatus::Success);
        CubicFloatPoint result;
        for (uint32_t component = 0; component < curve.Dimensions; ++component)
        {
            result.Values[component] = static_cast<float>(point.Values[component].Lower / 2 + point.Values[component].Upper / 2);
        }
        return result;
    }
}
int main()
{
    using Status = CubicBoundsStatus;
    CubicPoint a, z, first, last;
    z.Values[0] = 2; first.Values[0] = last.Values[0] = 1;
    CubicBezierBounds curve;
    assert(BuildHermiteBezierBounds(a,z,first,last,2,3,curve) == Status::Success);
    for (double u : {0.0,0.125,0.5,0.75,1.0})
    {
        CubicBoundedPoint point;
        assert(EvaluateCubicBounds(curve,u,point) == Status::Success && Contains(point.Values[0],2*u));
    }
    auto bound = BoundVectorChord(curve,Stored(curve,0),Stored(curve,1));
    assert(bound.Status == Status::Success && bound.ErrorUpper < 1e-12);
    // 中点だけなら誤差0に見えるS字を、全制御点上界が見落とさない。
    z = {}; first.Values[0] = last.Values[0] = 6;
    assert(BuildHermiteBezierBounds(a,z,first,last,1,3,curve) == Status::Success);
    CubicBoundedPoint point;
    assert(EvaluateCubicBounds(curve,0.5,point) == Status::Success && Contains(point.Values[0],0));
    assert(EvaluateCubicBounds(curve,0.25,point) == Status::Success && Contains(point.Values[0],0.5625));
    bound = BoundVectorChord(curve,{},{});
    assert(bound.Status == Status::Success && bound.ErrorUpper >= 2 && bound.ErrorUpper < 2.000001);
    const auto original = curve;
    CubicBezierBounds left,right;
    assert(SplitCubicBounds(curve,0.3,left,right) == Status::Success);
    for (size_t index = 0; index <= 100; ++index)
    {
        const double u = double(index)/100;
        assert(EvaluateCubicBounds(left,u,point) == Status::Success);
        assert(ContainsReference(point.Values[0],Hermite(0,0,6,6,1,static_cast<long double>(0.3)*u),12));
        assert(EvaluateCubicBounds(right,u,point) == Status::Success);
        const long double mapped = static_cast<long double>(0.3)+(1-static_cast<long double>(0.3))*u;
        assert(ContainsReference(point.Values[0],Hermite(0,0,6,6,1,mapped),12));
    }
    // 入力と片側出力の同一objectは候補を作ってから置換する。
    curve = original;
    assert(SplitCubicBounds(curve,0.3,curve,right) == Status::Success);
    assert(SameCurve(curve,left));
    auto retained = curve;
    assert(SplitCubicBounds(original,0.3,curve,curve) == Status::InvalidInput);
    assert(SameCurve(curve,retained));
    assert(SplitCubicBounds(original,1,curve,right) == Status::InvalidInput);
    assert(SameCurve(curve,retained));
    // 独立Hermite式と保存floatのchord差を多数区間で照合する。
    uint64_t random = 0x3141592653589793ull;
    auto next = [&]()
    {
        random = random*6364136223846793005ull+1; return double((random>>32)%2001)/100-10;
    };
    for (size_t trial = 0; trial < 200; ++trial)
    {
        for (size_t component=0;component<3;++component)
        {
            a.Values[component]=next(); z.Values[component]=next(); first.Values[component]=next(); last.Values[component]=next();
        }
        const double duration = 0.125 + double(trial%19)/4;
        assert(BuildHermiteBezierBounds(a,z,first,last,duration,3,curve) == Status::Success);
        const auto storedA=Stored(curve,0), storedZ=Stored(curve,1);
        bound=BoundVectorChord(curve,storedA,storedZ); assert(bound.Status==Status::Success);
        for(size_t sample=0;sample<=100;++sample)
        {
            const double u=double(sample)/100; long double distanceSquared=0;
            assert(EvaluateCubicBounds(curve,u,point)==Status::Success);
            for(size_t component=0;component<3;++component)
            {
                const long double value=Hermite(a.Values[component],z.Values[component],first.Values[component],last.Values[component],duration,u);
                assert(ContainsReference(point.Values[component],value, std::abs(a.Values[component])+std::abs(z.Values[component])+
                    duration*(std::abs(first.Values[component])+std::abs(last.Values[component]))));
                const long double chord=(1-static_cast<long double>(u))*storedA.Values[component]+static_cast<long double>(u)*storedZ.Values[component];
                distanceSquared+=(value-chord)*(value-chord);
            }
            assert(std::sqrt(distanceSquared)<=bound.ErrorUpper);
        }
    }
    // quaternionの符号やtangentを変えず、正規化後の理想補間を比較する。
    a={}; z={}; first={}; last={}; a.Values[3]=1; z.Values[2]=std::sqrt(0.5); z.Values[3]=std::sqrt(0.5);
    for(size_t component=0;component<4;++component)
    {
        first.Values[component]=last.Values[component]=z.Values[component]-a.Values[component];
    }
    assert(BuildHermiteBezierBounds(a,z,first,last,1,4,curve)==Status::Success);
    const auto start=Stored(curve,0), end=Stored(curve,1);
    bound=BoundRotationChord(curve,start,end);
    assert(bound.Status==Status::Success && bound.MinimumCurveNorm>0 && bound.ErrorUpper<0.586);
    for(size_t sample=0;sample<=1000;++sample)
    {
        const long double u=static_cast<long double>(sample)/1000;
        long double q[4]={}, norm=0;
        for(size_t component=0;component<4;++component)
        {
            q[component]=Hermite(a.Values[component],z.Values[component],first.Values[component],last.Values[component],1,u); norm+=q[component]*q[component];
        }
        norm=std::sqrt(norm); assert(norm>=bound.MinimumCurveNorm);
        const long double theta=std::acos(-1.0L)/4;
        const long double dot=(q[2]*std::sin(theta*u)+q[3]*std::cos(theta*u))/norm;
        const long double angle=2*std::acos(std::min(1.0L,std::abs(dot)));
        assert(angle<=bound.ErrorUpper);
    }
    assert(SplitCubicBounds(curve,0.5,left,right)==Status::Success);
    const auto halfBound=BoundRotationChord(left,Stored(left,0),Stored(left,1));
    assert(halfBound.Status==Status::Success && halfBound.ErrorUpper<bound.ErrorUpper);
    a={}; a.Values[3]=1; z=a; first={}; last={};
    assert(BuildHermiteBezierBounds(a,z,first,last,1,4,curve)==Status::Success);
    bound=BoundRotationChord(curve,Stored(curve,0),Stored(curve,1));
    assert(bound.Status==Status::Success && bound.ErrorUpper<1e-12);
    // 端点が同方向でも内部でzeroを横切る曲線は認証しない。
    first.Values[3] = -4; last.Values[3] = 4;
    assert(BuildHermiteBezierBounds(a,z,first,last,1,4,curve)==Status::Success);
    assert(EvaluateCubicBounds(curve,0.5,point)==Status::Success && Contains(point.Values[3],0));
    assert(BoundRotationChord(curve,Stored(curve,0),Stored(curve,1)).Status==Status::Uncertified);
    first = {}; last = {};
    z.Values[3]=-1;
    assert(BuildHermiteBezierBounds(a,z,first,last,1,4,curve)==Status::Success);
    assert(BoundRotationChord(curve,Stored(curve,0),Stored(curve,1)).Status==Status::Uncertified);
    assert(BoundRotationChord(curve,{},{}).Status==Status::InvalidInput);
    retained=curve;
    first.Values[1]=std::numeric_limits<double>::infinity();
    assert(BuildHermiteBezierBounds(a,z,first,last,1,4,curve)==Status::InvalidInput && SameCurve(curve,retained));
    first={}; first.Values[0]=std::numeric_limits<double>::max();
    assert(BuildHermiteBezierBounds(a,z,first,last,std::numeric_limits<double>::max(),4,curve)==Status::InvalidInput);
    assert(BuildHermiteBezierBounds(a,z,{}, {},0,4,curve)==Status::InvalidInput);
    assert(BuildHermiteBezierBounds(a,z,{}, {},1,2,curve)==Status::InvalidInput);
    auto malformed=curve; malformed.Controls[0].Values[0]={2,1};
    assert(BoundVectorChord(malformed,{},{}).Status==Status::InvalidInput);
    const int rounding=std::fegetround();
    assert(std::fesetround(FE_DOWNWARD)==0);
    assert(BuildHermiteBezierBounds(a,z,{}, {},1,4,curve)==Status::UnsupportedArithmetic);
    assert(std::fesetround(rounding)==0);
    std::cout << "SkeletalCubicBoundsTest PASS: hermite_enclosure_split_all_interval_chord_rotation_fail_closed\n";
    return 0;
}
