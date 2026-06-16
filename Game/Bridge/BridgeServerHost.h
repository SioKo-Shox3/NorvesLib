#pragma once

#include <cstdint>
#include <string_view>

// Workstream L-P2a: Bridge サーバーホスト（Game 層）。
//
// NorvesEditor Bridge engine SDK の WebSocket サーバー transport・
// BridgeEngineServer・受信スレッド・受信フレームキューを所有し、ゲームスレッドから
// ドレインして応答する最小ランタイムを提供する。シングルトンではなく
// GameApplicationHandler のメンバとして保持される。
//
// スレッド設計（必読）:
//   * 受信スレッド（NorvesLib::Thread::Thread。JobSystem Task ではない。Logger が
//     JobSystem ワーカーを常時占有しており枯渇するため）が transport->recv() を
//     ブロッキングで回し、得たフレームを Mutex 保護のキューへ push するだけ。
//     受信スレッドは GEngine やエンジン状態へ一切触れない。
//   * DrainInbound() はゲームスレッド（OnUpdate）から呼ばれ、キューからフレームを
//     取り出して server.handleFrame() を呼び、応答を transport->send() で返す。
//     アダプタのコールバックはすべてこのゲームスレッド上で実行される。
//   * Stop() は先に transport->close() で recv() を解除してから受信スレッドを
//     Join() する（close→join。逆順だと recv() がブロックしたままで join が
//     デッドロックする）。冪等。
//
// SDK 境界:
//   make_websocket_server_transport が返す std::unique_ptr<ITransport> は
//   SDK が返す形のまま保持する（TUniquePtr へ変換しない。これは SDK 境界の例外で、
//   AGENTS.md の std 禁止規則に対する明示的な許容点）。BridgeEngineServer は
//   NorvesLib の TUniquePtr で保持する。
//
// SDK 非設定ビルド対応:
//   Game/Bridge/*.cpp は GLOB で常に収集されコンパイルされるが、SDK ヘッダは
//   NORVES_BRIDGE_SDK_DIR 設定時のみ利用可能なので、SDK 依存部はすべて
//   NORVES_BRIDGE_ENABLED（Game/CMakeLists.txt が SDK 設定時のみ付与する compile
//   define）でガードする。define が無いビルドでは、同じ公開 API を持つ不活性な
//   スタブクラスを提供し、GameApplicationHandler は常に完全型を見られる。

#if defined(NORVES_BRIDGE_ENABLED)

#include <memory>
#include <string>

#include "norves/bridge/json_value.hpp"
#include "norves/bridge/server.hpp"
#include "norves/bridge/transport.hpp"

#include "BridgeLogSink.h"

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Logging/LogTypes.h"
#include "Core/Public/Thread/Atomic.h"
#include "Core/Public/Thread/Mutex.h"
#include "Core/Public/Thread/Thread.h"

namespace norves::bridge
{
    class IBridgeEngineAdapter;
} // namespace norves::bridge

namespace Game::Bridge
{

    /**
     * @brief Bridge WebSocket サーバーのホスト（受信スレッド + キュー + ゲームスレッドドレイン）
     */
    class BridgeServerHost
    {
    public:
        BridgeServerHost() = default;
        ~BridgeServerHost();

        BridgeServerHost(const BridgeServerHost &) = delete;
        BridgeServerHost &operator=(const BridgeServerHost &) = delete;
        BridgeServerHost(BridgeServerHost &&) = delete;
        BridgeServerHost &operator=(BridgeServerHost &&) = delete;

        /**
         * @brief WebSocket サーバーを bind し、READY を出力し、受信スレッドを起動する
         *
         * bind は数回リトライする（kill→同ポート再起動時に OS のリスナー解放待ちで
         * 一時的に失敗し得るため）。bind 成功後に WriteReadyLine(port) で READY を
         * 標準出力へ出し、recv() を回す受信スレッドを開始する。
         *
         * @param port 待ち受けポート（127.0.0.1 ループバックのみ）
         * @param adapter ハンドラ群。本ホストより長く生存しなければならない（参照保持）
         * @return bind と起動に成功したら true、失敗したら false
         */
        bool Start(uint16_t port, norves::bridge::IBridgeEngineAdapter &adapter);

        /**
         * @brief ゲームスレッドから呼び、キュー済みの受信フレームを処理して応答を送る
         *
         * Start() に成功していない場合は何もしない。handleFrame とアダプタ呼び出しは
         * このスレッド上で同期的に行われる。
         */
        void DrainInbound();

        /**
         * @brief サーバーを停止する（close→join、冪等）
         *
         * transport->close() で recv() を解除してから受信スレッドを Join() する。
         * World/RenderWorld 破棄より前に呼ぶこと。
         */
        void Stop();

