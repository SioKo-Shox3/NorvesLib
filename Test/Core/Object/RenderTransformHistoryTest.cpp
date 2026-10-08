#include "Scene/RenderTransformHistory.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib;
using namespace NorvesLib::Core::Scene;
int main()
{
    RenderTransformHistory history;
    Math::Transform out(Math::Vector3(99, 0, 0));
    assert(!history.Evaluate(.5f, out) && out.position.x == 99);
    Math::Transform start(Math::Vector3(10, 0, 0));
    assert(history.Prepare(start) && history.Evaluate(.5f, out) && out == start);
    auto finish = start;
    finish.position.x = 14;
    finish.scale = {3, 2, 1};
    finish.rotation = Math::Quaternion(Math::Vector3::UnitY, 1.570796326f);
    assert(history.Capture(finish) && history.Prepare(finish));
    assert(history.Evaluate(.25f, out) && out.position.x == 11 && out.scale.x == 1.5f);
    assert(history.Evaluate(.5f, out) && out.position.x == 12);
    auto forward = out.rotation * Math::Vector3::UnitZ;
    assert(std::fabs(forward.x - .70710678f) < 1e-5 && std::fabs(forward.z - .70710678f) < 1e-5);
    assert(history.Evaluate(0, out) && out == start);
    assert(history.Evaluate(1, out) && out == finish);
    auto sign = finish;
    sign.rotation = {-finish.rotation.x, -finish.rotation.y, -finish.rotation.z, -finish.rotation.w};
    assert(history.Capture(sign) && history.Evaluate(.5f, out));
    forward = out.rotation * Math::Vector3::UnitZ;
    assert(std::fabs(forward.x - 1) < 1e-5);
    auto teleport = finish;
    teleport.position.x = 100;
    assert(history.Prepare(teleport) && history.Evaluate(0, out) && out == teleport);
    history.Clear();
    assert(history.Capture(start) && history.Evaluate(0, out) && out == start);
    out.position.x = 77;
    assert(!history.Evaluate(-1, out) && out.position.x == 77);
    assert(!history.Evaluate(std::numeric_limits<float>::infinity(), out) && out.position.x == 77);
    auto invalid = start;
    invalid.rotation.w = 2;
    assert(!history.Capture(invalid) && history.GetCurrent() == start);
    invalid = start;
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    assert(!history.Prepare(invalid) && history.GetCurrent() == start);
    auto huge = start;
    huge.position.x = std::numeric_limits<float>::max();
    assert(history.Reset(huge));
    huge.position.x = -huge.position.x;
    assert(history.Capture(huge) && history.Evaluate(.5f, out) && out.position.x == 0);
    std::cout << "RenderTransformHistoryTest PASS\n";
    return 0;
}
