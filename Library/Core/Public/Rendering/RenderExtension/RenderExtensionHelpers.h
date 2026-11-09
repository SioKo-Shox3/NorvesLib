// GBuffer と PostTemporalAA の段に差し込む描画パス向けの補助。
#pragma once

#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "RHI/RHITypes.h"
#include "Text/IdentityPool.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderGraphBuilder;
    class RenderGraphResources;

    /** @brief GBuffer の段で 5 枚の色と深度に重ねて描くための描画先。 */
    class GBufferTargetBinder
    {
    public:
        GBufferTargetBinder() = default;
        GBufferTargetBinder(const GBufferTargetBinder&) = delete;
        GBufferTargetBinder& operator=(const GBufferTargetBinder&) = delete;

        /** @brief パスの Initialize でデバイスを渡す。デバイスは Reset まで有効に保つ。 */
        void Initialize(RHI::IDevice* device);

        /**
         * @brief パスの Declare で呼び、6 枚がそろうときだけ Load/Store の添付を宣言する。
         * @return 宣言できた場合は true。深度は書き込み可能として宣言する。
         */
        bool Declare(RenderGraphBuilder& builder);

        /**
         * @brief パスの Execute で呼び、宣言した資源から描画先を用意する。
         * @return 描画先を用意できた場合は true。
         *
         * 成功後に GetRenderPass と GetFramebuffer を BeginRenderPass に渡せる。
         * 描画先の形式が変わって GetRenderPass の結果が変わった場合、パス側のパイプラインも作り直す。
         */
        bool Prepare(RenderGraphResources& resources);

        /** @brief 直前の Prepare が成功したときの render pass を返す。 */
        RHI::RenderPassPtr GetRenderPass() const;
        /** @brief 直前の Prepare が成功したときの framebuffer を返す。 */
        RHI::FramebufferPtr GetFramebuffer() const;

        /** @brief パスの Shutdown で描画先とデバイスへの参照を手放す。 */
        void Reset();

    private:
        static constexpr uint32_t kColorTargetCount = 5;

        RHI::IDevice* m_Device = nullptr;
        RGTextureHandle m_ColorHandles[kColorTargetCount];
        RGTextureHandle m_DepthHandle;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        const RHI::ITexture* m_FramebufferTargets[kColorTargetCount + 1] = {};
        RHI::Format m_TargetFormats[kColorTargetCount + 1] = {};
        bool m_bTargetsReady = false;
    };

    /**
     * @brief PostTemporalAA の段で現在の HDR 色の資源名を返す。見つからなければ空の Identity を返す。
     *
     * 存在を問い合わせるだけで、アクセスは宣言しない。呼び出し側のパスが戻り値を使い、
     * TryLoadStoreColorAttachment などで必要なアクセスを宣言する。
     */
    Identity ResolveHdrChainHead(const RenderGraphBuilder& builder);
} // namespace NorvesLib::Core::Rendering
