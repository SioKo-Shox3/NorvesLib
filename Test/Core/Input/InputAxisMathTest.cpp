#include "Input/InputAxisMath.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Input;
using NorvesLib::Math::Vector2;

namespace
{
    bool Near(double a, double b, double tolerance = 1e-5) { return std::fabs(a-b) <= tolerance; }
    double Length(Vector2 value) { return std::hypot(static_cast<double>(value.x), static_cast<double>(value.y)); }
}

int main()
{
    InputAxisResponse response;
    float scalar = 9.0f;
    assert(TryApplyAxisResponse(0.5f, response, scalar) && Near(scalar, 0.5));
    assert(TryApplyAxisResponse(-4.0f, response, scalar) && scalar == -1.0f);
    response.DeadZone = 0.2f;
    assert(TryApplyAxisResponse(0.1f, response, scalar) && scalar == 0.0f);
    assert(TryApplyAxisResponse(0.2f, response, scalar) && scalar == 0.0f);
    assert(TryApplyAxisResponse(0.6f, response, scalar) && Near(scalar, 0.5));
    Vector2 result;
    assert(TryApplyRadialAxisResponse(Vector2(0.12f, 0.16f), response, result) && Length(result) < 1e-6);
    assert(TryApplyRadialAxisResponse(Vector2(3,4), response, result));
    assert(Near(result.x, 0.6) && Near(result.y, 0.8));
    Vector2 alias(3,4);
    assert(TryApplyRadialAxisResponse(alias, response, alias));
    assert(Near(alias.x,0.6) && Near(alias.y,0.8));

    for (const auto curve : {EInputResponseCurve::Linear, EInputResponseCurve::Power, EInputResponseCurve::Expo})
    {
        response.Curve = curve;
        response.Gamma = 2.2f;
        response.Expo = 0.75f;
        assert(TryApplyAxisResponse(1.0f,response,scalar) && scalar == 1.0f);
        assert(TryApplyAxisResponse(0.0f,response,scalar) && scalar == 0.0f);
        float boundary = 0;
        assert(TryApplyAxisResponse(response.DeadZone+1e-6f,response,boundary) && boundary < 2e-6f);
        for (int degrees = 0; degrees < 360; degrees += 15)
        {
            const double angle = degrees * 3.14159265358979323846 / 180.0;
            double previous = 0;
            for (int step = 0; step <= 2000; ++step)
            {
                const double radius = step/1000.0;
                const Vector2 input(static_cast<float>(std::cos(angle)*radius),static_cast<float>(std::sin(angle)*radius));
                assert(TryApplyRadialAxisResponse(input,response,result));
                const double length = Length(result);
                assert(length <= 1.000001 && length + 1e-6 >= previous);
                assert(std::fabs(input.x*result.y - input.y*result.x) < 1e-5);
                previous = length;
            }
        }
        for (int step=0; step<=1000; ++step)
        {
            const float value=step/1000.0f;
            float positive=0, negative=0;
            assert(TryApplyAxisResponse(value,response,positive));
            assert(TryApplyAxisResponse(-value,response,negative));
            assert(Near(positive,-negative));
        }
    }

    const float max = std::numeric_limits<float>::max();
    assert(TryApplyRadialAxisResponse(Vector2(max,max),response,result));
    assert(Near(Length(result),1) && Near(result.x,result.y));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float invalid : {nan, inf, -inf})
    {
        scalar=9;
        assert(!TryApplyAxisResponse(invalid,response,scalar) && scalar==0);
        result=Vector2(9,9);
        assert(!TryApplyRadialAxisResponse(Vector2(invalid,0),response,result) && Length(result)==0);
    }
    for (int invalid = 0; invalid < 8; ++invalid)
    {
        InputAxisResponse bad;
        if(invalid==0) bad.DeadZone=-0.1f;
        if(invalid==1) bad.DeadZone=1.0f;
        if(invalid==2) bad.DeadZone=nan;
        if(invalid==3) bad.Gamma=0.0f;
        if(invalid==4) bad.Gamma=inf;
        if(invalid==5) bad.Expo=-0.1f;
        if(invalid==6) bad.Expo=1.1f;
        if(invalid==7) bad.Curve=static_cast<EInputResponseCurve>(255);
        assert(!IsValidAxisResponse(bad));
        scalar=9; result=Vector2(9,9);
        assert(!TryApplyAxisResponse(0.5f,bad,scalar) && scalar==0);
        assert(!TryApplyRadialAxisResponse(Vector2(0.5f,0.5f),bad,result) && Length(result)==0);
    }
    for(int hz : {30,60,144})
    {
        double total=0, mouseTotal=0, stickTotal=0;
        for(int frame=0;frame<hz;++frame)
        {
            float value=0;
            assert(TryComputeLookDelta(1200.0f/hz,0.5f,0.25f,180.0f,1.0/hz,value));
            total+=value;
            assert(TryComputeLookDelta(1200.0f/hz,0.0f,0.25f,180.0f,1.0/hz,value));
            mouseTotal+=value;
            assert(TryComputeLookDelta(0.0f,0.5f,0.25f,180.0f,1.0/hz,value));
            stickTotal+=value;
        }
        assert(Near(total,390,1e-3) && Near(mouseTotal,300,1e-3) && Near(stickTotal,90,1e-3));
    }
    assert(TryComputeLookDelta(1200,0.5f,0.25f,180,0,scalar) && scalar==300);
    assert(TryComputeLookDelta(-1200,-0.5f,0.25f,180,1,scalar) && scalar==-390);
    assert(!TryComputeLookDelta(max,0,2,0,0,scalar) && scalar==0);
    assert(!TryComputeLookDelta(0,1,180,std::numeric_limits<float>::max(),std::numeric_limits<double>::max(),scalar) && scalar==0);
    assert(!TryComputeLookDelta(1,0,1,1,-1,scalar) && scalar==0);
    assert(!TryComputeLookDelta(1,0,1,1,std::numeric_limits<double>::quiet_NaN(),scalar) && scalar==0);
    assert(!TryComputeLookDelta(1,1.1f,1,1,1,scalar) && scalar==0);
    assert(!TryComputeLookDelta(nan,0,1,1,1,scalar) && scalar==0);
    assert(!TryComputeLookDelta(1,0,-1,1,1,scalar) && scalar==0);
    assert(TryComputeLookDeltaWide(static_cast<double>(max)*2,0,0.25f,0,0,scalar) && scalar==max*0.5f);
    assert(TryComputeLookDeltaWide(static_cast<double>(max)*2,-1,1,max,2,scalar) && scalar==0);
    assert(!TryComputeLookDeltaWide(std::numeric_limits<double>::infinity(),0,0,0,0,scalar) && scalar==0);
    assert(!TryComputeLookDeltaWide(std::numeric_limits<double>::max(),0,max,0,0,scalar) && scalar==0);
    response={};response.Curve=EInputResponseCurve::Power;response.Gamma=max;
    assert(TryApplyRadialAxisResponseWide(1,1,response,result));
    assert(Near(result.x,std::sqrt(0.5)) && Near(result.y,std::sqrt(0.5)));
    response.Gamma=0.01f;
    assert(TryApplyAxisResponseWide(1e-46,response,scalar));
    assert(Near(scalar,std::pow(1e-46,static_cast<double>(response.Gamma))));
    response={};
    assert(TryApplyRadialAxisResponseWide(std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),response,result));
    assert(Near(result.x,std::sqrt(0.5)) && Near(result.y,std::sqrt(0.5)));
    assert(!TryApplyRadialAxisResponseWide(std::numeric_limits<double>::infinity(),0,response,result) && Length(result)==0);
    std::cout << "InputAxisMathTest passed\n";
    return 0;
}
