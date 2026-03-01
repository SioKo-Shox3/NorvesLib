#pragma once

#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Text/IdentityPool.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief View間共有リソースレジストリ
     *
     * あるViewの出力リソース（テクスチャ等）を別のViewから参照するための仕組み。
     * Screen（またはRenderingCoordinator）レベルで保持され、各Viewに参照が渡されます。
     *
     * 使用例:
     * - SceneViewのDepthテクスチャをUIViewの深度テストに使用
     * - SceneColorをUI背景ブラーの入力に使用
     * - シャドウマップを複数SceneViewで共有
     *
     * リソースはフレーム開始時にクリアされ、各Viewが描画時に登録します。
     * このためView間の依存関係に注意が必要です（優先度の低いViewが先に描画される）。
     *
     * ```cpp
     * // SceneViewのGBufferPass内で登録
     * context.SharedResources->RegisterTexture("SceneDepth", depthTexture);
     *
     * // UIViewのパス内で参照
     * auto sceneDepth = context.SharedResources->GetTexture("SceneDepth");
     * ```
     */
    class SharedResourceRegistry
    {
    public:
        /**
         * @brief コンストラクタ
         */
        SharedResourceRegistry() = default;

        /**
         * @brief デストラクタ
         */
        ~SharedResourceRegistry() = default;

        // ========================================
        // テクスチャリソース
        // ========================================

        /**
         * @brief テクスチャを登録します（生ポインタ版）
         * @param name リソース名（Identity）
         * @param texture テクスチャ
         */
        void RegisterTexture(const Identity &name, RHI::ITexture *texture);

        /**
         * @brief テクスチャを登録します（TexturePtr版、DescriptorSetバインド用）
         * @param name リソース名（Identity）
         * @param texture テクスチャ（SharedPtr）
         */
        void RegisterTexturePtr(const Identity &name, RHI::TexturePtr texture);

        /**
         * @brief テクスチャを取得します（生ポインタ版）
         * @param name リソース名（Identity）
         * @return テクスチャ（見つからない場合nullptr）
         */
        RHI::ITexture *GetTexture(const Identity &name) const;

        /**
         * @brief テクスチャを取得します（TexturePtr版、DescriptorSetバインド用）
         * @param name リソース名（Identity）
         * @return テクスチャ（見つからない場合nullptr）
         */
        RHI::TexturePtr GetTexturePtr(const Identity &name) const;

        /**
         * @brief テクスチャが登録されているかチェック
         * @param name リソース名（Identity）
         * @return 登録されている場合true
         */
        bool HasTexture(const Identity &name) const;

        // ========================================
        // バッファリソース
        // ========================================

        /**
         * @brief バッファを登録します
         * @param name リソース名（Identity）
         * @param buffer バッファ
         */
        void RegisterBuffer(const Identity &name, RHI::IBuffer *buffer);

        /**
         * @brief バッファを取得します
         * @param name リソース名（Identity）
         * @return バッファ（見つからない場合nullptr）
         */
        RHI::IBuffer *GetBuffer(const Identity &name) const;

        // ========================================
        // フレーム管理
        // ========================================

        /**
         * @brief フレーム開始時のリセット
         *
         * フレーム内一時リソースの参照をクリアします。
         * 永続リソースは維持されます。
         */
        void BeginFrame();

        /**
         * @brief 全リソースをクリア
         */
        void Clear();

    private:
        UnorderedMap<Identity, RHI::ITexture *, Identity::Hasher> m_Textures;
        UnorderedMap<Identity, RHI::TexturePtr, Identity::Hasher> m_TexturePtrs;
        UnorderedMap<Identity, RHI::IBuffer *, Identity::Hasher> m_Buffers;
    };

} // namespace NorvesLib::Core::Rendering
