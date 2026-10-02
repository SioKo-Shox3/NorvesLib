#pragma once

#include "XInputStateConversion.h"

namespace NorvesLib::Core::Input
{
    enum class EXInputReadStatus : uint8_t
    {
        Connected, Disconnected, Error
    };
    struct XInputReadResult
    {
        EXInputReadStatus Status = EXInputReadStatus::Disconnected;
        uint32_t NativeError = 0;
    };
    // native adapterだけがWindows SDKへ依存する。rawの有効性はConnected時に限る。
    class IXInputApi
    {
    public:
        virtual ~IXInputApi() = default;
        virtual XInputReadResult ReadState(uint8_t slot, XInputRawGamepadState& raw) noexcept = 0;
    };
} // namespace NorvesLib::Core::Input
