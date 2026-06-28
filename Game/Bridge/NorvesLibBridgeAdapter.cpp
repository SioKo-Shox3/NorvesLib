#include "NorvesLibBridgeAdapter.h"

#if defined(NORVES_BRIDGE_ENABLED)

#include "Bridge/BridgeRuntimeState.h"
#include "Bridge/BridgeServerHost.h"
#include "GameApplicationHandler.h"

#include "Core/Public/Application/IWindow.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Logging/LogMacros.h"
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

        /**
         * @brief 制御下のリテラル JSON をパースして成功 Result を作る
         *
         * これらはコードが直接書いたリテラルなのでパース失敗は実装バグ。万一失敗したら
         * JSON null を返す（呼び出し側はそれでもスキーマ違反として扱える）よりは、
         * 不正でない最小値を返す。
         *
         * @param text パース対象のリテラル JSON（境界 std::string_view）
         * @return パース結果を収めた成功 Result（失敗時は空オブジェクト）
         */
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

        /**
         * @brief NorvesLib の Bridge runtime 状態を SDK の DTO enum へ変換する
         *
         * @param state NorvesLib の Bridge runtime 状態
         * @return 対応する SDK DTO の RuntimeState（未知値は Unknown）
         */
        norves::bridge::dto::RuntimeState ToDtoRuntimeState(Game::Bridge::BridgeRuntimeState state)
        {
            using DtoState = norves::bridge::dto::RuntimeState;
            switch (state)
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

        /**
         * @brief BridgeRuntimeState を人間可読な名前へ（状態遷移ログ用）
         *
         * @param state 名前へ変換する Bridge runtime 状態
         * @return 状態名の C 文字列（未知値は "unknown"）
         */
        const char* RuntimeStateName(Game::Bridge::BridgeRuntimeState state)
        {
            switch (state)
            {
                case Game::Bridge::BridgeRuntimeState::Edit:
                    return "edit";
                case Game::Bridge::BridgeRuntimeState::Playing:
                    return "playing";
                case Game::Bridge::BridgeRuntimeState::Paused:
                    return "paused";
                case Game::Bridge::BridgeRuntimeState::Stopped:
                    return "stopped";
            }
            return "unknown";
        }

        /**
         * @brief BridgeRuntimeState を runtime.stateChanged の wire 文字列へ
         *
         * SDK の to_wire の可視性に依存せず、ここで wire 値（edit/playing/paused/stopped）を
         * 直接綴る。
         *
         * @note これを SDK の to_wire(dto::RuntimeState) へ「DRY 解消」目的で置換しない。
         *       手書きは未知値で "edit" を返すが、to_wire 経路は未知値で "unknown" を返すため、
         *       置換は runtime.stateChanged の wire 出力を変える破壊的・可観測な挙動変更になる。
         *
         * @param state wire 値へ変換する Bridge runtime 状態
         * @return wire 文字列の C 文字列（未知値は "edit"）
         */
        const char* RuntimeStateWire(Game::Bridge::BridgeRuntimeState state)
        {
            switch (state)
            {
                case Game::Bridge::BridgeRuntimeState::Edit:
                    return "edit";
                case Game::Bridge::BridgeRuntimeState::Playing:
                    return "playing";
                case Game::Bridge::BridgeRuntimeState::Paused:
                    return "paused";
                case Game::Bridge::BridgeRuntimeState::Stopped:
                    return "stopped";
            }
            return "edit";
        }

        /**
         * @brief runtime.stateChanged の params を組んで host から emit する
         *
         * params リテラル {"state":...,"previous":...} を組んで emit する。state を新状態へ
         * 遷移させた直後、PlayAck 返却の前に呼ぶ。params は OkLiteral と同様 JsonValue::parse
         * で作る（リテラルなので妥当）。
         *
         * @param host イベント発火先の host（借用、nullptr なら何もしない）
         * @param previous 遷移前の状態
         * @param next 遷移後の状態
         * @note previous == next のとき、または host が nullptr のときは何もしない。
         */
        void EmitRuntimeStateChanged(Game::Bridge::BridgeServerHost* host,
                                     Game::Bridge::BridgeRuntimeState previous,
                                     Game::Bridge::BridgeRuntimeState next)
        {
            if (previous == next || host == nullptr)
            {
                return;
            }

            std::string text = R"({"state":")";
            text += RuntimeStateWire(next);
            text += R"(","previous":")";
            text += RuntimeStateWire(previous);
            text += R"("})";

            auto parsed = JsonValue::parse(text);
            if (parsed.is_err())
            {
                // 到達しないはず（リテラルは常に妥当）。発火を諦める（無言）。
                return;
            }
            host->EmitEvent("runtime.stateChanged", std::move(parsed).value());
        }
    } // namespace

    AdapterResult
    NorvesLibBridgeAdapter::hello(const JsonValue& /*params*/,
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
    NorvesLibBridgeAdapter::getCapabilities(const JsonValue& /*params*/)
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
    NorvesLibBridgeAdapter::getStatus(const JsonValue& /*params*/)
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
    NorvesLibBridgeAdapter::launchInfo(const JsonValue& /*params*/)
    {
        // スキーマ準拠 {pid, title}。{launched:true} は返さない（mock の非準拠点）。
        std::string payload = R"({"pid":)";
        payload += std::to_string(static_cast<unsigned long>(::GetCurrentProcessId()));
        payload += R"(,"title":"NorvesLib Game"})";
        return OkLiteral(payload);
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimePlay(const JsonValue& /*params*/)
    {
        // runtime 状態を Playing へ遷移させる（Tick ゲートが進行を再開する）。
        // previous は Set の前に読む。
        const auto previous = (m_Handler != nullptr) ? m_Handler->GetBridgeRuntimeState()
                                                      : Game::Bridge::BridgeRuntimeState::Edit;
        const auto next = Game::Bridge::BridgeRuntimeState::Playing;
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(next);
        }

        // 状態遷移を INFO ログへ（Logger 経由で sink -> log.message に流れる）。
        // drain の send 失敗は無言なので、この INFO ログが増幅ループになることはない。
        NORVES_LOG_INFO("Bridge", "runtime state changed: %s -> %s",
                        RuntimeStateName(previous), RuntimeStateName(next));

        // イベント先・response 後の順で runtime.stateChanged を発火する（PlayAck の前）。
        EmitRuntimeStateChanged(m_Host, previous, next);

        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Playing;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimePause(const JsonValue& /*params*/)
    {
        // runtime 状態を Paused へ遷移させる（Tick ゲートが進行を止める）。
        // previous は Set の前に読む。
        const auto previous = (m_Handler != nullptr) ? m_Handler->GetBridgeRuntimeState()
                                                      : Game::Bridge::BridgeRuntimeState::Edit;
        const auto next = Game::Bridge::BridgeRuntimeState::Paused;
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(next);
        }

        NORVES_LOG_INFO("Bridge", "runtime state changed: %s -> %s",
                        RuntimeStateName(previous), RuntimeStateName(next));

        EmitRuntimeStateChanged(m_Host, previous, next);

        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Paused;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeStop(const JsonValue& /*params*/)
    {
        // runtime 状態を Stopped へ遷移させる（Tick ゲートが進行を止める）。
        // previous は Set の前に読む。
        const auto previous = (m_Handler != nullptr) ? m_Handler->GetBridgeRuntimeState()
                                                      : Game::Bridge::BridgeRuntimeState::Edit;
        const auto next = Game::Bridge::BridgeRuntimeState::Stopped;
        if (m_Handler != nullptr)
        {
            m_Handler->SetBridgeRuntimeState(next);
        }

        NORVES_LOG_INFO("Bridge", "runtime state changed: %s -> %s",
                        RuntimeStateName(previous), RuntimeStateName(next));

        EmitRuntimeStateChanged(m_Host, previous, next);

        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        ack.requestedState = norves::bridge::dto::RuntimeState::Stopped;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeFocusViewport(const JsonValue& /*params*/)
    {
        // best-effort でエンジンのメインウィンドウを前面化する。Win32 ハンドルが
        // 有効に取得できたときだけ操作し、それ以外は {focused:false} を返す。
        bool bFocused = false;
        auto* engine = NorvesLib::Core::Engine::GEngine;
        if (engine != nullptr)
        {
            auto* window = engine->GetMainWindow();
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
    NorvesLibBridgeAdapter::logSubscribe(const JsonValue& /*params*/)
    {
        // Logger -> Bridge のログ転送を開始する（host が中継 sink を Logger へ登録）。
        // 単一購読前提（複数同時購読は非対応）。filter.minLevel は honor しない：
        // SDK の JsonValue には構造的な読み取りアクセス API が無い（parse/dump/is_null
        // のみ）ため、サーバー側レベルフィルタは既定 Trace（全転送）で進め、レベル
        // 絞り込みは editor 側 client フィルタへ委ねる。
        if (m_Host != nullptr)
        {
            m_Host->StartLogForwarding(NorvesLib::Core::Logging::LogLevel::Trace);
        }

        // スキーマ準拠 {subscriptionId}。{subscribed:true} は返さない（mock の非準拠点）。
        ++m_NextSubscriptionSeq;
        std::string payload = R"({"subscriptionId":"norveslib-sub-)";
        payload += std::to_string(static_cast<unsigned long long>(m_NextSubscriptionSeq));
        payload += R"("})";
        return OkLiteral(payload);
    }

    AdapterResult
    NorvesLibBridgeAdapter::logUnsubscribe(const JsonValue& /*params*/)
    {
        // ログ転送を停止する（冪等）。subscriptionId 照合はしない（単一購読前提）。
        if (m_Host != nullptr)
        {
            m_Host->StopLogForwarding();
        }

        // log.unsubscribe.result スキーマの必須フィールドは ok(boolean)。
        return OkLiteral(R"({"ok":true})");
    }

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
