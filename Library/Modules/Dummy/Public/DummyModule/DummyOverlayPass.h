#pragma once

#include "Rendering/IViewPass.h"
#include "RHI/RHITypes.h"

// DummyOverlayPass — 第1段1C-ii 検証用の最小 overlay(IViewPass)。
//
// 目的: overlay seam(RenderingCoordinator::RenderFrame の executor 戻り後・End 前)で、
// ViewRenderContext::OverlayLoadRenderPass / OverlayLoadFramebuffer(1C-i で新設)を
// 使い、経路依存(legacy / composite=graph)の正しい presentation load render pass 越しに
// back buffer へ実描画できることを最小コストで実証する。
//
// 描画方式: 頂点バッファ・ディスクリプタセット不要の自己完結シェーダー
// (triangle.vert / triangle.frag)で単色三角形を 1 つ描く。viewport/scissor は全面で、
// 三角形は triangle.vert により NDC 中央付近(y=-0.5..0.5)に出る。clear-rect は抽象 RHI に無いため不採用。
//
// 寿命: Initialize(録画窓内・遅延)で pipeline を生成し m_bInitialized を立てる。
// Setup は no-op。Execute で seam の load render pass を Begin→Draw(3)→End する
// (Begin/EndRecording は呼ばない=録画窓は RenderingCoordinator が管理)。
// Shutdown で pipeline 参照を解放する。
namespace NorvesLib::Modules::Dummy
{
    class DummyOverlayPass final : public NorvesLib::Core::Rendering::IViewPass
    {
    public:
        const char *GetName() const override;

        bool Initialize(NorvesLib::Core::Rendering::ViewRenderContext &context) override;
        void Shutdown() override;

        void Setup(NorvesLib::Core::Rendering::ViewRenderContext &context) override;
        void Execute(NorvesLib::Core::Rendering::ViewRenderContext &context) override;

    private:
        // triangle.vert/.frag から生成する単色三角形パイプライン(借用なし・所有)。
        // Initialize で seam の OverlayLoadRenderPass に対して生成する。
        NorvesLib::RHI::PipelinePtr m_Pipeline;
    };
} // namespace NorvesLib::Modules::Dummy
