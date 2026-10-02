#pragma once

#include "Container/PointerTypes.h"
#include "Input/IInputDevice.h"
#include "XInputPollingState.h"

namespace NorvesLib::Core::Input
{
    // APIを所有する入力源。全操作はGameThread、配送callbackから再入/破棄しない。
    // InputSystemはPoll中だけ借用する。Shutdownは通知しないためownerが操作取消を行う。
    class XInputDevice final : public IInputDevice
    {
    public:
        static Container::TUniquePtr<XInputDevice> Create(Container::TUniquePtr<IXInputApi> api)
        {
            if (!api)
            {
                return {};
            }
            return Container::TUniquePtr<XInputDevice>(new XInputDevice(std::move(api)));
        }
        ~XInputDevice() override
        {
            Shutdown();
        }
        XInputDevice(const XInputDevice&) = delete;
        XInputDevice& operator=(const XInputDevice&) = delete;
        XInputDevice(XInputDevice&&) = delete;
        XInputDevice& operator=(XInputDevice&&) = delete;

        bool Initialize() override
        {
            m_State.Initialize();
            return true;
        }
        void Shutdown() noexcept override
        {
            m_State.Shutdown();
        }
        void SetFocused(bool focused) noexcept override
        {
            m_State.SetFocused(focused);
        }
        bool ProvidesGamepadState() const noexcept override
        {
            return true;
        }
        void PollEvents(InputSystem& system) override;
        bool PollEvents(InputSystem& system, double unscaledTimeSeconds) override;

    private:
        explicit XInputDevice(Container::TUniquePtr<IXInputApi> api)
            : m_Api(std::move(api)), m_State(*m_Api)
        {
        }
        // 宣言順でstateの借用先を先に生成し、後から破棄する。
        Container::TUniquePtr<IXInputApi> m_Api;
        XInputPollingState m_State;
    };
} // namespace NorvesLib::Core::Input
