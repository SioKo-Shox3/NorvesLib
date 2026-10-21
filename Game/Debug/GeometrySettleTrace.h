#pragma once

#include <chrono>
#include <cstdint>

namespace Game::Debug
{
    /**
     * @brief ジオメトリのページの読み込みが落ち着いた後の推移を、決めた秒数だけ 1 秒ごとにログへ書く（--geom-settle-trace。開発ビルドだけ）
     *
     * 読み込み中（非同期の資産・骨格の資産・SceneReady の前・ジオメトリのページの読み込み）を一度見た後、読み込み中でない描画が
     * kQuietFrames 続き、さらにページの書き込みと追い出しの累計が kStreamingQuietFrames の描画の間変わらなかった時点を落ち着いた
     * 時点とする（予算が足りないとき、ストリーマは書き込みを見送った直後に待ちが無いと答えるが、細かい段から粗い段への入れ替えが
     * まだ続くことがあるため）。kMaxStreamingWaitSeconds 待っても止まらなければ、その時点から記録する（settle_timeout=1。検査が落とす）。
     * そこから 1 秒ごとに、要求・読み込み中・追い出し・常駐の量・段ごとの常駐の数・ページの目標・ジオメトリの枠の目標を
     * GEOM_SETTLE_TRACE の行へ書く。始めと終わりに GEOM_SETTLE_TRACE_BEGIN・_END を書く。
     * 書き終えるまでは、ApplicationProcessor の --capture-png の撮影と終了を止めておく（IsHolding を
     * GameApplicationHandler::HasPendingAssetConsumers が返す）ので、撮影は記録の後の落ち着いた状態を写す。
     * 検査は Scripts/Terrain/ReferenceCheck.py --geom-settle が行の値を読む。
     */
    class GeometrySettleTrace
    {
    public:
        /// 読み込み中でない描画がこの数だけ続いたら、落ち着いたとみなして記録を始める
        static constexpr uint32_t kQuietFrames = 60;
        /// 読み込み中を一度も見ないまま、この数の描画が過ぎたら記録を始める
        static constexpr uint32_t kFallbackFrames = 600;
        /// ページの書き込みと追い出しがこの数の描画の間止まったら、ストリーミングが落ち着いたとみなす
        static constexpr uint32_t kStreamingQuietFrames = 120;
        /// 読み込みが落ち着いてから、ストリーミングが止まるのを待つ上限（秒）
        static constexpr double kMaxStreamingWaitSeconds = 120.0;

        /// 記録する秒数を決める（0 は記録しない）
        void Configure(uint32_t seconds) { m_Seconds = seconds; }

        bool IsEnabled() const { return m_Seconds > 0; }

        /// 記録を書き終えるまで true（撮影と終了を止める）
        bool IsHolding() const { return IsEnabled() && m_State != State::Done; }

        /// 描画の後に毎フレーム呼ぶ（GameThread）
        void OnPostRender();

    private:
        enum class State : uint8_t
        {
            WaitingSettle,
            WaitingStreaming,
            Tracing,
            Done
        };

        void BeginTrace(bool bTimedOut);
        void WriteSample(double elapsedSeconds);

        uint32_t m_Seconds = 0;
        State m_State = State::WaitingSettle;
        bool m_bSawPending = false;
        uint32_t m_QuietFrames = 0;
        uint32_t m_FrameCount = 0;
        uint32_t m_SamplesWritten = 0;
        // ストリーミングの止まりを見る間の、書き込みと追い出しの累計の和と、それが変わらなかった描画の数
        uint64_t m_LastStreamingActivity = 0;
        uint32_t m_StreamingQuietFrames = 0;
        std::chrono::steady_clock::time_point m_StreamingWaitStart;
        std::chrono::steady_clock::time_point m_StartTime;
    };
} // namespace Game::Debug
