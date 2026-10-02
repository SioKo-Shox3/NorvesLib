#include "Engine/Engine.h"
#include "Engine/ApplicationProcessor.h"
#include "Engine/FixedStepScheduler.h"
#include "Application/ApplicationHandlerBase.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
namespace NorvesLib::Core::Engine
{
    struct ApplicationInputFrameTestAccess
    {
        static bool Begin(ApplicationProcessor& processor, int64_t time) { return processor.BeginInputFrame(time); }
        static bool Update(ApplicationProcessor& processor, int64_t time, int64_t delta) { return processor.UpdateInputFrame(time,delta); }
        static bool Ended(ApplicationProcessor& processor)
        {
            return processor.m_FixedStepScheduler->Advance(0,true).Status==EFixedStepAdvanceStatus::NotRunning;
        }
        static float Clamped(ApplicationProcessor& processor, int64_t delta) { return processor.ClampVariableDeltaTime(delta); }
    };
}
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace Eng = NorvesLib::Core::Engine;
namespace
{
    class RunProbePlatform final : public IApplication
    {
    public:
        bool ThrowOnPump=false,ExitAfterPump=false;
        bool Initialize(const Container::VariableArray<Container::String>&) override { return true; }
        void Shutdown() override {}
        IWindow* GetMainWindow() override { return nullptr; }
        void RegisterWindow(Container::TSharedPtr<IWindow>) override {}
        void UnregisterWindow(Container::TSharedPtr<IWindow>) override {}
        void PumpMessages() override
        {
            auto& input=Eng::GEngine->GetInputSystem();
            input.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
            assert(input.InjectRawMouseDelta(8,0));
            assert(Eng::GEngine->GetInputMapper().GetAction("Jump"_id).Button.Held);
            if (ThrowOnPump) throw 11;
        }
        bool IsExitRequested() const override { return ExitAfterPump; }
        int GetExitCode() const override { return 7; }
    };
    class ThrowingInputHandler final : public Application::ApplicationHandlerBase
    {
    public:
        void OnUpdate(float) override
        {
            auto& mapper=Eng::GEngine->GetInputMapper();
            // 実Run→message→Tick→Mapper.Update→OnUpdateの順序も確認する。
            assert(mapper.GetAction("Jump"_id).Button.Pressed);
            assert(mapper.GetAction("Look"_id).Axis.x==2);
            throw 12;
        }
    };
    class LegacyProbe final : public IInputController
    {
    public:
        int Calls=0;
        bool OnKey(const KeyEvent&) override { ++Calls;return false; }
        const char* DebugName() const override { return "LegacyProbe"; }
    };
}
int main()
{
    Eng::ApplicationProcessor processor;
    auto* previousEngine=Eng::GEngine;
    Eng::GEngine=nullptr;
    assert(!Eng::ApplicationInputFrameTestAccess::Begin(processor,0));
    assert(!Eng::ApplicationInputFrameTestAccess::Update(processor,0,0));
    auto engine=Container::MakeUnique<Eng::Engine>();
    Eng::GEngine=engine.get();
    auto& mapper=engine->GetInputMapper();
    auto& system=engine->GetInputSystem();
    const Eng::Engine& constEngine=*engine;
    assert(&constEngine.GetInputMapper()==&mapper);
    LegacyProbe legacy;
    engine->GetInputRouter().RegisterController(&legacy,InputRouter::PriorityGame);
    system.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
    assert(legacy.Calls==1 && mapper.GetCursorMode()==ECursorMode::Normal);
    engine->GetInputRouter().UnregisterController(&legacy);system.ReleaseAll();
    InputBindingSet definitions;
    assert(definitions.AddContext("Gameplay"_id,ECursorMode::Locked));
    InputActionDefinition action;action.Id="Jump"_id;
    InputBinding key;key.Source={EInputBindingSource::Key,static_cast<uint16_t>(KeyCode::Space),0};
    action.Bindings.push_back(key);assert(definitions.AddAction("Gameplay"_id,action));
    action={};action.Id="Look"_id;action.Settings.Type=EInputMappingValueType::Axis1D;
    action.Settings.Output=EInputAxisOutput::FrameDelta;action.Settings.MouseSensitivity=0.25f;action.Settings.RateSensitivity=90;
    InputBinding mouse;mouse.Source={EInputBindingSource::MouseDelta,0,0};
    InputBinding stick;stick.Source={EInputBindingSource::GamepadAxis,0,0};
    action.Bindings={mouse,stick};assert(definitions.AddAction("Gameplay"_id,action));
    assert(mapper.Configure(definitions));assert(mapper.PushContext("Gameplay"_id));
    assert(mapper.GetAction("Jump"_id).Active); // Engine constructorでRouterへ接続済み。
    assert(Eng::ApplicationInputFrameTestAccess::Begin(processor,0));
    system.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
    assert(system.InjectRawMouseDelta(8,0));
    GamepadState pad;pad.Connected=true;pad.Axes[0]=1;assert(system.InjectGamepadState(0,pad));
    assert(Eng::ApplicationInputFrameTestAccess::Update(processor,1'000'000'000,1'000'000'000));
    assert(std::abs(Eng::ApplicationInputFrameTestAccess::Clamped(processor,1'000'000'000)-0.1f)<0.00001f);
    assert(mapper.GetAction("Look"_id).Axis.x==92); // 8*.25 + 90*実1秒。clampの.1秒ではない。
    assert(mapper.GetAction("Jump"_id).Button.Hold && mapper.GetAction("Jump"_id).Button.Pressed);
    // fixed stepが無いframeを跨いでもpress latchを消費するまで保持する。
    assert(Eng::ApplicationInputFrameTestAccess::Begin(processor,1'100'000'000));
    assert(system.GetState().GetMouseState().RawDeltaX==0 && !mapper.GetAction("Jump"_id).Button.Pressed);
    assert(mapper.ConsumeFixedPress("Jump"_id) && !mapper.ConsumeFixedPress("Jump"_id));
    assert(Eng::ApplicationInputFrameTestAccess::Update(processor,1'200'000'000,200'000'000));
    assert(mapper.GetAction("Look"_id).Axis.x==18);
    assert(mapper.GetAction("Jump"_id).Button.Held && !mapper.GetAction("Jump"_id).Button.HoldStarted);
    assert(Eng::ApplicationInputFrameTestAccess::Begin(processor,2'000'000'000));
    system.InjectKeyEvent(KeyCode::Space,InputAction::Released);
    assert(Eng::ApplicationInputFrameTestAccess::Update(processor,2'010'000'000,810'000'000));
    assert(mapper.GetAction("Jump"_id).Button.Released && !mapper.GetAction("Jump"_id).Button.Tap);
    assert(Eng::ApplicationInputFrameTestAccess::Begin(processor,3'000'000'000));
    system.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);system.InjectKeyEvent(KeyCode::Space,InputAction::Released);
    assert(Eng::ApplicationInputFrameTestAccess::Update(processor,3'000'000'000,0));
    auto jump=mapper.GetAction("Jump"_id);
    assert(jump.Button.Pressed && jump.Button.Released && jump.Button.Tap);
    mapper.CancelAll();assert(!mapper.ConsumeFixedPress("Jump"_id));
    assert(system.InjectRawMouseDelta(5,0));
    assert(!Eng::ApplicationInputFrameTestAccess::Begin(processor,-1));
    assert(!Eng::ApplicationInputFrameTestAccess::Begin(processor,0));
    assert(system.GetState().GetMouseState().RawDeltaX==5);
    assert(!Eng::ApplicationInputFrameTestAccess::Update(processor,-1,0));
    assert(Eng::ApplicationInputFrameTestAccess::Begin(processor,4'000'000'000));
    assert(Eng::ApplicationInputFrameTestAccess::Update(processor,4'000'000'000,-1));
    assert(mapper.GetAction("Look"_id).Axis.x==0);
    // 別Engineを作ってもそれぞれの正本へ接続する。
    {
        auto independent=Container::MakeUnique<Eng::Engine>();
        auto& other=independent->GetInputMapper();
        assert(other.Configure(definitions));assert(other.PushContext("Gameplay"_id));
        independent->GetInputSystem().InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
        assert(other.GetAction("Jump"_id).Button.Held && !mapper.GetAction("Jump"_id).Button.Held);
    }
    // Runの実例外経路を通し、手動CancelAllではなくscope-exitの取消を確認する。
    for(int stage=0;stage<2;++stage)
    {
        system.ReleaseAll();
        auto platform=Container::MakeUnique<RunProbePlatform>();platform->ThrowOnPump=(stage==0);
        engine->SetPlatformApp(std::move(platform));
        engine->SetApplicationHandler(Container::MakeShared<ThrowingInputHandler>());
        engine->SetRunning(true);
        bool caught=false;
        try { (void)processor.Run(); }
        catch(int code) { caught=true;assert(code==11+stage); }
        assert(caught);
        assert(!mapper.GetAction("Jump"_id).Button.Held && !mapper.ConsumeFixedPress("Jump"_id));
        assert(mapper.GetAction("Look"_id).Axis.x==0);
        assert(Eng::ApplicationInputFrameTestAccess::Ended(processor));
    }
    system.ReleaseAll();
    auto quitting=Container::MakeUnique<RunProbePlatform>();quitting->ExitAfterPump=true;
    engine->SetPlatformApp(std::move(quitting));engine->SetApplicationHandler({});
    assert(processor.Run()==7);
    assert(!mapper.GetAction("Jump"_id).Button.Held && !mapper.ConsumeFixedPress("Jump"_id));
    assert(Eng::ApplicationInputFrameTestAccess::Ended(processor));
    mapper.Detach();assert(!mapper.GetAction("Jump"_id).Active);
    engine.reset();Eng::GEngine=previousEngine;
    std::cout << "InputFramePipelineTest passed\n";
    return 0;
}
