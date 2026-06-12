#include "RHI/TransientResourcePool.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "Container/Containers.h"
#include <cassert>
#include <iostream>
#include <utility>

namespace
{
    using NorvesLib::Core::Container::MakeUnique;
    using NorvesLib::Core::Container::TUniquePtr;
    using NorvesLib::Core::Container::VariableArray;

    size_t GetFormatBytesPerPixel(NorvesLib::RHI::Format format)
    {
        switch (format)
        {
        case NorvesLib::RHI::Format::R16G16B16A16_FLOAT:
            return 8;
        case NorvesLib::RHI::Format::D32_FLOAT:
            return 4;
        default:
            return 4;
        }
    }

    size_t EstimateTextureSize(const NorvesLib::RHI::TextureDesc& desc)
    {
        return static_cast<size_t>(desc.Width) *
               static_cast<size_t>(desc.Height) *
               static_cast<size_t>(desc.Depth) *
               static_cast<size_t>(desc.ArraySize) *
               GetFormatBytesPerPixel(desc.TextureFormat);
    }

    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }
        bool IsCubemap() const override { return m_Desc.IsCubemap; }
        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

    private:
        NorvesLib::RHI::TextureDesc m_Desc;
    };

    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const NorvesLib::RHI::BufferDesc& desc)
            : m_Desc(desc)
        {
        }

        uint64_t GetSize() const override { return m_Desc.Size; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)offset;
            (void)size;
            return nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            (void)data;
            (void)size;
            (void)offset;
        }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }

    private:
        NorvesLib::RHI::BufferDesc m_Desc;
    };

    class MockAllocator final : public NorvesLib::RHI::IGPUResourceAllocator
    {
    public:
        NorvesLib::RHI::BufferAllocation AllocateBuffer(
            const NorvesLib::RHI::BufferDesc& desc,
            NorvesLib::RHI::AllocationType type = NorvesLib::RHI::AllocationType::Dedicated) override
        {
            auto buffer = MakeUnique<FakeBuffer>(desc);
            NorvesLib::RHI::BufferAllocation allocation;
            allocation.Buffer = buffer.get();
            allocation.Offset = 0;
            allocation.Size = desc.Size;
            allocation.Type = type;

            BufferRecord record;
            record.Buffer = std::move(buffer);
            record.Size = static_cast<size_t>(allocation.Size);
            m_Buffers.push_back(std::move(record));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
            m_UsedMemory += static_cast<size_t>(allocation.Size);
            return allocation;
        }

        void FreeBuffer(NorvesLib::RHI::BufferAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Buffers.begin(); it != m_Buffers.end(); ++it)
            {
                if (it->Buffer.get() == allocation.Buffer)
                {
                    m_AllocatedMemory -= it->Size;
                    m_UsedMemory -= it->Size;
                    m_Buffers.erase(it);
                    allocation = {};
                    return;
                }
            }
            assert(false && "FreeBuffer received an unknown allocation");
        }

        NorvesLib::RHI::TextureAllocation AllocateTexture(
            const NorvesLib::RHI::TextureDesc& desc,
            NorvesLib::RHI::AllocationType type = NorvesLib::RHI::AllocationType::Dedicated) override
        {
            auto texture = MakeUnique<FakeTexture>(desc);
            NorvesLib::RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = EstimateTextureSize(desc);
            allocation.Type = type;

            TextureRecord record;
            record.Texture = std::move(texture);
            record.Size = allocation.Size;
            m_Textures.push_back(std::move(record));
            m_AllocatedMemory += allocation.Size;
            m_UsedMemory += allocation.Size;
            return allocation;
        }

        void FreeTexture(NorvesLib::RHI::TextureAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Textures.begin(); it != m_Textures.end(); ++it)
            {
                if (it->Texture.get() == allocation.Texture)
                {
                    m_AllocatedMemory -= it->Size;
                    m_UsedMemory -= it->Size;
                    m_Textures.erase(it);
                    allocation = {};
                    return;
                }
            }
            assert(false && "FreeTexture received an unknown allocation");
        }

        size_t GetAllocatedMemory() const override { return m_AllocatedMemory; }
        size_t GetUsedMemory() const override { return m_UsedMemory; }
        void Trim() override {}

        size_t GetLiveAllocationCount() const
        {
            return m_Textures.size() + m_Buffers.size();
        }

    private:
        struct TextureRecord
        {
            TUniquePtr<FakeTexture> Texture;
            size_t Size = 0;
        };

        struct BufferRecord
        {
            TUniquePtr<FakeBuffer> Buffer;
            size_t Size = 0;
        };

        VariableArray<TextureRecord> m_Textures;
        VariableArray<BufferRecord> m_Buffers;
        size_t m_AllocatedMemory = 0;
        size_t m_UsedMemory = 0;
    };

    void TestSameSlotReusesAfterNextBeginFrame()
    {
        MockAllocator allocator;
        NorvesLib::RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));

        pool.BeginFrame(0);
        auto* first = pool.AcquireRenderTarget(64, 64, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "First");
        assert(first);
        pool.EndFrame();

        pool.BeginFrame(0);
        auto* second = pool.AcquireRenderTarget(64, 64, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "Second");
        assert(second == first);
        pool.EndFrame();

        pool.Shutdown();
        assert(allocator.GetLiveAllocationCount() == 0);
        std::cout << "Same slot reuse passed\n";
    }

    void TestDifferentSlotDoesNotReuse()
    {
        MockAllocator allocator;
        NorvesLib::RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));

        pool.BeginFrame(0);
        auto* first = pool.AcquireRenderTarget(64, 64, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "Slot0");
        assert(first);
        pool.EndFrame();

        pool.BeginFrame(1);
        auto* second = pool.AcquireRenderTarget(64, 64, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "Slot1");
        assert(second);
        assert(second != first);
        pool.EndFrame();

        pool.Shutdown();
        assert(allocator.GetLiveAllocationCount() == 0);
        std::cout << "Different slot isolation passed\n";
    }

    void TestSameSerialDoesNotReuseOrTrim()
    {
        MockAllocator allocator;
        NorvesLib::RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));

        pool.BeginFrame(0);
        auto* first = pool.AcquireDepthStencil(32, 32, NorvesLib::RHI::Format::D32_FLOAT, "DepthA");
        assert(first);
        pool.EndFrame();

        auto* second = pool.AcquireDepthStencil(32, 32, NorvesLib::RHI::Format::D32_FLOAT, "DepthB");
        assert(second);
        assert(second != first);
        assert(allocator.GetLiveAllocationCount() == 2);

        pool.SetMaxPoolMemory(0);
        pool.Trim();
        assert(allocator.GetLiveAllocationCount() == 2);

        pool.EndFrame();
        pool.Shutdown();
        assert(allocator.GetLiveAllocationCount() == 0);
        std::cout << "Same serial protection passed\n";
    }

    void TestTrimOnlyFreesMatchingPastSlot()
    {
        MockAllocator allocator;
        NorvesLib::RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));

        pool.BeginFrame(0);
        auto* slot0 = pool.AcquireRenderTarget(16, 16, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "Slot0");
        assert(slot0);
        pool.EndFrame();

        pool.BeginFrame(1);
        auto* slot1 = pool.AcquireRenderTarget(16, 16, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "Slot1");
        assert(slot1);
        pool.EndFrame();
        assert(allocator.GetLiveAllocationCount() == 2);

        pool.SetMaxPoolMemory(0);
        pool.BeginFrame(0);
        pool.Trim();
        assert(allocator.GetLiveAllocationCount() == 1);

        pool.BeginFrame(1);
        pool.Trim();
        assert(allocator.GetLiveAllocationCount() == 0);

        pool.Shutdown();
        std::cout << "Trim slot and serial rule passed\n";
    }

    void TestReleaseAllAndShutdownFreeLiveAllocations()
    {
        MockAllocator allocator;
        NorvesLib::RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 2));

        pool.BeginFrame(0);
        auto* texture = pool.AcquireRenderTarget(8, 8, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "ReleaseTexture");
        auto* buffer = pool.AcquireBuffer(256, NorvesLib::RHI::ResourceUsage::StorageBuffer, "ReleaseBuffer");
        assert(texture);
        assert(buffer);
        pool.EndFrame();
        assert(allocator.GetLiveAllocationCount() == 2);

        pool.ReleaseAll();
        assert(allocator.GetLiveAllocationCount() == 0);

        pool.BeginFrame(1);
        texture = pool.AcquireRenderTarget(8, 8, NorvesLib::RHI::Format::R16G16B16A16_FLOAT, "ShutdownTexture");
        buffer = pool.AcquireBuffer(256, NorvesLib::RHI::ResourceUsage::StorageBuffer, "ShutdownBuffer");
        assert(texture);
        assert(buffer);
        assert(allocator.GetLiveAllocationCount() == 2);

        pool.Shutdown();
        assert(allocator.GetLiveAllocationCount() == 0);
        std::cout << "ReleaseAll and Shutdown passed\n";
    }
} // namespace

int main()
{
    TestSameSlotReusesAfterNextBeginFrame();
    TestDifferentSlotDoesNotReuse();
    TestSameSerialDoesNotReuseOrTrim();
    TestTrimOnlyFreesMatchingPastSlot();
    TestReleaseAllAndShutdownFreeLiveAllocations();

    std::cout << "TransientResourcePoolTest passed\n";
    return 0;
}
