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
        // 更新は現在のcontext stackを維持し、成功時だけCurrentとMapperへ一括反映する。
        // 変更だけでは保存しない。GameThreadの入力配送外で呼ぶ。
        bool ApplyActionBindings(NorvesLib::Core::Input::InputMapper& mapper,
            NorvesLib::Core::Identity context,NorvesLib::Core::Identity action,
            const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Input::InputBinding>& bindings);
        bool ResetActionBindings(NorvesLib::Core::Input::InputMapper& mapper,
            NorvesLib::Core::Identity context,NorvesLib::Core::Identity action);
        // 感度等も含むCurrent全体を既定へ戻す。startup読込報告は履歴として残す。
        bool ResetAllToDefaults(NorvesLib::Core::Input::InputMapper& mapper);
        NorvesLib::Core::Input::InputBindingStoreSaveResult Save() const;
        bool IsReady() const { return m_Ready; }
        // 内部設定への借用viewは更新成功/破棄で失効する。
        const NorvesLib::Core::Input::InputBindingConfiguration& GetConfiguration() const { return m_Configuration; }
        const NorvesLib::Core::Container::String& GetLastError() const { return m_LastError; }
    private:
        bool ApplyCandidate(NorvesLib::Core::Input::InputMapper& mapper,
            NorvesLib::Core::Input::InputBindingSet candidate);
        NorvesLib::Core::Input::InputBindingConfiguration m_Configuration;
        NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Input::IInputBindingStore> m_Store;
        NorvesLib::Core::Container::String m_LastError;
        bool m_Ready=false;
    };
}
