#include "Rendering/DynamicUniformAllocator.h"
#include "Rendering/FrameResourceOwner.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Logging/LogMacros.h"

#include <cstdio>
#include <cstring>

namespace NorvesLib::Core::Rendering
{

    void DynamicUniformAllocator::Allocation::Write(const void *data, uint32_t size) const
    {
        if (!UniformBuffer || !data || size == 0)
        {
            return;
        }
        const uint32_t writeSize = size < Size ? size : Size;
        if (CPUAddress)
        {
            // 借りた切れ端は常にマップしたままの HOST_COHERENT のページなので、写すだけで GPU から見える
            std::memcpy(CPUAddress, data, writeSize);
            return;
        }
        UniformBuffer->Update(data, writeSize, Offset);
    }

    bool DynamicUniformAllocator::Initialize(RHI::IDevice *device, uint32_t uboSize,
                                             const RHI::DescriptorSetDesc &descriptorSetDesc)
    {
        if (m_bInitialized)
        {
            NORVES_LOG_WARNING("DynamicUniformAllocator", "初期化済み（2 回目の Initialize は無視する）");
            return true;
        }

        if (!device || uboSize == 0)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Initialize の引数が不正（device が null か uboSize が 0）");
            return false;
        }

        m_Device = device;
        m_DescriptorSetDesc = descriptorSetDesc;
        m_UBOSize = uboSize;
        // 記述子セットは毎フレーム FrameResources から借りる。レイアウトは同じ形のパスと共有する 1 つをデバイスから引く
        // （引けないデバイスでは、フレームの持ち主が無い文脈と同じく Allocate のたびに作る）
        m_DescriptorSetLayout = device->GetOrCreateDescriptorSetLayout(descriptorSetDesc);
        m_bBorrowing = true;
        m_bInitialized = true;
        return true;
    }

    bool DynamicUniformAllocator::Initialize(RHI::IDevice *device, uint32_t uboSize, uint32_t maxSlots,
                                             const RHI::DescriptorSetDesc &descriptorSetDesc)
    {
        if (m_bInitialized)
        {
            NORVES_LOG_WARNING("DynamicUniformAllocator", "Already initialized");
            return true;
        }

        if (!device || uboSize == 0 || maxSlots == 0)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Invalid parameters for DynamicUniformAllocator::Initialize");
            return false;
        }

        m_Device = device;
        m_DescriptorSetDesc = descriptorSetDesc;
        m_UBOSize = uboSize;
        m_MaxSlots = maxSlots;
        m_GrowthLimit = maxSlots;
        m_CurrentIndex = 0;

        m_Slots.resize(maxSlots);

        for (uint32_t i = 0; i < maxSlots; ++i)
        {
            if (!CreateSlot(i))
            {
                Shutdown();
                return false;
            }
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("DynamicUniformAllocator", "Initialized: %u slots, %u bytes/slot", maxSlots, uboSize);
        return true;
    }

    bool DynamicUniformAllocator::CreateSlot(uint32_t slotIndex)
    {
        char debugName[128];
        // UBOバッファ作成
        std::snprintf(debugName, sizeof(debugName), "DynUBO_Slot%u", slotIndex);
        RHI::BufferDesc bufDesc(m_UBOSize, RHI::ResourceUsage::ConstantBuffer, true, debugName);
        m_Slots[slotIndex].UniformBuffer = m_Device->CreateBuffer(bufDesc);
        if (!m_Slots[slotIndex].UniformBuffer)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Failed to create UBO for slot %u", slotIndex);
            return false;
        }

        // DescriptorSet作成
        m_Slots[slotIndex].DescriptorSet = m_Device->CreateDescriptorSet(m_DescriptorSetDesc);
        if (!m_Slots[slotIndex].DescriptorSet)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Failed to create DescriptorSet for slot %u", slotIndex);
            return false;
        }

        // UBOをDescriptorSetのbinding 0にバインド
        m_Slots[slotIndex].DescriptorSet->BindConstantBuffer(0, m_Slots[slotIndex].UniformBuffer, 0, m_UBOSize);
        m_Slots[slotIndex].DescriptorSet->Update();
        return true;
    }

    void DynamicUniformAllocator::SetGrowthLimit(uint32_t hardLimit)
    {
        m_GrowthLimit = hardLimit > m_MaxSlots ? hardLimit : m_MaxSlots;
    }

    bool DynamicUniformAllocator::Grow()
    {
        if (!m_bInitialized || m_bBorrowing || m_MaxSlots >= m_GrowthLimit)
        {
            return false;
        }

        // 1回に足す数は今の半分（最低64）。1フレームの要求が大きくても足す回数が対数で済む。
        const uint32_t remaining = m_GrowthLimit - m_MaxSlots;
        uint32_t step = m_MaxSlots / 2u;
        step = step < 64u ? 64u : step;
        step = step > remaining ? remaining : step;

        const uint32_t firstNewSlot = m_MaxSlots;
        m_Slots.resize(m_MaxSlots + step);
        for (uint32_t i = firstNewSlot; i < firstNewSlot + step; ++i)
        {
            if (!CreateSlot(i))
            {
                // 作れた所までを残し、作れなかった分は捨てる（以後の確保はその数で頭打ちになる）。
                m_Slots.resize(i);
                m_MaxSlots = i;
                m_GrowthLimit = i;
                return i > firstNewSlot;
            }
        }
        m_MaxSlots = firstNewSlot + step;
        return true;
    }

    void DynamicUniformAllocator::Shutdown()
    {
        for (auto &slot : m_Slots)
        {
            slot.DescriptorSet.reset();
            slot.UniformBuffer.reset();
        }
        m_Slots.clear();
        m_Device = nullptr;
        m_DescriptorSetDesc = RHI::DescriptorSetDesc{};
        m_DescriptorSetLayout.reset();
        m_GrowthLimit = 0;
        m_MaxSlots = 0;
        m_UBOSize = 0;
        m_CurrentIndex = 0;
        m_bInitialized = false;
        m_bBorrowing = false;
    }

    void DynamicUniformAllocator::Reset()
    {
        m_CurrentIndex = 0;
    }

    DynamicUniformAllocator::Allocation DynamicUniformAllocator::Allocate(FrameResourceLane *frameResources) const
    {
        if (!m_bInitialized || !m_bBorrowing)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "借りる形で初期化されていない");
            return {};
        }

        if (!frameResources || !m_DescriptorSetLayout)
        {
            return CreateUnowned();
        }

        // 定数の切れ端と記述子セットをフレームの持ち主から借りる。戻るのはこのフレームの回収で、それまで動かず上書きもされない。
        // 借りられなかったときのエラーは持ち主がログに出す
        const FrameUploadAllocation upload = frameResources->AllocateUpload(m_UBOSize, FrameUploadUsage::Constants);
        if (!upload.IsValid())
        {
            return {};
        }
        const FrameDescriptorAllocation descriptor = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout);
        if (!descriptor.IsValid())
        {
            return {};
        }

        descriptor.Set->BindConstantBuffer(0, upload.Buffer, upload.GetOffset32(), upload.GetSize32());

        Allocation result;
        result.UniformBuffer = upload.Buffer;
        result.DescriptorSet = descriptor.Set;
        result.Offset = upload.Offset;
        result.Size = upload.GetSize32();
        result.CPUAddress = upload.CPUAddress;
        return result;
    }

    DynamicUniformAllocator::Allocation DynamicUniformAllocator::CreateUnowned() const
    {
        // フレームの持ち主が無い文脈では、毎回新しいバッファと記述子セットを作る（使い回さないので前の描画を書き換えない）
        Allocation result;
        RHI::BufferDesc bufDesc(m_UBOSize, RHI::ResourceUsage::ConstantBuffer, true, "DynUBO_Unowned");
        result.UniformBuffer = m_Device->CreateBuffer(bufDesc);
        if (!result.UniformBuffer)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "定数のバッファを作れなかった");
            return {};
        }
        result.DescriptorSet = m_Device->CreateDescriptorSet(m_DescriptorSetDesc);
        if (!result.DescriptorSet)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "記述子セットを作れなかった");
            return {};
        }
        result.DescriptorSet->BindConstantBuffer(0, result.UniformBuffer, 0, m_UBOSize);
        result.Size = m_UBOSize;
        return result;
    }

    DynamicUniformAllocator::Allocation DynamicUniformAllocator::Allocate()
    {
        Allocation result{};

        if (!m_bInitialized || m_bBorrowing)
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Not initialized");
            return result;
        }

        if (m_CurrentIndex >= m_MaxSlots && !Grow())
        {
            NORVES_LOG_ERROR("DynamicUniformAllocator", "Out of slots (%u/%u)", m_CurrentIndex, m_MaxSlots);
            return result;
        }

        auto &slot = m_Slots[m_CurrentIndex];
        result.UniformBuffer = slot.UniformBuffer;
        result.DescriptorSet = slot.DescriptorSet;
        result.Size = m_UBOSize;
        result.SlotIndex = m_CurrentIndex;

        ++m_CurrentIndex;
        return result;
    }

} // namespace NorvesLib::Core::Rendering
