#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <cstring>

namespace NorvesLib::RHI::Vulkan
{

    using namespace NorvesLib::Core::Container;

    // コンストラクタ
    VulkanBuffer::VulkanBuffer(TSharedPtr<VulkanDevice> device, const BufferDesc &desc)
        : m_device(device), m_desc(desc)
    {
        // Vulkanバッファ使用法フラグを取得
        vk::BufferUsageFlags usage = GetVkBufferUsage();

        // メモリプロパティを設定
        vk::MemoryPropertyFlags memProps;

        if (desc.hostVisible)
        {
            // CPUからアクセス可能なメモリ
            memProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        }
        else
        {
            // デバイスローカルメモリ
            memProps = vk::MemoryPropertyFlagBits::eDeviceLocal;
        }

        // バッファとメモリを作成
        CreateBuffer(usage, memProps);
    }

    // デストラクタ
    VulkanBuffer::~VulkanBuffer()
    {
        // マッピングされている場合はアンマップ
        if (m_bIsMapped)
        {
            Unmap();
        }

        auto vkDevice = m_device->GetVkDevice();

        // バッファを破棄
        if (m_buffer)
        {
            vkDevice.destroyBuffer(m_buffer);
        }

        // メモリを解放
        if (m_deviceMemory)
        {
            vkDevice.freeMemory(m_deviceMemory);
        }
    }

    // IBufferインターフェース実装: Map
    void *VulkanBuffer::Map(uint64_t offset, uint64_t size)
    {
        if (!m_desc.hostVisible)
        {
            throw std::runtime_error("ホスト可視でないバッファはマッピングできません");
        }

        if (m_bIsMapped)
        {
            throw std::runtime_error("バッファは既にマッピングされています");
        }

        // サイズが0の場合は全体をマップ
        if (size == 0)
        {
            size = m_desc.size - offset;
        }

        // オフセットとサイズの範囲チェック
        if (offset + size > m_desc.size)
        {
            throw std::runtime_error("マッピング範囲がバッファサイズを超えています");
        }

        // メモリのマッピング
        auto result = m_device->GetVkDevice().mapMemory(m_deviceMemory, offset, size, {});
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("バッファメモリのマッピングに失敗しました");
        }

        m_bIsMapped = true;
        m_mappedData = result.value;

        return m_mappedData;
    }

    // IBufferインターフェース実装: Unmap
    void VulkanBuffer::Unmap()
    {
        if (!m_bIsMapped)
        {
            return;
        }

        m_device->GetVkDevice().unmapMemory(m_deviceMemory);
        m_bIsMapped = false;
        m_mappedData = nullptr;
    }

    // IBufferインターフェース実装: Update
    void VulkanBuffer::Update(const void *data, uint64_t size, uint64_t offset)
    {
        if (offset + size > m_desc.size)
        {
            throw std::runtime_error("更新範囲がバッファサイズを超えています");
        }

        // ホスト可視の場合は直接マッピングして更新
        if (m_desc.hostVisible)
        {
            void *mappedData = Map(offset, size);
            std::memcpy(mappedData, data, size);
            Unmap();
        }
        // ホスト不可視の場合は一時的なステージングバッファを使用
        else
        {
            // ステージングバッファの作成
            BufferDesc stagingDesc;
            stagingDesc.size = size;
            stagingDesc.usage = ResourceUsage::TransferSrc;
            stagingDesc.hostVisible = true;

            auto stagingBuffer = MakeShared<VulkanBuffer>(m_device, stagingDesc);

            // データをステージングバッファに書き込み
            void *mappedStaging = stagingBuffer->Map(0, size);
            std::memcpy(mappedStaging, data, size);
            stagingBuffer->Unmap();

            // 単発コマンドバッファでコピー
            vk::CommandBuffer commandBuffer = m_device->BeginSingleTimeCommands();

            // コピーコマンドの実行
            vk::BufferCopy copyRegion{};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = offset;
            copyRegion.size = size;

            commandBuffer.copyBuffer(stagingBuffer->GetVkBuffer(), m_buffer, 1, &copyRegion);

            m_device->EndSingleTimeCommands(commandBuffer);

            // ステージングバッファは自動的に解放される（スマートポインタによる管理）
        }
    }

    // バッファとメモリの作成
    void VulkanBuffer::CreateBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
    {
        auto vkDevice = m_device->GetVkDevice();

        // バッファ作成情報
        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.size = m_desc.size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive; // 単一キュー専用

        // バッファの作成
        auto createResult = vkDevice.createBuffer(bufferInfo);
        if (createResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Vulkanバッファの作成に失敗しました");
        }
        m_buffer = createResult.value;

        // メモリ要件の取得
        vk::MemoryRequirements memRequirements = vkDevice.getBufferMemoryRequirements(m_buffer);

        // メモリタイプのインデックスを取得
        uint32_t memoryTypeIndex = m_device->FindMemoryType(memRequirements.memoryTypeBits, properties);

        // メモリ割り当て情報
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        // メモリの割り当て
        auto allocResult = vkDevice.allocateMemory(allocInfo);
        if (allocResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("バッファメモリの割り当てに失敗しました");
        }
        m_deviceMemory = allocResult.value;

        // メモリをバッファにバインド
        auto bindResult = vkDevice.bindBufferMemory(m_buffer, m_deviceMemory, 0);
        if (bindResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("バッファメモリのバインドに失敗しました");
        }
    }

    // Vulkanバッファ使用法フラグに変換
    vk::BufferUsageFlags VulkanBuffer::GetVkBufferUsage() const
    {
        vk::BufferUsageFlags usage{};

        // 使用法フラグの変換
        if ((m_desc.usage & ResourceUsage::VertexBuffer) == ResourceUsage::VertexBuffer)
        {
            usage |= vk::BufferUsageFlagBits::eVertexBuffer;
        }

        if ((m_desc.usage & ResourceUsage::IndexBuffer) == ResourceUsage::IndexBuffer)
        {
            usage |= vk::BufferUsageFlagBits::eIndexBuffer;
        }

        if ((m_desc.usage & ResourceUsage::ConstantBuffer) == ResourceUsage::ConstantBuffer)
        {
            usage |= vk::BufferUsageFlagBits::eUniformBuffer;
        }

        if ((m_desc.usage & ResourceUsage::ShaderRead) == ResourceUsage::ShaderRead)
        {
            usage |= vk::BufferUsageFlagBits::eStorageBuffer;
        }

        if ((m_desc.usage & ResourceUsage::ShaderWrite) == ResourceUsage::ShaderWrite)
        {
            usage |= vk::BufferUsageFlagBits::eStorageBuffer;
        }

        if ((m_desc.usage & ResourceUsage::TransferSrc) == ResourceUsage::TransferSrc)
        {
            usage |= vk::BufferUsageFlagBits::eTransferSrc;
        }

        if ((m_desc.usage & ResourceUsage::TransferDst) == ResourceUsage::TransferDst)
        {
            usage |= vk::BufferUsageFlagBits::eTransferDst;
        }

        // デフォルトでトランスファー先として使用可能にする
        if (!m_desc.hostVisible)
        {
            usage |= vk::BufferUsageFlagBits::eTransferDst;
        }

        return usage;
    }

} // namespace NorvesLib::RHI::Vulkan
