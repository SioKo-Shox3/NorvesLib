#include "Input/InputButtonState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Input;

namespace
{
    void TestHoldAndFixedPress()
    {
        InputButtonState button;
        assert(button.SetTiming({0.25,0.5,0.75}));
        button.BeginFrame();
        assert(button.AdvanceTo(1.0));
        button.SetDown(true);
        assert(button.GetState().Pressed && button.GetState().Held && button.GetState().HeldDuration==0);
        assert(button.HasPendingFixedPress());
        button.BeginFrame();
        button.SetDown(true); // repeat/二つ目bindingは新しいpressではない。
        assert(!button.GetState().Pressed && button.GetState().Held);
        assert(button.AdvanceTo(1.5));
        assert(button.GetState().HeldDuration==0.5 && !button.GetState().Hold);
        button.BeginFrame();
        assert(button.AdvanceTo(1.75));
        assert(button.GetState().Hold && button.GetState().HoldStarted);
        assert(button.HasPendingFixedPress()); // 0fixedのframeを跨いでも失わない。
        button.BeginFrame();
        assert(button.AdvanceTo(2.0));
        assert(button.GetState().Hold && !button.GetState().HoldStarted);
        button.SetDown(false);
        assert(button.GetState().Released && !button.GetState().Held && !button.GetState().Hold);
        assert(button.GetState().HeldDuration==0 && button.GetState().ReleasedHeldDuration==1.0);
        assert(!button.GetState().Tap && !button.GetState().DoubleTap);
        assert(button.ConsumeFixedPress());
        assert(!button.ConsumeFixedPress());
        button.BeginFrame();
        button.SetDown(false);
        assert(!button.GetState().Released && button.GetState().ReleasedHeldDuration==0);
    }

    void TestTapAndDoubleTap()
    {
        InputButtonState button;
        assert(button.SetTiming({0.25,0.5,0.75}));
        assert(button.AdvanceTo(1.0));
        button.SetDown(true);
        assert(button.AdvanceTo(1.25));
        button.SetDown(false); // Tapのinclusive境界
        assert(button.GetState().Pressed && button.GetState().Released && button.GetState().Tap);
        assert(!button.GetState().Held && !button.GetState().DoubleTap);
        button.BeginFrame();
        assert(button.AdvanceTo(1.5));
        button.SetDown(true);
        assert(button.AdvanceTo(1.75));
        button.SetDown(false); // release間gap 0.5 のinclusive境界
        assert(button.GetState().Tap && button.GetState().DoubleTap);
        button.BeginFrame();
        assert(button.AdvanceTo(1.875));
        button.SetDown(true);
        button.SetDown(false); // 同frameの短い入力、3tap目は前の組と重複しない。
        assert(button.GetState().Pressed && button.GetState().Released && button.GetState().Tap);
        assert(!button.GetState().DoubleTap);
        button.BeginFrame();
        assert(button.AdvanceTo(2.0));
        button.SetDown(true); button.SetDown(false);
        assert(button.GetState().DoubleTap);
        assert(button.ConsumeFixedPress() && !button.ConsumeFixedPress()); // 待機pressはboolとして合流

        InputButtonState outside;
        assert(outside.SetTiming({0.25,0.5,0.75}));
        outside.SetDown(true);
        assert(outside.AdvanceTo(0.250001));
        outside.SetDown(false);
        assert(!outside.GetState().Tap);
        outside.BeginFrame();
        outside.SetDown(true); outside.SetDown(false);
        outside.BeginFrame();
        assert(outside.AdvanceTo(0.750002));
        outside.SetDown(true); outside.SetDown(false);
        assert(outside.GetState().Tap && !outside.GetState().DoubleTap);

        InputButtonState heldNotTap;
        assert(heldNotTap.SetTiming({1.0,0.5,0.5}));
        heldNotTap.SetDown(true);
        assert(heldNotTap.AdvanceTo(0.5));
        heldNotTap.SetDown(false);
        assert(heldNotTap.GetState().HoldStarted && !heldNotTap.GetState().Tap);
    }

