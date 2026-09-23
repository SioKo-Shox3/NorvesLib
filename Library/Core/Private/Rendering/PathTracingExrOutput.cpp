// 有限なRGBA32F読戻しをTinyEXR v3のZIP scanlineへ書き込み、完成後に公開する。
#include "Rendering/PathTracingExrOutput.h"

#include "Logging/LogMacros.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <Windows.h>

#include "exr.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t ScanlinesPerBlock = 16u;

        bool IsSafeSceneName(const char* name)
        {
            if (!name || !name[0])
            {
                return false;
            }
            for (const unsigned char* current =
                     reinterpret_cast<const unsigned char*>(name);
                 *current; ++current)
            {
                const bool bLetter = (*current >= 'A' && *current <= 'Z') ||
                    (*current >= 'a' && *current <= 'z');
                const bool bDigit = *current >= '0' && *current <= '9';
                if (!bLetter && !bDigit && *current != '_' && *current != '-')
                {
                    return false;
                }
            }
            return true;
        }
    }

    bool WritePathTracingExrFrame(const char* outputDirectory,
                                  const char* sceneName,
                                  uint32_t seed,
                                  uint32_t samplesPerPixel,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  const float* linearRgbaPixels,
                                  size_t floatCount,
                                  Container::String* writtenPath)
    {
        if (!outputDirectory || !outputDirectory[0] ||
            !IsSafeSceneName(sceneName) || !linearRgbaPixels ||
            samplesPerPixel == 0u || width == 0u || height == 0u ||
            width > INT32_MAX || height > INT32_MAX)
        {
            NORVES_LOG_ERROR("PathTracingExrOutput", "EXRの出力条件が無効です");
            return false;
        }
        const uint64_t requiredCount = static_cast<uint64_t>(width) * height * 4u;
        if (requiredCount != floatCount ||
            requiredCount > std::numeric_limits<size_t>::max())
        {
            NORVES_LOG_ERROR("PathTracingExrOutput", "RGBA画素数が寸法と一致しません");
            return false;
        }
        for (size_t index = 0u; index < floatCount; ++index)
        {
            if (!std::isfinite(linearRgbaPixels[index]))
            {
                NORVES_LOG_ERROR("PathTracingExrOutput", "非有限のRGBA画素を拒否しました");
                return false;
            }
        }

        char finalPath[MAX_PATH] = {};
        const int pathLength = std::snprintf(
            finalPath, sizeof(finalPath), "%s/%s_seed%08x_spp%06u_frame%06u.exr",
            outputDirectory, sceneName, seed, samplesPerPixel, frameIndex);
        if (pathLength < 0 || pathLength >= static_cast<int>(sizeof(finalPath)))
        {
            NORVES_LOG_ERROR("PathTracingExrOutput", "EXRの出力パスが長すぎます");
            return false;
        }

        exr_channel channels[3] = {};
        std::memcpy(channels[0].name, "B", 2u);
        std::memcpy(channels[1].name, "G", 2u);
        std::memcpy(channels[2].name, "R", 2u);
        for (exr_channel& channel : channels)
        {
            channel.pixel_type = EXR_PIXEL_FLOAT;
            channel.x_sampling = 1;
            channel.y_sampling = 1;
        }
        exr_header header = {};
        header.part_type = EXR_PART_SCANLINE;
        header.compression = EXR_COMPRESSION_ZIP;
        header.line_order = EXR_LINEORDER_INCREASING_Y;
        header.data_window = {0, 0, static_cast<int32_t>(width - 1u),
                              static_cast<int32_t>(height - 1u)};
        header.display_window = header.data_window;
        header.pixel_aspect_ratio = 1.0f;
        header.screen_window_width = 1.0f;
        header.num_channels = 3;
        header.channels = channels;

        exr_writer* writer = nullptr;
        if (exr_writer_create(nullptr, &writer) != EXR_SUCCESS)
        {
            NORVES_LOG_ERROR("PathTracingExrOutput", "TinyEXR writerを作成できません");
            return false;
        }
        char temporaryPath[MAX_PATH] = {};
        bool bSuccess = exr_writer_add_part(writer, &header, nullptr) == EXR_SUCCESS &&
            GetTempFileNameA(outputDirectory, "pt", 0u, temporaryPath) != 0u &&
            exr_writer_begin_stream_file(writer, temporaryPath,
                                         EXR_COMPRESSION_ZIP) == EXR_SUCCESS;
        if (bSuccess)
        {
            const size_t blockPixels = static_cast<size_t>(width) * ScanlinesPerBlock;
            Container::VariableArray<float> blue(blockPixels);
            Container::VariableArray<float> green(blockPixels);
            Container::VariableArray<float> red(blockPixels);
            for (uint32_t y = 0u; y < height && bSuccess; y += ScanlinesPerBlock)
            {
                const uint32_t rows = height - y < ScanlinesPerBlock
                                          ? height - y : ScanlinesPerBlock;
                for (uint32_t row = 0u; row < rows; ++row)
                {
                    for (uint32_t x = 0u; x < width; ++x)
                    {
                        const size_t source =
                            (static_cast<size_t>(y + row) * width + x) * 4u;
                        const size_t destination =
                            static_cast<size_t>(row) * width + x;
                        blue[destination] = linearRgbaPixels[source + 2u];
                        green[destination] = linearRgbaPixels[source + 1u];
                        red[destination] = linearRgbaPixels[source];
                    }
                }
                const void* channelRows[3] = {
                    blue.data(), green.data(), red.data()};
                bSuccess = exr_writer_write_scanline_block(
                    writer, 0, static_cast<int32_t>(y), channelRows) == EXR_SUCCESS;
            }
            if (bSuccess)
            {
                bSuccess = exr_writer_end_stream(writer) == EXR_SUCCESS;
            }
        }
        exr_writer_destroy(writer);
        if (bSuccess)
        {
            bSuccess = MoveFileExA(temporaryPath, finalPath,
                                   MOVEFILE_REPLACE_EXISTING |
                                       MOVEFILE_WRITE_THROUGH) != 0;
        }
        if (!bSuccess)
        {
            if (temporaryPath[0])
            {
                DeleteFileA(temporaryPath);
            }
            NORVES_LOG_ERROR("PathTracingExrOutput", "EXRの保存に失敗しました");
            return false;
        }
        if (writtenPath)
        {
            *writtenPath = finalPath;
        }
        return true;
    }
}
