#pragma once

#include <cstdint>

namespace NorvesLib::Core::Engine
{

    /**
     * @brief 決定的な撮影（--capture-deterministic）の状態
     *
     * 同じコードを2回撮ると一致する撮影のために、壁時計に依らない固定刻みの時間と、
     * 「読み込み完了の時点（エポック）」から数えるフレーム番号を持つ。
     * 有効な間、ApplicationProcessor は毎フレームの経過時間を 1/60 秒に固定し、読み込みが
     * 落ち着いた時点でエポックを始める。エポックの最初のフレームを 0 番として、シミュレーションの時間
     * （大きな球の自転など）と描画側の時間的な状態（TAA の揺らし・RTGI の乱数と履歴・自動露出）を
     * そこから数え直す。
     *
     * 状態は GEngine のメンバとして持ち、GameThread だけが読み書きする。
     */
    class DeterministicCapture
    {
    public:
        /** @brief 固定刻みの経過時間（秒） */
        static constexpr float FixedDeltaSeconds = 1.0f / 60.0f;
        /** @brief 固定刻みの経過時間（ナノ秒。1/60 秒を切り上げた値） */
        static constexpr int64_t FixedDeltaNanoseconds = 16'666'667;

        /** @brief 決定的な撮影を有効にする（起動引数の解釈で一度だけ呼ぶ） */
        void Enable()
        {
            m_bEnabled = true;
        }

        [[nodiscard]] bool IsEnabled() const
        {
            return m_bEnabled;
        }

        /**
         * @brief シーンの非同期の組み立てがすべて終わったか
         *
         * テクスチャ・モデルの読み込みは描画側が数えるが、読み込み後にシーンへ組み込む処理
         * （大きな球の生成など）は GameMode が数える。false の間はエポックを始めない。
         */
        void SetSceneReady(bool bReady)
        {
            m_bSceneReady = bReady;
        }

        [[nodiscard]] bool IsSceneReady() const
        {
            return m_bSceneReady;
        }

        /** @brief 次のフレームを経過 0 番としてエポックを始める（読み込み完了の判定が変わるたびに呼ぶ） */
        void BeginEpoch()
        {
            m_bEpochPending = true;
            m_bEpochActive = false;
            m_EpochFrames = 0;
        }

        /** @brief フレームの最初に一度呼ぶ。エポックの 0 番のフレームを確定し、以降は数を進める */
        void AdvanceFrame()
        {
            if (m_bEpochPending)
            {
                m_bEpochPending = false;
                m_bEpochActive = true;
                m_EpochFrames = 0;
            }
            else if (m_bEpochActive)
            {
                ++m_EpochFrames;
            }
        }

        /** @brief エポックが始まっているか（0 番のフレームを過ぎたか） */
        [[nodiscard]] bool IsEpochActive() const
        {
            return m_bEpochActive;
        }

        /** @brief エポックの 0 番のフレームからの経過フレーム数 */
        [[nodiscard]] uint64_t GetEpochFrames() const
        {
            return m_EpochFrames;
        }

        /** @brief エポックからのシミュレーションの時間（秒）。エポック前は 0 */
        [[nodiscard]] double GetEpochSeconds() const
        {
            return m_bEpochActive ? static_cast<double>(m_EpochFrames) * static_cast<double>(FixedDeltaSeconds) : 0.0;
        }

    private:
        bool m_bEnabled = false;
        bool m_bSceneReady = true;
        bool m_bEpochPending = false;
        bool m_bEpochActive = false;
        uint64_t m_EpochFrames = 0;
    };

} // namespace NorvesLib::Core::Engine
