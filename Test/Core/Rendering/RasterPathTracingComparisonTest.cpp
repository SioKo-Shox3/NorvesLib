// ラスタとPTの比較の部品（RasterPathTracingComparison）のCPU契約。3組の中央値は同値を含む27通りで
// 輝度が中央の組を選ぶこと、太陽の可視の食い違いによる除外は影の縁の近くだけで、影の内側の食い違い
// （局所欠陥）は判定に残ること。
#include "RenderingValidation/RasterPathTracingComparison.h"

#include <cstdint>
#include <iostream>

namespace
{
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Test::RenderingValidation;

    int g_Failures = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_Failures;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    RgbaFloatImage MakeImage(uint32_t width, uint32_t height, float value)
    {
        RgbaFloatImage image;
        image.Width = width;
        image.Height = height;
        image.Values = VariableArray<float>(static_cast<size_t>(width) * height * 4u, value);
        return image;
    }

    void SetGray(RgbaFloatImage& image, uint32_t x, uint32_t y, float value)
    {
        const size_t pixel = static_cast<size_t>(y) * image.Width + x;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            image.Values[pixel * 4u + channel] = value;
        }
    }

    // 輝度0・1・2の3組の27通り（同値を含む）で、選んだ組の輝度が中央値に等しい。
    void TestMedianOfThreeHandlesTies()
    {
        uint32_t mismatches = 0u;
        for (uint32_t a = 0u; a < 3u; ++a)
        {
            for (uint32_t b = 0u; b < 3u; ++b)
            {
                for (uint32_t c = 0u; c < 3u; ++c)
                {
                    const RgbaFloatImage result = MedianOfThree(MakeImage(1u, 1u, static_cast<float>(a)),
                                                                MakeImage(1u, 1u, static_cast<float>(b)),
                                                                MakeImage(1u, 1u, static_cast<float>(c)));
                    const uint32_t low = a < b ? (a < c ? a : c) : (b < c ? b : c);
                    const uint32_t high = a > b ? (a > c ? a : c) : (b > c ? b : c);
                    const float expected = static_cast<float>(a + b + c - low - high);
                    if (result.Width != 1u || result.Values[0] != expected)
                    {
                        ++mismatches;
                    }
                }
            }
        }
        std::cout << "median_of_three cases=27 mismatches=" << mismatches << '\n';
        Expect(mismatches == 0u, "MedianOfThree picks the median luminance, ties included");
        Expect(MedianOfThree(MakeImage(1u, 1u, 0.0f), MakeImage(1u, 1u, 0.0f), MakeImage(2u, 1u, 0.0f)).Width == 0u,
               "MedianOfThree returns an empty image for mismatched sizes");
    }

    // 縦の影の縁（x<8が日向、x>=8が影）で、縁の隣の食い違いは外し、縁から離れた影の内側の食い違いは残す。
    void TestSunVisibilityExclusionIsLimitedToEdges()
    {
        constexpr uint32_t Size = 16u;
        RgbaFloatImage path = MakeImage(Size, Size, 0.0f);
        for (uint32_t y = 0u; y < Size; ++y)
        {
            for (uint32_t x = 0u; x < 8u; ++x)
            {
                SetGray(path, x, y, 1.0f);
            }
        }
        RgbaFloatImage raster = path;
        SetGray(raster, 8u, 4u, 0.9f);   // 縁の隣: ラスタは日向、PTは影
        SetGray(raster, 14u, 12u, 1.0f); // 影の内側（縁から6画素）: ラスタだけが太陽を見る欠陥
        VariableArray<uint8_t> agreement(static_cast<size_t>(Size) * Size, 1u);
        const uint32_t excluded = ExcludeSunVisibilityDisagreement(raster, path, 0.2f, 2u, agreement);
        Expect(excluded == 1u, "only the disagreement next to the shadow edge is excluded");
        Expect(agreement[4u * Size + 8u] == 0u, "the edge pixel leaves the agreeing set");
        Expect(agreement[12u * Size + 14u] == 1u, "the interior shadow defect stays in the agreeing set");

        const VariableArray<uint8_t> allAgree(static_cast<size_t>(Size) * Size, 1u);
        Expect(SunVisibilityExclusionKeepsInteriorDefect(path, path, 0.2f, 2u, allAgree),
               "the negative control finds an interior shadow pixel and keeps the injected defect");
    }
}

int main()
{
    TestMedianOfThreeHandlesTies();
    TestSunVisibilityExclusionIsLimitedToEdges();
    if (g_Failures != 0)
    {
        std::cerr << "RasterPathTracingComparisonTest failures: " << g_Failures << '\n';
        return 1;
    }
    std::cout << "RasterPathTracingComparisonTest passed\n";
    return 0;
}
