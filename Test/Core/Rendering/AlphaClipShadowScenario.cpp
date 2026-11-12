// 床上の四隅をカメラへ投影し、HDR の明暗から影の被覆を数える。
#include "AlphaClipShadowScenario.h"

#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        using namespace Core::Rendering;
        namespace Container = Core::Container;

        struct Point2
        {
            double X;
            double Y;
        };

        struct Quad
        {
            Point2 Points[4];
        };

        CameraProxy MakeShadowCamera()
        {
            CameraProxy camera;
            camera.CameraId = 1u;
            camera.PositionX = 0.0f;
            camera.PositionY = 12.0f;
            camera.PositionZ = 0.4f;
            camera.ForwardX = 0.0f;
            camera.ForwardY = -1.0f;
            camera.ForwardZ = 0.0f;
            camera.UpX = 0.0f;
            camera.UpY = 0.0f;
            camera.UpZ = -1.0f;
            camera.RightX = 1.0f;
            camera.RightY = 0.0f;
            camera.RightZ = 0.0f;
            camera.FieldOfView = 48.0f;
            camera.AspectRatio = 1.0f;
            camera.NearPlane = 0.1f;
            camera.FarPlane = 40.0f;
            camera.Viewport = {0.0f, 0.0f, static_cast<float>(ValidationWidth),
                               static_cast<float>(ValidationHeight), 0.0f, 1.0f};
            camera.ExposureMode = CameraExposureMode::Manual;
            camera.AntiAliasing = CameraAntiAliasingMode::FXAA;
            return camera;
        }

        Point2 ShadowOnFloor(float x, float z, bool point)
        {
            if (point)
            {
                const double t = AlphaClipShadowPointPosition[1] /
                    (AlphaClipShadowPointPosition[1] - AlphaClipShadowPlateHeight);
                return {AlphaClipShadowPointPosition[0] + t * (x - AlphaClipShadowPointPosition[0]),
                        AlphaClipShadowPointPosition[2] + t * (z - AlphaClipShadowPointPosition[2])};
            }
            const double distance = AlphaClipShadowPlateHeight / -AlphaClipShadowSunDirection[1];
            return {x + distance * AlphaClipShadowSunDirection[0],
                    z + distance * AlphaClipShadowSunDirection[2]};
        }

        bool ProjectFloorQuad(const Point2 floor[4], const CameraProxy& camera, Quad& result)
        {
            const auto correction = Math::MatrixUtils::CreateScale(1.0f, -1.0f, -1.0f);
            const auto viewProjection = CameraViewConstants::BuildProjectionMatrix(camera, 1.0f) *
                                        correction * CameraViewConstants::BuildViewMatrix(camera);
            for (uint32_t index = 0u; index < 4u; ++index)
            {
                const auto clip = viewProjection * Math::Vector4(
                    static_cast<float>(floor[index].X), 0.0f, static_cast<float>(floor[index].Y), 1.0f);
                if (!std::isfinite(clip.w) || clip.w <= 0.0f)
                {
                    return false;
                }
                const double x = (clip.x / clip.w * 0.5 + 0.5) * ValidationWidth;
                const double y = (clip.y / clip.w * 0.5 + 0.5) * ValidationHeight;
                if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || x >= ValidationWidth ||
                    y < 0.0 || y >= ValidationHeight)
                {
                    return false;
                }
                result.Points[index] = {x, y};
            }
            return true;
        }

        bool ShadowQuad(const AlphaClipPlateSpec& plate, bool point, double innerFraction,
                        const CameraProxy& camera, Quad& result)
        {
            Point2 corners[4];
            constexpr float signs[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f},
                                            {1.0f, 1.0f}, {-1.0f, 1.0f}};
            const Point2 center = ShadowOnFloor(plate.X, plate.Y, point);
            for (uint32_t index = 0u; index < 4u; ++index)
            {
                const Point2 corner = ShadowOnFloor(plate.X + plate.HalfExtent * signs[index][0],
                                                    plate.Y + plate.HalfExtent * signs[index][1], point);
                corners[index] = {center.X + (corner.X - center.X) * innerFraction,
                                  center.Y + (corner.Y - center.Y) * innerFraction};
            }
            return ProjectFloorQuad(corners, camera, result);
        }

        bool LitQuad(const AlphaClipPlateSpec& opaque, bool point, double innerFraction,
                     const CameraProxy& camera, Quad& result)
        {
            const Point2 center = ShadowOnFloor(opaque.X, opaque.Y, point);
            const double scale = point ? AlphaClipShadowPointPosition[1] /
                (AlphaClipShadowPointPosition[1] - AlphaClipShadowPlateHeight) : 1.0;
            const double half = opaque.HalfExtent * scale * innerFraction;
            // 組ごとに同じ奥行きの中央を取る。点光源からの距離の差を小さくし、板や影を避ける。
            const Point2 floor[4] = {{-half, center.Y - half}, {half, center.Y - half},
                                     {half, center.Y + half}, {-half, center.Y + half}};
            return ProjectFloorQuad(floor, camera, result);
        }

        bool Contains(const Quad& quad, double x, double y)
        {
            bool positive = false;
            bool negative = false;
            for (uint32_t index = 0u; index < 4u; ++index)
            {
                const Point2& a = quad.Points[index];
                const Point2& b = quad.Points[(index + 1u) % 4u];
                const double cross = (b.X - a.X) * (y - a.Y) - (b.Y - a.Y) * (x - a.X);
                positive = positive || cross > 0.0;
                negative = negative || cross < 0.0;
            }
            return !(positive && negative);
        }

        bool CollectLuminance(const RgbaFloatImage& image, const Quad& quad, Container::VariableArray<double>& values)
        {
            for (uint32_t y = 0u; y < image.Height; ++y)
            {
                for (uint32_t x = 0u; x < image.Width; ++x)
                {
                    if (!Contains(quad, x + 0.5, y + 0.5))
                    {
                        continue;
                    }
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double r = image.Values[offset];
                    const double g = image.Values[offset + 1u];
                    const double b = image.Values[offset + 2u];
                    const double value = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                    if (!std::isfinite(value) || r < 0.0 || g < 0.0 || b < 0.0)
                    {
                        return false;
                    }
                    values.push_back(value);
                }
            }
            return values.size() >= 16u;
        }

        double Median(Container::VariableArray<double>& values)
        {
            const size_t middle = values.size() / 2u;
            std::nth_element(values.begin(), values.begin() + middle, values.end());
            const double upper = values[middle];
            if (values.size() % 2u != 0u)
            {
                return upper;
            }
            return (upper + *std::max_element(values.begin(), values.begin() + middle)) * 0.5;
        }

        double Coverage(const Container::VariableArray<double>& values, double threshold)
        {
            size_t count = 0u;
            for (double value : values)
            {
                count += value < threshold ? 1u : 0u;
            }
            return static_cast<double>(count) / static_cast<double>(values.size());
        }
    }

    bool AlphaClipShadowScenario::ParseArgument(const Container::String& argument, Container::String& reason)
    {
        if (m_bEnabled || (argument != TEXT("--alpha-clip-shadow-scenario=sun") &&
                           argument != TEXT("--alpha-clip-shadow-scenario=point")))
        {
            reason = TEXT("切り抜きの影のシナリオが重複しているか値が不正です");
            return false;
        }
        m_bPoint = argument == TEXT("--alpha-clip-shadow-scenario=point");
        m_bEnabled = true;
        return true;
    }

    bool AlphaClipShadowScenario::ParseMethodArgument(const Container::String& argument, Container::String& reason)
    {
        if (argument.find(TEXT("--shadow-method=")) == 0)
        {
            if (m_bSunMethodSpecified || (argument != TEXT("--shadow-method=csm") &&
                                          argument != TEXT("--shadow-method=vsm")))
            {
                reason = TEXT("太陽の影の方式が重複しているか値が不正です");
                return false;
            }
            m_bSunMethodSpecified = true;
            m_bSunVsm = argument == TEXT("--shadow-method=vsm");
            return true;
        }
        if (m_bPointMethodSpecified || (argument != TEXT("--point-shadow-method=cube") &&
                                        argument != TEXT("--point-shadow-method=vsm")))
        {
            reason = TEXT("点光源の影の方式が重複しているか値が不正です");
            return false;
        }
        m_bPointMethodSpecified = true;
        m_bPointVsm = argument == TEXT("--point-shadow-method=vsm");
        return true;
    }

    bool AlphaClipShadowScenario::IsEnabled() const { return m_bEnabled; }

    bool AlphaClipShadowScenario::HasValidMethods() const
    {
        return m_bSunMethodSpecified && (!m_bPoint ? !m_bPointMethodSpecified :
            m_bPointMethodSpecified && m_bPointVsm == m_bSunVsm);
    }

    const char* AlphaClipShadowScenario::MethodName() const
    {
        return m_bSunVsm ? "vsm" : (m_bPoint ? "cube" : "csm");
    }

    bool AlphaClipShadowScenario::LoadThresholds()
    {
        Container::String path(NORVES_SOURCE_ROOT "/Test/Core/Rendering/Thresholds/RenderingValidation/AlphaClipShadow.tsv");
        auto stream = FileStream::FileStream::CreateUnique(path, FileStream::FileMode::Read, FileStream::FileAccess::Read);
        if (!stream || stream->GetSize() < 4 || stream->GetSize() > 4096)
        {
            return false;
        }
        const size_t size = static_cast<size_t>(stream->GetSize());
        Container::VariableArray<char> text(size + 1u, '\0');
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
            if (*cursor == '\0') return nullptr;
            char* line = cursor;
            while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') ++cursor;
            if (*cursor == '\r') *cursor++ = '\0';
            if (*cursor == '\n') *cursor++ = '\0';
            return line;
        };
        const char* header = takeLine();
        if (!header || std::strcmp(header, reinterpret_cast<const char*>(u8"名前\t値\t根拠")) != 0)
        {
            return false;
        }
        struct Entry { const char* Name; double* Value; bool Read; };
        Entry entries[] = {
            {"inner_fraction", &m_InnerFraction, false},
            {"opaque_minimum", &m_OpaqueMinimum, false},
            {"masked_minimum", &m_MaskedMinimum, false},
            {"masked_maximum", &m_MaskedMaximum, false},
            {"minimum_difference", &m_MinimumDifference, false},
            {"minimum_light_ratio", &m_MinimumLightRatio, false}};
        while (char* line = takeLine())
        {
            char* value = std::strchr(line, '\t');
            if (!value) return false;
            *value++ = '\0';
            char* rationale = std::strchr(value, '\t');
            if (!rationale || !rationale[1]) return false;
            *rationale = '\0';
            char* end = nullptr;
            const double number = std::strtod(value, &end);
            if (end == value || *end != '\0' || !std::isfinite(number) || number <= 0.0) return false;
            bool found = false;
            for (auto& entry : entries)
            {
                if (std::strcmp(entry.Name, line) == 0)
                {
                    if (entry.Read) return false;
                    *entry.Value = number;
                    entry.Read = found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        for (const auto& entry : entries) if (!entry.Read) return false;
        return m_InnerFraction <= 1.0 && m_OpaqueMinimum <= 1.0 &&
               m_MaskedMinimum < m_MaskedMaximum && m_MaskedMaximum <= 1.0 &&
               m_MinimumDifference <= 1.0 && m_MinimumLightRatio > 1.0;
    }

    bool AlphaClipShadowScenario::Initialize(RenderWorld& world, const RenderingValidationSceneFixture& fixture)
    {
        (void)world;
        m_Camera = MakeShadowCamera();
        if (!LoadThresholds() ||
            !fixture.ApplyAlphaClipFixture(m_bPoint ? AlphaClipFixtureKind::ShadowPoint : AlphaClipFixtureKind::ShadowSun))
        {
            LOG_ERROR("切り抜きの影のしきい値またはシーンを準備できませんでした");
            return false;
        }
        return true;
    }

    void AlphaClipShadowScenario::ApplyState(RenderWorld& world)
    {
        world.SetMainCamera(m_Camera);
        auto sky = MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = false;
        world.SetSkyAtmosphere(sky);
        auto fog = MakeDefaultVolumetricFogParameters();
        fog.bEnabled = false;
        world.SetVolumetricFogParameters(fog);
        world.SetStaticEnvironmentIntensityScale(0.0f);
    }

    bool AlphaClipShadowScenario::Evaluate(const CapturedFrame& frame, Container::String& reason) const
    {
        RgbaFloatImage image;
        if (!frame.IsSuccess() || frame.Width != ValidationWidth || frame.Height != ValidationHeight ||
            DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
        {
            reason = TEXT("切り抜きの影の HDR 画像を取得できませんでした");
            return false;
        }
        bool passed = true;
        for (uint32_t pair = 0u; pair < 2u; ++pair)
        {
            const auto& masked = AlphaClipShadowPlates[pair * 2u];
            const auto& opaque = AlphaClipShadowPlates[pair * 2u + 1u];
            Quad maskedQuad{}, opaqueQuad{}, litQuad{};
            Container::VariableArray<double> maskedValues, opaqueValues, litValues;
            const bool valid = ShadowQuad(masked, m_bPoint, m_InnerFraction, m_Camera, maskedQuad) &&
                               ShadowQuad(opaque, m_bPoint, m_InnerFraction, m_Camera, opaqueQuad) &&
                               LitQuad(opaque, m_bPoint, m_InnerFraction, m_Camera, litQuad) &&
                               CollectLuminance(image, maskedQuad, maskedValues) &&
                               CollectLuminance(image, opaqueQuad, opaqueValues) &&
                               CollectLuminance(image, litQuad, litValues);
            double maskedCoverage = -1.0, opaqueCoverage = -1.0, lit = -1.0, dark = -1.0;
            bool pairPassed = false;
            if (valid)
            {
                lit = Median(litValues);
                dark = Median(opaqueValues);
                const double midpoint = (lit + dark) * 0.5;
                maskedCoverage = Coverage(maskedValues, midpoint);
                opaqueCoverage = Coverage(opaqueValues, midpoint);
                pairPassed = lit > 0.0 && dark >= 0.0 && lit >= dark * m_MinimumLightRatio &&
                             opaqueCoverage >= m_OpaqueMinimum && maskedCoverage >= m_MaskedMinimum &&
                             maskedCoverage <= m_MaskedMaximum &&
                             opaqueCoverage - maskedCoverage >= m_MinimumDifference;
            }
            LOG_INFO("切り抜きの影の判定 ALPHA_CLIP_SHADOW_RESULT scenario=%s method=%s pair=%s masked=%g opaque=%g lit=%g dark=%g result=%s",
                     m_bPoint ? "point" : "sun", MethodName(), pair == 0u ? "static" : "skinned",
                     maskedCoverage, opaqueCoverage, lit, dark, pairPassed ? "PASS" : "FAIL");
            if (!valid)
            {
                LOG_ERROR("切り抜きの影の検査領域が画像外、画素不足、または輝度が不正です pair=%s",
                          pair == 0u ? "static" : "skinned");
            }
            passed = passed && pairPassed;
        }
        if (!passed)
        {
            reason = TEXT("切り抜きの影の被覆または明暗差がしきい値を満たしません");
        }
        return passed;
    }
}
