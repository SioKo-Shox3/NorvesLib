#include "NorvesLibBridgeAdapter.h"

#if defined(NORVES_BRIDGE_ENABLED)

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
        // P2a 最小スキーマ準拠。Engine::GEngine 連動の本マッピングは L-P2b。
        norves::bridge::dto::StatusSnapshot snap;
        snap.engineState = norves::bridge::dto::EngineState::Running;
        snap.runtimeState = norves::bridge::dto::RuntimeState::Edit;
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
        // 実挙動は L-P2b。P2a は受理応答のみ。
        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimePause(const JsonValue & /*params*/)
    {
        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeStop(const JsonValue & /*params*/)
    {
        norves::bridge::dto::PlayAck ack;
        ack.accepted = true;
        return AdapterResult::ok(ack.to_json());
    }

    AdapterResult
    NorvesLibBridgeAdapter::runtimeFocusViewport(const JsonValue & /*params*/)
    {
        // P2a は正直な no-op：{focused:false}。best-effort raise は L-P2b。
        return OkLiteral(R"({"focused":false})");
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
