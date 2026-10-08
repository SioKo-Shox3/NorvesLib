#include "Engine/Engine.h"
#include "Application/IApplication.h"
#include "Application/IWindow.h"
#include "Application/IApplicationHandler.h"
#include <cmath>

namespace NorvesLib::Core::Engine
{
    // 内部同期sinkは公開guardへ再入せず、開始済みの固定providerへだけ送る。
    class Engine::HapticsDeviceOutput final : public Input::IHapticsOutput
    {
    public:
        explicit HapticsDeviceOutput(Engine& engine) : m_Engine(engine)
        {
        }
        bool IsConnected(uint8_t slot) const noexcept override
        {
            return slot < Input::GamepadSlotCount && m_Engine.m_bInputDevicesStarted &&
                m_Engine.HasGamepadInputDevice() && m_Engine.m_InputSystem.GetState().GetGamepadState(slot).Connected;
        }
        bool SetVibration(uint8_t slot, float low, float high) noexcept override
        {
            if (!m_Engine.m_bInputDevicesStarted || slot >= Input::GamepadSlotCount)
            {
                return false;
            }
            for (auto& entry : m_Engine.m_InputDevices)
            {
                if (entry.bGamepadProvider && entry.bNeedsShutdown)
                {
                    return entry.Device->SetVibration(slot, low, high);
                }
            }
            return false;
        }
    private:
        Engine& m_Engine;
    };

    // グローバルエンジンインスタンスの実体
    Engine *GEngine = nullptr;

    Engine::Engine()
        : m_InputMapper(m_InputSystem.GetState()),
          m_InputDebugOverlay(m_InputSystem, m_InputRouter, m_InputMapper),
          m_InputRebindCapture(m_InputSystem, m_InputRouter, m_InputMapper)
    {
        m_InputSystem.SetRouter(&m_InputRouter);
        // 設定/active contextが無ければ入力を消費せず、旧controllerへ透過する。
        m_InputMapper.Attach(m_InputRouter);
        // 同一Engineの正本/配線と単一ownerがctorで確定している。
        (void)m_InputDebugOverlay.Attach();
        (void)m_InputRebindCapture.Attach();
    }

    Engine::~Engine()
    {
        (void)ShutdownInputDevices();
        m_InputRebindCapture.Detach();
        m_InputDebugOverlay.Detach();
        m_InputMapper.Detach();
        m_InputSystem.SetRouter(nullptr);
        // TUniquePtrにより自動的に解放される
        // m_GameModeStateMachineのデストラクタが呼ばれる
    }

    namespace
    {
        struct InputDeviceCallGuard
        {
            explicit InputDeviceCallGuard(bool& busy) : Busy(busy)
            {
                Busy = true;
            }
            ~InputDeviceCallGuard()
            {
                Busy = false;
            }
            bool& Busy;
        };
    }

