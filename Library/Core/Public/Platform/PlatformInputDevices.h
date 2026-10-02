#pragma once

#include "Container/PointerTypes.h"
#include "Input/IInputDevice.h"

namespace NorvesLib::Core::Platform
{
    // 未初期化のplatform標準padを生成する。生成だけでは入力APIを呼ばない。
    // WindowsはXInputを使用。呼出側が所有し、停止後に破棄する。
    Container::TUniquePtr<Input::IInputDevice> CreateGamepadDevice();
} // namespace NorvesLib::Core::Platform
