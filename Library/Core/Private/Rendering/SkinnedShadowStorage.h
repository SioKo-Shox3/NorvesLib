// 点光源影のスキン用storage binding。previousのbindingは作らない。
#pragma once
#include "Rendering/SkinnedMeshTypes.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
namespace NorvesLib::Core::Rendering
{
    inline bool BindSkinnedShadowStorage(const SkinnedMeshPreparedDraw& prepared, RHI::IDescriptorSet* descriptor)
    {
        if (!descriptor || !prepared.IsValid() || prepared.bUsesPreviousPalette || prepared.PreviousPaletteBuffer ||
            prepared.PaletteBuffer->GetSize() > UINT32_MAX || prepared.VertexBuffer->GetSize() > UINT32_MAX)
        {
            return false;
        }
        descriptor->BindStorageBuffer(8,prepared.PaletteBuffer,0,static_cast<uint32_t>(prepared.PaletteBuffer->GetSize()));
        descriptor->BindStorageBuffer(9,prepared.VertexBuffer,0,static_cast<uint32_t>(prepared.VertexBuffer->GetSize()));
        descriptor->Update();
        return true;
    }
}
