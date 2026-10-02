#include "Engine/Engine.h"
#include "Engine/ApplicationProcessor.h"
#include "Application/ApplicationHandlerBase.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
namespace Eng = NorvesLib::Core::Engine;
namespace
{
    struct Record
    {
        int Events[64]{};
        int Count = 0;
        int Destroyed = 0;
        int Phase = 0;
        void Add(int value)
        {
            assert(Count < 64);
            Events[Count++] = value;
        }
    };
    class Device final : public IInputDevice
    {
    public:
        Device(Record& record, int id) : Log(record), Id(id)
        {
        }
        ~Device() override
        {
            ++Log.Destroyed;
        }
        bool Initialize() override
        {
            ++Initializes;
            Log.Add(10 + Id);
            CheckReentry();
            if (ThrowStart)
            {
                throw 1;
            }
            return !FailStart;
        }
        void Shutdown() override
        {
            ++Stops;
            Log.Add(20 + Id);
            CheckReentry();
            if (ThrowStop)
            {
                throw 2;
            }
        }
        void SetFocused(bool focused) noexcept override
        {
            Focused = focused;
        }
        bool ProvidesGamepadState() const noexcept override
        {
            return Pad;
        }
        void PollEvents(InputSystem& system) override
        {
            (void)PollEvents(system, 0);
        }
        bool PollEvents(InputSystem& system, double time) override
        {
            ++Polls;
            LastTime = time;
            CheckReentry();
            if (CheckPhase)
            {
                assert(Log.Phase == 1);
                Log.Phase = 2;
            }
            if (Pad)
            {
                GamepadState sample;
                sample.Connected = true;
                sample.Buttons = static_cast<uint16_t>(GamepadButton::A);
                assert(system.InjectGamepadState(0, sample,
                    Focused ? EGamepadSampleMode::Live : EGamepadSampleMode::Background));
            }
            if (ThrowPoll)
            {
                throw 3;
            }
            return !FailPoll;
        }
        void CheckReentry()
        {
            if (Owner)
            {
                assert(!Owner->InitializeInputDevices());
                assert(!Owner->PollInputDevices(0));
                assert(!Owner->ShutdownInputDevices());
                assert(!Owner->SetInputDevicesFocused(false));
                assert(!Owner->AddInputDevice(Container::MakeUnique<Device>(Log, 9)));
            }
        }
        Record& Log;
        int Id;
        int Initializes = 0, Stops = 0, Polls = 0;
        double LastTime = 0;
        bool Pad = false, Focused = true, FailStart = false, ThrowStart = false;
        bool ThrowStop = false, FailPoll = false, ThrowPoll = false, CheckPhase = false;
        Eng::Engine* Owner = nullptr;
    };
    class ResetObserver final : public IInputController
    {
    public:
        void OnInputReset() override
        {
            ++Resets;
        }
        const char* DebugName() const override
        {
            return "DeviceResetObserver";
        }
        int Resets = 0;
    };
    class RunDevicePlatform final : public IApplication
    {
    public:
        explicit RunDevicePlatform(Record& log) : Log(log)
        {
        }
        bool Initialize(const Container::VariableArray<Container::String>&) override { return true; }
        void Shutdown() override {}
        IWindow* GetMainWindow() override { return nullptr; }
        void RegisterWindow(Container::TSharedPtr<IWindow>) override {}
        void UnregisterWindow(Container::TSharedPtr<IWindow>) override {}
        void PumpMessages() override
        {
            Log.Phase = 1;
        }
        bool IsExitRequested() const override { return Exit; }
        int GetExitCode() const override { return 7; }
        Record& Log;
        bool Exit = false;
    };
    class Handler final : public Application::ApplicationHandlerBase
    {
    public:
        explicit Handler(Record& log) : Log(log)
        {
        }
        void OnUpdate(float) override
        {
            assert(Log.Phase == 2);
            assert(Eng::GEngine->GetInputSystem().GetState().IsGamepadButtonDown(0, GamepadButton::A));
            throw 42;
        }
        Record& Log;
    };
}
int main()
{
    {
        Record log;
        Eng::Engine engine;
        assert(!engine.AddInputDevice({}));
        assert(!engine.PollInputDevices(0));
        auto first = Container::MakeUnique<Device>(log, 0);
        auto second = Container::MakeUnique<Device>(log, 1);
        auto* a = first.get();
        auto* b = second.get();
        a->Pad = true;
        a->Owner = b->Owner = &engine;
        assert(engine.SetInputDevicesFocused(false));
        assert(engine.AddInputDevice(std::move(first)));
        assert(!a->Focused && a->Initializes == 0);
        auto duplicate = Container::MakeUnique<Device>(log, 2);
        duplicate->Pad = true;
        assert(!engine.AddInputDevice(std::move(duplicate)));
        assert(log.Destroyed == 1 && engine.HasGamepadInputDevice());
        assert(engine.AddInputDevice(std::move(second)));
        b->FailStart = true;
        assert(!engine.InitializeInputDevices());
        assert(!engine.AreInputDevicesInitialized());
        assert(log.Count == 4 && log.Events[0] == 10 && log.Events[1] == 11 &&
            log.Events[2] == 21 && log.Events[3] == 20);
        b->FailStart = false;
        b->ThrowStart = true;
        a->ThrowStop = true;
        assert(!engine.InitializeInputDevices());
        assert(a->Stops == 2 && b->Stops == 2);
        const int starts = a->Initializes;
        assert(!engine.InitializeInputDevices());
        assert(a->Initializes == starts);
        auto rejected = Container::MakeUnique<Device>(log, 3);
        assert(!engine.AddInputDevice(std::move(rejected)));
        a->ThrowStop = false;
        assert(engine.ShutdownInputDevices());
        assert(a->Stops == 3 && b->Stops == 2);
        b->ThrowStart = false;
        assert(engine.InitializeInputDevices());
        assert(engine.InitializeInputDevices());
        assert(a->Initializes == starts + 1);
        assert(engine.SetInputDevicesFocused(true));
        assert(a->Focused && b->Focused);
        a->FailPoll = true;
        assert(!engine.PollInputDevices(1));
        assert(a->Polls == 1 && b->Polls == 1);
        for (double time : {-1.0, .5, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()})
        {
            assert(!engine.PollInputDevices(time));
        }
        assert(a->Polls == 1 && b->Polls == 1);
        a->FailPoll = false;
        assert(engine.PollInputDevices(1));
        assert(a->Polls == 2 && b->Polls == 2);
        a->ThrowPoll = true;
        try
        {
            (void)engine.PollInputDevices(2);
            assert(false);
        }
        catch (int value)
        {
            assert(value == 3);
        }
        ResetObserver observer;
        engine.GetInputRouter().RegisterController(&observer, InputRouter::PriorityGame);
        assert(engine.ShutdownInputDevices());
        assert(!engine.GetInputSystem().GetState().IsGamepadButtonDown(0, GamepadButton::A));
        assert(observer.Resets == 0);
        engine.GetInputSystem().BeginFrame();
        assert(observer.Resets == 1);
        assert(engine.ShutdownInputDevices());
        engine.GetInputSystem().BeginFrame();
        assert(observer.Resets == 1);
        engine.GetInputRouter().UnregisterController(&observer);
        a->ThrowPoll = false;
        assert(engine.InitializeInputDevices());
        assert(engine.PollInputDevices(0));
        assert(engine.ShutdownInputDevices());
    }
    {
        Record log;
        auto* previous = Eng::GEngine;
        auto engine = Container::MakeUnique<Eng::Engine>();
        Eng::GEngine = engine.get();
        Eng::ApplicationProcessor processor;
        auto device = Container::MakeUnique<Device>(log, 0);
        auto* deviceView = device.get();
        device->CheckPhase = true;
        device->Pad = true;
        assert(engine->AddInputDevice(std::move(device)));
        auto platform = Container::MakeUnique<RunDevicePlatform>(log);
        auto* platformView = platform.get();
        engine->SetPlatformApp(std::move(platform));
        engine->SetApplicationHandler(Container::MakeShared<Handler>(log));
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
            assert(deviceView->Initializes == run + 1 && deviceView->Stops == run + 1);
            assert(!engine->GetInputSystem().GetState().IsGamepadButtonDown(0, GamepadButton::A));
        }
        platformView->Exit = true;
        assert(processor.Run() == 7);
        assert(deviceView->Initializes == 3 && deviceView->Stops == 3 && deviceView->Polls == 2);
        engine.reset();
        assert(log.Destroyed == 1);
        Eng::GEngine = previous;
    }
    std::cout << "InputDeviceLifecycleTest passed\n";
    return 0;
}
