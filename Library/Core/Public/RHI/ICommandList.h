#pragma once

#include "RHITypes.h"
#include <array>

namespace NorvesLib::RHI
{

    /**
     * @brief ビューポート定義
     */
    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    /**
     * @brief シザー矩形定義
     */
    struct ScissorRect
    {
        int32_t left = 0;
        int32_t top = 0;
        int32_t right = 0;
        int32_t bottom = 0;
    };

    /**
     * @brief コマンドリストインターフェース
     * GPUに送信するコマンドをバッチ処理するためのオブジェクトです。
     */
    class ICommandList
    {
    public:
        virtual ~ICommandList() = default;

        /**
         * @brief コマンドリストの記録を開始
         *
         * 内部フェンスの待機・リセットを含みます。
         * Submit()による送信と対で使用してください。
         */
        virtual void Begin() = 0;

        /**
         * @brief コマンドリストの記録を開始（フェンス管理なし）
         *
         * 外部で同期制御を行う場合（SwapChain::BeginFrame/EndFrame等）に使用します。
         * フェンスの待機・リセットを行わず、コマンドバッファのリセットと記録開始のみを行います。
         */
        virtual void BeginRecording()
        {
            Begin();
        }

        /**
         * @brief 使用するフレームインデックスを設定
         *
         * ダブル/トリプルバッファリング時に、フレームごとのコマンドバッファを
         * ローテーションするために使用します。BeginRecording()の前に呼び出してください。
         * @param frameIndex フレームインデックス（SwapChain::GetCurrentFrameIndex()の値）
         */
        virtual void SetFrameIndex(uint32_t frameIndex)
        {
            (void)frameIndex;
        }

        /**
         * @brief コマンドリストの記録を終了
         */
        virtual void End() = 0;

        /**
         * @brief コマンドリストの実行
         * コマンドリストをGPUに送信して実行します。
         * @param waitForCompletion 完了を待つかどうか
         */
        virtual void Submit(bool waitForCompletion = false) = 0;

        /**
         * @brief レンダーパスの開始
         * @param renderPass レンダーパス
         * @param framebuffer フレームバッファ
         */
        virtual void BeginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer) = 0;

        /**
         * @brief レンダーパスの終了
         */
        virtual void EndRenderPass() = 0;

        /**
         * @brief ビューポートの設定
         * @param viewport ビューポート
         */
        virtual void SetViewport(const Viewport &viewport) = 0;

        /**
         * @brief シザー矩形の設定
         * @param scissor シザー矩形
         */
        virtual void SetScissor(const ScissorRect &scissor) = 0;

        /**
         * @brief パイプラインの設定
         * @param pipeline パイプラインオブジェクト
         */
        virtual void SetPipeline(PipelinePtr pipeline) = 0;

        /**
         * @brief 頂点バッファの設定
         * @param buffer 頂点バッファ
         * @param offset バッファ内のオフセット
         * @param slot 頂点バッファスロット
         */
        virtual void SetVertexBuffer(BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) = 0;

        /**
         * @brief インデックスバッファの設定
         * @param buffer インデックスバッファ
         * @param offset バッファ内のオフセット
         */
        virtual void SetIndexBuffer(BufferPtr buffer, uint64_t offset = 0) = 0;

        /**
         * @brief 定数バッファの設定
         * @param buffer 定数バッファ
         * @param slot レジスタスロット
         * @param stage シェーダーステージ
         */
        virtual void SetConstantBuffer(BufferPtr buffer, uint32_t slot, ShaderStage stage) = 0;

        /**
         * @brief テクスチャリソースの設定
         * @param texture テクスチャ
         * @param slot レジスタスロット
         * @param stage シェーダーステージ
         */
        virtual void SetTexture(TexturePtr texture, uint32_t slot, ShaderStage stage) = 0;

        /**
         * @brief サンプラーの設定
         * @param sampler サンプラー
         * @param slot レジスタスロット
         * @param stage シェーダーステージ
         */
        virtual void SetSampler(SamplerPtr sampler, uint32_t slot, ShaderStage stage) = 0;

        /**
         * @brief ディスクリプタセットの設定
         * @param descriptorSet ディスクリプタセット
         * @param slot セットスロット
         */
        virtual void SetDescriptorSet(DescriptorSetPtr descriptorSet, uint32_t slot = 0) = 0;

