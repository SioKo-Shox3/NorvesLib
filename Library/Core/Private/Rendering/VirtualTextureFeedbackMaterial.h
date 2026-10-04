#pragma once

// 材質のパス（GBuffer・MegaGeometry・Forward）が、VT の要求（フィードバック）を書くシェーダーへ渡すものをそろえる共通部分。
// 要求のバッファの binding は、対応するデバイスでだけ descriptor のレイアウトに足す（シェーダーも NORVES_VT_FEEDBACK のときだけ使う）。

#include "RHI/DeviceCapabilities.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Rendering/RenderResources.h"
#include "Rendering/VirtualTextureRequestSet.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief デバイスの材質シェーダーが要求のバッファを使うか（descriptor のレイアウトに binding を足すか） */
    inline bool UsesVirtualTextureFeedbackBinding(const RHI::IDevice *device)
    {
        return device != nullptr && device->GetCapabilities().SupportsVirtualTextureFeedback();
    }

    /** @brief 要求のバッファの binding（書き込み可の storage buffer。フラグメント段）をレイアウトへ足す */
    inline void AddVirtualTextureFeedbackBinding(RHI::DescriptorSetDesc &desc, uint32_t bindingIndex)
    {
        RHI::DescriptorBinding binding;
        binding.binding = bindingIndex;
        binding.type = RHI::ResourceBindType::RWBuffer;
        binding.stages = RHI::ShaderStage::Pixel;
        desc.bindings.push_back(binding);
    }

    /** @brief 今のフレームの書き込み先（または代替）を descriptor に束ねる。対応しないデバイス（Buffer が null）では何もしない */
    inline void BindVirtualTextureFeedback(RHI::IDescriptorSet &descriptorSet,
                                           uint32_t bindingIndex,
                                           const TextureResources::VirtualTextureFeedbackTarget &target)
    {
        if (target.Buffer)
        {
            descriptorSet.BindStorageBuffer(bindingIndex, target.Buffer, 0, static_cast<uint32_t>(target.Bytes));
        }
    }

    /**
     * @brief 材質の UBO に載せる、VT のフィードバックのパラメータ（シェーダーの WriteVirtualTextureFeedback の param）
     *
     * 材質のテクスチャ 1 枚（アルベド・法線・ORM・高さ。1 枚ごとに呼ぶ）が VT（sparse）で、このフレームの要求のバッファが
     * あるときだけ 0 でない値を返す。0 のときシェーダーは何も書かない。要求のテクスチャの番号はそのテクスチャの VT の表の添字、
     * タイルの大きさはそのテクスチャの sparse の標準ブロック形状。
     */
    inline uint32_t ResolveVirtualTextureFeedbackParam(const TextureResources *textures,
                                                       TextureHandle textureHandle,
                                                       const RHI::ITexture *rhiTexture,
                                                       const TextureResources::VirtualTextureFeedbackTarget &target)
    {
        if (textures == nullptr || !target.bWriting || !textureHandle.IsValid() || rhiTexture == nullptr ||
            !rhiTexture->IsSparse())
        {
            return 0;
        }
        uint32_t index = 0;
        RHI::SparseTextureInfo info;
        if (!textures->TryGetVirtualTextureIndex(textureHandle, index) || !rhiTexture->GetSparseInfo(info))
        {
            return 0;
        }
        return VirtualTextureFeedback::PackMaterialParam(index, info.TileWidth, info.TileHeight, target.Frame);
    }
} // namespace NorvesLib::Core::Rendering
