#pragma once

#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;

    /**
     * @brief 今のフレームの深度から作る HZB（階層的な深度ピラミッド）
     *
     * R32_FLOAT の全ミップ。深度は Less の標準の向き（手前ほど小さい）で、2x2 の最大（遠い方）で縮める。
     * ミップ0 は深度の半分の解像度（切り上げ）。幅・高さが奇数の段は、はみ出す行・列も最大に含めるので、
     * 縮めた 1 texel は、それが覆う深度の最大以上になる（遮蔽の判定が保守的になる）。
     *
     * 全ミップを GENERAL（UnorderedAccess）のまま作り、最後に全ミップを ShaderResource へ遷移する。
     * 遮蔽の判定のシェーダーは、Build の後に ShaderResource として読む。
     */
    class HiZPyramid
    {
    public:
        HiZPyramid();
        ~HiZPyramid();

        HiZPyramid(const HiZPyramid&) = delete;
        HiZPyramid& operator=(const HiZPyramid&) = delete;

        /** @brief パイプラインとサンプラーを作る。失敗したら false（以降の Build は何もしない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();

        /** @brief 深度の解像度に合うピラミッド・ミップごとの資源を作る。同じ解像度なら何もしない */
        bool Resize(uint32_t depthWidth, uint32_t depthHeight);

        /**
         * @brief コマンドリストへ HZB の生成を記録する
         * @param depthTexture 今のフレームの深度。記録時点で ShaderResource の状態であること
         * @return 記録できたら true。終わると全ミップが ShaderResource の状態になる
         */
        bool Build(RHI::ICommandList* commandList, const RHI::TexturePtr& depthTexture);

        bool IsReady() const { return m_Pyramid != nullptr && m_MipCount > 0; }
        const RHI::TexturePtr& GetTexture() const { return m_Pyramid; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        uint32_t GetMipCount() const { return m_MipCount; }

        /** @brief ミップ0 の1辺。深度の1辺の半分（切り上げ） */
        static uint32_t ComputeBaseSize(uint32_t depthSize);
        /** @brief ミップ mip の1辺。ミップ0 から半分（切り捨て）にして 1 で止める */
        static uint32_t ComputeMipSize(uint32_t baseSize, uint32_t mip);
        /** @brief 全ミップの数（長い辺が 1 になるまで） */
        static uint32_t ComputeMipCount(uint32_t baseWidth, uint32_t baseHeight);

    private:
        void ReleasePyramid();

        RHI::IDevice* m_Device = nullptr;
        ShaderManager* m_ShaderManager = nullptr;
        RHI::ShaderPtr m_Mip0Shader;
        RHI::ShaderPtr m_DownsampleShader;
        RHI::PipelinePtr m_Mip0Pipeline;
        RHI::PipelinePtr m_DownsamplePipeline;
        RHI::SamplerPtr m_Sampler;

        RHI::TexturePtr m_Pyramid;
        // ミップごとの資源。ミップ k の内容（書き込み先と元の大きさ）は作成時に固定なので、毎フレーム書き換えない
        // （コマンドの記録中に UBO を書き換えると、実行時には最後の値だけが効く）。
        Container::VariableArray<RHI::BufferPtr> m_ParamBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_DescriptorSets;
        // ミップ0 のディスクリプタが結んでいる深度。変わったときだけ結び直す。
        RHI::TexturePtr m_BoundDepth;

        uint32_t m_DepthWidth = 0;
        uint32_t m_DepthHeight = 0;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_MipCount = 0;
    };

    /**
     * @brief GBuffer の深度から HZB を作る RenderGraph のパス
     *
     * 描画の途中（2パスの遮蔽カリングの1パス目の後）に置いて使う。名前付きの GBuffer 深度を ShaderResource として
     * 読み、HZB を作る。深度の Load/Store の契約（MegaGeometry が GBuffer へ Load で描く）には触れない。
     * 完了は論理資源 GetCompleteHandle() で表す。後のパスはこれを Read して順序を取り、GetPyramidTexture() で読む。
     */
    class HiZPyramidPass final : public IRenderGraphPass
    {
    public:
        HiZPyramidPass();
        ~HiZPyramidPass() override;

        const char* GetName() const override { return "HiZPyramidPass"; }

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        void Shutdown();

        RGResourceHandle GetCompleteHandle() const { return m_CompleteHandle; }
        /** @brief 最後の Execute で作れたピラミッド。作れなかったときは null */
        RHI::TexturePtr GetPyramidTexture() const { return m_bBuilt ? m_Pyramid.GetTexture() : nullptr; }
        uint32_t GetMipCount() const { return m_bBuilt ? m_Pyramid.GetMipCount() : 0; }

    private:
        HiZPyramid m_Pyramid;
        RGTextureHandle m_DepthHandle;
        RGResourceHandle m_CompleteHandle;
        bool m_bInitialized = false;
        bool m_bBuilt = false;
    };

} // namespace NorvesLib::Core::Rendering
