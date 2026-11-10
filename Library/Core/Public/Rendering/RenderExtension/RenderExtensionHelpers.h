// GBuffer・Transparent・PostTemporalAA の段に差し込む描画パス向けの補助。
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
     * @brief TAA の前の Transparent 段で、Scene.Reactive へ被覆率を積む描画先。
     *
     * ブレンドは書き手が決める。色と同じ係数（SrcAlpha / InvSrcAlpha）で
     * (1, 0, 0, 被覆率) を出すと被覆率が積もる。
     */
    class ReactiveTargetBinder
    {
    public:
        ReactiveTargetBinder() = default;
        ReactiveTargetBinder(const ReactiveTargetBinder&) = delete;
        ReactiveTargetBinder& operator=(const ReactiveTargetBinder&) = delete;

        /** @brief パスの Initialize でデバイスを渡す。デバイスは Reset まで有効に保つ。 */
        void Initialize(RHI::IDevice* device);

        /**
         * @brief パスの Declare で Scene.Reactive の色添付と、必要なら読み取り専用の Scene.Depth を宣言する。
         * @param bWithDepth true のとき深度を添付する。パイプラインは深度書き込みを無効にする。
         * @return 必要な資源と描画寸法があり、宣言できた場合は true。
         *
         * 最初の書き手は描画領域と同じ寸法の R8_UNORM を作り、ゼロで消す。後の書き手はそれまでの値を読み込む。
         * SceneColor との寸法の一致は Prepare で確かめる。
         * 最初の書き手は描画が無い場合も render pass を開始してクリアを実行する。
         */
        bool Declare(RenderGraphBuilder& builder, bool bWithDepth);

        /**
         * @brief パスの Execute で、実際の寸法を確かめて render pass と framebuffer を用意する。
         * @return 資源がそろい、Scene.Reactive と SceneColor の寸法が一致するとき true。
         *
         * 形式・クリアの有無・深度添付の有無が変わると render pass を作り直す。
         * GetRenderPass の結果が変わった場合、パス側のパイプラインも作り直す。
         */
        bool Prepare(RenderGraphResources& resources);

        /** @brief 直前の Prepare が成功したときの render pass を返す。 */
        RHI::RenderPassPtr GetRenderPass() const;
        /** @brief 直前の Prepare が成功したときの framebuffer を返す。 */
        RHI::FramebufferPtr GetFramebuffer() const;
        /** @brief 直前の Prepare が成功したときの描画先を返す。今の Execute の中で使う。 */
        RHI::TexturePtr GetTexture() const;

        /** @brief パスの Shutdown で描画先とデバイスへの参照を手放す。 */
        void Reset();

    private:
        RHI::IDevice* m_Device = nullptr;
        RGTextureHandle m_SceneColorHandle;
        RGTextureHandle m_ReactiveHandle;
        RGTextureHandle m_DepthHandle;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::TexturePtr m_Texture;
        const RHI::ITexture* m_FramebufferTargets[2] = {};
        RHI::Format m_TargetFormats[2] = {};
        bool m_bWithDepth = false;
        bool m_bClear = false;
        bool m_bRenderPassHasDepth = false;
        bool m_bRenderPassClears = false;
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
