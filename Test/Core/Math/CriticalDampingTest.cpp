#include "Math/CriticalDamping.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib::Math;
int main()
{
    float p = 10, v = 0;
    assert(TryCriticalDamp(p, v, 0, .2f, .2f));
    assert(std::fabs(p - 5) < 1e-5);
    float referenceP = 12, referenceV = -3;
    assert(TryCriticalDamp(referenceP, referenceV, 2, .35f, 2));
    for (int rate : {30, 60, 144})
    {
        float x = 12, speed = -3;
        for (int i = 0; i < rate * 2; ++i)
            assert(TryCriticalDamp(x, speed, 2, .35f, 1.f / rate));
        assert(std::fabs(x - referenceP) < 2e-5);
        assert(std::fabs(speed - referenceV) < 2e-5);
    }
    p = 1;
    v = 4;
    assert(TryCriticalDamp(p, v, 8, 0, 0) && p == 1 && v == 4);
    assert(TryCriticalDamp(p, v, 8, 0, 1) && p == 8 && v == 0);
    p = 1;
    v = 4;
    const float inf = std::numeric_limits<float>::infinity();
    assert(!TryCriticalDamp(p, v, 8, -1, 1) && p == 1 && v == 4);
    assert(!TryCriticalDamp(p, v, 8, 1, -1) && p == 1 && v == 4);
    assert(!TryCriticalDamp(p, v, inf, 1, 1) && p == 1 && v == 4);
    assert(!TryCriticalDamp(p, v, 8, 1, inf) && p == 1 && v == 4);
    assert(TryCriticalDamp(p, v, 8, std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max()) &&
           p == 8 && v == 0);
    Vector3 position(1, 2, 3), velocity(4, 5, 6);
    assert(!TryCriticalDamp(position, velocity, Vector3(8), Vector3(.2f, -1, .2f), .1f));
    assert(position == Vector3(1, 2, 3) && velocity == Vector3(4, 5, 6));
    assert(TryCriticalDamp(position, velocity, Vector3(8), Vector3(.1f, .3f, .1f), .1f));
    assert(position.x != position.y);
    // 大きな位置誤差でも、指数だけで早期打ち切りせず解析値を保つ。
    p = 1e38f;
    v = 0;
    assert(TryCriticalDamp(p, v, 0, 1, static_cast<float>(80 / 1.6783469900166605)));
    assert(p > 1e4f && std::isfinite(v));
    p = 1;
    v = 0;
    assert(TryCriticalDamp(p, v, 1e20f, 1, 1e-10f));
    assert(std::fabs(p - 2.408424377f) < 1e-5f);
    assert(!TryCriticalDamp(p, p, 0, 1, 1));
    std::cout << "CriticalDampingTest PASS\n";
    return 0;
}
