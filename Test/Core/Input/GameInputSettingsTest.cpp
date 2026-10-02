#include "Game/Input/GameInputSettings.h"
#include "Game/Input/GameInputActions.h"
#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace
{
    struct StoreState
    {
        int Loads=0,Saves=0;
        EInputBindingStoreLoadStatus Status=EInputBindingStoreLoadStatus::Missing;
        Container::String Text;
        bool FailSave=false;
    };
    class FakeStore final : public IInputBindingStore
    {
    public:
        explicit FakeStore(StoreState& state):State(state) {}
        InputBindingStoreLoadResult Load() override
        {
            ++State.Loads;InputBindingStoreLoadResult result;result.Status=State.Status;result.Text=State.Text;
            if(State.Status==EInputBindingStoreLoadStatus::Error) result.Error="test read failure";
            return result;
        }
        InputBindingStoreSaveResult Save(const Container::String& text) override
        {
            ++State.Saves;InputBindingStoreSaveResult result;
            if(State.FailSave) { result.Error="test save failure";return result; }
            State.Text=text;result.Success=true;return result;
        }
    private:
        StoreState& State;
    };
    Container::String ReadDefaults(const Asset::AssetFileReader& reader)
    {
        Asset::AssetReadRequest request;request.InputPath="Config/DefaultInputBindings.json";request.bAllowAbsolutePath=false;
        const auto result=reader.Read(request);assert(result.Succeeded() && !result.Blob.IsEmpty());
        Container::String text;text.append(reinterpret_cast<const char*>(result.Blob.GetData()),result.Blob.GetSize());return text;
    }
}
int main()
{
    InputSystem system;InputRouter router;system.SetRouter(&router);
    InputMapper mapper(system.GetState());mapper.Attach(router);
    Asset::AssetFileReader reader;
    const auto json=ReadDefaults(reader);
    StoreState missing;
    Game::Input::GameInputSettings settings;
    assert(settings.Initialize(mapper,reader,Container::MakeUnique<FakeStore>(missing)));
    assert(settings.IsReady() && settings.GetLastError().empty() && missing.Loads==1 && missing.Saves==0);
    assert(mapper.GetActiveContext()==Game::InputActions::DebugContext && mapper.GetCursorMode()==ECursorMode::Normal);
    assert(settings.GetConfiguration().Current.FindContext(Game::InputActions::GameplayContext)->CursorMode==ECursorMode::Locked);
    for(const auto id:{Game::InputActions::Move,Game::InputActions::Look,Game::InputActions::Sprint,Game::InputActions::Bite,
        Game::InputActions::Swing,Game::InputActions::Jump,Game::InputActions::Sniff})
        assert(settings.GetConfiguration().Current.FindAction(Game::InputActions::GameplayContext,id));
    assert(mapper.BeginFrame(0));system.BeginFrame();system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    assert(mapper.Update(0,0));assert(mapper.GetAction(Game::InputActions::Move).Axis.y==1);
    assert(!mapper.GetAction(Game::InputActions::Jump).Valid); // DebugはMove/Lookだけを公開する。
    StoreState again;
    assert(!settings.InitializeFromJson(mapper,"broken",Container::MakeUnique<FakeStore>(again)));
    assert(again.Loads==0 && mapper.GetAction(Game::InputActions::Move).Axis.y==1);
    assert(settings.Save().Success && missing.Saves==1);
    assert(missing.Text.find("bindings.v1")!=Container::String::npos);
    missing.FailSave=true;assert(!settings.Save().Success);
    // 新しい設定ownerを使ってuser差分をロードし、起動時には書き戻さない。
    StoreState user;user.Status=EInputBindingStoreLoadStatus::Loaded;
    user.Text=R"({"schema":"bindings.v1","contexts":[{"id":"Debug","actions":[{"id":"Move","bindings":[]}]},{"id":"Gameplay","cursor":"hidden","actions":[{"id":"Look","mouse_sensitivity":2.5}]}]})";
    Game::Input::GameInputSettings rebound;
    assert(rebound.InitializeFromJson(mapper,json,Container::MakeUnique<FakeStore>(user)));
    assert(user.Saves==0 && rebound.GetConfiguration().UserStatus==EInputBindingUserStatus::Applied);
    assert(mapper.Update(0,0));assert(mapper.GetAction(Game::InputActions::Move).Axis.y==0);
    assert(rebound.Save().Success && user.Saves==1);
    assert(rebound.ResetActionBindings(mapper,Game::InputActions::DebugContext,Game::InputActions::Move));
    assert(rebound.GetConfiguration().Current.FindContext(Game::InputActions::GameplayContext)->CursorMode==ECursorMode::Hidden);
    assert(rebound.GetConfiguration().Current.FindAction(Game::InputActions::GameplayContext,Game::InputActions::Look)->Settings.MouseSensitivity==2.5f);
    assert(rebound.ResetAllToDefaults(mapper));
    assert(rebound.GetConfiguration().Current.FindContext(Game::InputActions::GameplayContext)->CursorMode==ECursorMode::Locked);
    assert(rebound.GetConfiguration().Current.FindAction(Game::InputActions::GameplayContext,Game::InputActions::Look)->Settings.MouseSensitivity==
        rebound.GetConfiguration().Defaults.FindAction(Game::InputActions::GameplayContext,Game::InputActions::Look)->Settings.MouseSensitivity);
    assert(user.Saves==1);

    // userだけの不正/読取失敗は既定で初期化を完了する。
    for(const auto status:{EInputBindingStoreLoadStatus::Loaded,EInputBindingStoreLoadStatus::Error})
    {
        StoreState bad;bad.Status=status;bad.Text="broken";
        Game::Input::GameInputSettings fallback;
        assert(fallback.InitializeFromJson(mapper,json,Container::MakeUnique<FakeStore>(bad)));
        assert(fallback.GetConfiguration().UserStatus==(status==EInputBindingStoreLoadStatus::Loaded?EInputBindingUserStatus::Invalid:EInputBindingUserStatus::ReadError));
        assert(!fallback.GetConfiguration().Current.FindAction(Game::InputActions::DebugContext,Game::InputActions::Move)->Bindings.empty());
        assert(bad.Saves==0);
    }
    const auto before=mapper.GetActiveContext();
    StoreState notRead;Game::Input::GameInputSettings invalid;
    assert(!invalid.InitializeFromJson(mapper,"broken",Container::MakeUnique<FakeStore>(notRead)));
    assert(notRead.Loads==0 && !invalid.IsReady() && !invalid.GetLastError().empty() && mapper.GetActiveContext()==before);
    assert(!invalid.Save().Success);
    Game::Input::GameInputSettings noContext;
    assert(!noContext.InitializeFromJson(mapper,R"({"schema":"bindings.v1","contexts":[]})"));
    assert(!noContext.IsReady() && mapper.GetActiveContext()==before);
    Game::Input::GameInputSettings noStore;
    assert(noStore.InitializeFromJson(mapper,json));assert(!noStore.Save().Success);
    mapper.Detach();system.SetRouter(nullptr);
    {
        using namespace Game::InputActions;
        InputSystem source;InputRouter route;source.SetRouter(&route);InputMapper target(source.GetState());target.Attach(route);
        StoreState disk;Game::Input::GameInputSettings editing;
        Container::VariableArray<InputBinding> keys;
        InputBinding key;key.Source={EInputBindingSource::Key,static_cast<uint16_t>(KeyCode::Q),0};keys.push_back(key);
        assert(!editing.ApplyActionBindings(target,GameplayContext,Jump,keys));assert(!editing.ResetAllToDefaults(target));
        assert(editing.InitializeFromJson(target,json,Container::MakeUnique<FakeStore>(disk)));
        assert(target.PushContext(GameplayContext) && target.PushContext(MenuContext));
        assert(editing.ApplyActionBindings(target,GameplayContext,Jump,keys));
        assert(target.GetActiveContext()==MenuContext && disk.Saves==0 && editing.GetLastError().empty());
        assert(target.PopContext() && target.GetActiveContext()==GameplayContext);
        source.InjectKeyEvent(KeyCode::Q,InputAction::Pressed);assert(target.GetAction(Jump).Button.Held);
        assert(editing.Save().Success && disk.Saves==1);
        InputBindingSet saved;assert(InputBindingJson::ApplyOverrides(editing.GetConfiguration().Defaults,disk.Text,saved));
        assert(saved.FindAction(GameplayContext,Jump)->Bindings.size()==1 && saved.FindAction(GameplayContext,Jump)->Bindings[0].Source.Code==static_cast<uint16_t>(KeyCode::Q));
        auto invalidKeys=keys;invalidKeys[0].Source.Code=65535;
        assert(!editing.ApplyActionBindings(target,GameplayContext,Jump,invalidKeys));
        assert(target.GetAction(Jump).Button.Held && editing.GetConfiguration().Current.FindAction(GameplayContext,Jump)->Bindings[0].Source.Code==static_cast<uint16_t>(KeyCode::Q));
        assert(!editing.ApplyActionBindings(target,"Unknown"_id,Jump,keys));assert(!editing.ResetActionBindings(target,GameplayContext,"Unknown"_id));
        key.Source.Code=static_cast<uint16_t>(KeyCode::F);keys[0]=key;
        assert(editing.ApplyActionBindings(target,GameplayContext,Jump,keys));
        assert(!target.GetAction(Jump).Button.Held && !target.ConsumeFixedPress(Jump));
        source.InjectKeyEvent(KeyCode::F,InputAction::Pressed);assert(target.GetAction(Jump).Button.Held);
        // 自分のCurrentから借用したbindingを直接渡しても、更新中に参照を失わない。
        const auto& alias=editing.GetConfiguration().Current.FindAction(GameplayContext,Jump)->Bindings;
        assert(editing.ApplyActionBindings(target,GameplayContext,Jump,alias));
        assert(editing.GetConfiguration().Current.FindAction(GameplayContext,Jump)->Bindings[0].Source.Code==static_cast<uint16_t>(KeyCode::F));
        assert(editing.ApplyActionBindings(target,GameplayContext,Jump,{}));
        assert(editing.GetConfiguration().Current.FindAction(GameplayContext,Jump)->Bindings.empty());
        assert(editing.ResetActionBindings(target,GameplayContext,Jump));
        assert(editing.GetConfiguration().Current.FindAction(GameplayContext,Jump)->Bindings.size()==editing.GetConfiguration().Defaults.FindAction(GameplayContext,Jump)->Bindings.size());
        assert(disk.Saves==1 && editing.GetConfiguration().UserStatus==EInputBindingUserStatus::Missing);
        // Mapperにだけ存在するcontextを暗黙に捨てず、両設定を変更しない。
        InputBindingSet foreign;assert(foreign.AddContext("Foreign"_id,ECursorMode::Normal));
        InputActionDefinition foreignAction;foreignAction.Id="ForeignAction"_id;foreignAction.Bindings=keys;assert(foreign.AddAction("Foreign"_id,foreignAction));
        assert(target.ConfigureWithContext(foreign,"Foreign"_id));
        source.InjectKeyEvent(KeyCode::F,InputAction::Released);source.InjectKeyEvent(KeyCode::F,InputAction::Pressed);
        assert(target.GetAction("ForeignAction"_id).Button.Held);
        const auto count=editing.GetConfiguration().Current.FindAction(GameplayContext,Jump)->Bindings.size();
        assert(!editing.ApplyActionBindings(target,GameplayContext,Jump,{}));
        assert(editing.GetConfiguration().Current.FindAction(GameplayContext,Jump)->Bindings.size()==count && target.GetAction("ForeignAction"_id).Button.Held);
        assert(!editing.ResetAllToDefaults(target));assert(target.GetActiveContext()=="Foreign"_id);
        assert(target.ConfigureWithContext(editing.GetConfiguration().Current,DebugContext));assert(target.PushContext(GameplayContext));
        assert(editing.ApplyActionBindings(target,GameplayContext,Jump,{}));
        assert(editing.ResetAllToDefaults(target));assert(target.GetActiveContext()==GameplayContext);
        assert(target.PopContext() && target.GetActiveContext()==DebugContext);
        assert(editing.Save().Success && disk.Saves==2);
        assert(InputBindingJson::ApplyOverrides(editing.GetConfiguration().Defaults,disk.Text,saved));
        assert(saved.FindAction(GameplayContext,Jump)->Bindings.size()==count);
        target.Detach();source.SetRouter(nullptr);
    }
    std::cout << "GameInputSettingsTest passed\n";
    return 0;
}
