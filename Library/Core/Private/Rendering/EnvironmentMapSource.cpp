// HDR正距円筒の環境マップを読み、ラスタのIBLとパストレーサーが共有する放射輝度へ換算する。
#include "Rendering/EnvironmentMapSource.h"

#include "Logging/LogMacros.h"

#include "stb_image.h"

#include <cmath>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    bool TryScaleEnvironmentSourceValue(float fileValue, double luminanceScale,
                                        float& outScaledValue)
    {
        if (!std::isfinite(luminanceScale) || luminanceScale < 0.0)
        {
            return false;
        }
        if (!std::isfinite(fileValue) || fileValue < 0.0f)
        {
            return false;
        }

        const double scaledValue = static_cast<double>(fileValue) * luminanceScale;
        if (!std::isfinite(scaledValue) || scaledValue < 0.0 ||
            scaledValue >= 65504.0)
        {
            return false;
        }

        outScaledValue = static_cast<float>(scaledValue);
        return true;
    }

    bool LoadEnvironmentRadianceSource(const Container::String& path, float luminanceScaleNits,
                                       Container::VariableArray<float>& outRgba,
                                       uint32_t& outWidth, uint32_t& outHeight)
    {
        outRgba.clear();
        outWidth = 0u;
        outHeight = 0u;
        if (path.empty())
        {
            NORVES_LOG_WARNING("EnvironmentMap", "No environment map path specified");
            return false;
        }

        Container::String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
        if (path.size() > 0 && path[0] != '/' && path[0] != '\\' &&
            (path.size() < 2 || path[1] != ':'))
        {
            Container::String relativePath = path;
            if (relativePath.size() > 7)
            {
                Container::String prefix = relativePath.substr(0, 7);
                if (prefix == "Assets/" || prefix == "Assets\\")
                {
                    relativePath = relativePath.substr(7);
                }
            }
            resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + relativePath;
        }
#endif

        NORVES_LOG_INFO("EnvironmentMap", "Loading HDR environment map...");
        NORVES_LOG_INFO("EnvironmentMap", resolvedPath.c_str());

        int width = 0;
        int height = 0;
        int channels = 0;
        float* hdrData = stbi_loadf(resolvedPath.c_str(), &width, &height, &channels, 4);
        if (hdrData == nullptr)
        {
            NORVES_LOG_ERROR("EnvironmentMap", "Failed to load HDR environment map");
            return false;
        }
        if (width <= 0 || height <= 0)
        {
            stbi_image_free(hdrData);
            NORVES_LOG_ERROR("EnvironmentMap", "HDR environment map has invalid dimensions");
            return false;
        }

        const double luminanceScale = static_cast<double>(luminanceScaleNits);
        if (!std::isfinite(luminanceScale) || luminanceScale < 0.0)
        {
            stbi_image_free(hdrData);
            NORVES_LOG_ERROR("EnvironmentMap", "HDR environment map scale is invalid");
            return false;
        }

        const uint32_t sourceWidth = static_cast<uint32_t>(width);
        const uint32_t sourceHeight = static_cast<uint32_t>(height);
        const size_t sourcePixelCount = static_cast<size_t>(sourceWidth) * sourceHeight;
        Container::VariableArray<float> sourceData(sourcePixelCount * 4u);
        bool bSourceValid = true;
        for (size_t pixel = 0u; pixel < sourcePixelCount && bSourceValid; ++pixel)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const float fileValue = hdrData[pixel * 4u + channel];
                float scaledValue = 0.0f;
                if (!TryScaleEnvironmentSourceValue(fileValue, luminanceScale, scaledValue))
                {
                    bSourceValid = false;
                    break;
                }
                sourceData[pixel * 4u + channel] = scaledValue;
            }
            sourceData[pixel * 4u + 3u] = 1.0f;
        }
        stbi_image_free(hdrData);

        if (!bSourceValid)
        {
            NORVES_LOG_ERROR("EnvironmentMap",
                             "HDR environment source contains a negative, non-finite, or out-of-range RGB value");
            return false;
        }
        outRgba = std::move(sourceData);
        outWidth = sourceWidth;
        outHeight = sourceHeight;
        return true;
    }
}
