// ファストトラベルのスナップロード・先読み・ヒッチの数え方を確かめる。
#include "Streaming/StreamingSystem.h"

#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Streaming;

namespace
{
    int g_Failures = 0;

    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    bool Check(bool bCondition, const char8_t* message)
    {
        if (!bCondition)
        {
            ++g_Failures;
            std::cerr << Utf8(u8"検査失敗: ") << Utf8(message) << "\n";
        }
        return bCondition;
    }

    void TestHitchCounting()
    {
        StreamingHitchCounter warmup;
        bool bWarmupAccepted = true;
        for (uint32_t i = 0; i < 29; ++i)
        {
            bWarmupAccepted &= !warmup.AddFrame(16.0);
        }
        Check(bWarmupAccepted && !warmup.AddFrame(1000.0) && warmup.GetHitchCount() == 0 &&
              warmup.GetLastMedianMs() == 0.0,
              u8"最少標本数に達する前のフレームがヒッチとして数えられました");

        StreamingHitchCounter counter;
        for (uint32_t i = 0; i < 30; ++i)
        {
            counter.AddFrame(16.0);
        }
        Check(counter.AddFrame(33.0) && counter.GetHitchCount() == 1 &&
              counter.GetLastHitchFrameMs() == 33.0 && counter.GetLastMedianMs() == 16.0,
              u8"中央値の 2 倍を超えたフレームを記録できません");
        Check(!counter.AddFrame(32.0) && counter.GetHitchCount() == 1 &&
              counter.GetLastHitchFrameMs() == 33.0 && counter.GetLastMedianMs() == 16.0,
              u8"中央値のちょうど 2 倍をヒッチとして数えました");

        StreamingHitchCounter moving(4, 2);
        Check(!moving.AddFrame(2.0), u8"最初の標本をヒッチとして数えました");
        Check(!moving.AddFrame(4.0), u8"最少標本数に達する前にヒッチを数えました");
        Check(moving.AddFrame(20.0) && moving.GetLastMedianMs() == 3.0,
              u8"偶数個の標本の中央値が中央の 2 つの平均になりません");
        Check(!moving.AddFrame(3.0) && moving.GetLastMedianMs() == 4.0,
              u8"奇数個の標本の中央値が正しくありません");
        Check(!moving.AddFrame(5.0) && moving.GetLastMedianMs() == 3.5,
              u8"満杯の窓の中央値が正しくありません");
        Check(!moving.AddFrame(6.0) && moving.GetLastMedianMs() == 4.5,
              u8"最も古い標本を捨てた後の中央値が正しくありません");
        Check(!moving.AddFrame(7.0) && moving.GetLastMedianMs() == 5.5,
              u8"窓を再び更新した後の中央値が正しくありません");

        StreamingHitchCounter invalid(4, 2);
        invalid.AddFrame(4.0);
        invalid.AddFrame(6.0);
        const double invalidFrames[] = {
            -1.0,
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN(),
        };
        bool bRejected = true;
        for (double frame : invalidFrames)
        {
            bRejected &= !invalid.AddFrame(frame);
        }
        Check(bRejected && invalid.GetHitchCount() == 0 && invalid.GetLastMedianMs() == 0.0 &&
              invalid.AddFrame(11.0) && invalid.GetHitchCount() == 1 &&
              invalid.GetLastHitchFrameMs() == 11.0 && invalid.GetLastMedianMs() == 5.0,
              u8"非有限または負の値が窓や回数に影響しました");
        Check(!invalid.AddFrame(-1.0) && invalid.GetLastHitchFrameMs() == 11.0 &&
              invalid.GetLastMedianMs() == 5.0,
              u8"無効な入力で直前の判定記録が変わりました");

        counter.Reset();
        Check(counter.GetHitchCount() == 0 && counter.GetLastHitchFrameMs() == 0.0 &&
              counter.GetLastMedianMs() == 0.0 && !counter.AddFrame(33.0) &&
              counter.GetHitchCount() == 0 && counter.GetLastMedianMs() == 0.0,
              u8"リセットで窓と判定記録が空になりません");

        StreamingHitchCounter clamped(0, 30);
        Check(!clamped.AddFrame(1.0) && clamped.AddFrame(3.0) &&
              clamped.GetHitchCount() == 1 && clamped.GetLastMedianMs() == 1.0,
              u8"窓の長さと最少標本数が正しく丸められません");
        StreamingHitchCounter minimum(4, 0);
        Check(!minimum.AddFrame(1.0) && !minimum.AddFrame(2.0) && minimum.GetLastMedianMs() == 1.0,
              u8"最少標本数が 0 のときに 1 へ丸められません");
    }
}

int main()
{
    TestHitchCounting();
    if (g_Failures != 0)
    {
        std::cerr << Utf8(u8"検査失敗数: ") << g_Failures << "\n";
    }
    return g_Failures;
}