        /**
         * @brief サーバーが起動済みかどうか
         */
        bool IsActive() const
        {
            return m_bActive.GetValue();
        }

        /**
         * @brief サーバー発イベント（kind=event）をクライアントへ送る
         *
         * eventName / params からイベントエンベロープをエンコードして transport で送る。
         * 起動前 / サーバー or transport 未設定なら無視する。emitEvent が空フレームを
         * 返した場合（エンコード失敗）は送らない。送信失敗（peer 不在 / キュー満杯）でも
         * 無言で破棄する（ログを出すと log.message 経路で増幅ループになり得るため）。
         *
         * 呼び出しスレッド: ゲームスレッド（DrainInbound 経由）からのみ呼ぶこと。
         *
         * @param eventName イベント名（例 "log.message" / "runtime.stateChanged"）
         * @param params イベント params（コピーされる）
         */
        void EmitEvent(std::string_view eventName, const norves::bridge::JsonValue &params);

        /**
         * @brief Logger -> Bridge のログ転送を開始する（log.subscribe 時）
         *
         * 転送最小レベルを設定して中継 sink を空にし、未開始なら Logger へ sink を登録する。
         * 既に開始済みなら minLevel の更新のみ行う（再登録しない）。
         *
         * 呼び出しスレッド: ゲームスレッド（DrainInbound 経由）からのみ呼ぶこと。
         */
        void StartLogForwarding(NorvesLib::Core::Logging::LogLevel minLevel);

        /**
         * @brief Logger -> Bridge のログ転送を停止する（log.unsubscribe / Stop 時、冪等）
         *
         * 登録済みなら Logger から sink を解除する。未開始なら何もしない。
         */
        void StopLogForwarding();

    private:
        // 受信スレッド本体。transport->recv() をブロッキングで回し、得たフレームを
        // キューへ push するだけ。GEngine/エンジン状態には触れない。
        void RecvLoop();

        // SDK が返す形のまま保持する unique_ptr（SDK 境界の例外的 std 利用）。
        std::unique_ptr<norves::bridge::ITransport> m_Transport;

        // BridgeEngineServer は NorvesLib の TUniquePtr で保持。adapter を参照保持
        // するため、adapter は本サーバーより長生きする必要がある。
        NorvesLib::Core::Container::TUniquePtr<norves::bridge::BridgeEngineServer> m_Server;

        // 受信フレームキュー（Mutex 保護）。受信スレッドが push、ゲームスレッドが pop。
        // 要素はトランスポートの生フレーム（UTF-8 JSON バイト列）で、SDK 境界の
        // std::string をそのまま運ぶ（コンテナは NorvesLib 型、要素は境界型）。
        NorvesLib::Thread::Mutex m_QueueMutex;
        NorvesLib::Core::Container::Queue<std::string> m_InboundFrames;

        // 受信スレッド（JobSystem Task ではない）。
        NorvesLib::Thread::Thread m_RecvThread;

        // 起動済みフラグ。Start 成功で true、Stop で false。
        NorvesLib::Thread::Atomic<bool> m_bActive{false};

        // Logger -> Bridge ログ転送の中継 sink（host が所有）。Logger ワーカーが push、
        // ゲームスレッドの DrainInbound が pop して log.message へ emit する。
        BridgeLogSink m_LogSink;

        // sink が Logger へ登録中かどうか。StartLogForwarding で true、
        // StopLogForwarding で false（冪等）。Stop / デストラクタで必ず解除する。
        bool m_bLogForwarding = false;
    };

} // namespace Game::Bridge

#else // NORVES_BRIDGE_ENABLED

namespace norves::bridge
{
    class IBridgeEngineAdapter;
    class JsonValue;
} // namespace norves::bridge

namespace Game::Bridge
{

    /**
     * @brief SDK 非設定ビルド用の不活性スタブ。公開 API は同一だが何もしない。
     */
    class BridgeServerHost
    {
    public:
        BridgeServerHost() = default;
        ~BridgeServerHost() = default;

        BridgeServerHost(const BridgeServerHost &) = delete;
        BridgeServerHost &operator=(const BridgeServerHost &) = delete;
        BridgeServerHost(BridgeServerHost &&) = delete;
        BridgeServerHost &operator=(BridgeServerHost &&) = delete;

        bool Start(uint16_t /*port*/, norves::bridge::IBridgeEngineAdapter & /*adapter*/)
        {
            return false;
        }

        void DrainInbound()
        {
        }

        void Stop()
        {
        }

        bool IsActive() const
        {
            return false;
        }

        // SDK 非設定ビルドでは Bridge engine SDK が無いため何もしない。public API を
        // 活性版と揃えるためだけのスタブ（このビルドでは誰も呼ばない）。
        void EmitEvent(std::string_view /*eventName*/, const norves::bridge::JsonValue & /*params*/)
        {
        }
    };

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
