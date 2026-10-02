#include "Platform/Windows/WindowsKeyRepeatGate.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using NorvesLib::Core::Platform::WindowsKeyRepeatGate;
int main()
{
    WindowsKeyRepeatGate gate;
    for(uint32_t key=0;key<256;++key)
    {
        assert(!gate.CanTranslate(key,true) && !gate.AcceptPress(key,true));
        assert(gate.CanTranslate(key,false) && gate.AcceptPress(key,false));
        assert(gate.CanTranslate(key,true) && gate.AcceptPress(key,true));
        gate.Release(key);assert(!gate.CanTranslate(key,true));
    }
    // VK_PROCESSKEY/OEM等もKeyCodeへの変換に依存せず追跡する。
    assert(gate.AcceptPress(0xe5,false) && gate.AcceptPress(0xba,false));
    gate.Clear();assert(!gate.CanTranslate(0xe5,true) && !gate.CanTranslate(0xba,true));
    gate.Clear();
    assert(!gate.CanTranslate(256,false) && !gate.AcceptPress(0xffffffffu,false));
    gate.Release(0xffffffffu);assert(!gate.CanTranslate(0,true));
    assert(gate.AcceptPress(0x41,false));assert(!gate.CanTranslate(0x42,true));
    // Translateは参照だけ。Dispatchで実keydownを受理するまで履歴を作らない。
    assert(gate.CanTranslate(0x42,false));assert(!gate.CanTranslate(0x42,true));
    std::cout << "WindowsKeyRepeatGateTest passed\n";
    return 0;
}
