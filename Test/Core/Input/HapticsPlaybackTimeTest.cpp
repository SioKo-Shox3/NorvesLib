#include "Input/HapticsPlaybackTime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <initializer_list>
using namespace NorvesLib::Core::Input;
int main()
{
    double next = 7;
    bool finished = true;
    assert(AdvanceHapticsPlaybackTime(1,false,.25,0,next,finished));
    assert(next == .25 && !finished);
    assert(AdvanceHapticsPlaybackTime(1,false,.75,.25,next,finished));
    assert(next == 1 && finished);
    assert(AdvanceHapticsPlaybackTime(1,false,1,0,next,finished));
    assert(next == 1 && finished);
    assert(AdvanceHapticsPlaybackTime(1,true,.75,.5,next,finished));
    assert(next == .25 && !finished);
    assert(AdvanceHapticsPlaybackTime(1,true,.75,.25,next,finished));
    assert(next == 0 && !finished);
    assert(AdvanceHapticsPlaybackTime(1,false,.75,std::nextafter(.25,0),next,finished));
    assert(next == 1 && finished);
    const double huge = std::numeric_limits<double>::max();
    assert(AdvanceHapticsPlaybackTime(huge,false,huge/2,huge,next,finished));
    assert(next == huge && finished);
    assert(AdvanceHapticsPlaybackTime(huge,true,huge*.75,huge*.75,next,finished));
    assert(next == huge*.75 - (huge - huge*.75) && std::isfinite(next) && !finished);
    assert(AdvanceHapticsPlaybackTime(std::numeric_limits<double>::denorm_min(),true,0,huge,next,finished));
    assert(next == 0 && !finished);
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    {
        next = 7;
        finished = true;
        assert(!AdvanceHapticsPlaybackTime(1,true,0,invalid,next,finished));
        assert(!AdvanceHapticsPlaybackTime(1,true,invalid,0,next,finished));
        assert(!AdvanceHapticsPlaybackTime(invalid,true,0,0,next,finished));
        assert(next == 7 && finished);
    }
    assert(!AdvanceHapticsPlaybackTime(0,true,0,0,next,finished));
    assert(!AdvanceHapticsPlaybackTime(1,true,1,0,next,finished));
    assert(!AdvanceHapticsPlaybackTime(1,false,2,0,next,finished));
    double phase = 0;
    for (int index = 0; index < 10000; ++index)
    {
        assert(AdvanceHapticsPlaybackTime(1,true,phase,.125,phase,finished));
        assert(phase >= 0 && phase < 1 && !finished);
        assert(phase == ((index + 1) % 8) * .125);
    }
    std::cout << "HapticsPlaybackTimeTest passed\n";
    return 0;
}
