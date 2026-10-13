#pragma once

#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief パスだけが持つ Texture の layout を、状態を明示したバリアで移し、共有の layout も書く
     *
     * パスだけが持つ Texture は、グラフに取り込まず、同時に記録するほかのパスが触らないもの（履歴・中間・アトラスなど。
     * 公開して後のパスが読む物は、読むパスがこのパスの記録の後に記録されること）。
     * 宣言から layout を決める区間のリストは共有の layout を書かない（SetSharedTextureLayoutWrites(false)）ので、
     * TextureBarrier の後に CommitTextureLayout で書き戻す（記述子の書き込みは共有の layout を読む）。
     * ほかのスレッドはこの Texture の layout を読まないので、並列の記録のワーカーから呼んでよい。
     * TextureBarrier と同じく、全体を覆う遷移だけ共有の layout を書く（一部の mip・層の遷移は書かない）。
     */
    inline void TransitionPassOwnedTexture(RHI::ICommandList& commandList,
                                           const RHI::TexturePtr& texture,
                                           RHI::ResourceState beforeState,
                                           RHI::ResourceState afterState,
                                           uint32_t mipLevel = 0,
                                           uint32_t arrayIndex = 0,
                                           uint32_t mipCount = 0,
                                           uint32_t arrayCount = 0)
    {
        if (!texture)
        {
            return;
        }

        commandList.TextureBarrier(texture, beforeState, afterState, mipLevel, arrayIndex, mipCount, arrayCount);
        const uint32_t totalMips = texture->GetMipLevels();
        const uint32_t totalLayers = texture->GetArraySize() * (texture->IsCubemap() ? 6u : 1u);
        const bool bWholeMips = mipLevel == 0 && (mipCount == 0 || mipCount == totalMips);
        const bool bWholeLayers = arrayIndex == 0 && (arrayCount == 0 || arrayCount == totalLayers);
        if (bWholeMips && bWholeLayers)
        {
            commandList.CommitTextureLayout(texture.get(), RHI::ResourceStateToTextureLayout(afterState));
        }
    }

} // namespace NorvesLib::Core::Rendering