    bool Engine::HasGamepadInputDevice() const noexcept
    {
        for (const auto& entry : m_InputDevices)
        {
            if (entry.bGamepadProvider)
            {
                return true;
            }
        }
        return false;
    }
    bool Engine::HasPendingInputDeviceShutdown() const noexcept
    {
        for (const auto& entry : m_InputDevices)
        {
            if (entry.bNeedsShutdown)
            {
                return true;
            }
        }
        return false;
    }
    bool Engine::AddInputDevice(Container::TUniquePtr<Input::IInputDevice> device)
    {
        if (!device || m_bInputDevicesBusy || m_HapticsService.IsBusy() || m_bInputDevicesStarted || HasPendingInputDeviceShutdown())
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        const bool gamepad = device->ProvidesGamepadState();
        if (gamepad && HasGamepadInputDevice())
        {
            return false;
        }
        device->SetFocused(m_bInputDevicesFocused);
        m_InputDevices.push_back({std::move(device), gamepad, false});
        return true;
    }
    bool Engine::InitializeInputDevices()
    {
        if (m_bInputDevicesBusy || m_HapticsService.IsBusy())
        {
            return false;
        }
        if (m_bInputDevicesStarted)
        {
            return true;
        }
        if (HasPendingInputDeviceShutdown())
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        m_bInputDevicesHaveTime = false;
        try
        {
            for (auto& entry : m_InputDevices)
            {
                // false/例外でも部分初期化資源の終了義務を保持する。
                entry.bNeedsShutdown = true;
                entry.Device->SetFocused(m_bInputDevicesFocused);
                if (!entry.Device->Initialize())
                {
                    (void)StopInputDevicesInternal();
                    return false;
                }
            }
        }
        catch (...)
        {
            (void)StopInputDevicesInternal();
            return false;
        }
        m_bInputDevicesStarted = true;
        (void)m_HapticsService.SetFocused(m_bInputDevicesFocused);
        (void)m_HapticsService.SetPaused(m_bHapticsPaused);
        return true;
    }
    bool Engine::PollInputDevices(double unscaledTimeSeconds)
    {
        if (m_bInputDevicesBusy || m_HapticsService.IsBusy() || !m_bInputDevicesStarted || !std::isfinite(unscaledTimeSeconds) ||
            unscaledTimeSeconds < 0 || (m_bInputDevicesHaveTime && unscaledTimeSeconds < m_InputDeviceTime))
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        m_InputDeviceTime = unscaledTimeSeconds;
        m_bInputDevicesHaveTime = true;
        bool success = true;
        for (auto& entry : m_InputDevices)
        {
            // falseでも他deviceを処理する。例外はRunのcleanupへ伝播する。
            if (!entry.Device->PollEvents(m_InputSystem, unscaledTimeSeconds))
            {
                success = false;
            }
        }
        return success;
    }
    bool Engine::SetInputDevicesFocused(bool focused) noexcept
    {
        if (m_bInputDevicesBusy || m_HapticsService.IsBusy())
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        m_bInputDevicesFocused = focused;
        m_InputSystem.SetInputFocused(focused);
        (void)m_HapticsService.SetFocused(focused);
        const bool stopped = focused || FlushHapticsInternal();
        for (auto& entry : m_InputDevices)
        {
            // service送信に失敗してもbackend自身の独立zeroを必ず試みる。
            entry.Device->SetFocused(focused);
        }
        return stopped;
    }
    bool Engine::StopInputDevicesInternal() noexcept
    {
        const bool hadActivity = m_bInputDevicesStarted || HasPendingInputDeviceShutdown();
        (void)m_HapticsService.StopAll();
        if (m_bInputDevicesStarted)
        {
            (void)FlushHapticsInternal();
        }
        // 最終停止結果は全slotを停止するbackendのTryShutdownを正とする。
        m_bInputDevicesStarted = false;
        m_bInputDevicesHaveTime = false;
        bool success = true;
        for (size_t index = m_InputDevices.size(); index > 0; --index)
        {
            auto& entry = m_InputDevices[index - 1];
            if (!entry.bNeedsShutdown)
            {
                continue;
            }
            if (entry.Device->TryShutdown())
            {
                entry.bNeedsShutdown = false;
            }
            else
            {
                // 残りを止め、失敗deviceの終了義務は次の呼出しまで保持する。
                success = false;
            }
        }
        if (hadActivity)
        {
            m_InputRebindCapture.Abort();
            m_InputMapper.CancelAll();
            m_InputSystem.DeferReleaseAll();
        }
        return success;
    }
    bool Engine::ShutdownInputDevices() noexcept
    {
        if (m_bInputDevicesBusy || m_HapticsService.IsBusy())
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        return StopInputDevicesInternal();
    }

    bool Engine::FlushHapticsInternal() noexcept
    {
        (void)m_HapticsService.SetFocused(m_bInputDevicesFocused);
        (void)m_HapticsService.SetPaused(m_bHapticsPaused);
        HapticsDeviceOutput output(*this);
        return m_HapticsService.FlushOutputs(output);
    }
    bool Engine::FlushHaptics() noexcept
    {
        if (m_bInputDevicesBusy || m_HapticsService.IsBusy())
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        return FlushHapticsInternal();
    }
    bool Engine::SetHapticsPaused(bool paused) noexcept
    {
        if (m_bInputDevicesBusy || m_HapticsService.IsBusy())
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        m_bHapticsPaused = paused;
        (void)m_HapticsService.SetPaused(paused);
        return !paused || FlushHapticsInternal();
    }
    bool Engine::UpdateHaptics(double unscaledDeltaSeconds) noexcept
    {
        if (m_bInputDevicesBusy || m_HapticsService.IsBusy() || !m_bInputDevicesStarted ||
            !std::isfinite(unscaledDeltaSeconds) || unscaledDeltaSeconds < 0)
        {
            return false;
        }
        InputDeviceCallGuard guard(m_bInputDevicesBusy);
        (void)m_HapticsService.SetFocused(m_bInputDevicesFocused);
        (void)m_HapticsService.SetPaused(m_bHapticsPaused);
        HapticsDeviceOutput output(*this);
        return m_HapticsService.Update(unscaledDeltaSeconds, output);
    }

} // namespace NorvesLib::Core::Engine
