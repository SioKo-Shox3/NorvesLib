// discard は深度と全 MRT の書き込みを一緒に捨てるため、GBuffer.Emissive の書き込みの有無で
// 切り抜きによる深度の書き込みの有無も確かめる。背景と区別できる固定の発光値を被覆の印にする。
#include "AlphaClipScenario.h"

#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        struct PixelRect
        {
            int32_t Left;
            int32_t Top;
            int32_t Right;
            int32_t Bottom;
        };

        struct PlateProjection
        {
            double CenterX;
            double CenterY;
            double HalfExtent;
        };

        PlateProjection ProjectPlate(const AlphaClipPlateSpec& plate, const Core::Rendering::CameraProxy& camera)
        {
            const double focal = ValidationHeight * 0.5 / std::tan(camera.FieldOfView * 3.141592653589793 / 360.0);
            const double scale = focal / camera.PositionZ;
            return {ValidationWidth * 0.5 + scale * plate.X,
                    ValidationHeight * 0.5 - scale * plate.Y, scale * plate.HalfExtent};
        }

        bool MakeRect(const PlateProjection& plate, double extentFraction, PixelRect& rect)
        {
            const double extent = plate.HalfExtent * extentFraction;
            // ピクセル中心が領域内に入る範囲を、右端・下端を含まない整数区間にする。
            rect = {static_cast<int32_t>(std::ceil(plate.CenterX - extent - 0.5)),
                    static_cast<int32_t>(std::ceil(plate.CenterY - extent - 0.5)),
                    static_cast<int32_t>(std::ceil(plate.CenterX + extent - 0.5)),
                    static_cast<int32_t>(std::ceil(plate.CenterY + extent - 0.5))};
            return rect.Left >= 0 && rect.Top >= 0 && rect.Right <= static_cast<int32_t>(ValidationWidth) &&
                   rect.Bottom <= static_cast<int32_t>(ValidationHeight) &&
                   rect.Left < rect.Right && rect.Top < rect.Bottom;
        }
    } // namespace

    bool AlphaClipScenario::ParseArgument(const Core::Container::String& argument, Core::Container::String& reason)
    {
        if (m_bEnabled)
        {
            reason = TEXT("切り抜きのシナリオが重複しています");
            return false;
        }
        if (argument == TEXT("--alpha-clip-scenario=test"))
        {
            m_Kind = AlphaClipFixtureKind::Test;
        }
        else if (argument == TEXT("--alpha-clip-scenario=two-sided"))
        {
            m_Kind = AlphaClipFixtureKind::TwoSided;
        }
        else if (argument == TEXT("--alpha-clip-scenario=opaque"))
        {
            m_Kind = AlphaClipFixtureKind::Opaque;
        }
        else
        {
            reason = TEXT("切り抜きのシナリオ名が不正です");
            return false;
        }
        m_bEnabled = true;
        return true;
    }

    bool AlphaClipScenario::IsEnabled() const
    {
        return m_bEnabled;
    }

    Core::Rendering::FrameCaptureSourceKind AlphaClipScenario::GetCaptureSource() const
    {
        return m_Kind == AlphaClipFixtureKind::TwoSided ? Core::Rendering::FrameCaptureSourceKind::BackBuffer :
                                                       Core::Rendering::FrameCaptureSourceKind::GBufferEmissive;
    }

    bool AlphaClipScenario::LoadThresholds()
    {
        using namespace Core::Container;
        String path(NORVES_SOURCE_ROOT "/Test/Core/Rendering/Thresholds/RenderingValidation/AlphaClip.tsv");
        auto stream = FileStream::FileStream::CreateUnique(path, FileStream::FileMode::Read, FileStream::FileAccess::Read);
        if (!stream || stream->GetSize() < 4 || stream->GetSize() > 4096)
        {
            return false;
        }
        const size_t size = static_cast<size_t>(stream->GetSize());
        VariableArray<char> text(size + 1u, '\0');
        if (stream->Read(text.data(), size) != size)
        {
            return false;
        }
        char* cursor = text.data();
        if (std::memcmp(cursor, "\xEF\xBB\xBF", 3u) == 0)
        {
            cursor += 3;
        }
        auto takeLine = [&cursor]() -> char*
        {
            if (*cursor == '\0')
            {
                return nullptr;
            }
            char* line = cursor;
            while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n')
            {
                ++cursor;
            }
            if (*cursor == '\r')
            {
                *cursor++ = '\0';
            }
            if (*cursor == '\n')
            {
                *cursor++ = '\0';
            }
            return line;
        };
        char* header = takeLine();
        if (header == nullptr || std::strcmp(header, reinterpret_cast<const char*>(u8"名前\t値\t根拠")) != 0)
        {
            return false;
        }
        struct Entry
        {
            const char* Name;
            double* Value;
            bool bRead;
        };
        Entry entries[] = {
            {"coverage_target", &m_CoverageTarget, false},
            {"coverage_tolerance", &m_CoverageTolerance, false},
            {"emissive_tolerance", &m_EmissiveTolerance, false},
            {"maximum_luma_delta", &m_MaximumLumaDelta, false},
            {"minimum_background_luma_gain", &m_MinimumBackgroundLumaGain, false},
            {"minimum_texel_pixels", &m_MinimumTexelPixels, false}};
        while (char* line = takeLine())
        {
            char* value = std::strchr(line, '\t');
            if (value == nullptr)
            {
                return false;
            }
            *value++ = '\0';
            char* rationale = std::strchr(value, '\t');
            if (rationale == nullptr || rationale[1] == '\0')
            {
                return false;
            }
            *rationale = '\0';
            char* end = nullptr;
            const double number = std::strtod(value, &end);
            if (end == value || *end != '\0' || !std::isfinite(number) || number <= 0.0)
            {
                return false;
            }
            bool bFound = false;
            for (auto& entry : entries)
            {
                if (std::strcmp(entry.Name, line) == 0)
                {
                    if (entry.bRead)
                    {
                        return false;
                    }
                    *entry.Value = number;
                    entry.bRead = bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                return false;
            }
        }
        for (const auto& entry : entries)
        {
            if (!entry.bRead)
            {
                return false;
            }
        }
        return m_CoverageTarget < 1.0 && m_CoverageTolerance < m_CoverageTarget &&
               m_EmissiveTolerance < 1.0 && m_MaximumLumaDelta < 1.0 && m_MinimumBackgroundLumaGain < 1.0;
    }

    bool AlphaClipScenario::Initialize(Core::Rendering::RenderWorld& world, const RenderingValidationSceneFixture& fixture)
    {
        (void)world;
        if (!LoadThresholds() || !fixture.ApplyAlphaClipFixture(m_Kind))
        {
            LOG_ERROR("切り抜きのしきい値または試験用シーンを準備できませんでした");
            return false;
        }
        m_Fixture = &fixture;
        return true;
    }

    void AlphaClipScenario::ApplyState(Core::Rendering::RenderWorld& world) const
    {
        world.SetMainCamera(m_Kind == AlphaClipFixtureKind::Opaque ? m_Fixture->GetCamera() :
                                                                   m_Fixture->GetShadingModelGBufferCamera());
        auto sky = Core::Rendering::MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = false;
        world.SetSkyAtmosphere(sky);
        auto fog = Core::Rendering::MakeDefaultVolumetricFogParameters();
        fog.bEnabled = false;
        world.SetVolumetricFogParameters(fog);
    }

    bool AlphaClipScenario::Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Container::String& reason) const
    {
        if (!m_Fixture || !frame.IsSuccess() || frame.Width != ValidationWidth || frame.Height != ValidationHeight)
        {
            reason = TEXT("切り抜きの検証画像を取得できませんでした");
            return false;
        }
        if (m_Kind == AlphaClipFixtureKind::Opaque)
        {
            LOG_INFO("切り抜きの判定 ALPHA_CLIP_RESULT scenario=opaque captured=1 result=PASS");
            return true;
        }
        const auto camera = m_Fixture->GetShadingModelGBufferCamera();
        if (m_Kind == AlphaClipFixtureKind::TwoSided)
        {
            const bool bBgra = frame.Format == RHI::Format::B8G8R8A8_UNORM || frame.Format == RHI::Format::B8G8R8A8_SRGB;
            const bool bRgba = frame.Format == RHI::Format::R8G8B8A8_UNORM || frame.Format == RHI::Format::R8G8B8A8_SRGB;
            const bool bHardware = RHI::IsPresentationSrgbFormat(frame.Format);
            if ((!bBgra && !bRgba) || frame.BytesPerPixel != 4u || frame.RowPitchBytes < frame.Width * 4u ||
                frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height ||
                frame.ColorSpace != RHI::PresentationColorSpace::Rec709D65 ||
                frame.Transfer != RHI::PresentationTransfer::SRGB ||
                frame.bHardwareSrgbEncode != bHardware || frame.bShaderSrgbEncode == bHardware)
            {
                reason = TEXT("両面の輝度を調べる sRGB 画像の形式が不正です");
                return false;
            }
            auto sampleLuma = [&frame, bBgra](const PixelRect& rect)
            {
                double sum = 0.0;
                for (int32_t y = rect.Top; y < rect.Bottom; ++y)
                {
                    for (int32_t x = rect.Left; x < rect.Right; ++x)
                    {
                        const size_t offset = static_cast<size_t>(y) * frame.RowPitchBytes + static_cast<size_t>(x) * 4u;
                        sum += (0.2126 * frame.Pixels[offset + (bBgra ? 2u : 0u)] +
                                0.7152 * frame.Pixels[offset + 1u] +
                                0.0722 * frame.Pixels[offset + (bBgra ? 0u : 2u)]) / 255.0;
                    }
                }
                return sum / ((rect.Right - rect.Left) * (rect.Bottom - rect.Top));
            };
            double luma[2]{};
            double backgroundLuma[2]{};
            double lumaGain[2]{};
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                const auto plate = ProjectPlate(AlphaClipLightingPlates[index], camera);
                PixelRect center;
                PixelRect background;
                // 板の中央半分と、その真上の、板を含まない同じ大きさの背景を比べる。
                auto above = plate;
                above.CenterY -= 2.0 * plate.HalfExtent;
                if (!MakeRect(plate, 0.5, center) || !MakeRect(above, 0.5, background))
                {
                    reason = TEXT("両面の検査領域が画像の範囲外です");
                    return false;
                }
                luma[index] = sampleLuma(center);
                backgroundLuma[index] = sampleLuma(background);
                lumaGain[index] = luma[index] - backgroundLuma[index];
            }
            const double delta = std::abs(luma[0] - luma[1]);
            // 板は Z だけを向き、光の送り先は -Z。板から光へ向かう L は -direction = +Z。
            // 表は N・L=+1。裏は反転前なら -1 で直接光が 0、反転後なら +1 で表と同じ明るさになる。
            // 下の内積と実測の輝度・背景からの増分を同じ行に残し、暗い板同士の一致を不合格にする。
            const double frontNdotL = AlphaClipPlateNormalZ[0] * -AlphaClipLightingDirection[2];
            const double backUnflippedNdotL = AlphaClipPlateNormalZ[1] * -AlphaClipLightingDirection[2];
            const double backNdotL = -backUnflippedNdotL;
            const bool bPassed = delta <= m_MaximumLumaDelta && lumaGain[0] >= m_MinimumBackgroundLumaGain &&
                                 lumaGain[1] >= m_MinimumBackgroundLumaGain &&
                                 frontNdotL > 0.0 && backNdotL > 0.0 && backUnflippedNdotL < 0.0;
            LOG_INFO("両面の判定 ALPHA_CLIP_RESULT scenario=two-sided front_luma=%g back_luma=%g luma_delta=%g front_background_luma=%g back_background_luma=%g front_luma_gain=%g back_luma_gain=%g front_ndotl=%g back_ndotl=%g back_unflipped_ndotl=%g direct_lux=%g result=%s",
                     luma[0], luma[1], delta, backgroundLuma[0], backgroundLuma[1], lumaGain[0], lumaGain[1],
                     frontNdotL, backNdotL, backUnflippedNdotL, AlphaClipLightingLux, bPassed ? "PASS" : "FAIL");
            if (!bPassed)
            {
                reason = TEXT("両面の表裏の輝度差・背景からの明るさの増分または光の向きが基準を満たしません");
            }
            return bPassed;
        }

        RgbaFloatImage image;
        if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
        {
            reason = TEXT("切り抜きの発光画像の形式が不正です");
            return false;
        }
        const double expectedEmissive = AlphaClipEmissiveNits * camera.PreExposure;
        bool bFinite = true;
        auto covered = [&](int32_t x, int32_t y)
        {
            const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
            bool bCovered = true;
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                const double value = image.Values[offset + channel];
                bFinite = bFinite && std::isfinite(value);
                if (channel < 3u)
                {
                    bCovered = bCovered && std::isfinite(value) && std::abs(value - expectedEmissive) <= m_EmissiveTolerance;
                }
            }
            return bCovered;
        };
        double coverage[3]{};
        uint32_t counts[3]{};
        uint32_t totals[3]{};
        const auto checker = ProjectPlate(AlphaClipCoveragePlates[0], camera);
        const double texelPixels = checker.HalfExtent * 2.0 / 8.0;
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            PixelRect rect;
            // 市松は外周から半テクセル内側、裏向きの板は外周のラスタ境界を除いた内側全体。
            const auto plate = ProjectPlate(AlphaClipCoveragePlates[index], camera);
            const double innerFraction = index == 0u ? 7.0 / 8.0 : 1.0 - 1.0 / plate.HalfExtent;
            if (!MakeRect(plate, innerFraction, rect))
            {
                reason = TEXT("切り抜きの検査領域が画像の範囲外です");
                return false;
            }
            for (int32_t y = rect.Top; y < rect.Bottom; ++y)
            {
                for (int32_t x = rect.Left; x < rect.Right; ++x)
                {
                    counts[index] += covered(x, y) ? 1u : 0u;
                    ++totals[index];
                }
            }
            coverage[index] = static_cast<double>(counts[index]) / totals[index];
        }
        uint32_t matched = 0u;
        for (uint32_t y = 0u; y < 8u; ++y)
        {
            for (uint32_t x = 0u; x < 8u; ++x)
            {
                const int32_t pixelX = static_cast<int32_t>(std::floor(checker.CenterX + checker.HalfExtent * ((x + 0.5) / 4.0 - 1.0)));
                const int32_t pixelY = static_cast<int32_t>(std::floor(checker.CenterY - checker.HalfExtent * ((y + 0.5) / 4.0 - 1.0)));
                if (pixelX < 0 || pixelY < 0 || pixelX >= static_cast<int32_t>(image.Width) || pixelY >= static_cast<int32_t>(image.Height))
                {
                    reason = TEXT("テクセル中心が画像の範囲外です");
                    return false;
                }
                matched += covered(pixelX, pixelY) == ((x + y) % 2u == 0u) ? 1u : 0u;
            }
        }
        const bool bPassed = bFinite && texelPixels >= m_MinimumTexelPixels &&
            std::abs(coverage[0] - m_CoverageTarget) <= m_CoverageTolerance && matched == 64u &&
            counts[1] == 0u && counts[2] == totals[2];
        LOG_INFO("切り抜きの判定 ALPHA_CLIP_RESULT scenario=test coverage=%g texel_match=%u/64 single_back_coverage=%g two_back_coverage=%g single_back_pixels=%u/%u two_back_pixels=%u/%u texel_pixels=%g finite=%d result=%s",
                 coverage[0], matched, coverage[1], coverage[2], counts[1], totals[1], counts[2], totals[2], texelPixels,
                 bFinite ? 1 : 0, bPassed ? "PASS" : "FAIL");
        if (!bPassed)
        {
            reason = TEXT("市松の被覆率・テクセル中心または裏向きの被覆が基準を満たしません");
        }
        return bPassed;
    }
} // namespace NorvesLib::Test::RenderingValidation
