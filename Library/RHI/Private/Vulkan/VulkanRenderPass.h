#pragma once

#include "IRenderPass.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief RenderPassのVulkan実装
 */
class VulkanRenderPass : public IRenderPass
{
public:
    /**
     * @brief コンストラクタ
     * @param device Vulkanデバイス
     * @param colorFormats カラーアタッチメントのフォーマット配列
     * @param depthStencilFormat デプスステンシルフォーマット（無効な場合はFormat::Unknown）
     */
    VulkanRenderPass(std::shared_ptr<VulkanDevice> device, 
                    const std::vector<Format>& colorFormats,
                    Format depthStencilFormat = Format::Unknown);
    
    /**
     * @brief デストラクタ
     */
    virtual ~VulkanRenderPass();

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
     * @brief カラーアタッチメントのフォーマットを取得
     * @param index アタッチメントインデックス
     * @return フォーマット
     */
    virtual Format GetColorAttachmentFormat(uint32_t index) const override;

    /**
     * @brief デプスステンシルアタッチメントのフォーマットを取得
     * @return デプスステンシルフォーマット
     */
    virtual Format GetDepthStencilFormat() const override;

    /**
     * @brief Vulkanレンダーパスハンドルを取得
     * @return VkRenderPassハンドル
     */
    VkRenderPass GetVkRenderPass() const { return m_renderPass; }

private:
    /**
     * @brief レンダーパスの作成
     */
    void CreateRenderPass();

private:
    std::shared_ptr<VulkanDevice> m_device;         ///< Vulkanデバイス
    VkRenderPass m_renderPass = VK_NULL_HANDLE;     ///< Vulkanレンダーパスハンドル
    std::vector<Format> m_colorFormats;             ///< カラーアタッチメントのフォーマット
    Format m_depthStencilFormat;                   ///< デプスステンシルフォーマット
};

} // namespace NorvesLib::RHI::Vulkan