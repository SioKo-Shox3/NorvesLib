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
    struct XInputWriteResult
    {
        bool Accepted = false;
        // 0はnative error情報なし。未対応実装もAccepted=falseを返す。
        uint32_t NativeError = 0;
    };
    // native adapterだけがWindows SDKへ依存する。rawの有効性はConnected時に限る。
    class IXInputApi
    {
    public:
        virtual ~IXInputApi() = default;
        virtual XInputReadResult ReadState(uint8_t slot, XInputRawGamepadState& raw) noexcept = 0;
        virtual XInputWriteResult WriteVibration(uint8_t slot, const XInputMotorState& motors) noexcept
        {
            (void)slot;
            (void)motors;
            return {};
        }
    };
} // namespace NorvesLib::Core::Input
