#pragma once

#include <cstdint>
#include <cstddef>

namespace NorvesLib::Core::Engine
{

    /**
     * @brief 有理数のナノ秒刻みで進むフレームの時計
     *
     * 端数を次のフレームへ持ち越し、整数のナノ秒で経過時間を累積する。
     * 状態は GameThread だけが読み書きする。
     */
    class FixedFrameClock
    {
    public:
        /** @brief 正の刻み（最大 1 秒）を設定し、時計を有効にして経過時間をリセットする */
        bool SetStep(int64_t numeratorNanoseconds, int64_t denominator)
        {
            if (numeratorNanoseconds <= 0 || denominator <= 0 || denominator > 1'000'000'000 ||
                numeratorNanoseconds > denominator * 1'000'000'000)
            {
                return false;
            }

            m_StepNumeratorNanoseconds = numeratorNanoseconds;
            m_StepDenominator = denominator;
            m_bEnabled = true;
            Reset();
            return true;
        }

        /** @brief 整数のナノ秒刻みを設定する */
        bool SetStepNanoseconds(int64_t nanoseconds)
        {
            return SetStep(nanoseconds, 1);
        }

        [[nodiscard]] bool IsEnabled() const
        {
            return m_bEnabled;
        }

        /** @brief 刻みと経過時間を保ったまま時計を無効にする */
        void Disable()
        {
            m_bEnabled = false;
        }

        /** @brief 刻みと有効状態を保ち、累積・端数・直前の経過時間をリセットする */
        void Reset()
        {
            m_Carry = 0;
            m_DeltaNanoseconds = 0;
            m_TotalNanoseconds = 0;
        }

        /** @brief 端数を持ち越しながら 1 フレーム進め、その経過ナノ秒を返す */
        int64_t NextDeltaNanoseconds()
        {
            if (!m_bEnabled)
            {
                return 0;
            }

            // 累積を切り上げ、k×刻み以上かつ超過を 1 ns 未満に保つ。
            // 持ち越しは超過×分母であり、0 以上かつ分母未満となる。
            // need が 0 以下となる 1 ns 未満の刻みも、差分 0 として扱う。
            const int64_t need = m_StepNumeratorNanoseconds - m_Carry;
            m_DeltaNanoseconds = (need + m_StepDenominator - 1) / m_StepDenominator;
            m_Carry = m_DeltaNanoseconds * m_StepDenominator - need;
            m_TotalNanoseconds += m_DeltaNanoseconds;
            return m_DeltaNanoseconds;
        }

        [[nodiscard]] int64_t GetDeltaNanoseconds() const
        {
            return m_DeltaNanoseconds;
        }

        [[nodiscard]] int64_t GetTotalNanoseconds() const
        {
            return m_TotalNanoseconds;
        }

        [[nodiscard]] float GetDeltaSeconds() const
        {
            return static_cast<float>(static_cast<double>(m_DeltaNanoseconds) * 1e-9);
        }

        [[nodiscard]] double GetTotalSeconds() const
        {
            return static_cast<double>(m_TotalNanoseconds) * 1e-9;
        }

        [[nodiscard]] int64_t GetStepNumeratorNanoseconds() const
        {
            return m_StepNumeratorNanoseconds;
        }

        [[nodiscard]] int64_t GetStepDenominator() const
        {
            return m_StepDenominator;
        }

    private:
        bool m_bEnabled = false;
        int64_t m_StepNumeratorNanoseconds = 0;
        int64_t m_StepDenominator = 1;
        int64_t m_Carry = 0;
        int64_t m_DeltaNanoseconds = 0;
        int64_t m_TotalNanoseconds = 0;
    };

    /**
     * @brief 秒の分数または小数を整数だけで解析し、フレームの刻みを返す
     *
     * 失敗したときは出力を変更しない。小数部は最大 18 桁、各整数の読み取りは最大 19 桁とする。
     */
    inline bool TryParseFixedFrameDelta(const char* text, size_t length,
        int64_t& outNumeratorNanoseconds, int64_t& outDenominator)
    {
        if (text == nullptr || length == 0)
        {
            return false;
        }

        size_t separator = length;
        for (size_t index = 0; index < length; ++index)
        {
            const char character = text[index];
            if (character == '/' || character == '.')
            {
                if (separator != length)
                {
                    return false;
                }
                separator = index;
            }
            else if (character < '0' || character > '9')
            {
                return false;
            }
        }

        // 既存の値に数字を継ぎ足し、小数の仮数も同じ上限で読み取る。
        const auto appendDigits = [text](size_t begin, size_t end, int64_t& value, size_t& digitCount)
        {
            if (begin == end)
            {
                return false;
            }
            for (size_t index = begin; index < end; ++index)
            {
                const int64_t digit = text[index] - '0';
                if (digitCount >= 19 || value > (INT64_MAX - digit) / 10)
                {
                    return false;
                }
                value = value * 10 + digit;
                ++digitCount;
            }
            return true;
        };

        int64_t numerator = 0;
        int64_t denominator = 1;
        size_t digitCount = 0;
        if (!appendDigits(0, separator, numerator, digitCount))
        {
            return false;
        }

        if (separator != length && text[separator] == '/')
        {
            denominator = 0;
            size_t denominatorDigitCount = 0;
            if (!appendDigits(separator + 1, length, denominator, denominatorDigitCount) ||
                numerator > INT64_MAX / 1'000'000'000)
            {
                return false;
            }
            numerator *= 1'000'000'000;
        }
        else
        {
            size_t fractionalDigits = 0;
            if (separator != length)
            {
                fractionalDigits = length - separator - 1;
                if (fractionalDigits > 18 || !appendDigits(separator + 1, length, numerator, digitCount))
                {
                    return false;
                }
            }

            if (fractionalDigits <= 9)
            {
                for (size_t index = fractionalDigits; index < 9; ++index)
                {
                    if (numerator > INT64_MAX / 10)
                    {
                        return false;
                    }
                    numerator *= 10;
                }
            }
            else
            {
                for (size_t index = 9; index < fractionalDigits; ++index)
                {
                    denominator *= 10;
                }
            }
        }

        FixedFrameClock clock;
        if (!clock.SetStep(numerator, denominator))
        {
            return false;
        }
        outNumeratorNanoseconds = numerator;
        outDenominator = denominator;
        return true;
    }

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
