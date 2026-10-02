#pragma once

#include "IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    class SSRPass;

    /**
     * @brief ブルームパス設定
     */
    struct BloomSettings
    {
        /**
         * @brief 輝度閾値（プリエクスポージャ後の値）
         *
         * 0以下（既定）はしきい値なしで、元の色とブルームを Intensity の割合で線形補間し、
         * 画面全体のエネルギーを保つ。正の値では、この値以上の明るい部分だけを取り出し、
         * Intensity 倍して元の色へ加える。
         */
        float Threshold = 0.0f;

        /** @brief ブルーム強度（しきい値なしでは補間の割合、ありでは加算の乗数） */
        float Intensity = 0.04f;

        /** @brief 拡大のテントフィルタの半径（1つ下の段のテクセル単位） */
        float Radius = 1.0f;

        /** @brief ソフト閾値の膝（0-1、閾値付近のフォールオフ制御。しきい値ありのときだけ使う） */
        float SoftKnee = 0.5f;

        /** @brief 縮小の段数（1段ごとに縦横半分。1〜MaxBloomMipCount） */
        uint32_t MipCount = 6;

        /**
         * @brief レンズダートの強さ（0で無効）
         *
         * カメラのレンズ効果（CameraLensEffects::LensDirtIntensity）が正ならそちらを使う。
         * ダートは、拡大したブルームのうち LensDirtThreshold を超えた分にダートの模様を掛けて加える。
         */
        float LensDirtIntensity = 0.0f;

        /**
         * @brief レンズダートが乗り始めるブルームの明るさ（プリエクスポージャ後の値）
         *
         * 画面全体のブルーム（しきい値なしでは画面の平均的な明るさ）にはダートを乗せず、太陽や発光体の
         * 周りの明るいにじみにだけ模様を浮かせる。
         */
        float LensDirtThreshold = 1.0f;

        /**
         * @brief ダートの加算の輝度を、その画素の明るさ（ブルーム合成後）の何倍までに抑えるか（0で抑えない）
         *
         * ブルームは明るい光源の周りで画面の明るさより桁違いに大きくなるため、暗い背景（夜の点光源の周り）では
         * しみがそのまま色付きの円として浮く。加算をこの倍率×画素の明るさへ向けてなめらかに頭打ちにし、
         * 明るい空やにじみの中の模様は残して、暗い背景の上では淡い斑にとどめる。0.25 は起動画面の夜の点光源の
         * 周りでしみの円がほぼ見えず、夕の太陽のにじみの中に模様が淡く残る値。
         */
        float LensDirtSceneRatio = 0.25f;

        /** @brief 出力フォーマット（HDR、ToneMappingの前にかかるため） */
        RHI::Format OutputFormat = RHI::Format::R16G16B16A16_FLOAT;
    };

    /**
     * @brief ブルームポストプロセスパス
     *
     * HDRシーンカラーを13タップのフィルタで段階的に縮小し（最初の段はKaris平均）、
     * 3×3のテントフィルタで下の段から拡大して各段へ加えたものを、元のシーンカラーへ混ぜます。
     * 縮小の各段は自前のテクスチャで、パス内で閉じています。
     * レンズダートが有効なら、ブルームの明るい部分へ、起動時に手続きで作ったダートの模様を掛けて加えます。
     *
     * PostProcessStackに追加して使用するポストプロセスパスです。
     * ToneMappingPassの前に配置してください。
     *
     * 標準経路では RenderGraph named resource から入力を読み取り、
     * "BloomSceneColor" graph output として後段へ渡します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     *
     * 入力:
     * - "SceneColor" : HDRライティング結果 (R16G16B16A16_FLOAT)
     *
     * 出力:
     * - "BloomSceneColor" : ブルーム適用済みHDRカラー (R16G16B16A16_FLOAT)
     *   ※ legacy/fallback bridge では "SceneColor" を上書き登録する
     */
    class BloomPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings ブルーム設定
         */
        explicit BloomPass(const BloomSettings &settings = BloomSettings{});

        /**
         * @brief デストラクタ
         */
        ~BloomPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "BloomPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;

        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

        // ========================================
        // パラメータ調整
        // ========================================

        /**
         * @brief Legacy bridge fallback 用の入力パス参照を設定
         *
         * RenderGraph named resource が主経路です。未移行 bridge / fallback でのみ使用します。
         */
        void SetInputPass(const SSRPass* inputPass) { m_InputPass = inputPass; }
        RGResourceHandle GetSceneColorHandle() const { return m_OutputHandle; }

        /**
         * @brief 輝度閾値を設定
         * @param threshold 閾値
         */
        void SetThreshold(float threshold) { m_Settings.Threshold = threshold; }

        /**
         * @brief ブルーム強度を設定
         * @param intensity 強度
         */
        void SetIntensity(float intensity) { m_Settings.Intensity = intensity; }

        /**
         * @brief 拡大のテントフィルタの半径を設定
         * @param radius 半径（1つ下の段のテクセル単位）
         */
        void SetRadius(float radius) { m_Settings.Radius = radius; }

        /**
         * @brief ソフト閾値の膝を設定
         * @param softKnee 膝の値（0-1）
         */
        void SetSoftKnee(float softKnee) { m_Settings.SoftKnee = softKnee; }

        /**
         * @brief 現在の設定を取得
         * @return ブルーム設定の参照
         */
        const BloomSettings &GetSettings() const { return m_Settings; }

        /** @brief 縮小の段数の上限 */
        static constexpr uint32_t MaxBloomMipCount = 8;

        /**
         * @brief 今の縮小の段数を取得（資源を作った後の実際の段数。未作成なら0）
         */
        uint32_t GetActiveMipCount() const { return static_cast<uint32_t>(m_MipLevels.size()); }

    private:
        /** @brief 縮小・拡大の1段分の資源 */
        struct BloomMipLevel
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::TexturePtr DownTexture;
            RHI::FramebufferPtr DownFramebuffer;
            RHI::DescriptorSetPtr DownDescriptorSet;
            RHI::BufferPtr DownParamsBuffer;
            // 拡大の結果（最下段では作らない）
            RHI::TexturePtr UpTexture;
            RHI::FramebufferPtr UpFramebuffer;
            RHI::DescriptorSetPtr UpDescriptorSet;
            RHI::BufferPtr UpParamsBuffer;
        };

        bool EnsureMipChain(uint32_t width, uint32_t height);
        void ReleaseMipChain();
        void EnqueueMipChain(ViewRenderContext &context, const RHI::TexturePtr& sceneColorPtr);
        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& outputTexture,
                              bool bUseRenderGraphInitialState);
        void ExecuteWithInput(ViewRenderContext &context,
                              const RHI::TexturePtr& sceneColorPtr,
                              bool bRegisterLegacyBridge);
        bool EnqueueEmptyNativePass(ViewRenderContext &context) const;

        // 設定
        BloomSettings m_Settings;

        // 出力テクスチャ（Device::CreateTextureで作成、自己所有）
        RHI::TexturePtr m_OutputTexture;
        RGResourceHandle m_OutputHandle;
        RGResourceHandle m_InputSceneColorHandle;

        // パイプラインリソース
        RHI::RenderPassPtr m_BloomRenderPass;
        RHI::FramebufferPtr m_BloomFramebuffer;
        RHI::PipelinePtr m_BloomPipeline;
        RHI::ShaderPtr m_BloomVertexShader;
        RHI::ShaderPtr m_BloomFragmentShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::DescriptorSetPtr m_BloomDescriptorSet;
        RHI::SamplerPtr m_SceneColorSampler;

        // レンズダートの模様（Initialize で手続きで作る。外部の画像は使わない）
        RHI::TexturePtr m_LensDirtTexture;

        // 縮小・拡大の段（パス内で閉じた自前のテクスチャ）
        RHI::ShaderPtr m_DownsampleFragmentShader;
        RHI::ShaderPtr m_UpsampleFragmentShader;
        RHI::RenderPassPtr m_MipRenderPass;
        RHI::PipelinePtr m_DownsamplePipeline;
        RHI::PipelinePtr m_UpsamplePipeline;
        VariableArray<BloomMipLevel> m_MipLevels;
        uint32_t m_MipChainWidth = 0;
        uint32_t m_MipChainHeight = 0;
        uint32_t m_MipChainRequestedCount = 0;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;
        const SSRPass* m_InputPass = nullptr;

        // 現在のサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferOutputTexture = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
