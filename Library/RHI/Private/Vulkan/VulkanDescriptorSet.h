#pragma once

#include "RHI/Public/IDescriptorSet.h"
#include <vulkan/vulkan.h>
#include <memory>
#include "Core/Public/Container/Containers.h"
#include <unordered_map>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;
class VulkanBuffer;
class VulkanTexture;
class VulkanSampler;

/**
 * @brief Vulkanディスクリプタセットレイアウト
 */
class VulkanDescriptorSetLayout
{
public:
    /**
     * @brief コンストラクタ
     * @param device Vulkanデバイス
     * @param bindings バインディング記述子のリスト
     */
    VulkanDescriptorSetLayout(
        std::shared_ptr<VulkanDevice> device,
        const NorvesLib::Core::Container::VariableArray<DescriptorBindingDesc>& bindings);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanDescriptorSetLayout();
    
    // Vulkanレイアウトハンドル取得
    VkDescriptorSetLayout GetVkDescriptorSetLayout() const { return m_layout; }
    
    // バインディング情報取得
    const NorvesLib::Core::Container::VariableArray<DescriptorBindingDesc>& GetBindings() const { return m_bindings; }

private:
    std::shared_ptr<VulkanDevice> m_device;
    NorvesLib::Core::Container::VariableArray<DescriptorBindingDesc> m_bindings;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    
    // バインディングタイプをVulkanディスクリプタタイプに変換
    VkDescriptorType ToVkDescriptorType(DescriptorType type) const;
    
    // シェーダーステージをVulkanシェーダーステージフラグに変換
    VkShaderStageFlags ToVkShaderStageFlags(ShaderStage stage) const;
};

/**
 * @brief Vulkanディスクリプタプール
 */
class VulkanDescriptorPool
{
public:
    /**
     * @brief コンストラクタ
     * @param device Vulkanデバイス
     * @param maxSets 最大セット数
     */
    VulkanDescriptorPool(std::shared_ptr<VulkanDevice> device, uint32_t maxSets = 100);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanDescriptorPool();
    
    // Vulkanプールハンドル取得
    VkDescriptorPool GetVkDescriptorPool() const { return m_pool; }
    
    // プールのリセット
    void Reset();

private:
    std::shared_ptr<VulkanDevice> m_device;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};

/**
 * @brief Vulkanディスクリプタセット実装クラス
 */
class VulkanDescriptorSet : public IDescriptorSet
{
public:
    /**
     * @brief VulkanDescriptorSetのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc ディスクリプタセット記述子
     * @param layout ディスクリプタセットレイアウト
     * @param pool ディスクリプタプール
     */
    VulkanDescriptorSet(
        std::shared_ptr<VulkanDevice> device,
        const DescriptorSetDesc& desc,
        std::shared_ptr<VulkanDescriptorSetLayout> layout,
        std::shared_ptr<VulkanDescriptorPool> pool);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanDescriptorSet() override;

    // IDescriptorSetインターフェース実装
    void SetConstantBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset = 0, uint64_t range = 0) override;
    void SetShaderResourceBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset = 0, uint64_t range = 0) override;
    void SetUnorderedAccessBuffer(uint32_t binding, BufferPtr buffer, uint64_t offset = 0, uint64_t range = 0) override;
    void SetTexture(uint32_t binding, TexturePtr texture) override;
    void SetStorageTexture(uint32_t binding, TexturePtr texture) override;
    void SetSampler(uint32_t binding, SamplerPtr sampler) override;
    void Update() override;

    // Vulkan固有のメソッド
    VkDescriptorSet GetVkDescriptorSet() const { return m_descriptorSet; }
    VkDescriptorSetLayout GetVkDescriptorSetLayout() const;
    
    // パイプラインレイアウトの取得
    VkPipelineLayout GetVkPipelineLayout() const;

private:
    std::shared_ptr<VulkanDevice> m_device;
    DescriptorSetDesc m_desc;
    std::shared_ptr<VulkanDescriptorSetLayout> m_layout;
    std::shared_ptr<VulkanDescriptorPool> m_pool;
    
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    
    // 更新が必要かどうか
    bool m_needsUpdate = false;
    
    // バインディング情報の構造体
    struct BindingInfo {
        enum class ResourceType {
            Buffer,
            Texture,
            Sampler
        };
        
        ResourceType type;
        union {
            struct {
                BufferPtr buffer;
                uint64_t offset;
                uint64_t range;
            } bufferInfo;
            
            TexturePtr texture;
            SamplerPtr sampler;
        };
    };
    
    // バインディング情報のマップ (binding -> info)
    NorvesLib::Core::Container::HashMap<uint32_t, BindingInfo> m_bindings;
    
    // パイプラインレイアウト作成
    void CreatePipelineLayout();
    
    // バインディングに対応するVkDescriptorTypeを取得
    VkDescriptorType GetVkDescriptorType(uint32_t binding) const;
};

} // namespace NorvesLib::RHI::Vulkan