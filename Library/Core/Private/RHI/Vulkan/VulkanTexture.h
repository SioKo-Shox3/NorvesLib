#pragma once

#include "RHI/ITexture.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"
#include <atomic>

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;

    class VulkanDevice;

    // テクスチャ作成と同じ形式・用途の変換（sparse の可否照会を実際の作成条件で行うために公開する）
    vk::Format ConvertToVkFormat(Format format);
    vk::ImageUsageFlags ConvertToVkImageUsageFlags(ResourceUsage usage);

    /**
     * @brief テクスチャの Vulkan 実装 (vulkan.hpp使用)
     */
    class VulkanTexture : public ITexture
    {
    public:
        /**
         * @brief コンストラクタ
         * @param device Vulkanデバイス
         * @param desc テクスチャ記述子
         */
        VulkanTexture(TSharedPtr<VulkanDevice> device, const TextureDesc &desc);

        /**
         * @brief VulkanTextureのコンストラクタ (既存のイメージから)
         * @param device Vulkanデバイス
         * @param desc テクスチャ記述子
         * @param image 既存のvk::Image (所有権は移行しない)
         */
        VulkanTexture(TSharedPtr<VulkanDevice> device, const TextureDesc &desc, vk::Image image);

        /**
         * @brief デストラクタ
         */
        virtual ~VulkanTexture();

        // ITextureインターフェース実装
        virtual uint32_t GetWidth() const override { return m_desc.Width; }
        virtual uint32_t GetHeight() const override { return m_desc.Height; }
        virtual uint32_t GetDepth() const override { return m_desc.Depth; }
        virtual uint32_t GetMipLevels() const override { return m_desc.MipLevels; }
        virtual uint32_t GetArraySize() const override { return m_desc.ArraySize; }
        virtual Format GetFormat() const override { return m_desc.TextureFormat; }
        virtual ResourceUsage GetUsage() const override { return m_desc.Usage; }
        virtual bool IsCubemap() const override { return m_desc.IsCubemap; }
        virtual void Update(const void *data, uint32_t rowPitch, uint32_t slicePitch,
                            uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override;

        // sparse（部分常駐）テクスチャ
        bool IsSparse() const override { return m_desc.bSparse; }
        bool GetSparseInfo(SparseTextureInfo &outInfo) const override;
        uint64_t GetSparseBoundBytes() const override { return m_sparseBoundBytes.load(std::memory_order_relaxed); }

        /**
         * @brief 結んだ物理メモリの量を増減する（タイルの結び付け・外しが呼ぶ。VRAM の台帳が読む）
         * @param deltaBytes 結んだ分は正、外した分は負
         */
        void AddSparseBoundBytes(int64_t deltaBytes);

        // sparse の結び付け（VulkanDevice::BindSparse が使う）。状態の更新は結び付けの提出側が直列に呼ぶ。
        uint32_t GetSparseMemoryTypeBits() const { return m_sparseMemoryTypeBits; }
        uint64_t GetSparseMemoryAlignment() const { return m_sparseMemoryAlignment; }
        const SparseTextureInfo &GetSparseInfoRef() const { return m_sparseInfo; }

        /** @brief ミップテイルのページ数（ミップテイルが無ければ 0） */
        uint32_t GetSparseMipTailPageCount() const;

        /** @brief タイルがミップ・x・y の範囲内か（ミップテイルのミップは範囲外） */
        bool IsValidSparseTile(uint32_t mipLevel, uint32_t tileX, uint32_t tileY) const;

        /**
         * @brief タイルの結び付け状態を更新し、変わったときだけ結んだ量を増減する
         * @param bBound 結んだら true、外したら false
         */
        void CommitSparseTileBinding(uint32_t mipLevel, uint32_t tileX, uint32_t tileY, bool bBound);

        /** @brief ミップテイルのページの結び付け状態を更新し、変わったときだけ結んだ量を増減する */
        void CommitSparseMipTailBinding(uint32_t pageIndex, bool bBound);

        // per-mip ImageView
        uint64_t GetMipImageViewHandle(uint32_t mipLevel) const override;
        vk::ImageView GetMipImageView(uint32_t mipLevel) const;

        /**
         * @brief 配列テクスチャの指定layerだけを参照するImageViewを取得
         * @param arrayLayer 0-based配列layer
         * @return 指定layerのImageView。範囲外または非2D配列なら無効値
         */
        vk::ImageView GetArrayLayerImageView(uint32_t arrayLayer) const;

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::Image GetVkImage() const { return m_image; }
        vk::ImageView GetVkImageView() const { return m_imageView; }
        vk::ImageLayout GetVkImageLayout() const { return m_currentLayout; }
        void SetVkImageLayout(vk::ImageLayout layout);

        /**
         * @brief イメージレイアウトの遷移
         * @param cmdBuffer コマンドバッファ
         * @param newLayout 新しいレイアウト
         * @param subresourceRange サブリソース範囲
         */
        void TransitionLayout(
            vk::CommandBuffer cmdBuffer,
            vk::ImageLayout newLayout,
            vk::ImageSubresourceRange subresourceRange);

        /**
         * @brief イメージレイアウトの遷移 (全サブリソース)
         * @param cmdBuffer コマンドバッファ
         * @param newLayout 新しいレイアウト
         */
        void TransitionLayout(
            vk::CommandBuffer cmdBuffer,
            vk::ImageLayout newLayout);

    private:
        void CreateTexture();
        void CreateSparseTexture(vk::ImageCreateInfo &imageInfo);
        void CreateImageView();
        void InitializeSubresourceLayouts(vk::ImageLayout layout);
        uint32_t GetTotalArrayLayerCount() const;
        uint32_t GetSubresourceLayoutIndex(uint32_t mipLevel, uint32_t arrayLayer) const;
        vk::ImageLayout GetTrackedSubresourceLayout(uint32_t mipLevel, uint32_t arrayLayer) const;
        void SetTrackedSubresourceLayout(uint32_t mipLevel, uint32_t arrayLayer, vk::ImageLayout layout);
        void RefreshCurrentLayoutFromSubresources();

    private:
        TSharedPtr<VulkanDevice> m_device;
        TextureDesc m_desc;
        vk::Image m_image;
        vk::DeviceMemory m_memory;
        vk::ImageView m_imageView;
        mutable NorvesLib::Core::Container::VariableArray<vk::ImageView> m_mipImageViews;
        mutable NorvesLib::Core::Container::VariableArray<vk::ImageView> m_arrayLayerImageViews;
        NorvesLib::Core::Container::VariableArray<vk::ImageLayout> m_subresourceLayouts;
        vk::ImageLayout m_currentLayout = vk::ImageLayout::eUndefined;
        bool m_bOwnsImage = true;
        SparseTextureInfo m_sparseInfo;
        std::atomic<uint64_t> m_sparseBoundBytes{0};
        uint32_t m_sparseMemoryTypeBits = 0;
        uint64_t m_sparseMemoryAlignment = 0;
        // タイルとミップテイルのページごとの結び付け状態（二重に結ぶ・外すで結んだ量がずれないようにする）
        NorvesLib::Core::Container::VariableArray<uint8_t> m_sparseTileBound;
        NorvesLib::Core::Container::VariableArray<uint32_t> m_sparseTileBase;
        NorvesLib::Core::Container::VariableArray<uint8_t> m_sparseMipTailBound;
    };

} // namespace NorvesLib::RHI::Vulkan
