#include "GameInputSettings.h"
#include "GameInputActions.h"
#include <utility>

namespace Game::Input
{
    namespace CoreInput=NorvesLib::Core::Input;
    namespace Container=NorvesLib::Core::Container;
    namespace Asset=NorvesLib::Core::Asset;
    bool GameInputSettings::Initialize(CoreInput::InputMapper& mapper)
    {
        Asset::AssetFileReader reader;
        return Initialize(mapper,reader,CoreInput::CreateWorkingDirectoryInputBindingStore());
    }
    bool GameInputSettings::Initialize(CoreInput::InputMapper& mapper,const Asset::AssetFileReader& reader,
        Container::TUniquePtr<CoreInput::IInputBindingStore> store)
    {
        if(m_Ready) { m_LastError="入力設定は初期化済みです";return false; }
        Asset::AssetReadRequest request;request.InputPath="Config/DefaultInputBindings.json";request.bAllowAbsolutePath=false;
        const auto asset=reader.Read(request);
        if(!asset.Succeeded()) { m_LastError="既定入力設定のAssetを読めません";return false; }
        if(asset.Blob.GetSize()>CoreInput::InputBindingJson::MaximumTextBytes) { m_LastError="既定入力設定がサイズ上限を超えています";return false; }
        Container::String text;
        if(!asset.Blob.IsEmpty()) text.append(reinterpret_cast<const char*>(asset.Blob.GetData()),asset.Blob.GetSize());
        return InitializeFromJson(mapper,text,std::move(store));
    }
    bool GameInputSettings::InitializeFromJson(CoreInput::InputMapper& mapper,const Container::String& json,
        Container::TUniquePtr<CoreInput::IInputBindingStore> store)
    {
        if(m_Ready) { m_LastError="入力設定は初期化済みです";return false; }
        auto candidate=CoreInput::LoadInputBindingConfiguration(json,store.get());
        if(!candidate.DefaultsValid)
        {
            m_LastError=candidate.DefaultReport.Error.empty()?Container::String("既定入力設定が不正です"):std::move(candidate.DefaultReport.Error);
            return false;
        }
        if(!mapper.ConfigureWithContext(candidate.Current,InputActions::DebugContext))
        {
            m_LastError="起動用Debug入力contextを構成できません";return false;
        }
        m_Configuration=std::move(candidate);m_Store=std::move(store);m_Ready=true;m_LastError.clear();
        return true;
    }
    CoreInput::InputBindingStoreSaveResult GameInputSettings::Save() const
    {
        if(!m_Ready || !m_Store)
        {
            CoreInput::InputBindingStoreSaveResult result;result.Error="入力設定の保存先が準備されていません";return result;
        }
        return CoreInput::SaveInputBindingOverrides(*m_Store,m_Configuration.Defaults,m_Configuration.Current);
    }
}
