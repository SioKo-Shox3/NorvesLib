#include "Rendering/FrameCaptureHdrWriter.h"

#include "Container/VariableArray.h"
#include "Debug/DebugConfig.h"
#include "FileStream/FileStream.h"
#include "Rendering/FrameCaptureTypes.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
#if NORVES_BUILD_DEVELOPMENT
    namespace
    {
        using Container::String;
        using Container::VariableArray;

        // IEEE 754 の半精度を単精度へ広げる（非正規化数・無限大・NaN もそのまま写す）
        float HalfToFloat(uint16_t half)
        {
            const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16u;
            const uint32_t exponent = (half >> 10u) & 0x1Fu;
            uint32_t mantissa = half & 0x03FFu;
            uint32_t bits = 0;
            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign;
                }
                else
                {
                    // 非正規化数: 仮数の先頭の 1 が出るまでずらして指数を下げる
                    int32_t shiftedExponent = 127 - 15 + 1;
                    while ((mantissa & 0x0400u) == 0)
                    {
                        mantissa <<= 1u;
                        --shiftedExponent;
                    }
                    mantissa &= 0x03FFu;
                    bits = sign | (static_cast<uint32_t>(shiftedExponent) << 23u) | (mantissa << 13u);
                }
            }
            else if (exponent == 0x1Fu)
            {
                bits = sign | 0x7F800000u | (mantissa << 13u);
            }
            else
            {
                bits = sign | ((exponent - 15u + 127u) << 23u) | (mantissa << 13u);
            }
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        bool WriteBytes(const String& path, const void* pData, size_t byteCount)
        {
            FileStream::FileStreamUniquePtr stream = FileStream::FileStream::CreateUnique(
                path, FileStream::FileMode::Write, FileStream::FileAccess::Write, FileStream::FileShare::None);
            if (!stream)
            {
                return false;
            }
            if (stream->Write(pData, byteCount) != byteCount)
            {
                return false;
            }
            stream->Flush();
            return true;
        }

        // パスの末尾の名前（区切りの後）を JSON に書ける ASCII にする（ASCII でない文字・制御文字・引用符は '_'）
        VariableArray<char> ToAsciiFileName(const String& path)
        {
            size_t start = 0;
            for (size_t i = 0; i < path.size(); ++i)
            {
                if (path[i] == TEXT('\\') || path[i] == TEXT('/'))
                {
                    start = i + 1;
                }
            }
            VariableArray<char> name;
            for (size_t i = start; i < path.size(); ++i)
            {
                const auto ch = path[i];
                const bool bPlain = ch >= 0x20 && ch < 0x7F && ch != '"' && ch != '\\';
                name.push_back(bPlain ? static_cast<char>(ch) : '_');
            }
            name.push_back('\0');
            return name;
        }

        // 拡張子（最後の区切りより後の最後の '.' から）を除いたパス
        String RemoveExtension(const String& path)
        {
            size_t lastSeparator = String::npos;
            size_t lastDot = String::npos;
            for (size_t i = 0; i < path.size(); ++i)
            {
                if (path[i] == TEXT('\\') || path[i] == TEXT('/'))
                {
                    lastSeparator = i;
                    lastDot = String::npos;
                }
                else if (path[i] == TEXT('.'))
                {
                    lastDot = i;
                }
            }
            if (lastDot == String::npos || (lastSeparator != String::npos && lastDot <= lastSeparator + 1))
            {
                return path;
            }
            return path.substr(0, lastDot);
        }
    } // namespace

    bool SaveCapturedSceneColorHdr(const CapturedFrame& frame, const Container::String& pngPath)
    {
        const CapturedSceneColor& sceneColor = frame.SceneColor;
        if (!frame.IsSuccess() || !sceneColor.bValid || sceneColor.Width == 0 || sceneColor.Height == 0 ||
            !(sceneColor.PreExposure > 0.0f) || !std::isfinite(sceneColor.PreExposure))
        {
            return false;
        }
        const bool bHalf = sceneColor.Format == RHI::Format::R16G16B16A16_FLOAT && sceneColor.BytesPerPixel == 8;
        const bool bFloat = sceneColor.Format == RHI::Format::R32G32B32A32_FLOAT && sceneColor.BytesPerPixel == 16;
        if (!bHalf && !bFloat)
        {
            return false;
        }
        const uint64_t tightRowPitch = static_cast<uint64_t>(sceneColor.Width) * sceneColor.BytesPerPixel;
        if (sceneColor.RowPitchBytes < tightRowPitch ||
            sceneColor.Pixels.size() < static_cast<size_t>(sceneColor.RowPitchBytes) * sceneColor.Height)
        {
            return false;
        }

        // PFM: 見出しの後に、下の行から RGB の float（負のスケールはリトルエンディアン）
        char header[64] = {};
        const int headerLength =
            std::snprintf(header, sizeof(header), "PF\n%u %u\n-1.0\n", sceneColor.Width, sceneColor.Height);
        if (headerLength <= 0 || headerLength >= static_cast<int>(sizeof(header)))
        {
            return false;
        }
        const size_t pixelCount = static_cast<size_t>(sceneColor.Width) * sceneColor.Height;
        VariableArray<uint8_t> pfm(static_cast<size_t>(headerLength) + pixelCount * 3u * sizeof(float));
        std::memcpy(pfm.data(), header, static_cast<size_t>(headerLength));
        float* pDest = reinterpret_cast<float*>(pfm.data() + headerLength);
        const float invPreExposure = 1.0f / sceneColor.PreExposure;
        for (uint32_t row = 0; row < sceneColor.Height; ++row)
        {
            // 画像の上の行（読み出しの 0 行目）を PFM の最後の行にする
            const uint32_t sourceRow = sceneColor.Height - 1u - row;
            const uint8_t* pSourceRow =
                sceneColor.Pixels.data() + static_cast<size_t>(sceneColor.RowPitchBytes) * sourceRow;
            for (uint32_t x = 0; x < sceneColor.Width; ++x)
            {
                for (uint32_t channel = 0; channel < 3u; ++channel)
                {
                    float value = 0.0f;
                    if (bHalf)
                    {
                        uint16_t half = 0;
                        std::memcpy(&half, pSourceRow + static_cast<size_t>(x) * 8u + channel * 2u, sizeof(half));
                        value = HalfToFloat(half);
                    }
                    else
                    {
                        std::memcpy(&value, pSourceRow + static_cast<size_t>(x) * 16u + channel * 4u, sizeof(value));
                    }
                    // 無限大・NaN は割っても残るので、検査が見つけられるようそのまま書く
                    *pDest++ = value * invPreExposure;
                }
            }
        }

        const String basePath = RemoveExtension(pngPath);
        const String pfmPath = basePath + String(TEXT(".hdr.pfm"));
        const String jsonPath = basePath + String(TEXT(".hdr.json"));
        if (!WriteBytes(pfmPath, pfm.data(), pfm.size()))
        {
            return false;
        }

        const VariableArray<char> pngName = ToAsciiFileName(pngPath);
        const VariableArray<char> pfmName = ToAsciiFileName(pfmPath);
        const FrameCaptureDisplayInfo& display = sceneColor.Display;
        char json[2048] = {};
        const int jsonLength = std::snprintf(
            json,
            sizeof(json),
            "{\n"
            "  \"format\": \"pfm_rgb_f32_le_bottom_up\",\n"
            "  \"file\": \"%s\",\n"
            "  \"png\": \"%s\",\n"
            "  \"width\": %u,\n"
            "  \"height\": %u,\n"
            "  \"frame_number\": %llu,\n"
            "  \"source\": \"scene_color_before_bloom\",\n"
            "  \"source_format\": \"%s\",\n"
            "  \"units\": \"cd/m2\",\n"
            "  \"color_space\": \"linear_rec709_d65\",\n"
            "  \"pre_exposure\": %.9g,\n"
            "  \"display\": {\n"
            "    \"neutral_linear\": %s,\n"
            "    \"grading_override\": %s,\n"
            "    \"contrast\": %.9g,\n"
            "    \"contrast_pivot\": %.9g,\n"
            "    \"saturation\": %.9g,\n"
            "    \"temperature\": %.9g,\n"
            "    \"vignette_intensity\": %.9g,\n"
            "    \"vignette_radius\": %.9g,\n"
            "    \"vignette_softness\": %.9g,\n"
            "    \"look_lut\": %s,\n"
            "    \"chromatic_aberration_pixels\": %.9g,\n"
            "    \"lens_dirt_intensity\": %.9g\n"
            "  }\n"
            "}\n",
            pfmName.data(),
            pngName.data(),
            sceneColor.Width,
            sceneColor.Height,
            static_cast<unsigned long long>(frame.FrameNumber),
            bHalf ? "R16G16B16A16_FLOAT" : "R32G32B32A32_FLOAT",
            static_cast<double>(sceneColor.PreExposure),
            display.bNeutralLinear ? "true" : "false",
            display.bGradingOverride ? "true" : "false",
            static_cast<double>(display.Contrast),
            static_cast<double>(display.ContrastPivot),
            static_cast<double>(display.Saturation),
            static_cast<double>(display.Temperature),
            static_cast<double>(display.VignetteIntensity),
            static_cast<double>(display.VignetteRadius),
            static_cast<double>(display.VignetteSoftness),
            display.bLookLut ? "true" : "false",
            static_cast<double>(display.ChromaticAberrationPixels),
            static_cast<double>(display.LensDirtIntensity));
        if (jsonLength <= 0 || jsonLength >= static_cast<int>(sizeof(json)))
        {
            return false;
        }
        return WriteBytes(jsonPath, json, static_cast<size_t>(jsonLength));
    }
#else
    bool SaveCapturedSceneColorHdr(const CapturedFrame&, const Container::String&)
    {
        return false;
    }
#endif
} // namespace NorvesLib::Core::Rendering
