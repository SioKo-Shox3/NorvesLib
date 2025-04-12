#pragma once

#include "IFramebuffer.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;
class VulkanRenderPass;
class VulkanTexture;

/**
 * @brief FramebufferのVulkan実装
 */
class VulkanFramebuffer : public IFramebuffer
{
public:
    /**
     * @brief コンストラクタ
     * @param device Vulkanデバイス
     * @param renderPass レンダーパス
     * @param colorAttachments カラーアタッチメント配列
     * @param depthStencilAttachment デプスステンシルアタッチメント（オプション）
     * @param width フレームバッファの幅
     * @param height フレームバッファの高さ
     */
    VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, 
                      std::shared_ptr<VulkanRenderPass> renderPass,
                      const std::vector<std::shared_ptr<VulkanTexture>>& colorAttachments,
                      std::shared_ptr<VulkanTexture> depthStencilAttachment = nullptr,
                      uint32_t width = 0,
                      uint32_t height = 0);
    
    /**
     * @brief デストラクタ
     */
    virtual ~VulkanFramebuffer();

    /**
     * @brief フレームバッファの幅を取得
     * @return フレームバッファの幅
     */
    virtual uint32_t GetWidth() const override;

    /**
     * @brief フレームバッファの高さを取得
     * @return フレームバッファの高さ
     */
    virtual uint32_t GetHeight() const override;

    /**
     * @brief 関連付けられたレンダーパスを取得
     * @return レンダーパス
     */
    virtual RenderPassPtr GetRenderPass() const override;

    /**
     * @brief カラーアタッチメントを取得
     * @param index アタッチメントインデックス
     * @return カラーアタッチメント
     */
    virtual TexturePtr GetColorAttachment(uint32_t index) const override;

    /**
     * @brief デプスステンシルアタッチメントを取得
     * @return デプスステンシルアタッチメント
     */
    virtual TexturePtr GetDepthStencilAttachment() const override;

    /**
     * @brief カラーアタッチメント数を取得
     * @return カラーアタッチメント数
     */
    virtual uint32_t GetColorAttachmentCount() const override;

    /**
     * @brief デプスステンシルアタッチメントを持つかどうか
     * @return デプスステンシルアタッチメントを持つ場合true
     */
    virtual bool HasDepthStencilAttachment() const override;

    /**
     * @brief Vulkanフレームバッファハンドルを取得
     * @return VkFramebufferハンドル
     */
    VkFramebuffer GetVkFramebuffer() const { return m_framebuffer; }

private:
    /**
     * @brief フレームバッファの作成
     */
    void CreateFramebuffer();

private:
    std::shared_ptr<VulkanDevice> m_device;                          ///< Vulkanデバイス
    std::shared_ptr<VulkanRenderPass> m_renderPass;                  ///< レンダーパス
    std::vector<std::shared_ptr<VulkanTexture>> m_colorAttachments;  ///< カラーアタッチメント
    std::shared_ptr<VulkanTexture> m_depthStencilAttachment;         ///< デプスステンシルアタッチメント
    uint32_t m_width;                                               ///< フレームバッファの幅
    uint32_t m_height;                                              ///< フレームバッファの高さ
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;                    ///< Vulkanフレームバッファハンドル
};

} // namespace NorvesLib::RHI::Vulkan