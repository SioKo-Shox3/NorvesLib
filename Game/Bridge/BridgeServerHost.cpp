#include "BridgeServerHost.h"

#if defined(NORVES_BRIDGE_ENABLED)

#include "ReadyHandshake.h"

#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Logging/Logger.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "norves/bridge/adapter.hpp"
#include "norves/bridge/dto/common.hpp"
#include "norves/bridge/dto/events.hpp"
#include "norves/bridge/ws_server_transport.hpp"

namespace Game::Bridge
{
    namespace
    {
        // SDK transport のフレームキュー容量。受信側は溢れると致命（接続クローズ）
        // 扱いのため広めに取る（mock-engine main.cpp と同じ方針）。
        constexpr size_t kSendCapacity = 256;
        constexpr size_t kRecvCapacity = 256;

        // bind リトライ。kill→同ポート再起動時の OS リスナー解放待ちを吸収する。
        constexpr int kMaxBindAttempts = 20;
        constexpr auto kBindRetryDelay = std::chrono::milliseconds(100);

        /**
         * @brief WebSocket サーバー transport を bind する（リトライ込み）
         *
         * kMaxBindAttempts 回まで make_websocket_server_transport を試み、失敗のたびに
         * kBindRetryDelay だけ待つ（kill→同ポート再起動時の OS リスナー解放待ちを吸収）。
         *
         * @param port 待ち受けポート（127.0.0.1 ループバックのみ）
         * @return bind 成功した transport。全試行が失敗したら nullptr
         */
        std::unique_ptr<norves::bridge::ITransport> BindWithRetry(uint16_t port)
        {
            for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt)
            {
                std::unique_ptr<norves::bridge::ITransport> transport =
                    norves::bridge::make_websocket_server_transport(port,
                                                                    kSendCapacity,
                                                                    kRecvCapacity,
                                                                    nullptr);
                if (transport != nullptr)
                {
                    return transport;
                }
                std::this_thread::sleep_for(kBindRetryDelay);
            }
            return nullptr;
        }

        // ゲームスレッドの DrainInbound でログ転送する 1 フレームあたりの上限。1 フレーム
        // で延々と drain して描画を止めないための上限（残りは次フレームへ持ち越す）。
        constexpr int kMaxDrainPerFrame = 64;

