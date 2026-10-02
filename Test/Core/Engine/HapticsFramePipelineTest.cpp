#include "Engine/Engine.h"
#include "Engine/ApplicationProcessor.h"
#include "Engine/FixedStepScheduler.h"
#include "Application/ApplicationHandlerBase.h"
#include "Application/IWindow.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

namespace NorvesLib::Core::Engine
{
    struct ApplicationHapticsTestAccess
    {
        static bool Update(ApplicationProcessor& processor, int64_t delta)
        {
            return processor.UpdateHapticsFrame(delta);
        }
        static void Connect(ApplicationProcessor& processor, Container::TSharedPtr<NorvesLib::IWindow> window)
        {
            processor.ConnectInputWindow(std::move(window));
        }
        static void Disconnect(ApplicationProcessor& processor) { processor.DisconnectInputWindow(); }
        static bool Focus(ApplicationProcessor& processor) { return processor.ApplyPendingInputDeviceFocus(); }
        static void Start(ApplicationProcessor& processor) { processor.m_FixedStepScheduler->BeginRun(); }
        static void End(ApplicationProcessor& processor) { processor.m_FixedStepScheduler->EndRun(); }
        static void Frame(ApplicationProcessor& processor, Application::IApplicationHandler* handler)
        {
            processor.TickSimulationAndHaptics(1'000'000, .001f, handler);
        }
    };
}
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace Eng = NorvesLib::Core::Engine;
namespace
{
    class Provider final : public IInputDevice
    {
    public:
        bool Initialize() override
        {
            Started = true;
            ++Starts;
            return true;
        }
        void Shutdown() override
        {
            if (!TryShutdown())
            {
                throw 9;
            }
        }
        bool TryShutdown() noexcept override
        {
            ++Stops;
            Record(100);
            bool success = true;
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
            {
                if ((Pending[slot] || !IsZeroHapticsOutput(Applied[slot])) && !Send(slot,0,0))
                {
                    success = false;
                }
            }
            Started = false;
            return success;
        }
        bool ProvidesGamepadState() const noexcept override { return true; }
        void SetFocused(bool focused) noexcept override
        {
            Focused = focused;
            if (!focused)
            {
                Record(200);
                for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
                {
                    if (Pending[slot] || !IsZeroHapticsOutput(Applied[slot]))
                    {
                        (void)Send(slot,0,0);
                    }
                }
            }
        }
        void PollEvents(InputSystem& system) override
        {
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
            {
                GamepadState state;
                state.Connected = true;
                state.Buttons = InjectHeld ? static_cast<uint16_t>(GamepadButton::A) : 0;
                assert(system.InjectGamepadState(slot,state, Focused ? EGamepadSampleMode::Baseline : EGamepadSampleMode::Background));
            }
        }
        bool SetVibration(uint8_t slot, float low, float high) noexcept override
        {
            assert(slot < GamepadSlotCount);
            if (!Started || (!Focused && (low != 0 || high != 0)))
            {
                return false;
            }
            if (Owner)
            {
                ++GuardChecks;
                assert(!Owner->UpdateHaptics(0));
                assert(!Owner->FlushHaptics());
                assert(!Owner->SetHapticsPaused(true));
                assert(!Owner->SetInputDevicesFocused(false));
                assert(!Owner->ShutdownInputDevices());
                assert(!Owner->AddInputDevice(Container::MakeUnique<Provider>()));
            }
            return Send(slot,low,high);
        }
        bool Send(uint8_t slot, float low, float high) noexcept
        {
            ++Writes[slot];
            Record((low == 0 && high == 0) ? slot : 10 + slot);
            if (Fail)
            {
                Pending[slot] = true;
                return false;
            }
            Applied[slot] = {low,high};
            Pending[slot] = false;
            return true;
        }
        void Record(int event) noexcept
        {
            assert(EventCount < 512);
            Events[EventCount++] = event;
        }
        int Events[512]{};
        int EventCount = 0, GuardChecks = 0;
        Eng::Engine* Owner = nullptr;
        bool Started = false, Focused = true, Fail = false, InjectHeld = false;
        bool Pending[GamepadSlotCount]{};
        HapticsOutput Applied[GamepadSlotCount]{};
        int Writes[GamepadSlotCount]{};
        int Starts = 0, Stops = 0;
    };
    class FocusWindow final : public NorvesLib::IWindow
    {
    public:
        bool Create(const Container::String&, int, int) override { return true; }
        void Destroy() override {}
        void Show() override { Emit(true); }
        void Hide() override { Emit(false); }
        void SetTitle(const Container::String&) override {}
        void Resize(int,int) override {}
        bool IsActive() const override { return Focused; }
        bool IsInputFocused() const override { return Focused; }
        bool SetRawMouseEnabled(bool) noexcept override { return true; }
        Platform::NativeWindowHandle GetNativeHandle() const override { return {}; }
        void Emit(bool focused)
        {
            Focused = focused;
            NotifyInputFocusChanged(focused);
        }
        bool Focused = true;
    };
    class FocusDuringSample final : public IInputController
    {
    public:
        bool OnGamepadSample(const GamepadSampleEvent& event) override
        {
            if (Armed && event.Slot == 0)
            {
                Armed = false;
                InSample = true;
                Window->Hide();
                if (Regain)
                {
                    Window->Show();
                }
                assert(Device->Focused);
                InSample = false;
            }
            return false;
        }
        void OnInputReset() override
        {
            assert(!InSample);
            ++Resets;
            // 本来禁止のobserver内window変更でも、後発focusを上書きしない防御的回帰。
            if (RegainOnReset)
            {
                RegainOnReset = false;
                Window->Show();
            }
        }
        void OnInputFocusChanged(bool focused) override
        {
            assert(!InSample);
            ++FocusEvents;
            if (!focused && RegainOnFocusNotification)
            {
                RegainOnFocusNotification = false;
                Window->Show();
            }
        }
        const char* DebugName() const override { return "FocusDuringSample"; }
        FocusWindow* Window = nullptr;
        Provider* Device = nullptr;
        bool Armed = false, Regain = false, InSample = false;
        bool RegainOnReset = false, RegainOnFocusNotification = false;
        int Resets = 0, FocusEvents = 0;
    };
    class DirectOutput final : public IHapticsOutput
    {
    public:
        DirectOutput(Eng::Engine& engine, Provider& provider) : Owner(engine), Device(provider) {}
        bool IsConnected(uint8_t slot) const noexcept override
        {
            return Owner.GetInputSystem().GetState().GetGamepadState(slot).Connected;
        }
        bool SetVibration(uint8_t slot, float low, float high) noexcept override
        {
            return Device.SetVibration(slot,low,high);
        }
        Eng::Engine& Owner;
        Provider& Device;
    };
    class PauseHandler final : public Application::ApplicationHandlerBase
    {
    public:
        bool ShouldAdvanceSimulation() const override
        {
            ++GateCalls;
            return Advance;
        }
        void OnLateUpdate(float) override
        {
            ++LateCalls;
            // pause解除はsimulation前、効果の出力はsimulation後でなければ成立しない。
            assert(IsZeroHapticsOutput(Device->Applied[0]));
            assert(Eng::GEngine->GetHapticsService().Play("Run"_id,0));
        }
        Provider* Device = nullptr;
        bool Advance = true;
        mutable int GateCalls = 0;
        int LateCalls = 0;
    };
    class ThrowingHandler final : public Application::ApplicationHandlerBase
    {
    public:
        void OnUpdate(float) override
        {
            auto& engine = *Eng::GEngine;
            assert(engine.GetHapticsService().Play("Run"_id,0));
            assert(engine.FlushHaptics());
            throw 42;
        }
    };
    HapticsEffectDefinition Effect(Identity id, bool loop)
    {
        HapticsEffectDefinition effect;
        effect.Id = id;
        effect.Duration = 1;
        effect.Loop = loop;
        effect.Low.push_back({0,1});
        effect.High.push_back({0,.5f});
        return effect;
    }
}
int main()
{
    auto* previous = Eng::GEngine;
    Eng::GEngine = nullptr;
    {
        Eng::ApplicationProcessor processor;
        assert(!Eng::ApplicationHapticsTestAccess::Update(processor,0));
        auto engine = Container::MakeUnique<Eng::Engine>();
        Eng::GEngine = engine.get();
        auto provider = Container::MakeUnique<Provider>();
        auto* view = provider.get();
        view->Owner = engine.get();
        assert(engine->AddInputDevice(std::move(provider)));
        auto& service = engine->GetHapticsService();
        HapticsEffectDefinition definitions[]{Effect("Run"_id,true),Effect("Hit"_id,false)};
        assert(service.Configure(definitions));
        assert(!engine->UpdateHaptics(0));
        assert(engine->InitializeInputDevices());
        assert(engine->PollInputDevices(0));
        assert(service.Play("Run"_id,0) && service.Play("Hit"_id,1));
        assert(service.Play("Run"_id,2) && service.Play("Hit"_id,3));
        assert(Eng::ApplicationHapticsTestAccess::Update(processor,0));
        assert(view->Applied[0].Low == .5f && view->Applied[1].High == .25f);
        assert(view->Applied[2].Low == .5f && view->Applied[3].High == .25f);
        const int oldWrites = view->Writes[0];
        assert(!Eng::ApplicationHapticsTestAccess::Update(processor,-1));
        assert(!engine->UpdateHaptics(std::numeric_limits<double>::quiet_NaN()));
        assert(view->Writes[0] == oldWrites);
        // 100msへclampせず実2秒で非loopが終わり、loopは継続する。
        assert(Eng::ApplicationHapticsTestAccess::Update(processor,2'000'000'000));
        assert(IsZeroHapticsOutput(view->Applied[1]) && IsZeroHapticsOutput(view->Applied[3]));
        assert(service.GetActiveVoiceCount() == 2);
        view->EventCount = 0;
        assert(engine->SetHapticsPaused(true));
        assert(view->EventCount == 2 && view->Events[0] == 0 && view->Events[1] == 2);
        assert(IsZeroHapticsOutput(view->Applied[0]) && service.GetActiveVoiceCount() == 0);
        assert(service.Play("Run"_id,0) == 0);
        assert(engine->SetHapticsPaused(false) && engine->UpdateHaptics(0));
        assert(IsZeroHapticsOutput(view->Applied[0]));
        assert(service.Play("Run"_id,0) && engine->FlushHaptics());
        view->EventCount = 0;
        assert(engine->SetInputDevicesFocused(false));
        assert(view->EventCount == 2 && view->Events[0] == 0 && view->Events[1] == 200);
        assert(!view->Focused && IsZeroHapticsOutput(view->Applied[0]));
        assert(service.Play("Run"_id,0) == 0);
        assert(engine->SetInputDevicesFocused(true));
        assert(service.Play("Run"_id,0) && engine->FlushHaptics());
        view->Fail = true;
        assert(!engine->SetHapticsPaused(true));
        assert(!engine->UpdateHaptics(0));
        view->Fail = false;
        assert(engine->UpdateHaptics(0));
        assert(IsZeroHapticsOutput(view->Applied[0]));
        assert(engine->SetHapticsPaused(false));
        assert(service.Play("Run"_id,0) && engine->FlushHaptics());
        view->Fail = true;
        view->EventCount = 0;
        assert(!engine->ShutdownInputDevices());
        assert(view->EventCount >= 3 && view->Events[0] == 0 && view->Events[1] == 100);
        assert(!engine->AreInputDevicesInitialized() && service.GetActiveVoiceCount() == 0);
        assert(!engine->InitializeInputDevices());
        view->Fail = false;
        assert(engine->ShutdownInputDevices());
        assert(engine->InitializeInputDevices() && engine->PollInputDevices(0));
        assert(engine->UpdateHaptics(0));
        assert(IsZeroHapticsOutput(view->Applied[0]) && service.GetActiveVoiceCount() == 0);
        assert(engine->ShutdownInputDevices());

        assert(engine->InitializeInputDevices() && engine->PollInputDevices(0));
        // 同じ実providerへ直結し、owner busyを立てずservice busyだけで拒否を検証する。
        DirectOutput direct(*engine,*view);
        const int oldGuards = view->GuardChecks;
        assert(service.Play("Run"_id,0,.7f) && service.Update(0,direct));
        assert(view->GuardChecks > oldGuards);
        assert(engine->SetHapticsPaused(true));
        PauseHandler pauseHandler;
        pauseHandler.Device = view;
        Eng::ApplicationHapticsTestAccess::Start(processor);
        Eng::ApplicationHapticsTestAccess::Frame(processor,&pauseHandler);
        assert(pauseHandler.GateCalls == 1 && pauseHandler.LateCalls == 1 && view->Applied[0].Low == .5f);
        pauseHandler.Advance = false;
        Eng::ApplicationHapticsTestAccess::Frame(processor,&pauseHandler);
        assert(pauseHandler.GateCalls == 2 && pauseHandler.LateCalls == 1 && IsZeroHapticsOutput(view->Applied[0]));
        pauseHandler.Advance = true;
        Eng::ApplicationHapticsTestAccess::Frame(processor,&pauseHandler);
        assert(pauseHandler.GateCalls == 3 && pauseHandler.LateCalls == 2 && view->Applied[0].Low == .5f);
        Eng::ApplicationHapticsTestAccess::End(processor);
        assert(service.StopAll() && engine->FlushHaptics());
        auto window = Container::MakeShared<FocusWindow>();
        engine->SetMainWindow(window);
        Eng::ApplicationHapticsTestAccess::Connect(processor,window);
        FocusDuringSample focusDuringSample;
        focusDuringSample.Window = window.get();
        focusDuringSample.Device = view;
        engine->GetInputRouter().RegisterController(&focusDuringSample,InputRouter::PriorityOverlay);
        view->InjectHeld = true;
        for (int caseIndex = 0; caseIndex < 2; ++caseIndex)
        {
            window->Show();
            assert(service.Play("Run"_id,0) && engine->FlushHaptics());
            focusDuringSample.Armed = true;
            focusDuringSample.Regain = caseIndex == 1;
            assert(engine->PollInputDevices(caseIndex + 1));
            assert(engine->GetInputSystem().GetState().IsGamepadButtonDown(3,GamepadButton::A));
            assert(Eng::ApplicationHapticsTestAccess::Focus(processor));
            assert(service.GetActiveVoiceCount() == 0 && IsZeroHapticsOutput(view->Applied[0]));
            assert(!engine->GetInputSystem().GetState().IsGamepadButtonDown(3,GamepadButton::A));
            assert(view->Focused == (caseIndex == 1));
            assert(engine->GetInputMapper().IsFocused() == (caseIndex == 1));
        }
        assert(focusDuringSample.Resets >= 2 && focusDuringSample.FocusEvents >= 3);
        // 適用途中の新focusは次batchへ残す。外側のfalseでtrueを失わない。
        for (int callbackCase = 0; callbackCase < 2; ++callbackCase)
        {
            window->Show();
            assert(service.Play("Run"_id,0) && engine->FlushHaptics());
            focusDuringSample.RegainOnReset = callbackCase == 0;
            focusDuringSample.RegainOnFocusNotification = callbackCase == 1;
            window->Hide();
            assert(window->Focused && engine->GetInputMapper().IsFocused());
            assert(Eng::ApplicationHapticsTestAccess::Focus(processor));
            assert(view->Focused && service.GetActiveVoiceCount() == 0 && IsZeroHapticsOutput(view->Applied[0]));
        }
        // 配送後、安全な地点でwindowを交換した場合は旧windowのRouter通知を持ち越さない。
        focusDuringSample.Armed = true;
        focusDuringSample.Regain = false;
        assert(service.Play("Run"_id,0) && engine->FlushHaptics());
        assert(engine->PollInputDevices(3));
        auto replacement = Container::MakeShared<FocusWindow>();
        engine->SetMainWindow(replacement);
        Eng::ApplicationHapticsTestAccess::Connect(processor,replacement);
        focusDuringSample.Window = replacement.get();
        assert(Eng::ApplicationHapticsTestAccess::Focus(processor));
        assert(view->Focused && service.GetActiveVoiceCount() == 0 && IsZeroHapticsOutput(view->Applied[0]));
        const int focusEventsAfterReplacement = focusDuringSample.FocusEvents;
        window->Show();
        window->Hide();
        assert(view->Focused && focusDuringSample.FocusEvents == focusEventsAfterReplacement);
        window = replacement;
        view->InjectHeld = false;
        engine->GetInputRouter().UnregisterController(&focusDuringSample);
        window->Show();
        Eng::ApplicationHapticsTestAccess::Disconnect(processor);
        engine->SetMainWindow({});
        assert(engine->SetInputDevicesFocused(true));
        assert(engine->ShutdownInputDevices());

        engine->SetApplicationHandler(Container::MakeShared<ThrowingHandler>());
        engine->SetRunning(true);
        for (int run = 0; run < 2; ++run)
        {
            try
            {
                (void)processor.Run();
                assert(false);
            }
            catch (int value)
            {
                assert(value == 42);
            }
            assert(!engine->AreInputDevicesInitialized());
            assert(service.GetActiveVoiceCount() == 0 && IsZeroHapticsOutput(view->Applied[0]));
        }
        engine.reset();
        Eng::GEngine = nullptr;
    }
    Eng::GEngine = previous;
    std::cout << "HapticsFramePipelineTest passed\n";
    return 0;
}
