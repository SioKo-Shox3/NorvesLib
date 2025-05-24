#pragma once

#include "RHITypes.h"
#include <vector>

namespace NorvesLib::RHI 
{

/**
 * @brief レンダーパスインターフェース
 * レンダーパスはレンダリングパスの定義を行うオブジェクトです。
 * 特にVulkanなどのモダンなAPIでは重要な概念です。
 */
class IRenderPass 
{
public:
    virtual ~IRenderPass() = default;

    /**
     * @brief カラーアタッチメント数を取得
     * @return カラーアタッチメント数
     */
    virtual uint32_t GetColorAttachmentCount() const = 0;

    /**
     * @brief デプスステンシルアタッチメントを持つかどうか
     * @return デプスステンシルアタッチメントを持つ場合true
     */
    virtual bool HasDepthStencilAttachment() const = 0;

    /**
     * @brief カラーアタッチメントのフォーマットを取得
     * @param index アタッチメントインデックス
     * @return フォーマット
     */
    virtual Format GetColorAttachmentFormat(uint32_t index) const = 0;

    /**
     * @brief デプスステンシルアタッチメントのフォーマットを取得
     * @return デプスステンシルフォーマット
     */
    virtual Format GetDepthStencilFormat() const = 0;
};

} // namespace NorvesLib::RHI