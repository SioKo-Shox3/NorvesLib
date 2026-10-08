#include "Platform/Windows/RawMouseMotionTracker.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
using namespace NorvesLib::Core::Platform;
int main()
{
    RawMouseMotionTracker tracker;RawMouseDelta delta;
    RawMouseDesktop desktop{0,0,1920,1080,false};
    assert(tracker.Absolute(0,0,0,desktop,delta) && delta.X==0 && delta.Y==0);
    assert(tracker.Absolute(0,65535,65535,desktop,delta) && delta.X==1919 && delta.Y==1079);
    assert(tracker.Absolute(0,0,0,desktop,delta) && delta.X==-1919 && delta.Y==-1079);
    assert(tracker.Absolute(1,300,400,desktop,delta) && delta.X==0 && delta.Y==0);
    assert(tracker.Absolute(0,65535,65535,desktop,delta) && delta.X==1919);
    assert(tracker.Absolute(1,300,400,desktop,delta) && delta.X==0);
    const auto relative=tracker.Relative(0,-10,20);assert(relative.X==-10 && relative.Y==20);
    assert(tracker.Absolute(0,42,24,desktop,delta) && delta.X==0 && delta.Y==0);
    tracker.Forget(0);assert(tracker.Absolute(0,100,100,desktop,delta) && delta.X==0);
    tracker.Clear();assert(tracker.Absolute(1,600,800,desktop,delta) && delta.X==0);
    for(auto changed:{RawMouseDesktop{-1920,0,1920,1080,true},RawMouseDesktop{0,-200,1920,1080,false},
        RawMouseDesktop{0,0,3840,2160,false},RawMouseDesktop{0,0,1920,1080,true}})
    {
        assert(tracker.Absolute(1,50000,50000,changed,delta) && delta.X==0 && delta.Y==0);
    }
    tracker.Clear();assert(tracker.Absolute(7,0,0,desktop,delta));
    for(int32_t bad:{-1,65536,std::numeric_limits<int32_t>::max(),std::numeric_limits<int32_t>::min()})
    {
        delta={1,2};assert(!tracker.Absolute(7,bad,0,desktop,delta) && delta.X==0 && delta.Y==0);
        assert(!tracker.Absolute(7,0,bad,desktop,delta));
    }
    for(auto bad:{RawMouseDesktop{0,0,0,20,false},RawMouseDesktop{0,0,20,-1,false}})
        assert(!tracker.Absolute(7,1000,1000,bad,delta));
    assert(tracker.Absolute(7,65535,65535,desktop,delta) && delta.X==1919 && delta.Y==1079);
    tracker.Clear();
    for(uintptr_t device=1;device<=RawMouseMotionTracker::Capacity;++device)
        assert(tracker.Absolute(device,0,0,desktop,delta) && delta.X==0);
    assert(tracker.Absolute(100,60000,60000,desktop,delta) && delta.X==0); // 最初のslotをevict。
    assert(tracker.Absolute(1,65535,65535,desktop,delta) && delta.X==0); // evict済みdeviceはseed。
    assert(tracker.Absolute(3,65535,65535,desktop,delta) && delta.X==1919); // 別deviceの履歴は維持。
    RawMouseDesktop onePixel{0,0,1,1,false};
    assert(tracker.Absolute(9,0,0,onePixel,delta));assert(tracker.Absolute(9,65535,65535,onePixel,delta) && delta.X==0 && delta.Y==0);
    RawMouseDesktop huge{std::numeric_limits<int32_t>::min(),0,std::numeric_limits<int32_t>::max(),std::numeric_limits<int32_t>::max(),true};
    assert(tracker.Absolute(9,0,0,huge,delta));assert(tracker.Absolute(9,65535,65535,huge,delta));
    assert(std::isfinite(delta.X) && std::isfinite(delta.Y) && delta.X>2.0e9f);
    tracker.Clear();
    int32_t previousX=0,previousY=0;assert(tracker.Absolute(0,0,0,desktop,delta));
    for(int i=1;i<=10000;++i)
    {
        const int32_t x=(i*347)%65536,y=(i*571)%65536;
        assert(tracker.Absolute(0,x,y,desktop,delta));
        const double expectedX=(double(x)-previousX)*1919/65535,expectedY=(double(y)-previousY)*1079/65535;
        assert(std::abs(delta.X-expectedX)<0.00013 && std::abs(delta.Y-expectedY)<0.00013);
        previousX=x;previousY=y;
    }
    std::cout << "RawMouseMotionTrackerTest passed\n";
    return 0;
}
