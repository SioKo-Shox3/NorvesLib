// 同じエポックの撮影結果が、画素ハッシュと PNG の全バイトで一致することを実 GPU で確かめる。
#include "Bridge/BridgeDeterministicCapture.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Engine/Engine.h"
#include "Object/World.h"
#include "Rendering/RenderWorld.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include <cmath>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;
    using namespace Game::Bridge;

    constexpr const char* TestName = "BridgeDeterministicCaptureVulkanTest";
    bool g_bTestPassed = false;

    class BridgeDeterministicCaptureHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize())
            {
                return false;
            }
            m_Source = MakeEngineDeterministicFrameSource();
            m_Request.SettleFrames = 60u;
            m_Request.MaxWidth = 128u;
            m_Request.MaxHeight = 128u;

            // 起動時は無効なので、GPU の撮影要求を出す前に拒否の契約も確かめる。
            const String disabledCode = m_Session.Begin(m_Request, m_Source);
            if (disabledCode != "deterministic_capture_disabled" ||
                m_Session.GetResult().Status != BridgeDeterministicCaptureStatus::Failed)
            {
                std::cerr << "決定的な撮影が無効なときの拒否が一致しません\n";
                return false;
            }

            // --capture-deterministic は --capture-png が無いと無視される。
            // 自動 PNG 撮影との競合を避け、試験側で有効にする。ApplicationProcessor は
            // この状態を毎フレーム読み、固定刻みと AdvanceFrame を通常の起動経路で実行する。
            auto& engine = *Core::Engine::GEngine;
            engine.GetDeterministicCapture().Enable();
            engine.GetRenderWorld().SetDeterministicCapture(true);
            engine.GetWorld().SetRenderInterpolationAllowed(false);
            return true;
        }

        bool ShouldAdvanceSimulation() const override
        {
            return true;
        }

        void OnPreRender() override
        {
            // 基底の自動撮影は使わず、カメラだけを共通のシーンから適用する。
            GetFixture().ApplyCamera(Core::Engine::GEngine->GetRenderWorld());
        }

        void OnPostRender() override
        {
            if (m_bFinished)
            {
                return;
            }
            auto& engine = *Core::Engine::GEngine;
            if (++m_TotalFrames > 600u)
            {
                Finish(false, "撮影の全体期限を超えました");
                return;
            }
            if (std::abs(engine.GetDeltaTime() - Core::Engine::DeterministicCapture::FixedDeltaSeconds) > 1.0e-7f)
            {
                Finish(false, "起動経路が固定刻みで進んでいません");
                return;
            }
            if (!m_bStarted)
            {
                if (!GetFixture().IsCaptureStateStable() || !m_Source.IsSceneReady(m_Source.Context))
                {
                    return;
                }
                m_bStarted = true;
                BeginCapture();
                return;
            }

            // EndFrame 後、次の AdvanceFrame 前に毎フレーム一度だけ進める。
            // 供給元は RenderWorld の要求と読み戻しを使い、基底の受取処理と競合させない。
            const auto status = m_Session.Tick();
            if (status == BridgeDeterministicCaptureStatus::Failed)
            {
                std::cerr << "撮影に失敗しました: " << m_Session.GetResult().ErrorCode.c_str() << '\n';
                Finish(false, "Bridge セッションが失敗しました");
                return;
            }
            if (status != BridgeDeterministicCaptureStatus::Completed)
            {
                return;
            }
            const auto& result = m_Session.GetResult();
            if (result.Width == 0u || result.Height == 0u || result.PngBytes.empty() ||
                result.Width > m_Request.MaxWidth || result.Height > m_Request.MaxHeight ||
                result.EpochFrameNumber != m_Request.SettleFrames + 1u)
            {
                Finish(false, "撮影結果の寸法・PNG・エポック内の番号が不正です");
                return;
            }
            std::cout << "BRIDGE_DETERMINISTIC_CAPTURE sample=" << (m_bFirstCaptured ? 2 : 1)
                      << " pixel_hash=" << result.PixelHash << " png_hash=" << result.PngHash
                      << " png_bytes=" << result.PngBytes.size()
                      << " width=" << result.Width << " height=" << result.Height
                      << " epoch_frame=" << result.EpochFrameNumber << '\n';
            if (!m_bFirstCaptured)
            {
                m_FirstResult = result;
                m_bFirstCaptured = true;
                // 同じ要求で新しいエポックを始める。時間や履歴の数え直しは部品側に任せる。
                BeginCapture();
                return;
            }
            const bool bPassed = m_FirstResult.Status == BridgeDeterministicCaptureStatus::Completed &&
                result.Status == BridgeDeterministicCaptureStatus::Completed &&
                m_FirstResult.Width == result.Width && m_FirstResult.Height == result.Height &&
                m_FirstResult.PixelHash == result.PixelHash && m_FirstResult.PngHash == result.PngHash &&
                m_FirstResult.PngBytes == result.PngBytes;
            Finish(bPassed, bPassed ? "同じ要求の撮影結果が一致しました" : "同じ要求の撮影結果が一致しません");
        }

    protected:
        bool ParseAdditionalArgument(const String& argument, String& outFailureReason) override
        {
            if (argument == TEXT("--window-background"))
            {
                return true;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument, outFailureReason);
        }

        bool EvaluateCapturedFrame(const CapturedFrame&, String& outFailureReason) override
        {
            outFailureReason = TEXT("撮影結果は Bridge セッションで受け取ります");
            return false;
        }

    private:
        void BeginCapture()
        {
            const String code = m_Session.Begin(m_Request, m_Source);
            if (!code.empty())
            {
                std::cerr << "撮影の開始が拒否されました: " << code.c_str() << '\n';
                Finish(false, "Bridge セッションを開始できません");
            }
        }

        void Finish(bool bPassed, const char* reason)
        {
            m_bFinished = true;
            g_bTestPassed = bPassed;
            std::cout << TestName << " result=" << (bPassed ? "PASS" : "FAIL") << " " << reason << '\n';
            Core::Engine::GEngine->RequestExit(bPassed ? 0 : 1);
        }

        BridgeDeterministicFrameSource m_Source;
        BridgeDeterministicCaptureSession m_Session;
        BridgeDeterministicCaptureRequest m_Request;
        BridgeDeterministicCaptureResult m_FirstResult;
        uint32_t m_TotalFrames = 0;
        bool m_bStarted = false;
        bool m_bFirstCaptured = false;
        bool m_bFinished = false;
    };

    TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return MakeShared<BridgeDeterministicCaptureHandler>();
    }
} // namespace

int main()
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip(TestName, "環境変数により GPU 検証をスキップします");
    }
    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip(TestName, "Vulkan デバイスを利用できません");
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("Bridge 決定的撮影の検証");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bWindowBackground = true;
    config.bEnableDebugConsole = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("BridgeDeterministicCaptureVulkanTest.log");
    config.CreateHandler = &CreateHandler;
    config.Arguments.push_back(TEXT("--scene=indoor"));
    config.Arguments.push_back(TEXT("--window-background"));
    const int exitCode = Core::Boot::LaunchApplication(config);
    // 窓を閉じるなどの中断で、比較前の正常終了を合格として扱わない。
    return exitCode != 0 ? exitCode : (g_bTestPassed ? 0 : 1);
}
