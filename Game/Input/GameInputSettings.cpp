#include "GameInputSettings.h"
#include "GameInputActions.h"
#include "Core/Public/Thread/Atomic.h"
#include <utility>
#include <type_traits>
#include <limits>

namespace Game::Input
{
    namespace CoreInput=NorvesLib::Core::Input;
    namespace Container=NorvesLib::Core::Container;
    namespace Asset=NorvesLib::Core::Asset;
    namespace
    {
        uint64_t NextSettingsRevision()
        {
            // 設定ownerを作り直しても古い値requestと衝突しない。失敗時の欠番は許す。
            static NorvesLib::Thread::Atomic<uint64_t> next{1};
            auto value = next.Load(std::memory_order_relaxed);
            for (;;)
            {
                if (value == std::numeric_limits<uint64_t>::max())
                {
                    return 0;
                }
                if (next.CompareExchangeWeak(value, value + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    return value;
                }
            }
        }
    }
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
        const auto revision = NextSettingsRevision();
        if (revision == 0)
        {
            m_LastError = "入力設定revisionの上限に到達しました";
            return false;
        }
        if(!mapper.ConfigureWithContext(candidate.Current,InputActions::DebugContext))
        {
            m_LastError="起動用Debug入力contextを構成できません";return false;
        }
        m_Configuration=std::move(candidate);m_Store=std::move(store);m_Ready=true;m_Revision=revision;m_LastError.clear();
        return true;
    }
    bool GameInputSettings::ApplyCandidate(CoreInput::InputMapper& mapper,CoreInput::InputBindingSet candidate)
    {
        // Mapper成功後のCurrent反映は例外を出さず、両所有者を食い違わせない。
        static_assert(std::is_nothrow_move_assignable_v<CoreInput::InputBindingSet>);
        if(!m_Ready) { m_LastError="入力設定が未初期化です";return false; }
        const auto revision = NextSettingsRevision();
        if (revision == 0)
        {
            m_LastError = "入力設定revisionの上限に到達しました";
            return false;
        }
        if(!mapper.ConfigurePreservingContexts(candidate))
        {
            m_LastError="現在のcontextを維持したまま入力設定を反映できません";return false;
        }
        m_Configuration.Current=std::move(candidate);m_Revision=revision;m_LastError.clear();return true;
    }
    bool GameInputSettings::ApplyActionBindings(CoreInput::InputMapper& mapper,
        NorvesLib::Core::Identity context,NorvesLib::Core::Identity action,
        const Container::VariableArray<CoreInput::InputBinding>& bindings)
    {
        if(!m_Ready) { m_LastError="入力設定が未初期化です";return false; }
        auto candidate=m_Configuration.Current;
        if(!candidate.SetBindings(context,action,bindings))
        {
            m_LastError="対象actionまたはbindingが不正です";return false;
        }
        return ApplyCandidate(mapper,std::move(candidate));
    }
    bool GameInputSettings::ResetActionBindings(CoreInput::InputMapper& mapper,
        NorvesLib::Core::Identity context,NorvesLib::Core::Identity action)
    {
        if(!m_Ready) { m_LastError="入力設定が未初期化です";return false; }
        const auto* defaults=m_Configuration.Defaults.FindAction(context,action);
        if(!defaults) { m_LastError="既定設定に対象actionがありません";return false; }
        return ApplyActionBindings(mapper,context,action,defaults->Bindings);
    }
    bool GameInputSettings::ResetAllToDefaults(CoreInput::InputMapper& mapper)
    {
        if(!m_Ready) { m_LastError="入力設定が未初期化です";return false; }
        return ApplyCandidate(mapper,m_Configuration.Defaults);
    }

