#include "NorvesLibBridgeAdapter.h"

#if defined(NORVES_BRIDGE_ENABLED)

#include "Bridge/BridgeRuntimeState.h"
#include "GameApplicationHandler.h"

#include "Core/Public/Application/IWindow.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Platform/NativeWindowHandle.h"

#include <optional>
#include <string>

#include <Windows.h>

#include "norves/bridge/dto/common.hpp"
#include "norves/bridge/dto/methods.hpp"

namespace Game::Bridge
{
    namespace
    {
        using norves::bridge::BridgeError;
        using norves::bridge::JsonValue;
        using norves::bridge::Result;

        using AdapterResult = Result<JsonValue, BridgeError>;

        // 制御下のリテラル JSON をパースして成功 Result を作る。これらはコードが
        // 直接書いたリテラルなのでパース失敗は実装バグ。万一失敗したら JSON null を
        // 返す（呼び出し側はそれでもスキーマ違反として扱える）よりは、不正でない最小値を返す。
        AdapterResult OkLiteral(std::string_view text)
        {
            auto parsed = JsonValue::parse(text);
            if (parsed.is_err())
            {
                // 到達しないはず（リテラルは常に妥当）。安全側として空オブジェクトを返す。
                return AdapterResult::ok(JsonValue{});
            }
            return AdapterResult::ok(std::move(parsed).value());
        }

        // NorvesLib の Bridge runtime 状態を SDK の DTO enum へ変換する。
        norves::bridge::dto::RuntimeState ToDtoRuntimeState(Game::Bridge::BridgeRuntimeState s)
        {
            using DtoState = norves::bridge::dto::RuntimeState;
            switch (s)
            {
                case Game::Bridge::BridgeRuntimeState::Edit:
                    return DtoState::Edit;
                case Game::Bridge::BridgeRuntimeState::Playing:
                    return DtoState::Playing;
                case Game::Bridge::BridgeRuntimeState::Paused:
                    return DtoState::Paused;
                case Game::Bridge::BridgeRuntimeState::Stopped:
                    return DtoState::Stopped;
            }
            return DtoState::Unknown;
        }
    } // namespace

    AdapterResult
    NorvesLibBridgeAdapter::hello(const JsonValue & /*params*/,
                                  std::string_view selectedProtocolVersion)
    {
        // 一意な sessionId を採番する（プロセス id + 単調カウンタ）。
        ++m_NextSessionSeq;
        std::string sessionId = "norveslib-";
        sessionId += std::to_string(static_cast<unsigned long>(::GetCurrentProcessId()));
        sessionId += "-";
        sessionId += std::to_string(static_cast<unsigned long long>(m_NextSessionSeq));

        norves::bridge::dto::HelloResult result;
        result.sessionId = std::move(sessionId);
        result.protocolVersion = std::string(selectedProtocolVersion);
        result.server = norves::bridge::dto::ServerInfo{
            "NorvesLib",
            std::optional<std::string>{"1.0"},
            std::optional<std::string>{"NorvesLib"}};
        return AdapterResult::ok(result.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::getCapabilities(const JsonValue & /*params*/)
    {
        // runtime.control / log.stream / viewport.focus を広告する
        // （mock_adapter と同形だが NorvesLib 用）。スキーマ準拠の result.capabilities。
        return OkLiteral(
            R"({"capabilities":[)"
            R"({"name":"runtime.control","version":"0.1","description":"Play/pause/stop control."},)"
            R"({"name":"log.stream"},)"
            R"({"name":"viewport.focus"}]})");
    }

    AdapterResult
    NorvesLibBridgeAdapter::getStatus(const JsonValue & /*params*/)
    {
        // 実エンジン状態を DTO スナップショットへ変換して返す。GEngine が走行中
        // （かつ終了要求なし）なら Running、そうでなければ Initializing として扱う。
        norves::bridge::dto::StatusSnapshot snap;
        const bool bRunning = (NorvesLib::Core::Engine::GEngine != nullptr) &&
                              NorvesLib::Core::Engine::GEngine->IsRunning() &&
                              !NorvesLib::Core::Engine::GEngine->IsExitRequested();
        snap.engineState = bRunning ? norves::bridge::dto::EngineState::Running
                                    : norves::bridge::dto::EngineState::Initializing;
        snap.runtimeState = (m_Handler != nullptr)
                                ? ToDtoRuntimeState(m_Handler->GetBridgeRuntimeState())
                                : norves::bridge::dto::RuntimeState::Unknown;
        snap.engineName = "NorvesLib";
        snap.engineVersion = "1.0";
        return AdapterResult::ok(snap.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::launchInfo(const JsonValue & /*params*/)
    {
        // スキーマ準拠 {pid, title}。{launched:true} は返さない（mock の非準拠点）。
        std::string payload = R"({"pid":)";
        payload += std::to_string(static_cast<unsigned long>(::GetCurrentProcessId()));
        payload += R"(,"title":"NorvesLib Game"})";
        return OkLiteral(payload);
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimePlay(const JsonValue & /*params*/)
    {
        // runtime 状態を Playing へ遷移させる（Tick ゲートが進行を再開する）。
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(Game::Bridge::BridgeRuntimeState::Playing);
        }
        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Playing;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimePause(const JsonValue & /*params*/)
    {
        // runtime 状態を Paused へ遷移させる（Tick ゲートが進行を止める）。
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(Game::Bridge::BridgeRuntimeState::Paused);
        }
        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Paused;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeStop(const JsonValue & /*params*/)
    {
        // runtime 状態を Stopped へ遷移させる（Tick ゲートが進行を止める）。
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(Game::Bridge::BridgeRuntimeState::Stopped);
        }
        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Stopped;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeFocusViewport(const JsonValue & /*params*/)
    {
        // best-effort でエンジンのメインウィンドウを前面化する。Win32 ハンドルが
        // 有効に取得できたときだけ操作し、それ以外は {focused:false} を返す。
        bool bFocused = false;
        auto *engine = NorvesLib::Core::Engine::GEngine;
        if (engine != nullptr)
        {
            auto *window = engine->GetMainWindow();
            if (window != nullptr)
            {
                const NorvesLib::Core::Platform::NativeWindowHandle handle = window->GetNativeHandle();
                if (handle.IsValid() &&
                    handle.WindowType == NorvesLib::Core::Platform::NativeWindowHandle::Type::Win32 &&
                    handle.Handle1 != nullptr)
                {
                    HWND hwnd = reinterpret_cast<HWND>(handle.Handle1);
                    ::ShowWindow(hwnd, SW_RESTORE);
                    ::BringWindowToTop(hwnd);
                    bFocused = (::SetForegroundWindow(hwnd) != 0);
                }
            }
        }
        return OkLiteral(bFocused ? R"({"focused":true})" : R"({"focused":false})");
    }

    AdapterResult
    NorvesLibBridgeAdapter::logSubscribe(const JsonValue & /*params*/)
    {
        // スキーマ準拠 {subscriptionId}。{subscribed:true} は返さない（mock の非準拠点）。
        // 実際の log.message ストリーミングは L-P3b。
        ++m_NextSubscriptionSeq;
        std::string payload = R"({"subscriptionId":"norveslib-sub-)";
        payload += std::to_string(static_cast<unsigned long long>(m_NextSubscriptionSeq));
        payload += R"("})";
        return OkLiteral(payload);
    }

    AdapterResult
    NorvesLibBridgeAdapter::logUnsubscribe(const JsonValue & /*params*/)
    {
        // log.unsubscribe.result スキーマの必須フィールドは ok(boolean)。
        return OkLiteral(R"({"ok":true})");
    }

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
