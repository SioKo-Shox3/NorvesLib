#include "Rendering/FrameCaptureHdrWriter.h"

#include "Container/VariableArray.h"
#include "Debug/DebugConfig.h"
#include "FileStream/FileStream.h"
#include "Rendering/CloudLayerPass.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/SkyAtmospherePass.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
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
            aov.Surface.Height != aov.Geometry.Height || aov.Surface.Width != aov.Lighting.Width ||
            aov.Surface.Height != aov.Lighting.Height)
        {
            return false;
        }
        // 表面 = アルベド（rgb）+ 粗さ（a）、幾何 = 法線（xyz）+ 視線方向の深度（w）、照明 = 太陽の可視率（r）
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
            {&aov.Lighting, {0u, 0u, 0u}, 1u, TEXT(".aov.sun_visibility.pfm")},
            {&aov.Lighting, {1u, 0u, 0u}, 1u, TEXT(".aov.rtgi_temporal_code.pfm")},
            {&aov.Lighting, {2u, 0u, 0u}, 1u, TEXT(".aov.rtgi_filtered_sky.pfm")},
            {&aov.Lighting, {3u, 0u, 0u}, 1u, TEXT(".aov.geometric_normal.pfm")},
        };
        constexpr uint32_t ImageCount = static_cast<uint32_t>(sizeof(images) / sizeof(images[0]));
        const String basePath = RemoveExtension(pngPath);
        VariableArray<char> fileNames[ImageCount];
        for (uint32_t index = 0; index < ImageCount; ++index)
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
        char json[8192] = {};
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
            "  \"sun_visibility\": {\"file\": \"%s\", \"channels\": 1, \"meaning\": \"sun_shadow_visibility\", \"source\": \"lighting\", \"sky_value\": 0, \"unavailable_value\": -1},\n"
            "  \"rtgi_temporal_code\": {\"file\": \"%s\", \"channels\": 1, \"meaning\": \"rtgi_occlusion_diag_2xcode_plus_temporal_sky\", \"code\": \"raw_sky_plus_2x_skip_class\", \"skip_class_heights_m\": [0.1, 2.0], \"source\": \"rtgi_temporal\", \"sky_value\": 0, \"unavailable_value\": -1},\n"
            "  \"rtgi_filtered_sky\": {\"file\": \"%s\", \"channels\": 1, \"meaning\": \"rtgi_occlusion_diag_filtered_sky\", \"source\": \"rtgi_denoised\", \"sky_value\": 0, \"unavailable_value\": -1},\n"
            "  \"geometric_normal\": {\"file\": \"%s\", \"channels\": 1, \"meaning\": \"triangle_face_normal_world_y_up\", \"encoding\": \"octahedral_12_12_plus_1\", \"source\": \"visibility_resolve\", \"sky_value\": 0, \"unavailable_value\": -1},\n"
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
            fileNames[4].data(),
            fileNames[5].data(),
            fileNames[6].data(),
            fileNames[7].data(),
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

    namespace
    {
        // printf の書式で JSON の文字列を後ろへ足す
        void AppendFormat(VariableArray<char>& out, const char* format, ...)
        {
            char buffer[1024];
            va_list arguments;
            va_start(arguments, format);
            const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
            va_end(arguments);
            if (length <= 0)
            {
                return;
            }
            const size_t count = std::min(static_cast<size_t>(length), sizeof(buffer) - 1u);
            out.insert(out.end(), buffer, buffer + count);
        }

        // JSON の数（有限でなければ null）
        void AppendNumber(VariableArray<char>& out, double value)
        {
            if (std::isfinite(value))
            {
                AppendFormat(out, "%.9g", value);
            }
            else
            {
                AppendFormat(out, "null");
            }
        }

        void AppendVector3(VariableArray<char>& out, const float* pValues)
        {
            AppendFormat(out, "[");
            for (uint32_t index = 0; index < 3u; ++index)
            {
                AppendNumber(out, pValues[index]);
                AppendFormat(out, index + 1u < 3u ? ", " : "]");
            }
        }

        void AppendVector3(VariableArray<char>& out, const Math::Vector3& value)
        {
            const float values[3] = {value.x, value.y, value.z};
            AppendVector3(out, values);
        }

        // 32 bit 浮動小数の並びをリトルエンディアンの生のファイルにする
        bool WriteFloats(const String& path, const VariableArray<float>& values)
        {
            return WriteBytes(path, values.data(), values.size() * sizeof(float));
        }

        // 半精度の texel の並びを単精度へ広げる（GPU へ上げた値そのもの）
        VariableArray<float> HalfTexelsToFloats(const VariableArray<uint16_t>& texels)
        {
            VariableArray<float> values;
            values.resize(texels.size());
            for (size_t index = 0; index < texels.size(); ++index)
            {
                values[index] = HalfToFloat(texels[index]);
            }
            return values;
        }

        // 空気遠近の高度ごとの表（SkyAerialProfile）を [行, 段, RGBA] の単精度の並びに写す（行 0 = 太陽の照度 × 透過率、
        // 行 1 = 太陽の照度 × Ψ_ms。VolumetricsPass が定数へ詰める値そのもの）
        VariableArray<float> AerialProfileToFloats(const SkyAerialProfile& profile)
        {
            VariableArray<float> values;
            values.resize(2u * SkyAerialProfileSize * 4u);
            std::memcpy(values.data(), profile.SunIlluminance, sizeof(profile.SunIlluminance));
            std::memcpy(values.data() + SkyAerialProfileSize * 4u, profile.MultipleScattering,
                        sizeof(profile.MultipleScattering));
            return values;
        }

        // 前計算の表（RGB のセル）を単精度の並びに写す
        VariableArray<float> TableToFloats(const SkyAtmosphereModel::TableView& table)
        {
            VariableArray<float> values;
            if (table.Data != nullptr)
            {
                values.assign(table.Data, table.Data + static_cast<size_t>(table.Rows) * table.Columns * 3u);
            }
            return values;
        }

        // 1 つのファイルの記述（"file"・"dtype"・"shape"・"layout"）を書く
        void AppendFileEntry(VariableArray<char>& out, const VariableArray<char>& fileName, uint32_t dim0, uint32_t dim1,
                             uint32_t dim2)
        {
            AppendFormat(out, "\"file\": \"%s\", \"dtype\": \"float32_le\", \"shape\": [%u, %u, %u], "
                              "\"layout\": \"row_major_first_row_first\"",
                         fileName.data(), dim0, dim1, dim2);
        }
    } // namespace

    bool SaveCapturedAtmosphere(const CapturedFrame& frame, const Container::String& pngPath)
    {
        const CapturedAtmosphere& atmosphere = frame.Atmosphere;
        const FrameCaptureAtmosphereInfo& info = atmosphere.Info;
        const CapturedImage& probe = atmosphere.Probe;
        if (!frame.IsSuccess() || !atmosphere.bValid || !info.bValid || info.RayCount == 0u ||
            info.RayCount > FrameCaptureAtmosphereMaxRays || probe.Format != RHI::Format::R32G32B32A32_FLOAT ||
            probe.BytesPerPixel != 16u || probe.Width != info.RayCount ||
            probe.Height != FrameCaptureAtmosphereProbeRows * FrameCaptureAtmosphereMediumCount ||
            probe.RowPitchBytes < static_cast<uint64_t>(probe.Width) * probe.BytesPerPixel ||
            probe.Pixels.size() < static_cast<size_t>(probe.RowPitchBytes) * probe.Height)
        {
            return false;
        }
        const String basePath = RemoveExtension(pngPath);

        // 決めた視線の値（GPU が volumetrics.frag と同じ式で評価したもの）: 媒質ごとに [行, 視線, RGBA]
        // （0 番 = そのフレームの媒質、1 番 = 検査用の割り振り）
        VariableArray<float> mediumProbeValues[FrameCaptureAtmosphereMediumCount];
        for (uint32_t medium = 0; medium < FrameCaptureAtmosphereMediumCount; ++medium)
        {
            VariableArray<float>& values = mediumProbeValues[medium];
            values.resize(static_cast<size_t>(FrameCaptureAtmosphereProbeRows) * probe.Width * 4u);
            for (uint32_t row = 0; row < FrameCaptureAtmosphereProbeRows; ++row)
            {
                const uint32_t sourceRow = medium * FrameCaptureAtmosphereProbeRows + row;
                std::memcpy(values.data() + static_cast<size_t>(row) * probe.Width * 4u,
                            probe.Pixels.data() + static_cast<size_t>(sourceRow) * probe.RowPitchBytes,
                            static_cast<size_t>(probe.Width) * 16u);
            }
        }

        // 空の LUT と前計算の表（SkyAtmospherePass が同じパラメータで作る値。CPU で決まるので作り直して同じ値になる）
        SkyAtmosphereModel model(info.Sky);
        model.BuildSkyViewTable();
        const SkyAtmospherePassSettings lutSettings;
        VariableArray<uint16_t> transmittanceTexels;
        VariableArray<uint16_t> radianceTexels;
        BuildSkyTransmittanceLutTexels(model, lutSettings.TransmittanceWidth, lutSettings.TransmittanceHeight,
                                       transmittanceTexels);
        BuildSkyRadianceLutTexels(model, lutSettings.RadianceWidth, lutSettings.RadianceHeight, radianceTexels);
        const SkyAtmosphereModel::TableView transmittanceTable = model.GetTransmittanceTable();
        const SkyAtmosphereModel::TableView multipleScatteringTable = model.GetMultipleScatteringTable();
        const SkyAtmosphereModel::TableView skyViewTable = model.GetSkyViewTable();
        SkyAerialProfile aerialProfile;
        BuildSkyAerialProfile(model, aerialProfile);
        if (transmittanceTable.Data == nullptr || multipleScatteringTable.Data == nullptr || skyViewTable.Data == nullptr ||
            !aerialProfile.bValid)
        {
            return false;
        }

        struct FloatFile
        {
            const TCHAR* Suffix;
            VariableArray<float> Values;
            VariableArray<char> Name;
        };
        FloatFile files[] = {
            {TEXT(".atmos.probe.f32"), mediumProbeValues[0], {}},
            {TEXT(".atmos.transmittance_lut.f32"), HalfTexelsToFloats(transmittanceTexels), {}},
            {TEXT(".atmos.radiance_lut.f32"), HalfTexelsToFloats(radianceTexels), {}},
            {TEXT(".atmos.transmittance_table.f32"), TableToFloats(transmittanceTable), {}},
            {TEXT(".atmos.multiple_scattering.f32"), TableToFloats(multipleScatteringTable), {}},
            {TEXT(".atmos.sky_view.f32"), TableToFloats(skyViewTable), {}},
            {TEXT(".atmos.aerial_profile.f32"), AerialProfileToFloats(aerialProfile), {}},
            {TEXT(".atmos.probe_test_media.f32"), mediumProbeValues[1], {}},
        };
        for (FloatFile& file : files)
        {
            const String path = basePath + String(file.Suffix);
            if (file.Values.empty() || !WriteFloats(path, file.Values))
            {
                return false;
            }
            file.Name = ToAsciiFileName(path);
        }

        const SkyAtmosphereParameters& sky = info.Sky;
        const Math::Vector3 sunNormal = ComputeSunGroundIlluminance(sky);
        const float sunSine = std::sin(sky.SunAltitudeDegrees * (3.14159265358979323846f / 180.0f));
        const Math::Vector3 sunHorizontal(sunNormal.x * sunSine, sunNormal.y * sunSine, sunNormal.z * sunSine);
        const Math::Vector3 skyHorizontal = model.GetGroundSkyIlluminance();
        const Math::Vector3& sunTransmittance = model.GetSunGroundTransmittance();
        const AtmosphereGroundExtinction groundExtinction = GetAtmosphereGroundExtinction(sky);

        VariableArray<char> json;
        const VariableArray<char> pngName = ToAsciiFileName(pngPath);
        AppendFormat(json, "{\n  \"format\": \"norves_atmosphere_dump_v1\",\n  \"png\": \"%s\",\n  \"frame_number\": %llu,\n",
                     pngName.data(), static_cast<unsigned long long>(frame.FrameNumber));
        AppendFormat(json, "  \"conventions\": {\"color\": \"linear_rec709_d65\", \"radiance_units\": \"cd/m2\", "
                           "\"illuminance_units\": \"lux\", \"world\": \"y_up_meters\", \"pre_exposure\": \"removed\", "
                           "\"direction_from_elevation_azimuth\": \"(cos(el)cos(az), sin(el), cos(el)sin(az))\"},\n");

        // 評価の条件
        AppendFormat(json, "  \"camera_position\": ");
        AppendVector3(json, info.CameraPosition);
        AppendFormat(json, ",\n  \"pre_exposure_of_frame\": ");
        AppendNumber(json, info.PreExposure);
        AppendFormat(json, ",\n  \"sky\": {\"enabled\": %s, \"sun_altitude_degrees\": ", sky.bEnabled ? "true" : "false");
        AppendNumber(json, sky.SunAltitudeDegrees);
        AppendFormat(json, ", \"sun_azimuth_degrees\": ");
        AppendNumber(json, sky.SunAzimuthDegrees);
        AppendFormat(json, ", \"sun_luminance_cd_m2\": ");
        AppendNumber(json, sky.SunLuminanceNits);
        AppendFormat(json, ", \"planet_radius_m\": ");
        AppendNumber(json, sky.PlanetRadiusMeters);
        AppendFormat(json, ", \"atmosphere_height_m\": ");
        AppendNumber(json, sky.AtmosphereHeightMeters);
        AppendFormat(json, ", \"rayleigh_scale_height_m\": ");
        AppendNumber(json, sky.RayleighScaleHeightMeters);
        AppendFormat(json, ", \"mie_scale_height_m\": ");
        AppendNumber(json, sky.MieScaleHeightMeters);
        AppendFormat(json, ", \"mie_density_scale\": ");
        AppendNumber(json, sky.MieDensityScale);
        AppendFormat(json, ", \"mie_anisotropy\": ");
        AppendNumber(json, sky.MieAnisotropy);
        AppendFormat(json, ", \"ground_albedo\": ");
        AppendVector3(json, sky.GroundAlbedo);
        AppendFormat(json, ", \"ground_altitude_m\": ");
        AppendNumber(json, model.GetParameters().GroundAltitudeMeters);
        AppendFormat(json, ", \"observer_altitude_m\": ");
        AppendNumber(json, model.GetObserverAltitudeMeters());
        AppendFormat(json, ", \"sun_disk_solid_angle_sr\": ");
        AppendNumber(json, SolarDiskSolidAngleSteradians);
        AppendFormat(json, ", \"ground_extinction_rayleigh_per_m\": ");
        AppendVector3(json, groundExtinction.Rayleigh);
        AppendFormat(json, ", \"ground_extinction_mie_per_m\": ");
        AppendNumber(json, groundExtinction.Mie);
        AppendFormat(json, ", \"ground_scattering_mie_per_m\": ");
        AppendNumber(json, groundExtinction.MieScattering);
        AppendFormat(json, "},\n");

        const AerialPerspectiveParameters& aerial = info.Aerial;
        AppendFormat(json, "  \"aerial_perspective\": {\"enabled\": %s, \"available\": %s, \"distance_scale\": ",
                     aerial.bEnabled ? "true" : "false", info.bAerialAvailable ? "true" : "false");
        AppendNumber(json, aerial.DistanceScale);
        AppendFormat(json, ", \"start_distance_m\": ");
        AppendNumber(json, aerial.StartDistanceMeters);
        AppendFormat(json, ", \"origin_altitude_m\": ");
        AppendNumber(json, aerial.OriginAltitudeMeters);
        AppendFormat(json, ", \"mie_density_scale\": ");
        AppendNumber(json, aerial.MieDensityScale);
        AppendFormat(json, ", \"shader_rayleigh_extinction_per_m\": ");
        AppendVector3(json, info.RayleighExtinction);
        AppendFormat(json, ", \"shader_mie_extinction_per_m\": ");
        AppendNumber(json, info.MieExtinction);
        AppendFormat(json, ", \"shader_mie_scattering_per_m\": ");
        AppendNumber(json, info.MieScattering);
        AppendFormat(json, ", \"march_steps\": %u", VolumetricsAtmosphereMarchSteps);
        AppendFormat(json, ", \"transmittance_lut_observer_v\": ");
        AppendNumber(json, info.TransmittanceLutObserverV);
        AppendFormat(json, "},\n");

        const VolumetricFogParameters& fog = info.Fog;
        AppendFormat(json, "  \"height_fog\": {\"enabled\": %s, \"density_at_base_per_m\": ", fog.bEnabled ? "true" : "false");
        AppendNumber(json, fog.DensityAtBaseHeight);
        AppendFormat(json, ", \"base_height_m\": ");
        AppendNumber(json, fog.BaseHeight);
        AppendFormat(json, ", \"height_falloff_per_m\": ");
        AppendNumber(json, fog.HeightFalloffPerUnit);
        AppendFormat(json, ", \"color_from_sky_radiance_lut\": %s, \"fallback_color\": ",
                     info.bSkyRadianceAvailable ? "true" : "false");
        AppendVector3(json, info.FallbackFogColor);
        AppendFormat(json, ", \"sun_single_scattering\": %s, \"sun_light_direction_of_travel\": ",
                     info.bDirectionalAvailable ? "true" : "false");
        AppendVector3(json, info.DirectionalDirection);
        AppendFormat(json, ", \"sun_light_illuminance_lux\": ");
        AppendVector3(json, info.DirectionalRadiance);
        AppendFormat(json, ", \"phase_anisotropy\": ");
        AppendNumber(json, info.DirectionalAnisotropy);
        AppendFormat(json, "},\n");

        // 地表の太陽と空の照度
        AppendFormat(json, "  \"ground_illuminance\": {\"sun_top_of_atmosphere_normal_lux\": ");
        AppendNumber(json, model.GetSunDiskIrradiance());
        AppendFormat(json, ", \"sun_ground_transmittance\": ");
        AppendVector3(json, sunTransmittance);
        AppendFormat(json, ", \"sun_normal_lux\": ");
        AppendVector3(json, sunNormal);
        AppendFormat(json, ", \"sun_horizontal_lux\": ");
        AppendVector3(json, sunHorizontal);
        AppendFormat(json, ", \"sky_horizontal_lux\": ");
        AppendVector3(json, skyHorizontal);
        AppendFormat(json, "},\n");

        // GPU の LUT（半精度の値を単精度で）
        AppendFormat(json, "  \"transmittance_lut\": {");
        AppendFileEntry(json, files[1].Name, lutSettings.TransmittanceHeight, lutSettings.TransmittanceWidth, 4u);
        AppendFormat(json, ", \"gpu_format\": \"rgba16f\", \"units\": \"transmittance\", "
                           "\"x\": \"cos_zenith = (x + 0.5) / width\", \"y\": \"altitude_fraction = y / (height - 1)\", "
                           "\"alpha\": \"1\"},\n");
        AppendFormat(json, "  \"radiance_lut\": {");
        AppendFileEntry(json, files[2].Name, lutSettings.RadianceHeight, lutSettings.RadianceWidth, 4u);
        AppendFormat(json, ", \"gpu_format\": \"rgba16f\", \"units\": \"cd/m2\", \"mapping\": \"equirectangular\", "
                           "\"x\": \"longitude = ((x + 0.5) / width - 0.5) * 2pi = atan2(z, x)\", "
                           "\"y\": \"latitude = ((y + 0.5) / height - 0.5) * pi, direction_y = -sin(latitude)\", "
                           "\"alpha\": \"1 inside sun disk\"},\n");

        // CPU の前計算の表（軸の値も書く）
        AppendFormat(json, "  \"transmittance_table\": {");
        AppendFileEntry(json, files[3].Name, transmittanceTable.Rows, transmittanceTable.Columns, 3u);
        AppendFormat(json, ", \"units\": \"optical_depth (transmittance = exp(-value))\", \"altitude_m\": [");
        for (uint32_t row = 0; row < transmittanceTable.Rows; ++row)
        {
            AppendNumber(json, model.GetTransmittanceTableAltitudeMeters(row));
            AppendFormat(json, row + 1u < transmittanceTable.Rows ? ", " : "], \"cos_zenith\": [");
        }
        for (uint32_t column = 0; column < transmittanceTable.Columns; ++column)
        {
            AppendNumber(json, SkyAtmosphereModel::GetTransmittanceTableCosine(column));
            AppendFormat(json, column + 1u < transmittanceTable.Columns ? ", " : "]},\n");
        }
        AppendFormat(json, "  \"multiple_scattering\": {");
        AppendFileEntry(json, files[4].Name, multipleScatteringTable.Rows, multipleScatteringTable.Columns, 3u);
        AppendFormat(json, ", \"units\": \"cd/m2 per lux of sun illuminance (psi_ms)\", \"altitude_m\": [");
        for (uint32_t row = 0; row < multipleScatteringTable.Rows; ++row)
        {
            AppendNumber(json, model.GetMultipleScatteringTableAltitudeMeters(row));
            AppendFormat(json, row + 1u < multipleScatteringTable.Rows ? ", " : "], \"sun_cos_zenith\": [");
        }
        for (uint32_t column = 0; column < multipleScatteringTable.Columns; ++column)
        {
            AppendNumber(json, SkyAtmosphereModel::GetMultipleScatteringTableSunCosine(column));
            AppendFormat(json, column + 1u < multipleScatteringTable.Columns ? ", " : "]},\n");
        }
        AppendFormat(json, "  \"sky_view\": {");
        AppendFileEntry(json, files[5].Name, skyViewTable.Rows, skyViewTable.Columns, 3u);
        AppendFormat(json, ", \"units\": \"cd/m2\", \"sun_azimuth_radians\": ");
        AppendNumber(json, model.GetSunAzimuthRadians());
        AppendFormat(json, ", \"horizon_elevation_radians\": ");
        AppendNumber(json, model.GetHorizonElevationRadians());
        AppendFormat(json, ", \"elevation_radians\": [");
        for (uint32_t row = 0; row < skyViewTable.Rows; ++row)
        {
            AppendNumber(json, model.GetSkyViewTableElevationRadians(row));
            AppendFormat(json, row + 1u < skyViewTable.Rows ? ", " : "], \"azimuth_difference_radians\": [");
        }
        for (uint32_t column = 0; column < skyViewTable.Columns; ++column)
        {
            AppendNumber(json, SkyAtmosphereModel::GetSkyViewTableAzimuthDifferenceRadians(column));
            AppendFormat(json, column + 1u < skyViewTable.Columns ? ", " : "]},\n");
        }

        // 空気遠近の高度ごとの表（VolumetricsPass が定数へ詰める値）
        AppendFormat(json, "  \"aerial_profile\": {");
        AppendFileEntry(json, files[6].Name, 2u, SkyAerialProfileSize, 4u);
        AppendFormat(json, ", \"rows\": [\"sun_illuminance_times_transmittance_rgb_lux|1\", "
                           "\"sun_illuminance_times_psi_ms_rgb_cd_m2|1\"], \"max_altitude_m\": ");
        AppendNumber(json, SkyAerialProfileMaxAltitudeMeters);
        AppendFormat(json, ", \"altitude\": \"sea_level_m = index * max_altitude_m / (size - 1)\", \"sun_cos_zenith\": ");
        AppendNumber(json, aerialProfile.SunCosine);
        AppendFormat(json, "},\n");

        // 決めた視線の値（行の意味は AtmosphereProbePass の説明と同じ）
        AppendFormat(json, "  \"probe\": {");
        AppendFileEntry(json, files[0].Name, FrameCaptureAtmosphereProbeRows, probe.Width, 4u);
        AppendFormat(json, ", \"ground_eye_height_m\": ");
        AppendNumber(json, info.GroundEyeHeightMeters);
        AppendFormat(json, ",\n    \"rows\": ["
                           "\"fog_transmittance|fog_opacity|aerial_mean_transmittance|composite_alpha\", "
                           "\"fog_color_rgb_cd_m2|sky_lut_used\", "
                           "\"fog_sun_single_scattering_rgb_cd_m2|enabled\", "
                           "\"aerial_transmittance_rgb|enabled\", "
                           "\"aerial_scattering_rgb_cd_m2|0\", "
                           "\"sky_radiance_lut_rgb_cd_m2|0\", "
                           "\"composite_added_light_rgb_cd_m2|composite_alpha\", "
                           "\"aerial_single_scattering_rgb_cd_m2|0\", "
                           "\"aerial_multiple_scattering_rgb_cd_m2|0\", "
                           "\"fog_ambient_light_rgb_cd_m2|0\"],\n    \"rays\": [\n");
        for (uint32_t rayIndex = 0; rayIndex < info.RayCount; ++rayIndex)
        {
            const FrameCaptureAtmosphereRay& ray = info.Rays[rayIndex];
            AppendFormat(json, "      {\"kind\": \"%s\", \"azimuth_offset_degrees\": ",
                         ray.Kind == FrameCaptureAtmosphereRayKind::Ground ? "ground" : "direction");
            AppendNumber(json, ray.AzimuthOffsetDegrees);
            AppendFormat(json, ", \"elevation_degrees\": ");
            AppendNumber(json, ray.ElevationDegrees);
            AppendFormat(json, ", \"nominal_distance_m\": ");
            AppendNumber(json, ray.NominalDistanceMeters);
            AppendFormat(json, ", \"distance_m\": ");
            AppendNumber(json, ray.DistanceMeters);
            AppendFormat(json, ", \"direction\": ");
            AppendVector3(json, ray.Direction);
            AppendFormat(json, rayIndex + 1u < info.RayCount ? "},\n" : "}\n");
        }
        AppendFormat(json, "    ]\n  },\n");

        // 検査用の割り振り（空気遠近の Mie の一部を高さフォグへ移した媒質）で同じ視線を評価した値（行の意味は probe と同じ）
        const FrameCaptureAtmosphereTestMedia& testMedia = info.TestMedia;
        AppendFormat(json, "  \"test_media\": {\"valid\": %s, \"fog_share\": ", testMedia.bValid ? "true" : "false");
        AppendNumber(json, testMedia.FogShare);
        AppendFormat(json, ", ");
        AppendFileEntry(json, files[7].Name, FrameCaptureAtmosphereProbeRows, probe.Width, 4u);
        AppendFormat(json, ", \"rows\": \"same_as_probe\",\n    \"height_fog\": {\"enabled\": %s, \"density_at_base_per_m\": ",
                     testMedia.Fog.bEnabled ? "true" : "false");
        AppendNumber(json, testMedia.Fog.DensityAtBaseHeight);
        AppendFormat(json, ", \"base_height_m\": ");
        AppendNumber(json, testMedia.Fog.BaseHeight);
        AppendFormat(json, ", \"height_falloff_per_m\": ");
        AppendNumber(json, testMedia.Fog.HeightFalloffPerUnit);
        AppendFormat(json, "},\n    \"aerial_perspective\": {\"enabled\": %s, \"mie_density_scale\": ",
                     testMedia.Aerial.bEnabled ? "true" : "false");
        AppendNumber(json, testMedia.Aerial.MieDensityScale);
        AppendFormat(json, ", \"shader_mie_extinction_per_m\": ");
        AppendNumber(json, testMedia.MieExtinction);
        AppendFormat(json, ", \"shader_mie_scattering_per_m\": ");
        AppendNumber(json, testMedia.MieScattering);
        AppendFormat(json, "}\n  }\n}\n");
        return WriteBytes(basePath + String(TEXT(".atmos.json")), json.data(), json.size());
    }

    namespace
    {
        // 名前付きの 4 つの float（cloud_layer.frag の定数の vec4 の 1 つ）を JSON の配列で書く
        void AppendFloat4(VariableArray<char>& out, const char* name, const float* pValues, bool bLast = false)
        {
            AppendFormat(out, "\"%s\": [", name);
            for (uint32_t index = 0; index < 4u; ++index)
            {
                AppendNumber(out, pValues[index]);
                AppendFormat(out, index + 1u < 4u ? ", " : "]");
            }
            AppendFormat(out, bLast ? "" : ", ");
        }
    } // namespace

    bool SaveCapturedClouds(const CapturedFrame& frame, const Container::String& pngPath)
    {
        const CapturedClouds& clouds = frame.Clouds;
        const FrameCaptureCloudInfo& info = clouds.Info;
        const CapturedImage& probe = clouds.Probe;
        if (!frame.IsSuccess() || !clouds.bValid || !info.bValid || info.RayCount == 0u ||
            info.RayCount > FrameCaptureCloudMaxRays || probe.Format != RHI::Format::R32G32B32A32_FLOAT ||
            probe.BytesPerPixel != 16u || probe.Width != info.RayCount || probe.Height != FrameCaptureCloudProbeRows ||
            probe.RowPitchBytes < static_cast<uint64_t>(probe.Width) * probe.BytesPerPixel ||
            probe.Pixels.size() < static_cast<size_t>(probe.RowPitchBytes) * probe.Height)
        {
            return false;
        }
        const String basePath = RemoveExtension(pngPath);

        // 決めた画素の視線の値（GPU が cloud_layer.frag と同じ式で評価したもの）: [行, 視線, RGBA]
        VariableArray<float> probeValues;
        probeValues.resize(static_cast<size_t>(FrameCaptureCloudProbeRows) * probe.Width * 4u);
        for (uint32_t row = 0; row < FrameCaptureCloudProbeRows; ++row)
        {
            std::memcpy(probeValues.data() + static_cast<size_t>(row) * probe.Width * 4u,
                        probe.Pixels.data() + static_cast<size_t>(row) * probe.RowPitchBytes,
                        static_cast<size_t>(probe.Width) * 16u);
        }

        // 密度場の 3D ノイズ（CloudLayerPass が GPU へ上げる値と同じ関数で作り直す。CPU で決まるので同じ値になる）
        VariableArray<uint8_t> shapeTexels;
        VariableArray<uint8_t> detailTexels;
        BuildCloudNoiseTexels(shapeTexels, detailTexels);
        const uint32_t shapeSize = info.ShapeNoiseSize;
        const uint32_t detailSize = info.DetailNoiseSize;
        if (shapeTexels.size() != static_cast<size_t>(shapeSize) * shapeSize * shapeSize * 4u ||
            detailTexels.size() != static_cast<size_t>(detailSize) * detailSize * detailSize * 4u)
        {
            return false;
        }

        // 空の放射輝度 LUT（雲の環境光と雲の後ろの空を引いた表。SkyAtmospherePass と同じ関数で作り直す）
        SkyAtmosphereModel model(info.Sky);
        model.BuildSkyViewTable();
        const SkyAtmospherePassSettings lutSettings;
        VariableArray<uint16_t> radianceTexels;
        BuildSkyRadianceLutTexels(model, lutSettings.RadianceWidth, lutSettings.RadianceHeight, radianceTexels);
        const VariableArray<float> radianceValues = HalfTexelsToFloats(radianceTexels);
        if (radianceValues.empty())
        {
            return false;
        }

        const String probePath = basePath + String(TEXT(".cloud.probe.f32"));
        const String shapePath = basePath + String(TEXT(".cloud.shape_noise.u8"));
        const String detailPath = basePath + String(TEXT(".cloud.detail_noise.u8"));
        const String radiancePath = basePath + String(TEXT(".cloud.sky_radiance_lut.f32"));
        // 雲までの空気遠近の高度ごとの表（定数の AerialProfile。[行, 段, RGBA]、行 0 = 太陽の照度 × 透過率、行 1 = 太陽の照度 × Ψ_ms）
        const String aerialProfilePath = basePath + String(TEXT(".cloud.aerial_profile.f32"));
        const float* shaderParams = info.ShaderParams;
        constexpr uint32_t AerialProfileParamOffset = 68u;
        static_assert(AerialProfileParamOffset + 8u * SkyAerialProfileSize == FrameCaptureCloudShaderParamCount);
        VariableArray<float> aerialProfileValues;
        aerialProfileValues.resize(2u * SkyAerialProfileSize * 4u);
        std::memcpy(aerialProfileValues.data(), shaderParams + AerialProfileParamOffset,
                    aerialProfileValues.size() * sizeof(float));
        if (!WriteFloats(probePath, probeValues) || !WriteBytes(shapePath, shapeTexels.data(), shapeTexels.size()) ||
            !WriteBytes(detailPath, detailTexels.data(), detailTexels.size()) || !WriteFloats(radiancePath, radianceValues) ||
            !WriteFloats(aerialProfilePath, aerialProfileValues))
        {
            return false;
        }

        const CloudLayerParameters& cloud = info.Cloud;
        const SkyAtmosphereParameters& sky = info.Sky;
        VariableArray<char> json;
        const VariableArray<char> pngName = ToAsciiFileName(pngPath);
        AppendFormat(json, "{\n  \"format\": \"norves_cloud_dump_v1\",\n  \"png\": \"%s\",\n  \"frame_number\": %llu,\n",
                     pngName.data(), static_cast<unsigned long long>(frame.FrameNumber));
        AppendFormat(json, "  \"conventions\": {\"color\": \"linear_rec709_d65\", \"radiance_units\": \"cd/m2\", "
                           "\"illuminance_units\": \"lux\", \"world\": \"y_up_meters\", \"pre_exposure\": \"removed\", "
                           "\"pixel\": \"gl_FragCoord (origin top-left, pixel center at +0.5)\", "
                           "\"uv\": \"pixel / target_size\", \"clip\": \"(uv * 2 - 1, 1, 1)\", "
                           "\"matrix\": \"column_major (inverse_view_projection[column * 4 + row])\"},\n");

        // 時刻と画面
        AppendFormat(json, "  \"time_seconds\": ");
        AppendNumber(json, info.TimeSeconds);
        AppendFormat(json, ",\n  \"temporal_frame_index\": %llu,\n  \"target\": {\"width\": %u, \"height\": %u},\n",
                     static_cast<unsigned long long>(info.TemporalFrameIndex), info.TargetWidth, info.TargetHeight);

        // cloud_layer.frag の定数（CloudLayerParams。CloudLayerPass と同じ規則で詰めた値そのもの）
        AppendFormat(json, "  \"shader_params\": {\"inverse_view_projection\": [");
        for (uint32_t index = 0; index < 16u; ++index)
        {
            AppendNumber(json, shaderParams[index]);
            AppendFormat(json, index + 1u < 16u ? ", " : "],\n    ");
        }
        AppendFloat4(json, "camera_position_and_pre_exposure", shaderParams + 16);
        AppendFloat4(json, "sun_direction_and_frame_index", shaderParams + 20);
        AppendFloat4(json, "sun_illuminance", shaderParams + 24);
        AppendFormat(json, "\n    ");
        AppendFloat4(json, "layer_altitudes_and_coverage", shaderParams + 28);
        AppendFloat4(json, "extinction_and_inverse_scales", shaderParams + 32);
        AppendFormat(json, "\n    ");
        AppendFloat4(json, "shape_and_weather_offset", shaderParams + 36);
        AppendFloat4(json, "detail_offset", shaderParams + 40);
        AppendFloat4(json, "ground_albedo", shaderParams + 44);
        AppendFloat4(json, "weather_high_offset", shaderParams + 48);
        AppendFormat(json, "\n    ");
        AppendFloat4(json, "aerial_perspective", shaderParams + 52);
        AppendFloat4(json, "rayleigh_extinction_and_scale_height", shaderParams + 56);
        AppendFormat(json, "\n    ");
        AppendFloat4(json, "mie_extinction_and_scattering", shaderParams + 60);
        AppendFloat4(json, "aerial_profile_range", shaderParams + 64, true);
        AppendFormat(json, "},\n");
        AppendFormat(json, "  \"aerial_profile\": {");
        const VariableArray<char> aerialProfileName = ToAsciiFileName(aerialProfilePath);
        AppendFileEntry(json, aerialProfileName, 2u, SkyAerialProfileSize, 4u);
        AppendFormat(json, ", \"rows\": [\"sun_illuminance_times_transmittance_lux\", \"sun_illuminance_times_psi_ms_cd_m2\"], "
                           "\"altitude\": \"sea level altitude = index * aerial_profile_range.x / (size - 1)\"},\n");

        // 雲の層と空のパラメータ
        AppendFormat(json, "  \"cloud_layer\": {\"enabled\": %s, \"bottom_altitude_m\": ", cloud.bEnabled ? "true" : "false");
        AppendNumber(json, cloud.BottomAltitudeMeters);
        AppendFormat(json, ", \"top_altitude_m\": ");
        AppendNumber(json, cloud.TopAltitudeMeters);
        AppendFormat(json, ", \"coverage\": ");
        AppendNumber(json, cloud.Coverage);
        AppendFormat(json, ", \"extinction_per_m\": ");
        AppendNumber(json, cloud.ExtinctionPerMeter);
        AppendFormat(json, ", \"shape_scale_m\": ");
        AppendNumber(json, cloud.ShapeScaleMeters);
        AppendFormat(json, ", \"wind_velocity_m_s\": [");
        AppendNumber(json, cloud.WindVelocityX);
        AppendFormat(json, ", ");
        AppendNumber(json, cloud.WindVelocityZ);
        AppendFormat(json, "]},\n  \"sky\": {\"sun_altitude_degrees\": ");
        AppendNumber(json, sky.SunAltitudeDegrees);
        AppendFormat(json, ", \"sun_azimuth_degrees\": ");
        AppendNumber(json, sky.SunAzimuthDegrees);
        AppendFormat(json, ", \"planet_radius_m\": ");
        AppendNumber(json, sky.PlanetRadiusMeters);
        AppendFormat(json, ", \"ground_albedo\": ");
        AppendVector3(json, sky.GroundAlbedo);
        AppendFormat(json, "},\n");

        // 密度場: 3D ノイズの格子と、世界の位置からの引き方（式は Common/CloudLayerCommon.glsl の SampleCloudDensity）
        const VariableArray<char> shapeName = ToAsciiFileName(shapePath);
        const VariableArray<char> detailName = ToAsciiFileName(detailPath);
        AppendFormat(json, "  \"density_field\": {\n");
        AppendFormat(json, "    \"shape_noise\": {\"file\": \"%s\", \"dtype\": \"uint8\", \"shape\": [%u, %u, %u, 4], "
                           "\"layout\": \"z_y_x_rgba\", \"fnv1a64\": \"%016llx\", "
                           "\"channels\": \"r=perlin_worley g,b,a=inverted_worley_fbm(cells 4,8,16 first)\"},\n",
                     shapeName.data(), shapeSize, shapeSize, shapeSize,
                     static_cast<unsigned long long>(HashCloudNoiseTexels(shapeTexels)));
        AppendFormat(json, "    \"detail_noise\": {\"file\": \"%s\", \"dtype\": \"uint8\", \"shape\": [%u, %u, %u, 4], "
                           "\"layout\": \"z_y_x_rgba\", \"fnv1a64\": \"%016llx\", "
                           "\"channels\": \"r,g,b=inverted_worley_fbm(cells 2,4,8 first) a=1\"},\n",
                     detailName.data(), detailSize, detailSize, detailSize,
                     static_cast<unsigned long long>(HashCloudNoiseTexels(detailTexels)));
        AppendFormat(json, "    \"texel_format\": \"rgba8_unorm (value / 255, linear)\", \"interpolation\": \"trilinear\", "
                           "\"wrap\": \"repeat\", \"texel_center\": \"(index + 0.5) / size\", \"mip\": 0,\n");
        AppendFormat(json, "    \"sample_position\": \"(world_x, altitude_above_planet_surface_m, world_z); "
                           "altitude = |(x - camera_x, planet_radius + max(camera_y, 1) + direction_y * t, z - camera_z)| - planet_radius\",\n");
        AppendFormat(json, "    \"shape_period_m\": ");
        AppendNumber(json, cloud.ShapeScaleMeters);
        AppendFormat(json, ", \"detail_scale_ratio\": ");
        AppendNumber(json, info.DetailScaleRatio);
        AppendFormat(json, ", \"weather_scale_ratio\": ");
        AppendNumber(json, info.WeatherScaleRatio);
        AppendFormat(json, ",\n    \"wind\": {\"detail_wind_ratio\": ");
        AppendNumber(json, info.DetailWindRatio);
        AppendFormat(json, ", \"detail_rise_velocity_m_s\": ");
        AppendNumber(json, info.DetailRiseVelocity);
        AppendFormat(json, ", \"shape_offset_m\": \"fmod(wind * time, shape_period)\", "
                           "\"detail_offset_m\": \"fmod((wind_x * ratio, rise, wind_z * ratio) * time, detail_period)\", "
                           "\"weather_offset_uv\": \"fract(rotation * wind * time / weather_period)\", "
                           "\"weather_high_offset_uv\": \"fract((v, u) * high_scale + high_phase)\"},\n");
        AppendFormat(json, "    \"weather\": {\"rotation_cos_sin\": [");
        AppendNumber(json, info.WeatherRotationCos);
        AppendFormat(json, ", ");
        AppendNumber(json, info.WeatherRotationSin);
        AppendFormat(json, "], \"high_scale\": ");
        AppendNumber(json, info.WeatherHighScale);
        AppendFormat(json, ", \"high_phase_uw\": [");
        AppendNumber(json, info.WeatherHighPhaseU);
        AppendFormat(json, ", ");
        AppendNumber(json, info.WeatherHighPhaseW);
        AppendFormat(json, "], \"low_slice_v\": 0.37, \"high_slice_v\": 0.71, \"weights_low_r_low_g_high_r\": [0.55, 0.2, 0.25], "
                           "\"coverage_smoothstep\": \"(1 - coverage - 0.08, 1 - coverage + 0.22)\", "
                           "\"mask_threshold\": 0.001},\n");
        AppendFormat(json, "    \"height_profile\": {\"bottom_ramp\": [0.0, 0.07], \"top_limit\": \"mix(0.35, 1, coverage_mask)\", "
                           "\"top_ramp_start_fraction\": 0.55},\n");
        AppendFormat(json, "    \"shape\": {\"low_fbm_weights_gba\": [0.625, 0.25, 0.125], "
                           "\"base\": \"clamp(remap(r, low_fbm - 1, 1, 0, 1))\", "
                           "\"coverage\": \"clamp(remap(base * profile, 1 - coverage_mask, 1, 0, 1)) * coverage_mask\"},\n");
        AppendFormat(json, "    \"detail\": {\"fbm_weights_rgb\": [0.625, 0.25, 0.125], \"height_blend_scale\": 4.0, "
                           "\"erosion\": 0.35, \"max_distance_m\": 18000, \"light_march_detail_steps\": 3},\n");
        AppendFormat(json, "    \"remap_min_range\": 1e-05\n  },\n");

        // 視線の進め方（cloud_layer.frag と同じ定数）
        AppendFormat(json, "  \"march\": {\"range\": \"cloud layer shell entry to exit\", "
                           "\"min_steps\": 32, \"max_steps\": 192,\"step_target_m\": 90, \"min_transmittance\": 0.01, "
                           "\"density_threshold\": 0.001, \"camera_altitude_floor_m\": 1.0, "
                           "\"jitter\": \"fract(52.9829189 * fract(dot(pixel + 5.588238 * frame_phase, (0.06711056, 0.00583715))))\", "
                           "\"frame_phase\": \"temporal_frame_index mod 1024\", "
                           "\"step_transmittance\": \"exp(-density * extinction * step_length)\"},\n");

        // 雲の照明に使う値（太陽・空・位相・多重散乱の近似・背景）
        AppendFormat(json, "  \"lighting\": {\"sun_direction\": [");
        AppendNumber(json, shaderParams[20]);
        AppendFormat(json, ", ");
        AppendNumber(json, shaderParams[21]);
        AppendFormat(json, ", ");
        AppendNumber(json, shaderParams[22]);
        AppendFormat(json, "], \"sun_illuminance_lux\": [");
        AppendNumber(json, shaderParams[24]);
        AppendFormat(json, ", ");
        AppendNumber(json, shaderParams[25]);
        AppendFormat(json, ", ");
        AppendNumber(json, shaderParams[26]);
        AppendFormat(json, "],\n    \"phase\": {\"function\": \"mix(hg(cos, g_forward * s), hg(cos, g_backward * s), backward_weight)\", "
                           "\"g_forward\": 0.8, \"g_backward\": -0.3, \"backward_weight\": 0.3, "
                           "\"octave_scale\": \"s = 0.5^octave\", \"hg_denominator_floor\": 0.0001},\n");
        AppendFormat(json, "    \"multiple_scattering\": {\"octaves\": 3, \"contribution\": 0.5, \"attenuation\": 0.5, "
                           "\"sun_term\": \"sum c^i * exp(-light_optical_depth * a^i) * phase_i\"},\n");
        AppendFormat(json, "    \"light_march\": {\"steps\": 6, \"first_step_m\": 70, \"step_growth\": 1.8, "
                           "\"sample\": \"noise_position + sun_direction * (distance + step / 2), height_fraction from position y\"},\n");
        AppendFormat(json, "    \"ambient\": {\"sky_above\": \"0.2 * (sky(0,1,0) + sky(0.866,0.5,0) + sky(-0.866,0.5,0) + "
                           "sky(0,0.5,0.866) + sky(0,0.5,-0.866))\", "
                           "\"ground_radiance\": \"ground_albedo * (sun_illuminance * max(sun_y, 0) / pi + sky_above)\", "
                           "\"term\": \"sky_above * mix(0.6, 1, h) + 0.5 * ground_radiance * mix(1, 0.3, h)\"},\n");
        AppendFormat(json, "    \"integration\": \"scattered += T * sigma * (sun_illuminance * sun_term + ambient) * (1 - exp(-sigma * dt)) / max(sigma, 1e-7)\",\n");
        AppendFormat(json, "    \"background\": {\"aerial_steps\": 32, "
                           "\"radiance\": \"scattered * aerial_transmittance(d) + aerial_scattering(0, d) * opacity\", "
                           "\"aerial\": \"VolumetricsCommon.glsl MarchFogAndAerial without fog, d = cloud_distance\", "
                           "\"fade\": \"1\"}},\n");
        AppendFormat(json, "  \"sky_radiance_lut\": {");
        const VariableArray<char> radianceName = ToAsciiFileName(radiancePath);
        AppendFileEntry(json, radianceName, lutSettings.RadianceHeight, lutSettings.RadianceWidth, 4u);
        AppendFormat(json, ", \"gpu_format\": \"rgba16f\", \"units\": \"cd/m2\", \"mapping\": \"equirectangular\", "
                           "\"sampler\": \"bilinear, wrap u, clamp v\", "
                           "\"x\": \"longitude = ((x + 0.5) / width - 0.5) * 2pi = atan2(z, x)\", "
                           "\"y\": \"latitude = ((y + 0.5) / height - 0.5) * pi, direction_y = -sin(latitude)\"},\n");

        // 決めた画素の視線の値（行の意味は CloudProbePass の説明と同じ）
        const VariableArray<char> probeName = ToAsciiFileName(probePath);
        AppendFormat(json, "  \"probe\": {");
        AppendFileEntry(json, probeName, FrameCaptureCloudProbeRows, probe.Width, 4u);
        AppendFormat(json, ",\n    \"rows\": ["
                           "\"transmittance|alpha|fade|steps_taken\", "
                           "\"scattered_rgb_cd_m2|cloud_distance_m\", "
                           "\"cloud_radiance_rgb_cd_m2|aerial_visibility\", "
                           "\"ray_direction_xyz|jitter\", "
                           "\"sky_above_rgb_cd_m2|start_distance_m\", "
                           "\"ground_radiance_rgb_cd_m2|end_distance_m\", "
                           "\"background_sky_rgb_cd_m2|scene_depth\", "
                           "\"optical_depth|step_count|step_length_m|flags(1=marched,2=direction)\"],\n    \"pixels\": [");
        for (uint32_t rayIndex = 0; rayIndex < info.RayCount; ++rayIndex)
        {
            AppendFormat(json, "[");
            AppendNumber(json, info.RayPixels[rayIndex][0]);
            AppendFormat(json, ", ");
            AppendNumber(json, info.RayPixels[rayIndex][1]);
            AppendFormat(json, rayIndex + 1u < info.RayCount ? "], " : "]");
        }
        AppendFormat(json, "]\n  }\n}\n");
        return WriteBytes(basePath + String(TEXT(".cloud.json")), json.data(), json.size());
    }

    bool SaveCapturedCloudOpacity(const CapturedFrame& frame, const Container::String& pngPath)
    {
        const CapturedClouds& clouds = frame.Clouds;
        const FrameCaptureCloudInfo& info = clouds.Info;
        const CapturedImage& image = clouds.Opacity;
        if (!frame.IsSuccess() || !clouds.bValid || !clouds.bOpacityValid || !info.bValid || !info.bOpacity ||
            image.Format != RHI::Format::R32G32B32A32_FLOAT || image.BytesPerPixel != 16u || image.Width == 0u ||
            image.Height == 0u || image.Width != info.TargetWidth || image.Height != info.TargetHeight ||
            image.RowPitchBytes < static_cast<uint64_t>(image.Width) * image.BytesPerPixel ||
            image.Pixels.size() < static_cast<size_t>(image.RowPitchBytes) * image.Height)
        {
            return false;
        }
        const String basePath = RemoveExtension(pngPath);

        // [行, 列, RGBA]。読み出しの行は上から（gl_FragCoord と同じ）なので、そのまま詰める
        VariableArray<float> values;
        values.resize(static_cast<size_t>(image.Width) * image.Height * 4u);
        for (uint32_t row = 0; row < image.Height; ++row)
        {
            std::memcpy(values.data() + static_cast<size_t>(row) * image.Width * 4u,
                        image.Pixels.data() + static_cast<size_t>(row) * image.RowPitchBytes,
                        static_cast<size_t>(image.Width) * 16u);
        }

        // 記録の集計（合否は Scripts/Terrain/CloudOpacityCheck.py が値から求め直す）
        uint64_t terrainPixels = 0;
        uint64_t skyPixels = 0;
        uint64_t cloudPixels = 0;
        uint64_t nonFinite = 0;
        double opacitySum = 0.0;
        float opacityMax = 0.0f;
        for (size_t pixel = 0; pixel < values.size() / 4u; ++pixel)
        {
            const float* rgba = values.data() + pixel * 4u;
            if (!std::isfinite(rgba[0]) || !std::isfinite(rgba[1]) || !std::isfinite(rgba[2]) || !std::isfinite(rgba[3]))
            {
                ++nonFinite;
                continue;
            }
            if (rgba[2] > 0.5f)
            {
                ++terrainPixels;
                continue;
            }
            ++skyPixels;
            opacitySum += rgba[0];
            opacityMax = std::max(opacityMax, rgba[0]);
            cloudPixels += rgba[0] >= 0.05f ? 1u : 0u;
        }

        const String imagePath = basePath + String(TEXT(".cloud.opacity.f32"));
        if (!WriteFloats(imagePath, values))
        {
            return false;
        }

        const float* shaderParams = info.ShaderParams;
        VariableArray<char> json;
        const VariableArray<char> pngName = ToAsciiFileName(pngPath);
        const VariableArray<char> imageName = ToAsciiFileName(imagePath);
        const VariableArray<char> dumpName = ToAsciiFileName(basePath + String(TEXT(".cloud.json")));
        AppendFormat(json, "{\n  \"format\": \"norves_cloud_opacity_v1\",\n  \"png\": \"%s\",\n  \"cloud_dump\": \"%s\",\n"
                           "  \"frame_number\": %llu,\n",
                     pngName.data(), dumpName.data(), static_cast<unsigned long long>(frame.FrameNumber));
        AppendFormat(json, "  \"time_seconds\": ");
        AppendNumber(json, info.TimeSeconds);
        AppendFormat(json, ",\n  \"temporal_frame_index\": %llu,\n  \"target\": {\"width\": %u, \"height\": %u},\n",
                     static_cast<unsigned long long>(info.TemporalFrameIndex), info.TargetWidth, info.TargetHeight);
        AppendFormat(json, "  \"image\": {");
        AppendFileEntry(json, imageName, image.Height, image.Width, 4u);
        AppendFormat(json, ",\n    \"orientation\": \"row 0 = top of the screen, column 0 = left (gl_FragCoord, pixel center at +0.5)\",\n"
                           "    \"channels\": [\"opacity_after_fade\", \"opacity_before_fade\", \"terrain_mask\", \"fade\"],\n"
                           "    \"value\": \"opacity_after_fade = (1 - cloud transmittance) * fade, the alpha cloud_layer.frag blends; "
                           "no haze, sun disk or terrain; 0 on terrain pixels\",\n"
                           "    \"terrain_mask\": \"1 where scene depth is finite and < 0.999999 (not sky), else 0\"},\n");
        // カメラ（cloud_layer.frag の定数の逆の射影とカメラの位置。視線の向き = normalize(逆の射影 × (uv * 2 - 1, 1, 1) の点 − 位置)）
        AppendFormat(json, "  \"camera\": {\"position\": ");
        AppendVector3(json, shaderParams + 16);
        AppendFormat(json, ", \"clip\": \"(uv * 2 - 1, 1, 1), uv = pixel / target_size\", "
                           "\"matrix\": \"column_major (inverse_view_projection[column * 4 + row])\", \"inverse_view_projection\": [");
        for (uint32_t index = 0; index < 16u; ++index)
        {
            AppendNumber(json, shaderParams[index]);
            AppendFormat(json, index + 1u < 16u ? ", " : "]},\n");
        }
        AppendFormat(json, "  \"summary\": {\"terrain_pixels\": %llu, \"sky_pixels\": %llu, \"cloud_pixels_0_05\": %llu, "
                           "\"non_finite\": %llu, \"sky_mean_opacity\": ",
                     static_cast<unsigned long long>(terrainPixels), static_cast<unsigned long long>(skyPixels),
                     static_cast<unsigned long long>(cloudPixels), static_cast<unsigned long long>(nonFinite));
        AppendNumber(json, skyPixels > 0u ? opacitySum / static_cast<double>(skyPixels) : 0.0);
        AppendFormat(json, ", \"max_opacity\": ");
        AppendNumber(json, opacityMax);
        AppendFormat(json, "}\n}\n");
        return WriteBytes(basePath + String(TEXT(".cloud.opacity.json")), json.data(), json.size());
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

    bool SaveCapturedAtmosphere(const CapturedFrame&, const Container::String&)
    {
        return false;
    }

    bool SaveCapturedClouds(const CapturedFrame&, const Container::String&)
    {
        return false;
    }

    bool SaveCapturedCloudOpacity(const CapturedFrame&, const Container::String&)
    {
        return false;
    }
#endif
} // namespace NorvesLib::Core::Rendering
