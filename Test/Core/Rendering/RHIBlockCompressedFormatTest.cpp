// BC 形式と R16_UNORM の RHI 契約テスト。
// 形式のブロックの幅・高さ・バイト数と、ミップを1ブロックまで切り上げて数える確保量の見積りを、GPU を使わずに確かめる。
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/RHITypes.h"

#include <cstdint>
#include <iostream>

namespace NorvesLib
{
namespace
{

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "RHIBlockCompressedFormatTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

RHI::TextureDesc MakeDesc(uint32_t width, uint32_t height, uint32_t mipLevels, RHI::Format format)
{
    RHI::TextureDesc desc;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mipLevels;
    desc.TextureFormat = format;
    return desc;
}

void ExpectBlock(RHI::Format format, uint32_t width, uint32_t height, uint32_t bytes, const char* message)
{
    const RHI::FormatBlockInfo info = RHI::GetFormatBlockInfo(format);
    Expect(info.BlockWidth == width && info.BlockHeight == height && info.BlockBytes == bytes, message);
}

void TestBlockInfo()
{
    ExpectBlock(RHI::Format::BC1_UNORM, 4, 4, 8, "BC1_UNORM は 4x4 画素で 8 バイト");
    ExpectBlock(RHI::Format::BC1_SRGB, 4, 4, 8, "BC1_SRGB は 4x4 画素で 8 バイト");
    ExpectBlock(RHI::Format::BC4_UNORM, 4, 4, 8, "BC4_UNORM は 4x4 画素で 8 バイト");
    ExpectBlock(RHI::Format::BC5_UNORM, 4, 4, 16, "BC5_UNORM は 4x4 画素で 16 バイト");
    ExpectBlock(RHI::Format::BC7_UNORM, 4, 4, 16, "BC7_UNORM は 4x4 画素で 16 バイト");
    ExpectBlock(RHI::Format::BC7_SRGB, 4, 4, 16, "BC7_SRGB は 4x4 画素で 16 バイト");
    // 非圧縮は 1x1 を1ブロックとして扱う。
    ExpectBlock(RHI::Format::R16_UNORM, 1, 1, 2, "R16_UNORM は 1x1 画素で 2 バイト");
    ExpectBlock(RHI::Format::R8G8B8A8_UNORM, 1, 1, 4, "RGBA8 は 1x1 画素で 4 バイト");

    Expect(RHI::IsBlockCompressedFormat(RHI::Format::BC7_SRGB), "BC7_SRGB はブロック圧縮");
    Expect(RHI::IsBlockCompressedFormat(RHI::Format::BC1_UNORM), "BC1_UNORM はブロック圧縮");
    Expect(!RHI::IsBlockCompressedFormat(RHI::Format::R16_UNORM), "R16_UNORM はブロック圧縮でない");
    Expect(!RHI::IsBlockCompressedFormat(RHI::Format::R8G8B8A8_SRGB), "RGBA8_SRGB はブロック圧縮でない");
}

void TestEstimate()
{
    // 4096x4096 の全ミップ(13段): 4x4 ブロックの数は 4^10 + ... + 1(11段) に、1ブロック未満になる 2 段を足す。
    Expect(RHI::EstimateTextureSize(MakeDesc(4096, 4096, 13, RHI::Format::BC7_UNORM)) == 22369648ull,
           "BC7 4096x4096 の全ミップは 22,369,648 バイトでなければならない");
    Expect(RHI::EstimateTextureSize(MakeDesc(4096, 4096, 13, RHI::Format::BC7_SRGB)) == 22369648ull,
           "BC7_SRGB 4096x4096 の全ミップは 22,369,648 バイトでなければならない");
    Expect(RHI::EstimateTextureSize(MakeDesc(4096, 4096, 13, RHI::Format::BC5_UNORM)) == 22369648ull,
           "BC5 4096x4096 の全ミップは 22,369,648 バイトでなければならない");
    Expect(RHI::EstimateTextureSize(MakeDesc(4096, 4096, 13, RHI::Format::BC4_UNORM)) == 11184824ull,
           "BC4 4096x4096 の全ミップは 11,184,824 バイトでなければならない");
    Expect(RHI::EstimateTextureSize(MakeDesc(4096, 4096, 13, RHI::Format::BC1_UNORM)) == 11184824ull,
           "BC1 4096x4096 の全ミップは 11,184,824 バイトでなければならない");
    Expect(RHI::EstimateTextureSize(MakeDesc(1024, 1024, 11, RHI::Format::R16_UNORM)) == 2796202ull,
           "R16 1024x1024 の全ミップは 2,796,202 バイトでなければならない");

    // ミップの最小は1ブロック。1x1・2x2 でも BC7 は 16 バイト。
    Expect(RHI::EstimateTextureSize(MakeDesc(1, 1, 1, RHI::Format::BC7_UNORM)) == 16ull,
           "BC7 1x1 は 1 ブロック(16 バイト)として数える");
    Expect(RHI::EstimateTextureSize(MakeDesc(2, 2, 2, RHI::Format::BC1_UNORM)) == 8ull + 8ull,
           "BC1 の 2x2 と 1x1 はそれぞれ 1 ブロック(8 バイト)として数える");
    // 4 の倍数でない大きさは切り上げる(5x5 は 2x2 ブロック)。
    Expect(RHI::EstimateTextureSize(MakeDesc(5, 5, 1, RHI::Format::BC7_UNORM)) == 4ull * 16,
           "BC7 5x5 は 2x2 ブロックに切り上げる");
    // 配列数は掛ける。
    RHI::TextureDesc array = MakeDesc(8, 8, 1, RHI::Format::BC5_UNORM);
    array.ArraySize = 6;
    Expect(RHI::EstimateTextureSize(array) == 4ull * 16 * 6, "配列数が掛け合わされる");

    // 既存の非圧縮の見積りは変わらない。
    Expect(RHI::EstimateTextureSize(MakeDesc(4096, 4096, 13, RHI::Format::R8G8B8A8_UNORM)) == 89478484ull,
           "RGBA8 4096x4096 の全ミップは 89,478,484 バイトのまま");
}

int RunTest()
{
    TestBlockInfo();
    TestEstimate();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "RHIBlockCompressedFormatTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
