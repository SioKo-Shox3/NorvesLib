// PTのRGBA32F読戻しから、固定条件を示す名前のリニアRGB EXRを保存する。
#pragma once

#include "Container/String.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    // 出力先ディレクトリは既存とする。成功時だけ完成ファイルを公開する。
    bool WritePathTracingExrFrame(const char* outputDirectory,
                                  const char* sceneName,
                                  uint32_t seed,
                                  uint32_t samplesPerPixel,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  const float* linearRgbaPixels,
                                  size_t floatCount,
                                  Container::String* writtenPath = nullptr);
}