        /**
         * @brief インデックス付き描画
         * @param indexCount インデックス数
         * @param startIndexLocation 開始インデックス
         * @param baseVertexLocation ベース頂点位置
         */
        virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0) = 0;

        /**
         * @brief インデックスなし描画
         * @param vertexCount 頂点数
         * @param startVertexLocation 開始頂点位置
         */
        virtual void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) = 0;

        /**
         * @brief インスタンシング描画（インデックス付き）
         * @param indexCount インデックス数
         * @param instanceCount インスタンス数
         * @param startIndexLocation 開始インデックス
         * @param baseVertexLocation ベース頂点位置
         * @param startInstanceLocation 開始インスタンス位置
         */
        virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                                          uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0, uint32_t startInstanceLocation = 0) = 0;

        /**
         * @brief インスタンシング描画（インデックスなし）
         * @param vertexCount 頂点数
         * @param instanceCount インスタンス数
         * @param startVertexLocation 開始頂点位置
         * @param startInstanceLocation 開始インスタンス位置
         */
        virtual void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                                   uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0) = 0;

        /**
         * @brief コンピュートシェーダーディスパッチ
         * @param threadGroupCountX Xスレッドグループ数
         * @param threadGroupCountY Yスレッドグループ数
         * @param threadGroupCountZ Zスレッドグループ数
         */
        virtual void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) = 0;

        /**
         * @brief バッファコピー
         * @param src コピー元バッファ
         * @param dst コピー先バッファ
         * @param size コピーサイズ。0の場合はsrcのサイズ
         * @param srcOffset コピー元オフセット
         * @param dstOffset コピー先オフセット
         */
        virtual void CopyBuffer(BufferPtr src, BufferPtr dst, uint64_t size = 0,
                                uint64_t srcOffset = 0, uint64_t dstOffset = 0) = 0;

        /**
         * @brief バッファからテクスチャへのコピー
         * @param src コピー元バッファ
         * @param dst コピー先テクスチャ
         * @param width コピー幅
         * @param height コピー高さ
         * @param bufferOffset バッファ内のオフセット
         * @param mipLevel コピー先のミップレベル
         * @param arrayIndex コピー先の配列インデックス
         */
        virtual void CopyBufferToTexture(BufferPtr src, TexturePtr dst,
                                         uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                         uint32_t mipLevel = 0, uint32_t arrayIndex = 0) = 0;

        /**
         * @brief テクスチャからバッファへのコピー
         * @param src コピー元テクスチャ
         * @param dst コピー先バッファ
         * @param width コピー幅
         * @param height コピー高さ
         * @param bufferOffset バッファ内のオフセット
         * @param mipLevel コピー元のミップレベル
         * @param arrayIndex コピー元の配列インデックス
         */
        virtual void CopyTextureToBuffer(TexturePtr src, BufferPtr dst,
                                         uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                         uint32_t mipLevel = 0, uint32_t arrayIndex = 0) = 0;

        /**
         * @brief テクスチャからテクスチャへのコピー
         * @param src コピー元テクスチャ
         * @param dst コピー先テクスチャ
         * @param width コピー幅
         * @param height コピー高さ
         * @param srcMipLevel コピー元のミップレベル
         * @param srcArrayIndex コピー元の配列インデックス
         * @param dstMipLevel コピー先のミップレベル
         * @param dstArrayIndex コピー先の配列インデックス
         */
        virtual void CopyTexture(TexturePtr src, TexturePtr dst,
                                 uint32_t width, uint32_t height,
                                 uint32_t srcMipLevel = 0, uint32_t srcArrayIndex = 0,
                                 uint32_t dstMipLevel = 0, uint32_t dstArrayIndex = 0) = 0;

        /**
         * @brief バッファのリソースバリア（状態遷移）
         * @param buffer バッファ
         * @param beforeState 遷移前の状態
         * @param afterState 遷移後の状態
         * @param offset バッファ内のオフセット
         * @param size 対象のサイズ
         */
        virtual void BufferBarrier(BufferPtr buffer, ResourceState beforeState, ResourceState afterState,
                                   uint64_t offset = 0, uint64_t size = 0) = 0;

        /**
         * @brief テクスチャのリソースバリア（状態遷移）
         * @param texture テクスチャ
         * @param beforeState 遷移前の状態
         * @param afterState 遷移後の状態
         * @param mipLevel 対象のミップレベル
         * @param arrayIndex 対象の配列インデックス
         * @param mipCount 対象のミップレベル数（0で全て）
         * @param arrayCount 対象の配列数（0で全て）
         */
        virtual void TextureBarrier(TexturePtr texture, ResourceState beforeState, ResourceState afterState,
                                    uint32_t mipLevel = 0, uint32_t arrayIndex = 0, uint32_t mipCount = 0, uint32_t arrayCount = 0) = 0;
    };

} // namespace NorvesLib::RHI