    void TestAggregateAndCancel()
    {
        InputButtonState button;
        bool a=true,b=false;
        button.SetDown(a||b);
        assert(button.GetState().Pressed);
        button.BeginFrame();
        b=true;button.SetDown(a||b);
        assert(button.GetState().Held && !button.GetState().Pressed);
        a=false;button.SetDown(a||b);
        assert(button.GetState().Held && !button.GetState().Released);
        b=false;button.SetDown(a||b);
        assert(button.GetState().Released);
        button.BeginFrame();
        assert(button.AdvanceTo(1));
        button.SetDown(true);
        button.Cancel();
        button.Cancel();
        assert(button.GetState().Released && !button.GetState().Held && !button.GetState().Pressed);
        assert(!button.GetState().Tap && !button.GetState().DoubleTap && !button.GetState().HoldStarted);
        assert(!button.ConsumeFixedPress());
        button.BeginFrame();
        assert(!button.GetState().Released);
        button.SetDown(true);button.SetDown(false);
        assert(button.GetState().Tap && !button.GetState().DoubleTap);
        assert(button.ConsumeFixedPress());
    }

    void TestDecimalDeadlinesAndOverflow()
    {
        InputButtonState tap;
        assert(tap.AdvanceTo(2.0)); tap.SetDown(true);
        assert(tap.AdvanceTo(2.0 + 0.2)); tap.SetDown(false);
        assert(tap.GetState().Tap);

        InputButtonState pair;
        assert(pair.AdvanceTo(1.0)); pair.SetDown(true); pair.SetDown(false);
        pair.BeginFrame();
        assert(pair.AdvanceTo(1.0 + 0.3)); pair.SetDown(true); pair.SetDown(false);
        assert(pair.GetState().DoubleTap);

        InputButtonState hold;
        assert(hold.SetTiming({0.2,0.3,0.2}));
        assert(hold.AdvanceTo(1.0)); hold.SetDown(true);
        const double deadline = 1.0 + 0.2;
        assert(hold.AdvanceTo(std::nextafter(deadline, 1.0)));
        assert(!hold.GetState().Hold);
        assert(hold.AdvanceTo(deadline));
        assert(hold.GetState().Hold && hold.GetState().HoldStarted);

        InputButtonState outside;
        assert(outside.AdvanceTo(2.0)); outside.SetDown(true);
        assert(outside.AdvanceTo(std::nextafter(2.0 + 0.2, 3.0))); outside.SetDown(false);
        assert(!outside.GetState().Tap);

        InputButtonState tiny;
        assert(tiny.SetTiming({0,0,std::numeric_limits<double>::denorm_min()}));
        assert(tiny.AdvanceTo(1.0)); tiny.SetDown(true);
        assert(tiny.AdvanceTo(1.0));
        assert(!tiny.GetState().Hold); // deadline加算が同じ値へ丸められても0経過でHoldにしない。
        assert(tiny.AdvanceTo(std::nextafter(1.0,2.0)) && tiny.GetState().Hold);

        InputButtonState overflow;
        const double max=std::numeric_limits<double>::max();
        assert(overflow.SetTiming({max,max,max}));
        assert(overflow.AdvanceTo(max/2)); overflow.SetDown(true);
        assert(overflow.AdvanceTo(max));
        assert(!overflow.GetState().Hold && std::isfinite(overflow.GetState().HeldDuration));
        overflow.SetDown(false);
        assert(overflow.GetState().Tap);
        overflow.BeginFrame();
        overflow.SetDown(true); overflow.SetDown(false);
        assert(overflow.GetState().DoubleTap);
    }

