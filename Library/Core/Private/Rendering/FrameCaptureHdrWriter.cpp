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

        // RGBA32F の 1 枚から、選んだチャンネルを PFM（channelCount が 3 なら "PF"、1 なら "Pf"）にする
        bool BuildFloatPfm(const CapturedImage& image,
                           const uint32_t* pChannels,
                           uint32_t channelCount,
                           VariableArray<uint8_t>& outPfm)
        {
            if (image.Format != RHI::Format::R32G32B32A32_FLOAT || image.BytesPerPixel != 16 || image.Width == 0 ||
                image.Height == 0 || (channelCount != 1u && channelCount != 3u))
            {
                return false;
            }
            const uint64_t tightRowPitch = static_cast<uint64_t>(image.Width) * image.BytesPerPixel;
            if (image.RowPitchBytes < tightRowPitch ||
                image.Pixels.size() < static_cast<size_t>(image.RowPitchBytes) * image.Height)
            {
                return false;
            }
            char header[64] = {};
            const int headerLength = std::snprintf(header, sizeof(header), "%s\n%u %u\n-1.0\n",
                                                   channelCount == 3u ? "PF" : "Pf", image.Width, image.Height);
            if (headerLength <= 0 || headerLength >= static_cast<int>(sizeof(header)))
            {
                return false;
            }
            const size_t pixelCount = static_cast<size_t>(image.Width) * image.Height;
            outPfm.resize(static_cast<size_t>(headerLength) + pixelCount * channelCount * sizeof(float));
            std::memcpy(outPfm.data(), header, static_cast<size_t>(headerLength));
            float* pDest = reinterpret_cast<float*>(outPfm.data() + headerLength);
            for (uint32_t row = 0; row < image.Height; ++row)
            {
                // 画像の上の行（読み出しの 0 行目）を PFM の最後の行にする
                const uint8_t* pSourceRow =
                    image.Pixels.data() + static_cast<size_t>(image.RowPitchBytes) * (image.Height - 1u - row);
                for (uint32_t x = 0; x < image.Width; ++x)
                {
                    for (uint32_t channel = 0; channel < channelCount; ++channel)
                    {
                        std::memcpy(pDest++, pSourceRow + static_cast<size_t>(x) * 16u + pChannels[channel] * 4u,
                                    sizeof(float));
                    }
                }
            }
            return true;
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
        char json[4096] = {};
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
            "  },\n",
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

        // ポストのパスがこのフレームで実際に使った値（Scripts/Terrain/DisplayTransform.py が表示変換を再現する）
        const FrameCaptureDisplayInfo::ToneMapRecord& toneMap = display.ToneMap;
        const FrameCaptureDisplayInfo::VignetteRecord& vignette = display.Vignette;
        const int postLength = std::snprintf(
            json + jsonLength,
            sizeof(json) - static_cast<size_t>(jsonLength),
            "  \"post\": {\n"
            "    \"tone_map_recorded\": %s,\n"
            "    \"tone_map_operator\": %u,\n"
            "    \"tone_map_bypass\": %s,\n"
            "    \"grading_mode\": %u,\n"
            "    \"color_filter\": [%.9g, %.9g, %.9g, %.9g],\n"
            "    \"contrast\": %.9g,\n"
            "    \"contrast_pivot\": %.9g,\n"
            "    \"saturation\": %.9g,\n"
            "    \"brightness\": %.9g,\n"
            "    \"temperature\": %.9g,\n"
            "    \"look_lut_intensity\": %.9g,\n"
            "    \"tone_map_vignette\": [%.9g, %.9g, %.9g],\n"
            "    \"film_grain_strength\": %.9g,\n"
            "    \"display_ramp\": %s,\n"
            "    \"vignette_recorded\": %s,\n"
            "    \"vignette_enabled\": %s,\n"
            "    \"vignette\": [%.9g, %.9g, %.9g],\n"
            "    \"chromatic_aberration_pixels\": %.9g,\n"
            "    \"bloom\": %s,\n"
            "    \"fxaa\": %s,\n"
            "    \"hardware_srgb_encode\": %s,\n"
            "    \"shader_srgb_encode\": %s\n"
            "  }\n"
            "}\n",
            toneMap.bRecorded ? "true" : "false",
            toneMap.OperatorType,
            toneMap.bBypass ? "true" : "false",
            toneMap.GradingMode,
            static_cast<double>(toneMap.ColorFilter[0]),
            static_cast<double>(toneMap.ColorFilter[1]),
            static_cast<double>(toneMap.ColorFilter[2]),
            static_cast<double>(toneMap.ColorFilter[3]),
            static_cast<double>(toneMap.Contrast),
            static_cast<double>(toneMap.ContrastPivot),
            static_cast<double>(toneMap.Saturation),
            static_cast<double>(toneMap.Brightness),
            static_cast<double>(toneMap.Temperature),
            static_cast<double>(toneMap.LookLutIntensity),
            static_cast<double>(toneMap.Vignette[0]),
            static_cast<double>(toneMap.Vignette[1]),
            static_cast<double>(toneMap.Vignette[2]),
            static_cast<double>(toneMap.FilmGrainStrength),
            toneMap.bDisplayRamp ? "true" : "false",
            vignette.bRecorded ? "true" : "false",
            vignette.bEnabled ? "true" : "false",
            static_cast<double>(vignette.Intensity),
            static_cast<double>(vignette.Radius),
            static_cast<double>(vignette.Softness),
            static_cast<double>(vignette.ChromaticAberrationPixels),
            display.bBloom ? "true" : "false",
            display.bFxaa ? "true" : "false",
            frame.bHardwareSrgbEncode ? "true" : "false",
            frame.bShaderSrgbEncode ? "true" : "false");
        if (postLength <= 0 || jsonLength + postLength >= static_cast<int>(sizeof(json)))
        {
            return false;
        }
        return WriteBytes(jsonPath, json, static_cast<size_t>(jsonLength + postLength));
    }

    bool SaveCapturedAov(const CapturedFrame& frame, const Container::String& pngPath)
    {
        const CapturedAov& aov = frame.Aov;
        if (!frame.IsSuccess() || !aov.bValid || !aov.Camera.bValid || aov.Surface.Width != aov.Geometry.Width ||
            aov.Surface.Height != aov.Geometry.Height)
        {
            return false;
        }
        // 表面 = アルベド（rgb）+ 粗さ（a）、幾何 = 法線（xyz）+ 視線方向の深度（w）
        struct AovImage
        {
            const CapturedImage* Source;
            uint32_t Channels[3];
            uint32_t ChannelCount;
            const TCHAR* Suffix;
        };
        const AovImage images[] = {
            {&aov.Surface, {0u, 1u, 2u}, 3u, TEXT(".aov.albedo.pfm")},
            {&aov.Geometry, {0u, 1u, 2u}, 3u, TEXT(".aov.normal.pfm")},
            {&aov.Surface, {3u, 0u, 0u}, 1u, TEXT(".aov.roughness.pfm")},
            {&aov.Geometry, {3u, 0u, 0u}, 1u, TEXT(".aov.depth.pfm")},
        };
        const String basePath = RemoveExtension(pngPath);
        VariableArray<char> fileNames[4];
        for (uint32_t index = 0; index < 4u; ++index)
        {
            VariableArray<uint8_t> pfm;
            if (!BuildFloatPfm(*images[index].Source, images[index].Channels, images[index].ChannelCount, pfm))
            {
                return false;
            }
            const String path = basePath + String(images[index].Suffix);
            if (!WriteBytes(path, pfm.data(), pfm.size()))
            {
                return false;
            }
            fileNames[index] = ToAsciiFileName(path);
        }

        const VariableArray<char> pngName = ToAsciiFileName(pngPath);
        const FrameCaptureAovCamera& camera = aov.Camera;
        char json[4096] = {};
        const int jsonLength = std::snprintf(
            json,
            sizeof(json),
            "{\n"
            "  \"format\": \"pfm_f32_le_bottom_up\",\n"
            "  \"png\": \"%s\",\n"
            "  \"width\": %u,\n"
            "  \"height\": %u,\n"
            "  \"frame_number\": %llu,\n"
            "  \"source\": \"gbuffer_raw\",\n"
            "  \"sky\": \"albedo_alpha_below_0.01\",\n"
            "  \"albedo\": {\"file\": \"%s\", \"channels\": 3, \"meaning\": \"linear_albedo\", \"sky_value\": 0},\n"
            "  \"normal\": {\"file\": \"%s\", \"channels\": 3, \"meaning\": \"world_normal_y_up\", \"sky_value\": 0},\n"
            "  \"roughness\": {\"file\": \"%s\", \"channels\": 1, \"meaning\": \"perceptual_roughness\", \"sky_value\": 0},\n"
            "  \"depth\": {\"file\": \"%s\", \"channels\": 1, \"meaning\": \"view_axis_distance\", \"units\": \"m\", \"sky_value\": \"inf\"},\n"
            "  \"camera\": {\n"
            "    \"position\": [%.9g, %.9g, %.9g],\n"
            "    \"forward\": [%.9g, %.9g, %.9g],\n"
            "    \"up\": [%.9g, %.9g, %.9g],\n"
            "    \"right\": [%.9g, %.9g, %.9g],\n"
            "    \"vertical_fov_degrees\": %.9g,\n"
            "    \"aspect_ratio\": %.9g,\n"
            "    \"near\": %.9g,\n"
            "    \"far\": %.9g,\n"
            "    \"orthographic\": %s\n"
            "  }\n"
            "}\n",
            pngName.data(),
            aov.Surface.Width,
            aov.Surface.Height,
            static_cast<unsigned long long>(frame.FrameNumber),
            fileNames[0].data(),
            fileNames[1].data(),
            fileNames[2].data(),
            fileNames[3].data(),
            static_cast<double>(camera.Position[0]),
            static_cast<double>(camera.Position[1]),
            static_cast<double>(camera.Position[2]),
            static_cast<double>(camera.Forward[0]),
            static_cast<double>(camera.Forward[1]),
            static_cast<double>(camera.Forward[2]),
            static_cast<double>(camera.Up[0]),
            static_cast<double>(camera.Up[1]),
            static_cast<double>(camera.Up[2]),
            static_cast<double>(camera.Right[0]),
            static_cast<double>(camera.Right[1]),
            static_cast<double>(camera.Right[2]),
            static_cast<double>(camera.VerticalFieldOfViewDegrees),
            static_cast<double>(camera.AspectRatio),
            static_cast<double>(camera.NearPlane),
            static_cast<double>(camera.FarPlane),
            camera.bOrthographic ? "true" : "false");
        if (jsonLength <= 0 || jsonLength >= static_cast<int>(sizeof(json)))
        {
            return false;
        }
        return WriteBytes(basePath + String(TEXT(".aov.json")), json, static_cast<size_t>(jsonLength));
    }
#else
    bool SaveCapturedSceneColorHdr(const CapturedFrame&, const Container::String&)
    {
        return false;
    }

    bool SaveCapturedAov(const CapturedFrame&, const Container::String&)
    {
        return false;
    }
#endif
} // namespace NorvesLib::Core::Rendering