        /**
         * @brief NorvesLib の LogLevel を Bridge wire の dto::LogLevel へ畳み込む
         *
         * dto には Fatal / Warning が無いため、Fatal -> Error・Warning -> Warn と畳む
         * （editor は "fatal" を拒否する。スキーマ準拠のため必須）。
         *
         * @param level NorvesLib のログレベル
         * @return 対応する Bridge wire の dto::LogLevel
         */
        norves::bridge::dto::LogLevel FoldLevel(NorvesLib::Core::Logging::LogLevel level)
        {
            using SrcLevel = NorvesLib::Core::Logging::LogLevel;
            using DtoLevel = norves::bridge::dto::LogLevel;
            switch (level)
            {
                case SrcLevel::Trace:
                    return DtoLevel::Trace;
                case SrcLevel::Debug:
                    return DtoLevel::Debug;
                case SrcLevel::Info:
                    return DtoLevel::Info;
                case SrcLevel::Warning:
                    return DtoLevel::Warn;
                case SrcLevel::Error:
                    return DtoLevel::Error;
                case SrcLevel::Fatal:
                    return DtoLevel::Error;
            }
            return DtoLevel::Info;
        }
    } // namespace

    /**
     * @brief デストラクタ
     *
     * @note 念のための後始末。通常は OnPreShutdown の Stop() で停止済み。
     */
    BridgeServerHost::~BridgeServerHost()
    {
        // 念のための後始末。通常は OnPreShutdown の Stop() で停止済み。
        Stop();
    }

    /**
     * @brief WebSocket サーバーを bind し、READY を出力し、受信スレッドを起動する
     */
    bool BridgeServerHost::Start(uint16_t port, norves::bridge::IBridgeEngineAdapter& adapter)
    {
        if (m_bActive.GetValue())
        {
            NORVES_LOG_WARNING("Bridge", "BridgeServerHost::Start ignored: already active");
            return true;
        }

        // WebSocket サーバー transport を bind（リトライ込み）。
        m_Transport = BindWithRetry(port);
        if (!m_Transport)
        {
            NORVES_LOG_ERROR("Bridge", "Failed to bind Bridge WebSocket server on port %u",
                             static_cast<unsigned>(port));
            return false;
        }

        // サーバーを生成（adapter を参照保持。adapter は本ホストより長生きする）。
        m_Server = NorvesLib::Core::Container::MakeUnique<norves::bridge::BridgeEngineServer>(adapter, nullptr);

        // bind 成功後に READY <port> を生ハンドルへ出力する（エディタはこれを待つ）。
        if (!WriteReadyLine(port))
        {
            NORVES_LOG_ERROR("Bridge", "Failed to write READY line for port %u",
                             static_cast<unsigned>(port));
            // READY を出せないとエディタは接続できないため、起動失敗として後始末する。
            m_Server.reset();
            m_Transport->close();
            m_Transport.reset();
            return false;
        }

        // 起動済みに遷移してから受信スレッドを開始する。
        m_bActive.SetValue(true);
        m_RecvThread.Start([this]() { RecvLoop(); });

        NORVES_LOG_INFO("Bridge", "Bridge server started on 127.0.0.1:%u",
                        static_cast<unsigned>(port));
        return true;
    }

    /**
     * @brief 受信スレッド本体
     *
     * @note 受信スレッド上でのみ実行され、GEngine/エンジン状態には触れない。
     */
    void BridgeServerHost::RecvLoop()
    {
        // 受信スレッド：transport->recv() をブロッキングで回し、得たフレームを
        // キューへ push するだけ。GEngine/エンジン状態には一切触れない。
        // recv() は close() 後にのみ nullopt を返す（クライアント切断では返らない）。
        while (true)
        {
            std::optional<std::string> frame = m_Transport->recv();
            if (!frame.has_value())
            {
                break; // close() 済み：クリーン終了。
            }

            NorvesLib::Thread::ScopedLock lock(m_QueueMutex);
            m_InboundFrames.push(std::move(*frame));
        }
    }

    /**
     * @brief キュー済みの受信フレームを処理し、応答送信とログ転送を行う
     *
     * @note ゲームスレッド（OnUpdate）上でのみ実行される。
     */
    void BridgeServerHost::DrainInbound()
    {
        if (!m_bActive.GetValue() || !m_Server || !m_Transport)
        {
            return;
        }

        // ロックは「キューから 1 フレーム取り出す」区間に限定し、handleFrame /
        // send はロック外で行う（recv スレッドの push を長くブロックしないため）。
        while (true)
        {
            std::string frame;
            {
                NorvesLib::Thread::ScopedLock lock(m_QueueMutex);
                if (m_InboundFrames.empty())
                {
                    break;
                }
                frame = std::move(m_InboundFrames.front());
                m_InboundFrames.pop();
            }

            // handleFrame とアダプタ呼び出しはこのゲームスレッド上で同期実行される。
            std::optional<std::string> response = m_Server->handleFrame(frame);
            if (response.has_value())
            {
                if (!m_Transport->send(std::move(*response)))
                {
                    // 送信先が居ない/満杯。本フレームは破棄し、次フレームへ進む
                    // （residential：接続が落ちてもサーバーは生かす）。
                    NORVES_LOG_WARNING("Bridge", "Bridge response send failed (peer gone or queue full)");
                }
            }
        }

        // 中継 sink に溜まったログを log.message イベントとして送る（ゲームスレッド）。
        // 1 フレームあたり kMaxDrainPerFrame 件までに制限し、残りは次フレームへ持ち越す。
        // EmitEvent は冒頭ガード（m_bActive / m_Server / m_Transport）を再確認するが、
        // ここに来た時点で全て有効。
        BridgeLogSink::ForwardEntry fe;
        for (int drained = 0; drained < kMaxDrainPerFrame && m_LogSink.TryPopForward(fe); ++drained)
        {
            norves::bridge::dto::LogMessageEvent evt;
            evt.level = FoldLevel(fe.level);
            // String は UTF-8 バイト列（TCHAR=char）。data()/size() でそのまま運ぶ。
            evt.message = std::string(fe.message.data(), fe.message.size());
            // category は schema 上 minLength:1。空なら未設定（optional を立てない）。
            if (!fe.category.empty())
            {
                evt.category = std::string(fe.category.data(), fe.category.size());
            }
            // timestamp は alpha では省略（未設定）。
            EmitEvent("log.message", evt.to_json());
        }
    }

    /**
     * @brief サーバーを停止する（close→join、冪等）
     *
     * @note StopLogForwarding → close → join の順を厳守する（逆順だと recv() が
     *       ブロックしたまま join がデッドロックする）。
     */
    void BridgeServerHost::Stop()
    {
        // ログ転送を先に止める（RemoveSink → close → join の順。L-P3a 契約）。
        // これで以降 Logger ワーカーが中継 sink を触らなくなり、sink の解体が安全になる。
        // 冪等なので Stop が複数回呼ばれても問題ない。
        StopLogForwarding();

        // 冪等。close→join 順を厳守する：先に close() で recv() を解除し、
        // その後で受信スレッドを Join する（逆順だと recv() がブロックしたまま
        // join がデッドロックする）。
        if (m_Transport)
        {
            m_Transport->close();
        }

        if (m_RecvThread.Joinable())
        {
            m_RecvThread.Join();
        }

        m_bActive.SetValue(false);

        // server は transport より先に破棄してよい（互いに直接参照しない）。
        m_Server.reset();
        m_Transport.reset();
    }

    /**
     * @brief サーバー発イベント（kind=event）をクライアントへ送る
     *
     * @note ゲームスレッド（DrainInbound 経由）上でのみ実行される。
     */
    void BridgeServerHost::EmitEvent(std::string_view eventName, const norves::bridge::JsonValue& params)
    {
        if (!m_bActive.GetValue() || !m_Server || !m_Transport)
        {
            return;
        }

        std::string frame = m_Server->emitEvent(eventName, params);
        if (frame.empty())
        {
            // emitEvent がエンコード失敗を空フレームで通知（不正なエンベロープは送らない）。
            return;
        }

        // 送信失敗（peer 不在 / キュー満杯）でも無言で破棄する。ここでログを出すと
        // log.message 経路に乗って再び EmitEvent され、増幅ループになり得るため。
        m_Transport->send(std::move(frame));
    }

    /**
     * @brief Logger -> Bridge のログ転送を開始する（log.subscribe 時）
     *
     * @note ゲームスレッド（DrainInbound 経由）上でのみ実行される。
     */
    void BridgeServerHost::StartLogForwarding(NorvesLib::Core::Logging::LogLevel minLevel)
    {
        // 転送レベルを設定し、開始時の積み残しを捨ててから登録する。
        m_LogSink.SetMinLevel(minLevel);
        m_LogSink.Clear();

        if (!m_bLogForwarding)
        {
            NorvesLib::Core::Logging::Logger::GetInstance().AddSink(&m_LogSink);
            m_bLogForwarding = true;
        }
        // 既に開始済みなら minLevel の更新のみ（再登録しない）。
    }

    /**
     * @brief Logger -> Bridge のログ転送を停止する（log.unsubscribe / Stop 時、冪等）
     */
    void BridgeServerHost::StopLogForwarding()
    {
        // 冪等。登録済みのときだけ Logger から外す。
        if (m_bLogForwarding)
        {
            NorvesLib::Core::Logging::Logger::GetInstance().RemoveSink(&m_LogSink);
            m_bLogForwarding = false;
        }
    }

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