    void TestInvalidArgumentsDoNotMutate()
    {
        const double nan=std::numeric_limits<double>::quiet_NaN();
        const double inf=std::numeric_limits<double>::infinity();
        InputButtonState button;
        const auto original=button.GetTiming();
        for(int index=0;index<7;++index)
        {
            auto invalid=original;
            if(index==0)invalid.TapMaxSeconds=-1;
            if(index==1)invalid.TapMaxSeconds=nan;
            if(index==2)invalid.DoubleTapMaxGapSeconds=-1;
            if(index==3)invalid.DoubleTapMaxGapSeconds=inf;
            if(index==4)invalid.HoldSeconds=0;
            if(index==5)invalid.HoldSeconds=-1;
            if(index==6)invalid.HoldSeconds=nan;
            assert(!button.SetTiming(invalid));
            assert(button.GetTiming().TapMaxSeconds==original.TapMaxSeconds);
            assert(button.GetTiming().DoubleTapMaxGapSeconds==original.DoubleTapMaxGapSeconds);
            assert(button.GetTiming().HoldSeconds==original.HoldSeconds);
        }
        assert(button.AdvanceTo(2));
        button.SetDown(true);
        assert(button.AdvanceTo(2.125));
        for(double invalid : {nan,inf,-inf,-1.0,2.0})
        {
            assert(!button.AdvanceTo(invalid));
            assert(button.GetTime()==2.125 && button.GetState().HeldDuration==0.125);
            assert(button.GetState().Pressed && button.GetState().Held && !button.GetState().Hold);
            assert(button.HasPendingFixedPress());
        }
        assert(!button.SetTiming({0.1,0.2,0.3}));
        assert(button.GetTiming().HoldSeconds==original.HoldSeconds);
        assert(button.AdvanceTo(std::numeric_limits<double>::max()));
        assert(button.GetState().Hold && std::isfinite(button.GetState().HeldDuration));
        button.SetDown(false);
        assert(button.GetState().Released && !button.GetState().Tap);
        button.Cancel();
        assert(!button.HasPendingFixedPress());
    }
}

namespace
{
    void TestFixedEventFifo()
    {
        InputButtonState button;
        InputButtonEvent event;
        button.SetDown(true);
        button.SetDown(false);
        assert(!button.ConsumeFixedEvent(event));
        button.SetFixedEventCapture(true);
        assert(button.AdvanceTo(1));
        button.SetDown(true);
        button.SetDown(true);
        assert(button.AdvanceTo(1.01));
        button.SetDown(false);
        button.BeginFrame();
        button.BeginFrame();
        button.SetDown(true);
        button.SetDown(false);
        for (unsigned i = 0; i < 4; ++i)
        {
            assert(button.ConsumeFixedEvent(event));
            assert(event.Type == (i % 2 == 0 ? EInputButtonEventType::Pressed : EInputButtonEventType::Released));
            if (i == 1)
                assert(std::fabs(event.HeldDuration - .01) < 1e-12);
        }
        event.UnscaledTimeSeconds = 123;
        assert(!button.ConsumeFixedEvent(event) && event.UnscaledTimeSeconds == 123);
        button.SetDown(true);
        button.Cancel();
        assert(!button.ConsumeFixedEvent(event));
        assert(button.IsFixedEventCaptureEnabled());
        button.SetDown(true);
        button.SetDown(false);
        button.SetFixedEventCapture(false);
        assert(!button.ConsumeFixedEvent(event));
        button.SetFixedEventCapture(true);
        button.SetDown(true);
        button.SetDown(false);
        assert(button.ConsumeFixedEvent(event));
        InputButtonState moved(std::move(button));
        assert(!button.ConsumeFixedEvent(event));
        button.SetDown(true);
        assert(button.ConsumeFixedEvent(event) && event.Type == EInputButtonEventType::Pressed);
        assert(moved.ConsumeFixedEvent(event) && event.Type == EInputButtonEventType::Released);
        button.Cancel();
        for (int i = 0; i < 1000; ++i)
        {
            button.SetDown(true);
            button.SetDown(false);
            button.BeginFrame();
        }
        for (int i = 0; i < 2000; ++i)
            assert(button.ConsumeFixedEvent(event));
        assert(!button.ConsumeFixedEvent(event));
    }
} // namespace

int main()
{
    TestFixedEventFifo();
    TestHoldAndFixedPress();
    TestTapAndDoubleTap();
    TestAggregateAndCancel();
    TestInvalidArgumentsDoNotMutate();
    TestDecimalDeadlinesAndOverflow();
    std::cout << "InputButtonStateTest passed\n";
    return 0;
}
