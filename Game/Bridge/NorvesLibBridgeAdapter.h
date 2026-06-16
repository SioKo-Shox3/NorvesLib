#pragma once

// Workstream L-P2a: NorvesLib 用 Bridge エンジンアダプタ。
//
// NorvesEditor Bridge engine SDK の IBridgeEngineAdapter を NorvesLib 向けに実装する。
// P2a は「最小だがスキーマ準拠」の応答に留める（runtime/status/log の本実装は L-P2b、
// log.message ストリーミングは L-P3b）。
//
// スレッド前提: すべてのコールバックは BridgeServerHost::DrainInbound 経由で
// ゲームスレッド上から同期的に呼ばれる。params は呼び出し中のみ借用で、戻り値の
// 後に保持しない。エンジン値は DTO スナップショットへ変換してから返す。
//
// SDK 非設定ビルド対応: SDK 依存は NORVES_BRIDGE_ENABLED でガードする。define が
// 無いビルドでは何も定義しない（このアダプタは BridgeServerHost からのみ参照され、
// その参照経路も同じガード下にある）。

#if defined(NORVES_BRIDGE_ENABLED)

#include <cstdint>
#include <string>
#include <string_view>

#include "norves/bridge/adapter.hpp"
#include "norves/bridge/error.hpp"
#include "norves/bridge/json_value.hpp"
#include "norves/bridge/result.hpp"

namespace Game::Bridge
{

    /**
     * @brief NorvesLib の Bridge アダプタ（P2a 最小スキーマ準拠実装）
     */
    class NorvesLibBridgeAdapter final : public norves::bridge::IBridgeEngineAdapter
    {
    public:
        NorvesLibBridgeAdapter() = default;
        ~NorvesLibBridgeAdapter() override = default;

        // --- Handshake ---
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        hello(const norves::bridge::JsonValue &params,
              std::string_view selectedProtocolVersion) override;

        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        getCapabilities(const norves::bridge::JsonValue &params) override;

        // --- Engine status / launch ---
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        getStatus(const norves::bridge::JsonValue &params) override;

        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        launchInfo(const norves::bridge::JsonValue &params) override;

        // --- Runtime control ---
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimePlay(const norves::bridge::JsonValue &params) override;

        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimePause(const norves::bridge::JsonValue &params) override;

        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimeStop(const norves::bridge::JsonValue &params) override;

        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimeFocusViewport(const norves::bridge::JsonValue &params) override;

        // --- Log streaming ---
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        logSubscribe(const norves::bridge::JsonValue &params) override;

        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        logUnsubscribe(const norves::bridge::JsonValue &params) override;

        // scene/object/schema は adapter.hpp の既定実装（METHOD_NOT_SUPPORTED）のまま。

    private:
        // セッション/サブスクリプションの一意 id 採番用カウンタ。すべてのコールバックは
        // ゲームスレッド上で逐次実行されるため、単純なカウンタで十分（同時呼び出しなし）。
        std::uint64_t m_NextSessionSeq = 0;
        std::uint64_t m_NextSubscriptionSeq = 0;
    };

} // namespace Game::Bridge

#else // NORVES_BRIDGE_ENABLED

namespace Game::Bridge
{

    /**
     * @brief SDK 非設定ビルド用の不活性スタブ。SDK 型に一切依存しない空クラス。
     *
     * BridgeServerHost のスタブ Start() は adapter を無視するため、これは
     * GameApplicationHandler のメンバとして完全型を提供する目的だけを持つ。
     */
    class NorvesLibBridgeAdapter
    {
    public:
        NorvesLibBridgeAdapter() = default;
        ~NorvesLibBridgeAdapter() = default;
    };

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
