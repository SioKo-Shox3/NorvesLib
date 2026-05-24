#pragma once

#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::Core::Container::UnorderedMap;
    using ::NorvesLib::Core::Container::VariableArray;

    class VulkanDevice;
    class VulkanBuffer;
    class VulkanTexture;
    class VulkanSampler;

    /**
     * @brief Vulkanディスクリプタセットレイアウト (vulkan.hpp使用)
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
            TSharedPtr<VulkanDevice> device,
            const VariableArray<DescriptorBindingDesc> &bindings);

        /**
         * @brief デストラクタ
         */
        ~VulkanDescriptorSetLayout();

        vk::DescriptorSetLayout GetVkDescriptorSetLayout() const { return m_layout; }
        const VariableArray<DescriptorBindingDesc> &GetBindings() const { return m_bindings; }
        vk::DescriptorType ToVkDescriptorType(DescriptorType type) const;

    private:
        TSharedPtr<VulkanDevice> m_device;
        VariableArray<DescriptorBindingDesc> m_bindings;
        vk::DescriptorSetLayout m_layout;

        vk::ShaderStageFlags ToVkShaderStageFlags(ShaderStage stage) const;
    };

    /**
     * @brief Vulkanディスクリプタプール (vulkan.hpp使用)
     */
    class VulkanDescriptorPool
    {
    public:
        /**
         * @brief コンストラクタ
         * @param device Vulkanデバイス
         * @param maxSets 最大セット数
         */
        VulkanDescriptorPool(TSharedPtr<VulkanDevice> device, uint32_t maxSets = 100);

        /**
         * @brief デストラクタ
         */
        ~VulkanDescriptorPool();

        vk::DescriptorPool GetVkDescriptorPool() const { return m_pool; }
        void Reset();

    private:
        TSharedPtr<VulkanDevice> m_device;
        vk::DescriptorPool m_pool;
    };

    /**
     * @brief Vulkanディスクリプタセット実装クラス (vulkan.hpp使用)
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
            TSharedPtr<VulkanDevice> device,
            const DescriptorSetDesc &desc,
            TSharedPtr<VulkanDescriptorSetLayout> layout,
            TSharedPtr<VulkanDescriptorPool> pool);

        /**
         * @brief デストラクタ
         */
        ~VulkanDescriptorSet() override;

        // IDescriptorSetインターフェース実装
        void BindConstantBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size) override;
        void BindTexture(uint32_t binding, TexturePtr texture) override;
        void BindSampler(uint32_t binding, SamplerPtr sampler) override;
        void BindStorageBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size) override;
        void BindStorageTexture(uint32_t binding, TexturePtr texture) override;
        void BindStorageTexture(uint32_t binding, TexturePtr texture, uint32_t mipLevel) override;
        void Update() override;

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::DescriptorSet GetVkDescriptorSet() const { return m_descriptorSet; }
        vk::DescriptorSetLayout GetVkDescriptorSetLayout() const;
        vk::PipelineLayout GetVkPipelineLayout() const;

    private:
        TSharedPtr<VulkanDevice> m_device;
        DescriptorSetDesc m_desc;
        TSharedPtr<VulkanDescriptorSetLayout> m_layout;
        TSharedPtr<VulkanDescriptorPool> m_pool;

        vk::DescriptorSet m_descriptorSet;
        vk::PipelineLayout m_pipelineLayout;

        bool m_bNeedsUpdate = false;

        struct BindingInfo
        {
            enum class ResourceType
            {
                Buffer,
                Texture,
                Sampler
            };

            ResourceType type;
            BufferPtr buffer;
            TexturePtr texture;
            SamplerPtr sampler;
            uint64_t offset = 0;
            uint64_t range = VK_WHOLE_SIZE;
            int32_t mipLevel = -1; // -1 = 全ミップ（デフォルト）、0+ = 特定ミップ
        };

        UnorderedMap<uint32_t, BindingInfo> m_bindings;

        void CreatePipelineLayout();
        vk::DescriptorType GetVkDescriptorType(uint32_t binding) const;
    };

} // namespace NorvesLib::RHI::Vulkan