    bool GameInputSettings::BeginRebindCapture(CoreInput::InputRebindCaptureManager& capture,
        NorvesLib::Core::Identity context, NorvesLib::Core::Identity action, size_t bindingSlot,
        const GameInputRebindOutput& output, GameInputRebindRequest& request,
        const CoreInput::InputRebindCaptureOptions& options)
    {
        if (!m_Ready)
        {
            m_LastError = "入力設定が未初期化です";
            return false;
        }
        const auto* definition = m_Configuration.Current.FindAction(context, action);
        if (!definition || bindingSlot > definition->Bindings.size())
        {
            m_LastError = "捕捉対象のactionまたはslotが不正です";
            return false;
        }
        CoreInput::InputBinding probe;
        const CoreInput::InputCapturedControl keyProbe{
            {CoreInput::EInputBindingSource::Key, static_cast<uint16_t>(CoreInput::KeyCode::A), 0}, 0, 1};
        if (!BuildGameInputRebindBinding(keyProbe, output, definition->Settings.Type, definition->Settings.Output, probe))
        {
            m_LastError = "捕捉先の出力設定が不正です";
            return false;
        }
        auto allowed = options;
        if (definition->Settings.Type != CoreInput::EInputMappingValueType::Button &&
            definition->Settings.Output == CoreInput::EInputAxisOutput::Normalized)
        {
            allowed.AllowedSources &= static_cast<CoreInput::InputRebindSourceMask>(
                ~(CoreInput::InputRebindSourceBit(CoreInput::EInputBindingSource::MouseDelta) |
                    CoreInput::InputRebindSourceBit(CoreInput::EInputBindingSource::MouseWheel)));
        }
        static_assert(std::is_nothrow_copy_assignable_v<GameInputRebindRequest>);
        GameInputRebindRequest candidate;
        candidate.m_Revision = m_Revision;
        candidate.m_Context = context;
        candidate.m_Action = action;
        candidate.m_BindingSlot = bindingSlot;
        candidate.m_Output = output;
        candidate.m_CaptureRequest = capture.Begin(allowed);
        if (candidate.m_CaptureRequest == 0)
        {
            m_LastError = "入力捕捉を開始できません";
            return false;
        }
        request = candidate;
        m_LastError.clear();
        return true;
    }
    EGameInputRebindApplyResult GameInputSettings::ApplyRebindCapture(CoreInput::InputMapper& mapper,
        CoreInput::InputRebindCaptureManager& capture, const GameInputRebindRequest& request)
    {
        if (!m_Ready || request.m_CaptureRequest == 0 || !capture.UsesMapper(mapper))
        {
            m_LastError = "入力捕捉の設定ownerまたはMapperが不正です";
            return EGameInputRebindApplyResult::Invalid;
        }
        if (request.m_Revision != m_Revision)
        {
            m_LastError = "捕捉開始後に入力設定が変更されています";
            return EGameInputRebindApplyResult::Stale;
        }
        if (!capture.IsCurrentRequest(request.m_CaptureRequest))
        {
            m_LastError = "入力捕捉要求が置き換わっています";
            return EGameInputRebindApplyResult::Stale;
        }
        CoreInput::InputRebindCaptureResult result;
        if (!capture.TryGetResult(request.m_CaptureRequest, result))
        {
            return EGameInputRebindApplyResult::Pending;
        }
        if (result.Outcome == CoreInput::EInputRebindOutcome::Cancelled)
        {
            m_LastError.clear();
            return EGameInputRebindApplyResult::Cancelled;
        }
        const auto* definition = m_Configuration.Current.FindAction(request.m_Context, request.m_Action);
        CoreInput::InputBinding binding;
        if (result.Outcome != CoreInput::EInputRebindOutcome::Captured || !definition ||
            request.m_BindingSlot > definition->Bindings.size() ||
            !BuildGameInputRebindBinding(result.Control, request.m_Output, definition->Settings.Type, definition->Settings.Output, binding))
        {
            m_LastError = "捕捉結果を対象slotへ反映できません";
            return EGameInputRebindApplyResult::Invalid;
        }
        auto bindings = definition->Bindings;
        if (request.m_BindingSlot == bindings.size())
        {
            bindings.push_back(binding);
        }
        else
        {
            bindings[request.m_BindingSlot] = binding;
        }
        if (!ApplyActionBindings(mapper, request.m_Context, request.m_Action, bindings))
        {
            return EGameInputRebindApplyResult::Invalid;
        }
        return EGameInputRebindApplyResult::Applied;
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
