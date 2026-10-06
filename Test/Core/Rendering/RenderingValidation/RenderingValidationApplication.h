#pragma once

#include "RenderingValidation/RenderingValidationScene.h"
#include "RenderingValidation/VisibilityBufferPathProbe.h"
#include "Application/ApplicationHandlerBase.h"
#include "Rendering/FrameCaptureTypes.h"

namespace NorvesLib::Test::RenderingValidation
{
    class RenderingValidationApplicationHandler : public Core::Application::ApplicationHandlerBase
    {
    public:
        bool OnPreInitialize(const Core::Container::VariableArray<Core::Container::String>& args) override;
        bool OnInitialize() override;
        bool ShouldAdvanceSimulation() const override;
        void OnPreRender() override;
        void OnPostRender() override;
        void OnPreShutdown() override;

    protected:
        virtual bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason);
        virtual bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason) = 0;
        virtual bool RequestFollowupCapture(
            const Core::Rendering::CapturedFrame& frame,
            Core::Rendering::FrameCaptureRequest& outRequest);
        virtual void ApplyCaptureStageState(Core::Rendering::RenderWorld& renderWorld);
        virtual void AdvanceCaptureStage();
        const RenderingValidationRunConfig& GetRunConfig() const;
        uint64_t GetLastAcceptedRequestId() const;
        uint64_t GetLastAcceptedRequestStageToken() const;
        const RenderingValidationSceneFixture& GetFixture() const;
        /**
         * @brief 走った描画の経路が、--visibility-buffer の指定（既定は on、off は予備）どおりだったかをログの記録で確かめる
         *
         * on は解決を記録した（VISBUFFER_RESOLVE_TILES）ことと、予備へ戻っていない（VISBUFFER_FALLBACK が無い）ことを求める。
         * 装置が対応しないと黙って予備へ落ちて検査が通るのを防ぐ。off は解決を通っていないことを求める。
         * ログが無効なビルドでは観測できないので true を返す。失敗したときは理由を返す。
         */
        bool VerifyVisibilityBufferPath(Core::Container::String& outFailureReason) const;
        /** @brief 取得の評価が通った後、終了の前に VerifyVisibilityBufferPath を行う（派生の OnPreInitialize から呼ぶ） */
        void RequireVisibilityBufferPath();

    private:
        void Fail(const char* summary);

        RenderingValidationRunConfig m_RunConfig;
        RenderingValidationSceneFixture m_Fixture;
        bool m_bCaptureRequested = false;
        bool m_bExitRequested = false;
        bool m_bRequireVisibilityBufferPath = false;
        VisibilityBufferPathProbe m_PathProbe;
        uint64_t m_CaptureRequestRenderedFrame = 0;
        uint64_t m_LastAcceptedRequestId = 0;
        uint64_t m_LastAcceptedRequestStageToken = 0;
        uint64_t m_NextStageToken = 0;
    };
}
