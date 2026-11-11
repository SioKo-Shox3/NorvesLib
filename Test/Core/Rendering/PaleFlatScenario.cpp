#include "PaleFlatScenario.h"

#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationScene.h"
#include "Logging/LogMacros.h"
#include "Rendering/PaleFlatShading.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/ShadingModelIds.h"
#include "Rendering/VolumetricFog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        using namespace Core::Rendering;
        enum class CheckKind { Normals, Baseline, Sun, FogReference, Fog, Rotation };

        struct Stage
        {
            const char* Name;
            FrameCaptureSourceKind Source;
            CheckKind Check;
            float Lux;
            float ToLight[3];
            bool bFog;
            float FogDensity;
        };

        // 状態と取得元は各行で完結する。照度・霧・向きの段はこの表へ追加する。
        constexpr Stage Stages[] = {
            {"normals", FrameCaptureSourceKind::GBufferNormal, CheckKind::Normals,
                1000.0f, {0.8f, 0.0f, 0.6f}, false, 0.0f},
            {"baseline", FrameCaptureSourceKind::SceneColor, CheckKind::Baseline,
                1000.0f, {0.8f, 0.0f, 0.6f}, false, 0.0f},
            {"lux4000", FrameCaptureSourceKind::SceneColor, CheckKind::Sun,
                4000.0f, {0.8f, 0.0f, 0.6f}, false, 0.0f},
            {"fog_reference", FrameCaptureSourceKind::SceneColor, CheckKind::FogReference,
                1000.0f, {0.0f, 0.6f, 0.8f}, false, 0.0f},
            {"fog_low", FrameCaptureSourceKind::SceneColor, CheckKind::Fog,
                1000.0f, {0.0f, 0.6f, 0.8f}, true, 0.03f},
            {"fog_high", FrameCaptureSourceKind::SceneColor, CheckKind::Fog,
                1000.0f, {0.0f, 0.6f, 0.8f}, true, 0.08f},
            {"rotate_minus40", FrameCaptureSourceKind::SceneColor, CheckKind::Rotation,
                16000.0f, {-0.64278761f, 0.0f, 0.76604444f}, false, 0.0f},
            {"rotate_0", FrameCaptureSourceKind::SceneColor, CheckKind::Rotation,
                16000.0f, {0.0f, 0.0f, 1.0f}, false, 0.0f},
            {"rotate_plus40", FrameCaptureSourceKind::SceneColor, CheckKind::Rotation,
                16000.0f, {0.64278761f, 0.0f, 0.76604444f}, false, 0.0f}};
        constexpr uint32_t StageCount = sizeof(Stages) / sizeof(Stages[0]);
        constexpr uint64_t SettleFrames = 16;
        // 強い直接光で環境光の寄与を抑え、粗さ 1 の鏡面も検出する。
        constexpr float RotationCvLimit = 0.001f;
        constexpr float Pi = 3.14159265358979323846f;

        size_t Offset(const RgbaFloatImage& image, int32_t x, int32_t y)
        {
            return (static_cast<size_t>(y) * image.Width + x) * 4u;
        }

        bool ReadId(const RgbaFloatImage& image, int32_t x, int32_t y, uint32_t& id)
        {
            if (x < 0 || y < 0 || x >= static_cast<int32_t>(image.Width) ||
                y >= static_cast<int32_t>(image.Height))
            {
                return false;
            }
            const float packed = image.Values[Offset(image, x, y) + 3u];
            if (!std::isfinite(packed) || packed < 0.0f || packed > 255.0f || packed != std::round(packed))
            {
                return false;
            }
            id = static_cast<uint32_t>(packed) & GBufferContract::ShadingModelIdMask;
            return true;
        }

        bool ReadNormal(const RgbaFloatImage& image, int32_t x, int32_t y, float normal[3])
        {
            const size_t offset = Offset(image, x, y);
            float length = 0.0f;
            for (uint32_t c = 0; c < 3u; ++c)
            {
                normal[c] = image.Values[offset + c];
                length += normal[c] * normal[c];
            }
            if (!std::isfinite(length) || length < 0.5f || length > 1.5f)
            {
                return false;
            }
            for (uint32_t c = 0; c < 3u; ++c)
            {
                normal[c] /= std::sqrt(length);
            }
            return true;
        }

        bool Check(const char* name, float value, float limit, bool bPassed)
        {
            std::cout << "PALE_FLAT_CHECK name=" << name << " value=" << value
                << " limit=" << limit << " result=" << (bPassed ? "PASS" : "FAIL") << '\n';
            return bPassed;
        }

        float Luminance(const float rgb[3])
        {
            return rgb[0] * 0.2126f + rgb[1] * 0.7152f + rgb[2] * 0.0722f;
        }

        VolumetricFogParameters StageFog(const Stage& stage)
        {
            auto fog = MakeDefaultVolumetricFogParameters();
            fog.bEnabled = stage.bFog;
            fog.DensityAtBaseHeight = stage.FogDensity;
            fog.HeightFalloffPerUnit = 0.01f;
            fog.BaseHeight = 0.0f;
            return fog;
        }

        bool Average(const RgbaFloatImage& image, int32_t x, int32_t y, int32_t radius, float rgb[3])
        {
            const float count = static_cast<float>((radius * 2 + 1) * (radius * 2 + 1));
            for (int32_t dy = -radius; dy <= radius; ++dy)
            {
                for (int32_t dx = -radius; dx <= radius; ++dx)
                {
                    if (x + dx < 0 || y + dy < 0 || x + dx >= static_cast<int32_t>(image.Width) ||
                        y + dy >= static_cast<int32_t>(image.Height))
                    {
                        return false;
                    }
                    const size_t offset = Offset(image, x + dx, y + dy);
                    for (uint32_t c = 0; c < 3u; ++c)
                    {
                        const float value = image.Values[offset + c];
                        if (!std::isfinite(value) || value < 0.0f)
                        {
                            return false;
                        }
                        rgb[c] += value / count;
                    }
                }
            }
            return true;
        }
    }

    bool PaleFlatScenario::ParseArgument(const Core::Container::String& argument,
                                         Core::Container::String& reason)
    {
        if (m_bEnabled || argument != TEXT("--pale-flat-scenario=checks"))
        {
            reason = TEXT("PaleFlat のシナリオが重複しているか値が不正です");
            return false;
        }
        m_bEnabled = true;
        return true;
    }

    bool PaleFlatScenario::IsEnabled() const
    {
        return m_bEnabled;
    }

    bool PaleFlatScenario::Initialize(RenderWorld& world, const RenderingValidationSceneFixture& fixture)
    {
        if (!fixture.ApplyPaleFlatFixture())
        {
            LOG_ERROR("PaleFlat の球・スキン・灰色の球を準備できませんでした");
            return false;
        }
        m_Fixture = &fixture;
        world.SetPaleFlatParameters(MakeDefaultPaleFlatParameters());
        return true;
    }

    void PaleFlatScenario::ApplyState(RenderWorld& world)
    {
        if (!m_Fixture || m_Stage >= StageCount)
        {
            return;
        }
        const auto& stage = Stages[m_Stage];
        if (m_AppliedStage != m_Stage)
        {
            m_AppliedStage = m_Stage;
            m_StageStartFrame = world.GetRenderedFrameCount();
            LOG_INFO("PaleFlat の段を適用 stage=%s wait_frames=%llu", stage.Name,
                static_cast<unsigned long long>(SettleFrames));
            std::cout << "PALE_FLAT_CHECK name=stage_state value=" << stage.Name
                << " lux=" << stage.Lux << " to_light=" << stage.ToLight[0] << ','
                << stage.ToLight[1] << ',' << stage.ToLight[2] << " fog_enabled=" << stage.bFog
                << " fog_density=" << stage.FogDensity << " fog_falloff=0.01 fog_base_height=0"
                << " pre_exposure=" << m_Fixture->GetPaleFlatCamera().PreExposure
                << " limit=fixed result=PASS\n";
        }
        m_bStateValid = m_Fixture->SetPaleFlatLightState(stage.Lux,
            stage.ToLight[0], stage.ToLight[1], stage.ToLight[2]);
        world.SetMainCamera(m_Fixture->GetPaleFlatCamera());
        world.SetPaleFlatParameters(MakeDefaultPaleFlatParameters());
        auto sky = MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = false;
        world.SetSkyAtmosphere(sky);
        world.SetVolumetricFogParameters(StageFog(stage));
        // 暗部を正値で保ちつつ、1000 lux の方向光より環境光を十分小さくする。
        world.SetStaticEnvironmentIntensityScale(0.001f);
    }

    bool PaleFlatScenario::Evaluate(const CapturedFrame& frame, Core::Container::String& reason)
    {
        m_bAdvance = false;
        if (!m_bStateValid || m_Stage >= StageCount)
        {
            reason = TEXT("PaleFlat の段の状態を適用できませんでした");
            return false;
        }
        // 入口の SceneColor は捨て、以後は状態表の取得元を使う。
        if (m_bInitialCapture)
        {
            m_bInitialCapture = false;
            return true;
        }
        if (frame.FrameNumber < m_StageStartFrame + SettleFrames)
        {
            return true;
        }
        RgbaFloatImage image;
        if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success ||
            image.Width != ValidationWidth || image.Height != ValidationHeight || !IsFiniteImage(image))
        {
            reason = TEXT("PaleFlat の取得画像の形式・寸法・画素値が不正です");
            return false;
        }
        bool bPassed = false;
        switch (Stages[m_Stage].Check)
        {
        case CheckKind::Normals:
            bPassed = EvaluateNormals(image, reason);
            break;
        case CheckKind::Baseline:
            bPassed = EvaluateColor(image, reason);
            m_bSunBaselineValid = MeasureSun(image, m_SunBaseline);
            bPassed = m_bSunBaselineValid && bPassed;
            break;
        case CheckKind::Sun:
            bPassed = EvaluateSun(image);
            break;
        case CheckKind::FogReference:
            m_bFogReferenceValid = MeasureFogDifference(image, m_FogReferenceDifference);
            bPassed = Check("fog_reference_difference", m_FogReferenceDifference, 0.0f,
                m_bFogReferenceValid && m_FogReferenceDifference > 0.0f);
            break;
        case CheckKind::Fog:
            bPassed = EvaluateFog(image);
            break;
        case CheckKind::Rotation:
            bPassed = EvaluateRotation(image);
            break;
        }
        m_bChecksPassed = m_bChecksPassed && bPassed;
        m_bAdvance = true;
        // ID の不一致も色の判定と併せて出力してから、最後の段で不合格にする。
        if (m_Stage + 1u == StageCount && !m_bChecksPassed)
        {
            if (reason.empty())
            {
                reason = TEXT("PaleFlat の GPU の判定に許容範囲外の値があります");
            }
            return false;
        }
        return true;
    }

    bool PaleFlatScenario::IsSpherePixel(int32_t x, int32_t y, uint32_t object) const
    {
        uint32_t id = 0;
        if (!ReadId(m_Normals, x, y, id) || id != (object == 0u
            ? GBufferContract::ShadingModelPaleFlat : GBufferContract::ShadingModelDefaultLit))
        {
            return false;
        }
        float normal[3];
        if (!ReadNormal(m_Normals, x, y, normal))
        {
            return false;
        }
        // 固定カメラの視線と球の交点で、同じ ID の空や別の物を除く。
        const auto camera = m_Fixture->GetPaleFlatCamera();
        const float focal = m_Normals.Height * 0.5f / std::tan(camera.FieldOfView * Pi / 360.0f);
        const float dx = (x + 0.5f - m_Normals.Width * 0.5f) / focal;
        const float dy = -(y + 0.5f - m_Normals.Height * 0.5f) / focal;
        const float ox = -PaleFlatSpherePositions[object][0];
        const float oy = -PaleFlatSpherePositions[object][1];
        const float oz = camera.PositionZ;
        const float b = dx * ox + dy * oy - oz;
        const float c = ox * ox + oy * oy + oz * oz - PaleFlatSphereRadius * PaleFlatSphereRadius;
        return b < 0.0f && b * b >= (dx * dx + dy * dy + 1.0f) * c;
    }

    bool PaleFlatScenario::IsSphereInterior(int32_t x, int32_t y, uint32_t object) const
    {
        // 半径 3 画素の円でマスクを収縮し、輪郭の混色を比較から外す。
        for (int32_t dy = -3; dy <= 3; ++dy)
        {
            for (int32_t dx = -3; dx <= 3; ++dx)
            {
                if (dx * dx + dy * dy <= 9 && !IsSpherePixel(x + dx, y + dy, object))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool PaleFlatScenario::EvaluateNormals(const RgbaFloatImage& image, Core::Container::String& reason)
    {
        m_Normals = image;
        const auto camera = m_Fixture->GetPaleFlatCamera();
        const float focal = image.Height * 0.5f / std::tan(camera.FieldOfView * Pi / 360.0f);
        const auto projectX = [&](float x) { return static_cast<int32_t>(std::lround(image.Width * 0.5f + focal * x / camera.PositionZ)); };
        const auto projectY = [&](float y) { return static_cast<int32_t>(std::lround(image.Height * 0.5f - focal * y / camera.PositionZ)); };
        m_SkinX = projectX(PaleFlatSkinPosition[0]);
        m_SkinY = projectY(PaleFlatSkinPosition[1]);
        const int32_t sphereX = projectX(PaleFlatSpherePositions[0][0]);
        const int32_t sphereY = projectY(PaleFlatSpherePositions[0][1]);
        float skinNormal[3] = {};
        bool bPassed = true;
        for (uint32_t object = 0u; object < 2u; ++object)
        {
            bool bIdPassed = true;
            uint32_t lastId = ~0u;
            for (int32_t dy = -2; dy <= 2; ++dy)
            {
                for (int32_t dx = -2; dx <= 2; ++dx)
                {
                    const int32_t x = (object == 0u ? m_SkinX : sphereX) + dx;
                    const int32_t y = (object == 0u ? m_SkinY : sphereY) + dy;
                    uint32_t id = ~0u;
                    const bool bIdValid = ReadId(image, x, y, id);
                    bIdPassed = bIdPassed && bIdValid && id == GBufferContract::ShadingModelPaleFlat;
                    if (!bIdValid || id != GBufferContract::ShadingModelPaleFlat)
                    {
                        lastId = id;
                    }
                    float normal[3];
                    if (object == 0u && bIdValid && ReadNormal(image, x, y, normal))
                    {
                        for (uint32_t c = 0; c < 3u; ++c)
                        {
                            skinNormal[c] += normal[c] / 25.0f;
                        }
                    }
                    else if (object == 0u)
                    {
                        bIdPassed = false;
                    }
                }
            }
            bPassed = Check(object == 0u ? "skin_id" : "static_id",
                bIdPassed ? 3.0f : static_cast<float>(lastId), 3.0f, bIdPassed) && bPassed;
        }
        const float length = std::sqrt(skinNormal[0] * skinNormal[0] + skinNormal[1] * skinNormal[1] + skinNormal[2] * skinNormal[2]);
        float bestDot = -1.0f;
        m_StaticX = m_StaticY = -1;
        for (int32_t y = 3; y < static_cast<int32_t>(image.Height) - 3; ++y)
        {
            for (int32_t x = 3; x < static_cast<int32_t>(image.Width) - 3; ++x)
            {
                if (!IsSphereInterior(x, y, 0u) || length < 0.5f)
                {
                    continue;
                }
                float normal[3];
                if (!ReadNormal(image, x, y, normal))
                {
                    continue;
                }
                const float dot = (normal[0] * skinNormal[0] + normal[1] * skinNormal[1] + normal[2] * skinNormal[2]) / length;
                if (dot > bestDot)
                {
                    bestDot = dot;
                    m_StaticX = x;
                    m_StaticY = y;
                }
            }
        }
        const float angle = std::acos(std::clamp(bestDot, -1.0f, 1.0f)) * 180.0f / Pi;
        LOG_INFO("PaleFlat の法線対応 angle_degrees=%g skin_roi=(%d,%d) static_roi=(%d,%d)",
            angle, m_SkinX, m_SkinY, m_StaticX, m_StaticY);
        if (angle > 2.0f)
        {
            m_StaticX = m_StaticY = -1;
            reason = TEXT("スキンの法線に角度差 2 度以内で一致する球の画素がありません");
            bPassed = false;
        }
        return bPassed;
    }

    bool PaleFlatScenario::EvaluateColor(const RgbaFloatImage& image, Core::Container::String& reason) const
    {
        bool bPassed = true;
        const float contrastLimit = 1.1f / MakeDefaultPaleFlatParameters().WrapMin;
        for (uint32_t object = 0; object < 2u; ++object)
        {
            float minimum = std::numeric_limits<float>::infinity();
            float maximum = 0.0f;
            uint32_t count = 0u;
            bool bValuesValid = true;
            bool bLit = false;
            bool bDark = false;
            for (int32_t y = 3; y < static_cast<int32_t>(image.Height) - 3; ++y)
            {
                for (int32_t x = 3; x < static_cast<int32_t>(image.Width) - 3; ++x)
                {
                    if (!IsSphereInterior(x, y, object))
                    {
                        continue;
                    }
                    const size_t offset = Offset(image, x, y);
                    const float value = image.Values[offset] * 0.2126f + image.Values[offset + 1] * 0.7152f + image.Values[offset + 2] * 0.0722f;
                    bValuesValid = bValuesValid && std::isfinite(value) && value >= 0.0f;
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                    float normal[3];
                    if (ReadNormal(m_Normals, x, y, normal))
                    {
                        const auto& light = Stages[m_Stage].ToLight;
                        const float dot = normal[0] * light[0] + normal[1] * light[1] + normal[2] * light[2];
                        bLit = bLit || dot > 0.5f;
                        bDark = bDark || dot < 0.0f;
                    }
                    ++count;
                }
            }
            const float ratio = minimum > 0.0f ? maximum / minimum : std::numeric_limits<float>::infinity();
            const bool bValid = bValuesValid && count >= 25u && bLit && bDark && maximum > 0.0f;
            bPassed = Check(object == 0u ? "contrast_ratio" : "default_lit_contrast", ratio,
                object == 0u ? contrastLimit : 3.0f,
                bValid && (object == 0u ? std::isfinite(ratio) && ratio <= contrastLimit : ratio >= 3.0f)) && bPassed;
            LOG_INFO("PaleFlat の球の検査 object=%u pixels=%u min=%g max=%g lit=%d dark=%d",
                object, count, minimum, maximum, bLit, bDark);
        }
        float skin[3] = {};
        float sphere[3] = {};
        const bool bAverageValid = m_StaticX >= 0 && Average(image, m_SkinX, m_SkinY, 2, skin) &&
            Average(image, m_StaticX, m_StaticY, 1, sphere);
        float difference[3] = {};
        float maximumDifference = 0.0f;
        for (uint32_t c = 0; c < 3u; ++c)
        {
            difference[c] = bAverageValid && sphere[c] > 0.0f
                ? std::abs(skin[c] - sphere[c]) / sphere[c] : std::numeric_limits<float>::infinity();
            maximumDifference = std::max(maximumDifference, difference[c]);
        }
        bPassed = Check("static_vs_skin", maximumDifference, 0.03f,
            std::isfinite(maximumDifference) && maximumDifference <= 0.03f) && bPassed;
        LOG_INFO("PaleFlat の静的とスキンの相対差 rgb=(%g,%g,%g)", difference[0], difference[1], difference[2]);
        if (!bPassed)
        {
            reason = TEXT("PaleFlat の明暗比・DefaultLit の対照・静的とスキンの RGB 差のいずれかが許容範囲外です");
        }
        return bPassed;
    }

    bool PaleFlatScenario::FindBrightRoi(uint32_t object, const float light[3], int32_t& outX, int32_t& outY) const
    {
        float bestDot = -2.0f;
        outX = outY = -1;
        for (int32_t y = 3; y < static_cast<int32_t>(m_Normals.Height) - 3; ++y)
        {
            for (int32_t x = 3; x < static_cast<int32_t>(m_Normals.Width) - 3; ++x)
            {
                float normal[3];
                if (!IsSphereInterior(x, y, object) || !ReadNormal(m_Normals, x, y, normal))
                {
                    continue;
                }
                const float dot = normal[0] * light[0] + normal[1] * light[1] + normal[2] * light[2];
                if (dot > bestDot)
                {
                    bestDot = dot;
                    outX = x;
                    outY = y;
                }
            }
        }
        return outX >= 0;
    }

    bool PaleFlatScenario::MeasureSun(const RgbaFloatImage& image, float outLuma[2]) const
    {
        for (uint32_t object = 0u; object < 2u; ++object)
        {
            int32_t x, y;
            float rgb[3] = {};
            if (!FindBrightRoi(object, Stages[1].ToLight, x, y) || !Average(image, x, y, 2, rgb))
            {
                return false;
            }
            outLuma[object] = Luminance(rgb);
            if (!(outLuma[object] > 0.0f))
            {
                return false;
            }
        }
        return true;
    }

    bool PaleFlatScenario::EvaluateSun(const RgbaFloatImage& image) const
    {
        float luma[2] = {};
        const bool bValid = m_bSunBaselineValid && MeasureSun(image, luma);
        const float paleRatio = bValid ? luma[0] / m_SunBaseline[0] : 0.0f;
        const float cardRatio = bValid ? paleRatio / (luma[1] / m_SunBaseline[1]) : 0.0f;
        const bool bPalePassed = Check("sun_4x_pale_ratio", paleRatio, 0.03f,
            bValid && std::abs(paleRatio / 4.0f - 1.0f) <= 0.03f);
        return Check("sun_4x_gray_card_ratio", cardRatio, 0.03f,
            bValid && std::abs(cardRatio - 1.0f) <= 0.03f) && bPalePassed;
    }

    bool PaleFlatScenario::MeasureFogDifference(const RgbaFloatImage& image, float& outDifference) const
    {
        int32_t x, y;
        if (!FindBrightRoi(0u, Stages[m_Stage].ToLight, x, y))
        {
            return false;
        }
        // 対称な視線を対にし、高さ・距離・光源との角度を揃えて霧の光を差し引く。
        const int32_t mirroredX = static_cast<int32_t>(image.Width) - 1 - x;
        float pale[3] = {}, gray[3] = {};
        if (!IsSphereInterior(mirroredX, y, 1u) ||
            !Average(image, x, y, 2, pale) || !Average(image, mirroredX, y, 2, gray))
        {
            return false;
        }
        outDifference = Luminance(pale) - Luminance(gray);
        return std::isfinite(outDifference) && outDifference > 0.0f;
    }

    bool PaleFlatScenario::EvaluateFog(const RgbaFloatImage& image) const
    {
        float difference = 0.0f;
        int32_t x, y;
        float normal[3] = {};
        const bool bValid = m_bFogReferenceValid && m_FogReferenceDifference > 0.0f &&
            MeasureFogDifference(image, difference) &&
            FindBrightRoi(0u, Stages[m_Stage].ToLight, x, y) && ReadNormal(m_Normals, x, y, normal);
        const auto camera = m_Fixture->GetPaleFlatCamera();
        const float dx = PaleFlatSpherePositions[0][0] + normal[0] * PaleFlatSphereRadius - camera.PositionX;
        const float dy = PaleFlatSpherePositions[0][1] + normal[1] * PaleFlatSphereRadius - camera.PositionY;
        const float dz = normal[2] * PaleFlatSphereRadius - camera.PositionZ;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const auto fog = StageFog(Stages[m_Stage]);
        const float expected = ComputeHeightFogTransmittance(fog, camera.PositionY, dy / distance, distance);
        const float measured = bValid ? difference / m_FogReferenceDifference : -1.0f;
        std::cout << "PALE_FLAT_CHECK name=fog_sample value=" << measured << " expected=" << expected
            << " distance=" << distance << " ray_y=" << dy / distance << " limit=0.01 result="
            << (bValid ? "PASS" : "FAIL") << '\n';
        return Check(Stages[m_Stage].FogDensity < 0.05f ? "fog_low_transmittance" : "fog_high_transmittance",
            std::abs(measured - expected), 0.01f,
            bValid && expected >= 0.4f && expected <= 0.9f && std::abs(measured - expected) <= 0.01f);
    }

    bool PaleFlatScenario::EvaluateRotation(const RgbaFloatImage& image) const
    {
        double sum = 0.0, sumSquared = 0.0;
        uint32_t count = 0u;
        const auto& light = Stages[m_Stage].ToLight;
        const auto parameters = MakeDefaultPaleFlatParameters();
        for (int32_t y = 3; y < static_cast<int32_t>(image.Height) - 3; ++y)
        {
            for (int32_t x = 3; x < static_cast<int32_t>(image.Width) - 3; ++x)
            {
                float normal[3];
                if (!IsSphereInterior(x, y, 0u) || !ReadNormal(m_Normals, x, y, normal))
                {
                    continue;
                }
                const float dot = normal[0] * light[0] + normal[1] * light[1] + normal[2] * light[2];
                if (dot <= 0.2f)
                {
                    continue;
                }
                // 凸な球の正面は自分の影の外。球間の影もこの方位の範囲では重ならない。
                const float ratio = Luminance(&image.Values[Offset(image, x, y)]) /
                    EvaluatePaleFlatDirectFactor(dot, 1.0f, parameters);
                sum += ratio;
                sumSquared += static_cast<double>(ratio) * ratio;
                ++count;
            }
        }
        const double mean = count > 0u ? sum / count : 0.0;
        const float cv = mean > 0.0 ? static_cast<float>(std::sqrt(std::max(0.0, sumSquared / count - mean * mean)) / mean)
            : std::numeric_limits<float>::infinity();
        LOG_INFO("PaleFlat の回転の検査 stage=%s pixels=%u mean=%g", Stages[m_Stage].Name, count, mean);
        const char* name = light[0] < -0.1f ? "rotate_minus40_specular_cv"
            : (light[0] > 0.1f ? "rotate_plus40_specular_cv" : "rotate_0_specular_cv");
        return Check(name, cv, RotationCvLimit, count >= 200u && std::isfinite(cv) && cv <= RotationCvLimit);
    }

    void PaleFlatScenario::Advance()
    {
        if (m_bAdvance)
        {
            ++m_Stage;
            m_bAdvance = false;
        }
    }

    bool PaleFlatScenario::RequestFollowup(FrameCaptureRequest& request) const
    {
        if (m_Stage >= StageCount)
        {
            return false;
        }
        request.SourceKind = Stages[m_Stage].Source;
        return true;
    }
} // namespace NorvesLib::Test::RenderingValidation
