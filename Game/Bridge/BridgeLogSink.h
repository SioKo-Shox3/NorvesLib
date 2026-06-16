#pragma once

// Workstream L-P3b: Logger -> Bridge log.message 転送用の中継 sink。
//
// NorvesLib の Logger（NorvesLib::Core::Logging::ILogSink）に登録され、Logger ワーカー
// スレッドから届く各 LogEntry を内部キューへ積むだけの軽量 sink。実際の Bridge への
// 配信（log.message イベントの emit）は、ゲームスレッドの DrainInbound から
// TryPopForward で取り出して BridgeServerHost::EmitEvent が行う。
//
// スレッド設計（必読）:
//   * OnLog は Logger ワーカースレッド（または同期 Log 呼び出し元スレッド）から、
//     Logger の m_sinkMutex 保持下で直列に呼ばれる。OnLog は transport / Logger に
//     一切触れず（再入禁止）、自分の m_Mutex だけを取り、ForwardEntry をキューへ
//     push して即座に返る（高速返却の要求）。例外は投げない。
//   * TryPopForward はゲームスレッド（DrainInbound 経由）から呼ばれ、同じく m_Mutex
//     だけを取ってキューから 1 件取り出す。Logger を一切呼ばない。
//
// ロック順序（m-2）:
//   Logger::m_sinkMutex -> BridgeLogSink::m_Mutex の一方向のみ（OnLog 経路）。
//   drain 経路（TryPopForward）は m_Mutex だけを取り Logger へは触れない。よって
//   m_Mutex と Logger::m_sinkMutex の間に逆転は発生しない。
//
// SDK 非設定ビルド対応:
//   このファイルは丸ごと NORVES_BRIDGE_ENABLED でガードする。非 SDK ビルドでは
//   何も定義されず、Game/Bridge/*.cpp の GLOB 収集に乗っても空コンパイルとなる。
//   この sink 自体は SDK 型に依存しないが、BridgeServerHost からのみ参照され、
//   その参照経路が同じガード下にあるため、ガードを揃えておく。

#if defined(NORVES_BRIDGE_ENABLED)

#include <cstddef>
#include <cstdint>
#include <utility>

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Logging/LogTypes.h"
#include "Core/Public/Thread/Atomic.h"
#include "Core/Public/Thread/Mutex.h"

namespace Game::Bridge
{

    /**
     * @brief Logger -> Bridge 転送中継 sink（Logger ワーカーで push、ゲームスレッドで pop）
     */
    class BridgeLogSink final : public NorvesLib::Core::Logging::ILogSink
    {
    public:
        /**
         * @brief Logger から取り出した 1 件分のログ（転送用スナップショット）
         *
         * LogEntry のうち Bridge log.message に必要な level / category / message のみを
         * 値コピーで保持する（timestamp は alpha では省略）。Logger の LogEntry を借用
         * せず、独立した寿命を持つ。host はこれを BridgeLogSink::ForwardEntry として参照する。
         */
        struct ForwardEntry
        {
            NorvesLib::Core::Logging::LogLevel level = NorvesLib::Core::Logging::LogLevel::Info;
            NorvesLib::Core::Container::String category;
            NorvesLib::Core::Container::String message;
        };

        BridgeLogSink() = default;
        ~BridgeLogSink() override = default;

        BridgeLogSink(const BridgeLogSink &) = delete;
        BridgeLogSink &operator=(const BridgeLogSink &) = delete;
        BridgeLogSink(BridgeLogSink &&) = delete;
        BridgeLogSink &operator=(BridgeLogSink &&) = delete;

        /**
         * @brief Logger からのログ配送を受ける（Logger ワーカースレッドで呼ばれる）
         *
         * m_MinLevel 未満は捨てる。それ以外は ForwardEntry へコピーし、m_Mutex 下で
         * キューへ push する。満杯時は最古を 1 件捨てて m_Dropped を加算する。
         * transport / Logger には一切触れず、例外も投げない（ILogSink 契約）。
         */
        void OnLog(const NorvesLib::Core::Logging::LogEntry &entry) override;

        /**
         * @brief キュー先頭の 1 件を取り出す（ゲームスレッドが DrainInbound から呼ぶ）
         * @return 取り出せたら true、空なら false
         */
        bool TryPopForward(ForwardEntry &out);

        /**
         * @brief 転送する最小ログレベルを設定する（log.subscribe 時に更新）
         */
        void SetMinLevel(NorvesLib::Core::Logging::LogLevel level);

        /**
         * @brief キューを空にする（log.subscribe 開始時に積み残しを捨てる）
         */
        void Clear();

    private:
        // キュー容量の上限。溢れたら最古から捨てる（残留メモリの暴走を防ぐ）。
        static constexpr std::size_t kMaxQueued = 4096;

        // 転送キュー本体と保護ロック。OnLog（push）/ TryPopForward（pop）/ Clear が
        // この m_Mutex だけを取る。Logger へは触れない（ロック順序 m-2 を参照）。
        NorvesLib::Thread::Mutex m_Mutex;
        NorvesLib::Core::Container::Queue<ForwardEntry> m_Queue;

        // 転送最小レベル。既定は Trace=0（全通し）。alpha は server 側フィルタを
        // 最小限に留め、レベル絞り込みは editor 側 client フィルタへ委ねる。
        NorvesLib::Thread::Atomic<std::uint8_t> m_MinLevel{
            static_cast<std::uint8_t>(NorvesLib::Core::Logging::LogLevel::Trace)};

        // 溢れて捨てた件数（観測用。alpha では emit しない）。
        NorvesLib::Thread::Atomic<std::uint64_t> m_Dropped{0};
    };

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
