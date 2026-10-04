#include "BlockCompressor.h"

#include "bc7decomp.h"
#include "bc7enc.h"
#include "rgbcx.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        constexpr uint32_t BlockSize = 4;
        constexpr uint32_t BlockPixelCount = BlockSize * BlockSize;
        constexpr uint32_t BytesPerPixel = 4;

        // bc7enc_rdo のテーブル初期化はグローバル状態を書き換えるので、最初の 1 回だけ行う。
        void EnsureEncoderInitialized()
        {
            static std::once_flag once;
            std::call_once(once, []() {
                rgbcx::init();
                bc7enc_compress_block_init();
            });
        }

        uint32_t ResolveBc1Level(BlockQuality quality) noexcept
        {
            switch (quality)
            {
            case BlockQuality::Fast: return 2;
            case BlockQuality::Best: return rgbcx::MAX_LEVEL;
            case BlockQuality::Normal:
            default: return 10;
            }
        }

        bc7enc_compress_block_params MakeBc7Params(const BlockCompressParams &params)
        {
            bc7enc_compress_block_params bc7;
            bc7enc_compress_block_params_init(&bc7);
            if (!params.bPerceptual)
            {
                bc7enc_compress_block_params_init_linear_weights(&bc7);
            }

            switch (params.Quality)
            {
            case BlockQuality::Fast:
                bc7.m_uber_level = 0;
                bc7.m_max_partitions = 16;
                break;
            case BlockQuality::Best:
                bc7.m_uber_level = BC7ENC_MAX_UBER_LEVEL;
                bc7.m_max_partitions = BC7ENC_MAX_PARTITIONS;
                break;
            case BlockQuality::Normal:
            default:
                bc7.m_uber_level = 1;
                bc7.m_max_partitions = 32;
                break;
            }
            return bc7;
        }

        // (blockX, blockY) の 4x4 画素を取り出す。画像の外は端の画素を複製する。
        void GatherBlock(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t blockX, uint32_t blockY, uint8_t *outPixels)
        {
            for (uint32_t py = 0; py < BlockSize; ++py)
            {
                const uint32_t sy = std::min(blockY * BlockSize + py, height - 1);
                for (uint32_t px = 0; px < BlockSize; ++px)
                {
                    const uint32_t sx = std::min(blockX * BlockSize + px, width - 1);
                    std::memcpy(outPixels + (py * BlockSize + px) * BytesPerPixel,
                                rgba + (static_cast<size_t>(sy) * width + sx) * BytesPerPixel,
                                BytesPerPixel);
                }
            }
        }

        void EncodeBlock(const BlockCompressParams &params,
                         const bc7enc_compress_block_params &bc7,
                         const uint8_t *pixels,
                         uint8_t *outBlock)
        {
            switch (params.Format)
            {
            case BlockFormat::BC1:
                rgbcx::encode_bc1(ResolveBc1Level(params.Quality), outBlock, pixels,
                                  params.Quality != BlockQuality::Fast, false);
                break;
            case BlockFormat::BC4:
                rgbcx::encode_bc4(outBlock, pixels, BytesPerPixel);
                break;
            case BlockFormat::BC5:
                rgbcx::encode_bc5(outBlock, pixels, 0, 1, BytesPerPixel);
                break;
            case BlockFormat::BC7:
                bc7enc_compress_block(outBlock, pixels, &bc7);
                break;
            }
        }

        void EncodeBand(const uint8_t *rgba,
                        uint32_t width,
                        uint32_t height,
                        uint32_t blocksX,
                        uint32_t firstRow,
                        uint32_t endRow,
                        const BlockCompressParams &params,
                        uint8_t *outBlocks)
        {
            const uint32_t blockBytes = GetBlockBytes(params.Format);
            const bc7enc_compress_block_params bc7 = MakeBc7Params(params);
            uint8_t pixels[BlockPixelCount * BytesPerPixel];

            for (uint32_t row = firstRow; row < endRow; ++row)
            {
                for (uint32_t column = 0; column < blocksX; ++column)
                {
                    GatherBlock(rgba, width, height, column, row, pixels);
                    EncodeBlock(params, bc7, pixels,
                                outBlocks + (static_cast<size_t>(row) * blocksX + column) * blockBytes);
                }
            }
        }

        bool DecodeBlock(BlockFormat format, const uint8_t *block, uint8_t *outPixels)
        {
            // 16 画素の RGBA。使わないチャンネルは 0、アルファは 255 にそろえる。
            for (uint32_t i = 0; i < BlockPixelCount; ++i)
            {
                outPixels[i * BytesPerPixel + 0] = 0;
                outPixels[i * BytesPerPixel + 1] = 0;
                outPixels[i * BytesPerPixel + 2] = 0;
                outPixels[i * BytesPerPixel + 3] = 255;
            }

            switch (format)
            {
            case BlockFormat::BC1:
                rgbcx::unpack_bc1(block, outPixels, true);
                return true;
            case BlockFormat::BC4:
                rgbcx::unpack_bc4(block, outPixels, BytesPerPixel);
                return true;
            case BlockFormat::BC5:
                rgbcx::unpack_bc5(block, outPixels, 0, 1, BytesPerPixel);
                return true;
            case BlockFormat::BC7:
            {
                bc7decomp::color_rgba decoded[BlockPixelCount];
                if (!bc7decomp::unpack_bc7(block, decoded))
                {
                    return false;
                }
                for (uint32_t i = 0; i < BlockPixelCount; ++i)
                {
                    outPixels[i * BytesPerPixel + 0] = decoded[i].r;
                    outPixels[i * BytesPerPixel + 1] = decoded[i].g;
                    outPixels[i * BytesPerPixel + 2] = decoded[i].b;
                    outPixels[i * BytesPerPixel + 3] = decoded[i].a;
                }
                return true;
            }
            }
            return false;
        }
    }

    uint32_t GetBlockBytes(BlockFormat format) noexcept
    {
        switch (format)
        {
        case BlockFormat::BC1:
        case BlockFormat::BC4:
            return 8;
        case BlockFormat::BC5:
        case BlockFormat::BC7:
            return 16;
        }
        return 0;
    }

    size_t ComputeCompressedSize(BlockFormat format, uint32_t width, uint32_t height) noexcept
    {
        if (width == 0 || height == 0)
        {
            return 0;
        }

        const uint64_t blocksX = (static_cast<uint64_t>(width) + BlockSize - 1) / BlockSize;
        const uint64_t blocksY = (static_cast<uint64_t>(height) + BlockSize - 1) / BlockSize;
        const uint64_t blockBytes = GetBlockBytes(format);
        const uint64_t total = blocksX * blocksY * blockBytes;
        if (total > std::numeric_limits<size_t>::max())
        {
            return 0;
        }
        return static_cast<size_t>(total);
    }

    bool CompressRGBA8(const uint8_t *rgba,
                       uint32_t width,
                       uint32_t height,
                       const BlockCompressParams &params,
                       std::vector<uint8_t> &outBlocks,
                       std::string &error)
    {
        outBlocks.clear();
        if (rgba == nullptr)
        {
            error = "BlockCompressor: 入力の画素が null です";
            return false;
        }

        const size_t compressedSize = ComputeCompressedSize(params.Format, width, height);
        if (compressedSize == 0)
        {
            error = "BlockCompressor: 画像の大きさが 0 か大きすぎます";
            return false;
        }

        EnsureEncoderInitialized();

        const uint32_t blocksX = (width + BlockSize - 1) / BlockSize;
        const uint32_t blocksY = (height + BlockSize - 1) / BlockSize;
        outBlocks.assign(compressedSize, 0);

        uint32_t threadCount = params.ThreadCount;
        if (threadCount == 0)
        {
            threadCount = std::max(1u, std::thread::hardware_concurrency());
        }
        threadCount = std::min(threadCount, blocksY);

        if (threadCount <= 1)
        {
            EncodeBand(rgba, width, height, blocksX, 0, blocksY, params, outBlocks.data());
            return true;
        }

        // ブロックの行を連続した帯に分け、帯ごとに別のスレッドで焼く。書き込み先は帯ごとに交わらない。
        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (uint32_t i = 0; i < threadCount; ++i)
        {
            const uint32_t firstRow = static_cast<uint32_t>(static_cast<uint64_t>(blocksY) * i / threadCount);
            const uint32_t endRow = static_cast<uint32_t>(static_cast<uint64_t>(blocksY) * (i + 1) / threadCount);
            workers.emplace_back([=, &params, &outBlocks]() {
                EncodeBand(rgba, width, height, blocksX, firstRow, endRow, params, outBlocks.data());
            });
        }
        for (std::thread &worker : workers)
        {
            worker.join();
        }
        return true;
    }

    bool DecompressToRGBA8(const uint8_t *blocks,
                           size_t blockBytesSize,
                           uint32_t width,
                           uint32_t height,
                           BlockFormat format,
                           std::vector<uint8_t> &outRgba,
                           std::string &error)
    {
        outRgba.clear();
        if (blocks == nullptr)
        {
            error = "BlockCompressor: 入力のブロックが null です";
            return false;
        }

        const size_t expectedSize = ComputeCompressedSize(format, width, height);
        if (expectedSize == 0 || blockBytesSize != expectedSize)
        {
            error = "BlockCompressor: ブロック列のバイト数が画像の大きさと合いません";
            return false;
        }

        EnsureEncoderInitialized();

        const uint32_t blocksX = (width + BlockSize - 1) / BlockSize;
        const uint32_t blocksY = (height + BlockSize - 1) / BlockSize;
        const uint32_t blockBytes = GetBlockBytes(format);
        outRgba.assign(static_cast<size_t>(width) * height * BytesPerPixel, 0);

        uint8_t pixels[BlockPixelCount * BytesPerPixel];
        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                const uint8_t *block = blocks + (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
                if (!DecodeBlock(format, block, pixels))
                {
                    outRgba.clear();
                    error = "BlockCompressor: ブロックを復号できません";
                    return false;
                }

                for (uint32_t py = 0; py < BlockSize; ++py)
                {
                    const uint32_t y = by * BlockSize + py;
                    if (y >= height)
                    {
                        break;
                    }
                    for (uint32_t px = 0; px < BlockSize; ++px)
                    {
                        const uint32_t x = bx * BlockSize + px;
                        if (x >= width)
                        {
                            break;
                        }
                        std::memcpy(outRgba.data() + (static_cast<size_t>(y) * width + x) * BytesPerPixel,
                                    pixels + (py * BlockSize + px) * BytesPerPixel,
                                    BytesPerPixel);
                    }
                }
            }
        }
        return true;
    }
}
