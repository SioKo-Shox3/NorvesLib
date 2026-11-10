// スキンをカメラの前後へ置き、描画数とバックバッファを照合する。
#include "SkinnedCullScenario.h"

#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"

#include <algorithm>
#include <cstddef>
#include <iostream>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        using namespace Core::Rendering;

        bool IsValidBackBuffer(const CapturedFrame& frame)
        {
            return frame.IsSuccess() && frame.Width == ValidationWidth && frame.Height == ValidationHeight &&
                frame.BytesPerPixel == 4u && frame.RowPitchBytes >= frame.Width * 4u &&
                frame.Pixels.size() >= static_cast<size_t>(frame.RowPitchBytes) * frame.Height &&
                (RHI::IsPresentationSrgbFormat(frame.Format) || RHI::IsPresentationUnormFormat(frame.Format));
        }

        struct ImageDifference
        {
            uint32_t Pixels = 0u;
            uint32_t Maximum = 0u;
        };

        ImageDifference Compare(const CapturedFrame& left, const CapturedFrame& right, bool bCenterOnly)
        {
            ImageDifference difference;
            const uint32_t minX = bCenterOnly ? left.Width / 4u : 0u;
            const uint32_t maxX = bCenterOnly ? left.Width * 3u / 4u : left.Width;
            const uint32_t minY = bCenterOnly ? left.Height / 4u : 0u;
            const uint32_t maxY = bCenterOnly ? left.Height * 3u / 4u : left.Height;
            for (uint32_t y = minY; y < maxY; ++y)
            {
                for (uint32_t x = minX; x < maxX; ++x)
                {
                    bool bDifferent = false;
                    for (uint32_t channel = 0; channel < 4u; ++channel)
                    {
                        const size_t leftOffset = static_cast<size_t>(y) * left.RowPitchBytes + x * 4u + channel;
                        const size_t rightOffset = static_cast<size_t>(y) * right.RowPitchBytes + x * 4u + channel;
                        const int delta = static_cast<int>(left.Pixels[leftOffset]) -
                                          static_cast<int>(right.Pixels[rightOffset]);
                        const uint32_t magnitude = static_cast<uint32_t>(delta < 0 ? -delta : delta);
                        difference.Maximum = std::max(difference.Maximum, magnitude);
                        bDifferent |= magnitude != 0u;
                    }
                    difference.Pixels += bDifferent ? 1u : 0u;
                }
            }
            return difference;
        }
    } // namespace

    bool SkinnedCullScenario::ParseArgument(const Core::Container::String& argument, Core::Container::String& reason)
    {
        if (m_bEnabled || argument != TEXT("--skinned-cull-scenario=behind-front"))
        {
            reason = TEXT("スキンの絞り込みシナリオが重複しているか値が不正です");
            return false;
        }
        m_bEnabled = true;
        return true;
    }

    bool SkinnedCullScenario::IsEnabled() const
    {
        return m_bEnabled;
    }

    bool SkinnedCullScenario::Initialize(const RenderingValidationSceneFixture& fixture, RenderWorld& world,
                                         SceneKind scene, FrameCaptureSourceKind source)
    {
        if (scene != SceneKind::Outdoor || source != FrameCaptureSourceKind::BackBuffer ||
            !fixture.ApplyR5RayTracingShadowFixture() || !fixture.AddSkinnedCullGiant())
        {
            std::cerr << "スキンの絞り込み検証のシーンを準備できません\n";
            return false;
        }
        world.SetSkyAtmosphere(SkyAtmosphereParameters{});
        world.SetVolumetricFogParameters(VolumetricFogParameters{});
        return true;
    }

    void SkinnedCullScenario::ApplyState(const RenderingValidationSceneFixture& fixture, RenderWorld& world)
    {
        CameraProxy camera = fixture.GetR5RayTracingShadowCamera();
        camera.AntiAliasing = CameraAntiAliasingMode::FXAA;
        camera.ExposureMode = CameraExposureMode::Manual;
        world.SetMainCamera(camera);
        if (m_Stage >= StageCount || m_bStageApplied)
        {
            return;
        }

        // 各状態の開始後、3 回の取得を経てから公開済みの描画統計を読む。
        m_StageStartSequence = world.GetRenderingCoordinator().GetStatsSnapshot().PublicationSequence;
        m_bApplyFailed = !fixture.SetSkinnedCullGiantFront(m_Stage >= 2u);
        world.SetSkinnedViewCullingEnabled(m_Stage == 0u || m_Stage == 2u);
        m_bStageApplied = true;
    }

    bool SkinnedCullScenario::Evaluate(const CapturedFrame& frame, RenderWorld& world, uint64_t requestId,
                                       Core::Container::String& reason)
    {
        if (m_bApplyFailed || m_Stage >= StageCount || !IsValidBackBuffer(frame) ||
            frame.RequestId != requestId || (m_LastFrameNumber != 0u && frame.FrameNumber <= m_LastFrameNumber))
        {
            reason = TEXT("スキン検証の状態またはバックバッファ取得が不正です");
            return false;
        }
        m_LastFrameNumber = frame.FrameNumber;
        if (m_CaptureInStage + 1u < CaptureCountPerStage)
        {
            return true;
        }

        const auto snapshot = world.GetRenderingCoordinator().GetStatsSnapshot();
        if (!snapshot.bRenderFrameCompleted || snapshot.PublicationSequence < m_StageStartSequence + 3u)
        {
            reason = TEXT("スキン検証の状態変更後の描画統計が足りません");
            return false;
        }
        m_RecordedDraws[m_Stage] = snapshot.SkinnedGBufferRecordedDraws;
        if (m_Stage == 0u)
        {
            m_BehindFrame = frame;
        }
        else if (m_Stage == 2u)
        {
            m_FrontFrame = frame;
        }
        else if (m_Stage == 3u)
        {
            if (m_BehindFrame.Format != frame.Format || m_FrontFrame.Format != frame.Format)
            {
                reason = TEXT("スキン検証のバックバッファ形式が途中で変わりました");
                return false;
            }
            const ImageDifference parity = Compare(m_FrontFrame, frame, false);
            const ImageDifference visible = Compare(m_BehindFrame, m_FrontFrame, true);
            std::cout << "SKINNED_CULL behind_on=" << m_RecordedDraws[0]
                      << " behind_off=" << m_RecordedDraws[1]
                      << " front_on=" << m_RecordedDraws[2]
                      << " front_off=" << m_RecordedDraws[3]
                      << " diff_pixels=" << parity.Pixels << " max_diff=" << parity.Maximum
                      << " front_changed_pixels=" << visible.Pixels << '\n';
            if (m_RecordedDraws[0] != 0u || m_RecordedDraws[1] == 0u ||
                m_RecordedDraws[2] == 0u || m_RecordedDraws[3] == 0u ||
                parity.Pixels != 0u || visible.Pixels < 16u)
            {
                reason = TEXT("スキンの描画数、前方の表示、または全画素の一致が期待と異なります");
                return false;
            }
        }
        return true;
    }

    void SkinnedCullScenario::Advance()
    {
        if (++m_CaptureInStage == CaptureCountPerStage)
        {
            m_CaptureInStage = 0u;
            ++m_Stage;
            m_bStageApplied = false;
        }
    }

    bool SkinnedCullScenario::RequestFollowup(FrameCaptureRequest& request) const
    {
        if (m_Stage >= StageCount)
        {
            return false;
        }
        request.SourceKind = FrameCaptureSourceKind::BackBuffer;
        return true;
    }
} // namespace NorvesLib::Test::RenderingValidation
