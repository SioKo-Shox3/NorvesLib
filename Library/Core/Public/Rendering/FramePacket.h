#pragma once

#include "RenderTypes.h"
#include "SceneProxy.h"
#include "DrawCommand.h"
#include "ViewportSnapshot.h"
#include "Container/Containers.h"
#include "Thread/Atomic.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief フレームパケットの状態
     */
    enum class FramePacketState : uint8_t
    {
        Empty,     // 空（書き込み可能）
        Writing,   // 書き込み中
        Ready,     // 読み取り可能
        Queued,    // RenderThread待機中
        Reading,   // 読み取り中
        Recycling  // Clear中の一時状態
    };

    // ========================================
    // FramePacket
    // ========================================

    /**
     * @brief フレームパケット
     *
     * 1フレーム分の描画データを格納する構造体。
     * Triple Bufferingで使用され、GameThreadとRenderThreadの
     * 同期を最小限に抑えます。
     */
    struct FramePacket
    {
        // ========================================
        // フレーム情報
        // ========================================

        uint64_t FrameNumber = 0; // フレーム番号
        float DeltaTime = 0.0f;   // 前フレームからの経過時間
        double TotalTime = 0.0;   // アプリケーション開始からの経過時間

        // ========================================
        // シーンデータ
        // ========================================

        bool bHasMainCamera = false;
        SceneProxy Scene;

        // ========================================
        // DrawCommandスナップショット（GameThreadで生成、RenderThreadで読み取り専用）
        // ========================================

        /** @brief 全DrawCommandのスナップショット（Opaque→Transparent順の単一実体配列） */
        Container::VariableArray<DrawCommand> DrawCommands;

        /** @brief DrawCommands全体の範囲 */
        CommandRange DrawCommandRange;

        /** @brief DrawCommands内の不透明DrawCommand範囲 */
        CommandRange OpaqueCommandRange;

        /** @brief DrawCommands内の半透明DrawCommand範囲 */
        CommandRange TransparentCommandRange;

        /** @brief DrawCommandから参照するGPUシーンインスタンスデータ */
        Container::VariableArray<GPUSceneInstanceData> InstanceData;

        // ========================================
        // View/Viewport render plan（新描画フロー用）
        // ========================================

        /** @brief Screen配下のView/Viewportごとの描画入力 */
        Container::VariableArray<ViewRenderPlan> Views;

        // ========================================
        // 状態管理
        // ========================================

        Thread::Atomic<uint8_t> State{static_cast<uint8_t>(FramePacketState::Empty)};

        // ========================================
        // メソッド
        // ========================================

        /**
         * @brief パケットをクリア
         */
        void Clear()
        {
            FrameNumber = 0;
            DeltaTime = 0.0f;
            TotalTime = 0.0;
            bHasMainCamera = false;
            Scene.Clear();
            DrawCommands.clear();
            DrawCommandRange = CommandRange{};
            OpaqueCommandRange = CommandRange{};
            TransparentCommandRange = CommandRange{};
            InstanceData.clear();
            Views.clear();
        }

        /**
         * @brief 現在の状態を取得
         */
        FramePacketState GetState() const
        {
            return static_cast<FramePacketState>(State.Load(std::memory_order_acquire));
        }

        /**
         * @brief 状態を設定
         */
        void SetState(FramePacketState newState)
        {
            State.Store(static_cast<uint8_t>(newState), std::memory_order_release);
        }

        /**
         * @brief 状態をアトミックに比較・交換
         * @param expected 期待される状態
         * @param desired 設定したい状態
         * @return 交換に成功した場合true
         */
        bool CompareExchangeState(FramePacketState expected, FramePacketState desired)
        {
            uint8_t expectedVal = static_cast<uint8_t>(expected);
            return State.CompareExchangeStrong(expectedVal, static_cast<uint8_t>(desired),
                                               std::memory_order_acq_rel);
        }
    };

    // ========================================
    // FramePacketManager
    // ========================================

    /**
     * @brief フレームパケットバッファ数
     *
     * GameThread/RenderThread間のproducer/consumerキューのスロット数です。
     * SwapChainやRHIのframes-in-flight数とは独立して管理します。
     */
    constexpr uint32_t FRAME_PACKET_BUFFER_COUNT = 3;

    /**
     * @brief フレームパケットマネージャー
     *
     * Triple Bufferingを管理し、GameThreadとRenderThread間の
     * ロックフリーな同期を提供します。
     */
    class FramePacketManager
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        FramePacketManager() = default;

        /**
         * @brief 初期化
         */
        void Initialize()
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                m_Packets[i].Clear();
                m_Packets[i].SetState(FramePacketState::Empty);
            }
            m_WriteIndex.Store(0, std::memory_order_release);
            m_ReadIndex.Store(0, std::memory_order_release);
            m_CurrentFrameNumber.Store(0, std::memory_order_release);
        }

        /**
         * @brief 終了処理
         */
        void Shutdown()
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                m_Packets[i].Clear();
            }
        }

        // ========================================
        // GameThread用インターフェース
        // ========================================

        /**
         * @brief 書き込み用パケットを取得（GameThread用）
         *
         * 書き込み可能なパケットを探し、Writing状態にして返します。
         * 全てのバッファが使用中の場合はnullptrを返します。
         *
         * @return 書き込み可能なパケット、なければnullptr
         */
        FramePacket *AcquireForWrite()
        {
            uint32_t writeIdx = m_WriteIndex.Load(std::memory_order_acquire);

            // 全スロットをスキャン（writeIdxから順に）して最初の空きを取得
            for (uint32_t attempt = 0; attempt < FRAME_PACKET_BUFFER_COUNT; ++attempt)
            {
                uint32_t idx = (writeIdx + attempt) % FRAME_PACKET_BUFFER_COUNT;
                if (m_Packets[idx].CompareExchangeState(FramePacketState::Empty,
                                                        FramePacketState::Writing))
                {
                    m_WriteIndex.Store(idx, std::memory_order_release);
                    m_Packets[idx].FrameNumber = m_CurrentFrameNumber.FetchAdd(1, std::memory_order_relaxed);
                    return &m_Packets[idx];
                }
            }

            // 全バッファが使用中
            return nullptr;
        }

        /**
         * @brief 書き込み完了を通知（GameThread用）
         *
         * パケットをReady状態にし、RenderThreadが読み取り可能にします。
         *
         * @param packet 書き込み完了したパケット
         */
        void FinishWrite(FramePacket *packet)
        {
            if (packet)
            {
                packet->SetState(FramePacketState::Ready);
                // 次のバッファへ進む
                uint32_t currentIdx = static_cast<uint32_t>(packet - m_Packets.data());
                m_WriteIndex.Store((currentIdx + 1) % FRAME_PACKET_BUFFER_COUNT, std::memory_order_release);
            }
        }

        /**
         * @brief 書き込みをキャンセル（GameThread用）
         *
         * @param packet キャンセルするパケット
         */
        void CancelWrite(FramePacket *packet)
        {
            if (packet)
            {
                packet->Clear();
                packet->SetState(FramePacketState::Empty);
            }
        }

        // ========================================
        // RenderThread用インターフェース
        // ========================================

        /**
         * @brief 読み取り用パケットを取得（RenderThread用）
         *
         * 読み取り可能な最新のパケットを探し、Reading状態にして返します。
         * 利用可能なパケットがない場合はnullptrを返します。
         *
         * @return 読み取り可能なパケット、なければnullptr
         */
        FramePacket *AcquireForRead()
        {
            // 最新のReadyパケットを探す
            FramePacket *latestReady = nullptr;
            uint64_t latestFrame = 0;

            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                if (m_Packets[i].GetState() == FramePacketState::Ready)
                {
                    if (m_Packets[i].FrameNumber >= latestFrame)
                    {
                        latestReady = &m_Packets[i];
                        latestFrame = m_Packets[i].FrameNumber;
                    }
                }
            }

            if (latestReady)
            {
                if (latestReady->CompareExchangeState(FramePacketState::Ready,
                                                      FramePacketState::Reading))
                {
                    return latestReady;
                }

                // CASレースに負けた場合、Readyな任意のパケットを取得（フレーム番号順不問）
                for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
                {
                    if (m_Packets[i].CompareExchangeState(FramePacketState::Ready,
                                                          FramePacketState::Reading))
                    {
                        return &m_Packets[i];
                    }
                }
            }

            return nullptr;
        }

        /**
         * @brief 読み取り完了を通知（RenderThread用）
         *
         * パケットをEmpty状態にし、再利用可能にします。
         *
         * @param packet 読み取り完了したパケット
         */
        void FinishRead(FramePacket *packet)
        {
            if (packet)
            {
                if (!packet->CompareExchangeState(FramePacketState::Reading,
                                                  FramePacketState::Recycling))
                {
                    return;
                }
                packet->Clear();
                packet->SetState(FramePacketState::Empty);
            }
        }

        // ========================================
        // シャットダウン・リサイズ補助
        // ========================================

        /**
         * @brief Writing状態のパケットをEmptyに戻す
         *
         * シャットダウンやリサイズ前にGameThread側の書き込みを安全に中断する。
         * Reading中のパケットはRenderThread完了を待つ必要があるため変更しない。
         */
        void CancelInflightWrites()
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                // Writing→Recyclingへ遷移して排他的に回収する
                if (m_Packets[i].CompareExchangeState(FramePacketState::Writing,
                                                      FramePacketState::Recycling))
                {
                    m_Packets[i].Clear();
                    m_Packets[i].SetState(FramePacketState::Empty);
                }
            }
        }

        /**
         * @brief Ready状態の未消費パケットをEmptyに戻す
         *
         * RenderThreadが動作していない（シングルスレッドモード）場合や
         * AcquireForWriteが失敗する前にGameThread側から呼び出し、
         * 蓄積したReadyパケットを再利用可能にします。
         *
         * 安全性: Ready→Writing（一時確保）→Clear→Emptyの順で遷移するため、
         * 並行するAcquireForWriteがClear前に同一スロットを取得しません。
         *
         * @return 解放したパケット数
         */
        uint32_t DrainUnconsumedPackets()
        {
            uint32_t count = 0;
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                // Ready→Writingに遷移して一時確保（AcquireForWriteとの競合防止）
                if (m_Packets[i].CompareExchangeState(FramePacketState::Ready,
                                                      FramePacketState::Writing))
                {
                    m_Packets[i].Clear();
                    m_Packets[i].SetState(FramePacketState::Empty);
                    ++count;
                }
            }
            return count;
        }

        /**
         * @brief Reading状態のパケットが全てなくなるまでスピン待機
         *
         * RenderThreadがAcquireForRead()/FinishRead()経由で
         * Reading状態を運用している構成向けの補助です。
         * 現在のシングルスレッド経路では同期境界としては使いません。
         *
         * @param maxSpinCount 最大スピン回数（デフォルト: 50000）
         * @return Readingパケットが全て消えた場合true、タイムアウトでfalse
         */
        bool SpinUntilReadingDrained(uint32_t maxSpinCount = 50000u) const
        {
            for (uint32_t spin = 0u; spin <= maxSpinCount; ++spin)
            {
                bool bHasReading = false;
                for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
                {
                    const auto state = m_Packets[i].GetState();
                    if (state == FramePacketState::Queued ||
                        state == FramePacketState::Reading)
                    {
                        bHasReading = true;
                        break;
                    }
                }
                if (!bHasReading)
                {
                    return true;
                }
                // キャッシュ同期フェンスを挿入して過剰なキャッシュ衝突を軽減
                std::atomic_thread_fence(std::memory_order_seq_cst);
            }
            return false;
        }

        /**
         * @brief Writing/Reading状態のパケットが存在するか
         *
         * シャットダウン・リサイズ前のドレイン確認に使用する。
         * @return inflight（処理中）のパケットがある場合true
         */
        bool HasInflightPackets() const
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                auto s = m_Packets[i].GetState();
                if (s == FramePacketState::Writing ||
                    s == FramePacketState::Queued ||
                    s == FramePacketState::Reading ||
                    s == FramePacketState::Recycling)
                {
                    return true;
                }
            }
            return false;
        }

        // ========================================
        // ステータス
        // ========================================

        /**
         * @brief 現在のフレーム番号を取得
         */
        uint64_t GetCurrentFrameNumber() const
        {
            return m_CurrentFrameNumber.Load(std::memory_order_acquire);
        }

        /**
         * @brief Readyなパケット数を取得
         */
        uint32_t GetReadyPacketCount() const
        {
            uint32_t count = 0;
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                if (m_Packets[i].GetState() == FramePacketState::Ready)
                {
                    ++count;
                }
            }
            return count;
        }

        /**
         * @brief 全パケットがEmptyかどうか
         */
        bool IsEmpty() const
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                if (m_Packets[i].GetState() != FramePacketState::Empty)
                {
                    return false;
                }
            }
            return true;
        }

    private:
        Container::FixedArray<FramePacket, FRAME_PACKET_BUFFER_COUNT> m_Packets;
        Thread::Atomic<uint32_t> m_WriteIndex{0};
        Thread::Atomic<uint32_t> m_ReadIndex{0};
        Thread::Atomic<uint64_t> m_CurrentFrameNumber{0};
    };

} // namespace NorvesLib::Core::Rendering
