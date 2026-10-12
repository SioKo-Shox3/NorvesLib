#pragma once

#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/FrameResourceWriteTracker.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;

    class VulkanDevice;

    /**
     * @brief Vulkanバッファ実装クラス (vulkan.hpp使用)
     */
    class VulkanBuffer : public IBuffer
    {
    public:
        /**
         * @brief VulkanBufferのコンストラクタ
         * @param device Vulkanデバイス
         * @param desc バッファ記述子
         */
        VulkanBuffer(
            TSharedPtr<VulkanDevice> device,
            const BufferDesc &desc,
            vk::BufferUsageFlags additionalUsage = {});

        /**
         * @brief デストラクタ
         */
        ~VulkanBuffer() override;

        // IBufferインターフェース実装
        uint64_t GetSize() const override { return m_desc.Size; }
        void *Map(uint64_t offset = 0, uint64_t size = 0) override;
        void Unmap() override;
        void Update(const void *data, uint64_t size, uint64_t offset = 0) override;
        void NoteExplicitWrite(uint64_t offset, uint64_t size) override;
        ResourceUsage GetUsage() const override { return m_desc.Usage; }

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::Buffer GetVkBuffer() const { return m_buffer; }
        vk::DeviceMemory GetVkDeviceMemory() const { return m_deviceMemory; }
        bool IsHostVisible() const { return m_desc.CPUAccessible; }
        uint64_t GetDeviceAddress() const override { return m_deviceAddress; }
        // 実際に選ばれたメモリタイプの属性（VkMemoryPropertyFlags）
        uint32_t GetMemoryPropertyFlags() const { return static_cast<uint32_t>(m_memoryPropertyFlags); }

        /** @brief 記録したコマンドがこのバッファの範囲を参照した（開発ビルドの守られていない上書きの見張り。size が ~0 なら終わりまで） */
        void NoteFrameResourceReference(uint64_t offset, uint64_t size)
        {
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
            if (m_writeTrack.Tracker)
            {
                m_writeTrack.Tracker->OnBufferReference(m_writeTrack, offset, size);
            }
#else
            (void)offset;
            (void)size;
#endif
        }

#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
        /** @brief 見張りの状態（記述子セットが参照するバッファを束縛のときに知らせるのに使う） */
        FrameResourceTrackState &GetFrameResourceTrackState() { return m_writeTrack; }
#endif

    private:
        friend class VulkanAccelerationStructure;

        TSharedPtr<VulkanDevice> m_device;
        BufferDesc m_desc;
        vk::Buffer m_buffer;
        vk::DeviceMemory m_deviceMemory;
        // 確保したメモリの大きさ（破棄の列に残るメモリの量の統計に使う）
        uint64_t m_memorySize = 0;
        uint64_t m_deviceAddress = 0;
        vk::MemoryPropertyFlags m_memoryPropertyFlags{};

        bool m_bIsMapped = false;
        void *m_mappedData = nullptr;

#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
        // 守られていない上書きの見張りの状態（作成のときに登録する）
        FrameResourceTrackState m_writeTrack;
#endif

        bool ShouldEnableDeviceAddress() const;

        // 作成済みの VkBuffer とメモリをその場で破棄する（コンストラクタ失敗時の後始末。まだ提出に使われていない）
        void ReleaseHandles() noexcept;

        // バッファとメモリの作成
        void CreateBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

        // Vulkanバッファ使用法フラグに変換
        vk::BufferUsageFlags GetVkBufferUsage() const;
    };

} // namespace NorvesLib::RHI::Vulkan
