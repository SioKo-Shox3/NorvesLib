#pragma once
#include "Core/Public/Asset/AssetFileReader.h"
#include "Core/Public/Input/InputBindingPersistence.h"
#include "Core/Public/Input/InputMapper.h"
#include "Core/Public/Input/InputRebindCaptureManager.h"
#include "GameInputRebindTypes.h"

namespace Game::Input
{
    // 値request。設定/managerへの借用pointerは持たない。設定変更後は適用不可。
    class GameInputRebindRequest
    {
    public:
        uint64_t GetCaptureRequestId() const
        {
            return m_CaptureRequest;
        }
    private:
        friend class GameInputSettings;
        uint64_t m_CaptureRequest = 0;
        uint64_t m_Revision = 0;
        NorvesLib::Core::Identity m_Context;
        NorvesLib::Core::Identity m_Action;
        size_t m_BindingSlot = 0;
        GameInputRebindOutput m_Output;
    };
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
        // 配送外から開始する。slot==bindings.size()は末尾追加、それ以外は置換。
        // 出力設定を明示し、拒否時はrequestを保持する。自動適用/保存はしない。
        bool BeginRebindCapture(NorvesLib::Core::Input::InputRebindCaptureManager& capture,
            NorvesLib::Core::Identity context, NorvesLib::Core::Identity action, size_t bindingSlot,
            const GameInputRebindOutput& output, GameInputRebindRequest& request,
            const NorvesLib::Core::Input::InputRebindCaptureOptions& options = {});
        EGameInputRebindApplyResult ApplyRebindCapture(NorvesLib::Core::Input::InputMapper& mapper,
            NorvesLib::Core::Input::InputRebindCaptureManager& capture, const GameInputRebindRequest& request);
        uint64_t GetRevision() const
        {
            return m_Revision;
        }
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
        uint64_t m_Revision = 0;
    };
}
