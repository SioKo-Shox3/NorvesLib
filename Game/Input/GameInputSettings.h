#pragma once
#include "Core/Public/Asset/AssetFileReader.h"
#include "Core/Public/Input/InputBindingPersistence.h"
#include "Core/Public/Input/InputMapper.h"

namespace Game::Input
{
    // GameHandler所有。Mapperを借用保持せず、起動設定を値として所有する。
    class GameInputSettings
    {
    public:
        GameInputSettings()=default;
        GameInputSettings(const GameInputSettings&)=delete;
        GameInputSettings& operator=(const GameInputSettings&)=delete;
        bool Initialize(NorvesLib::Core::Input::InputMapper& mapper);
        bool Initialize(NorvesLib::Core::Input::InputMapper& mapper,const NorvesLib::Core::Asset::AssetFileReader& reader,
            NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Input::IInputBindingStore> store);
        bool InitializeFromJson(NorvesLib::Core::Input::InputMapper& mapper,const NorvesLib::Core::Container::String& json,
            NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Input::IInputBindingStore> store = {});
        NorvesLib::Core::Input::InputBindingStoreSaveResult Save() const;
        bool IsReady() const { return m_Ready; }
        const NorvesLib::Core::Input::InputBindingConfiguration& GetConfiguration() const { return m_Configuration; }
        const NorvesLib::Core::Container::String& GetLastError() const { return m_LastError; }
    private:
        NorvesLib::Core::Input::InputBindingConfiguration m_Configuration;
        NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Input::IInputBindingStore> m_Store;
        NorvesLib::Core::Container::String m_LastError;
        bool m_Ready=false;
    };
}
