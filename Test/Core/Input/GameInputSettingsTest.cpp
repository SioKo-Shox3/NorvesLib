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
    user.Text=R"({"schema":"bindings.v1","contexts":[{"id":"Debug","actions":[{"id":"Move","bindings":[]}]}]})";
    Game::Input::GameInputSettings rebound;
    assert(rebound.InitializeFromJson(mapper,json,Container::MakeUnique<FakeStore>(user)));
    assert(user.Saves==0 && rebound.GetConfiguration().UserStatus==EInputBindingUserStatus::Applied);
    assert(mapper.Update(0,0));assert(mapper.GetAction(Game::InputActions::Move).Axis.y==0);
    assert(rebound.Save().Success && user.Saves==1);
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
    std::cout << "GameInputSettingsTest passed\n";
    return 0;
}
