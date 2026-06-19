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

// GameApplicationHandler を adapter.h で include すると循環になる
// （GameApplicationHandler.h が adapter.h を include する）。adapter は実体を
// .cpp でのみ使うため、ここでは前方宣言に留める。
namespace Game
{
    class GameApplicationHandler;
} // namespace Game

// BridgeServerHost も同様に .cpp でのみ完全型を使う（EmitEvent / Start・StopLogForwarding
// の呼び出し）。ヘッダでは前方宣言に留める。
namespace Game::Bridge
{
    class BridgeServerHost;
} // namespace Game::Bridge

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

        /**
         * @brief 実エンジン状態へアクセスするための handler を注入する
         *
         * host.Start の前に GameApplicationHandler::OnInitialize から設定される。
         * adapter は host より長生きするため借用ポインタで十分（所有しない）。
         *
         * @param handler 実エンジン状態アクセス用の handler（借用）
         */
        void SetHandler(Game::GameApplicationHandler& handler) { m_Handler = &handler; }

        /**
         * @brief サーバー発イベントを発火するための host を注入する
         *
         * runtime.stateChanged / log.message の発火に使う。host.Start の前に
         * GameApplicationHandler::OnInitialize から設定される。すべてのコールバックは
         * ゲームスレッド上で逐次実行されるため借用ポインタで十分。
         *
         * @param host イベント発火用の host（借用）
         */
        void SetHost(Game::Bridge::BridgeServerHost& host) { m_Host = &host; }

        // --- Handshake ---

        /**
         * @brief bridge.hello。sessionId を採番し HelloResult を返す
         *
         * @param params リクエスト params（借用、呼び出し中のみ有効）
         * @param selectedProtocolVersion サーバが選んだプロトコルバージョン
         * @return HelloResult を収めた JsonValue
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        hello(const norves::bridge::JsonValue& params,
              std::string_view selectedProtocolVersion) override;

        /**
         * @brief bridge.getCapabilities。広告する capability 一覧を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return capabilities を収めた JsonValue
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        getCapabilities(const norves::bridge::JsonValue& params) override;

        // --- Engine status / launch ---

        /**
         * @brief engine.getStatus。実エンジン状態を DTO スナップショットへ変換して返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return StatusSnapshot を収めた JsonValue
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        getStatus(const norves::bridge::JsonValue& params) override;

        /**
         * @brief engine.launchInfo。pid と title を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return {pid, title} を収めた JsonValue
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        launchInfo(const norves::bridge::JsonValue& params) override;

        // --- Runtime control ---

        /**
         * @brief runtime.play。runtime 状態を Playing へ遷移させ PlayAck を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return PlayAck を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。runtime.stateChanged を先に emit する。
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimePlay(const norves::bridge::JsonValue& params) override;

        /**
         * @brief runtime.pause。runtime 状態を Paused へ遷移させ PlayAck を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return PlayAck を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。runtime.stateChanged を先に emit する。
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimePause(const norves::bridge::JsonValue& params) override;

        /**
         * @brief runtime.stop。runtime 状態を Stopped へ遷移させ PlayAck を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return PlayAck を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。runtime.stateChanged を先に emit する。
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimeStop(const norves::bridge::JsonValue& params) override;

        /**
         * @brief runtime.focusViewport。best-effort でエンジンウィンドウを前面化する
         *
         * @param params リクエスト params（借用、未使用）
         * @return {focused} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。Win32 ハンドル取得時のみ操作する。
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        runtimeFocusViewport(const norves::bridge::JsonValue& params) override;

        // --- Log streaming ---

        /**
         * @brief log.subscribe。Logger -> Bridge のログ転送を開始し subscriptionId を返す
         *
         * @param params リクエスト params（借用、filter.minLevel は honor しない）
         * @return {subscriptionId} を収めた JsonValue
         * @note 単一購読前提。ゲームスレッド上から逐次呼ばれる。
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        logSubscribe(const norves::bridge::JsonValue& params) override;

        /**
         * @brief log.unsubscribe。ログ転送を停止する（冪等）
         *
         * @param params リクエスト params（借用、未使用）
         * @return {ok} を収めた JsonValue
         * @note 単一購読前提で subscriptionId 照合はしない。ゲームスレッド上から逐次呼ばれる。
         */
        norves::bridge::Result<norves::bridge::JsonValue, norves::bridge::BridgeError>
        logUnsubscribe(const norves::bridge::JsonValue& params) override;

        // scene/object/schema は adapter.hpp の既定実装（METHOD_NOT_SUPPORTED）のまま。

    private:
        /**
         * @brief セッション id 採番用の単調カウンタ
         *
         * @note すべてのコールバックはゲームスレッド上で逐次実行されるため、単純な
         *       カウンタで十分（同時呼び出しなし）。
         */
        uint64_t m_NextSessionSeq = 0;

        /**
         * @brief サブスクリプション id 採番用の単調カウンタ
         *
         * @note すべてのコールバックはゲームスレッド上で逐次実行されるため、単純な
         *       カウンタで十分（同時呼び出しなし）。
         */
        uint64_t m_NextSubscriptionSeq = 0;

        /**
         * @brief 実エンジン状態アクセス用の借用ポインタ（非所有）
         *
         * @note SetHandler で注入され、すべてのコールバックはゲームスレッド上で逐次実行される。
         */
        Game::GameApplicationHandler* m_Handler = nullptr;

        /**
         * @brief サーバー発イベント発火用の host への借用ポインタ（非所有）
         *
         * @note SetHost で注入され、EmitEvent / Start・StopLogForwarding はゲームスレッド上から呼ばれる。
         */
        Game::Bridge::BridgeServerHost* m_Host = nullptr;
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
