#pragma once

#include "RHI/ITexture.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 材質へ張るテクスチャのうち1枚でも sparse（VT）なら true
     *
     * 材質のシェーダー（gbuffer.frag・megageometry.frag・forward_transparent.frag）は、これが true のときだけ
     * sparseTexture*ARB で標本し、常駐していないタイルを読まず粗いミップへ逃げる。
     * false のときは従来の texture() のまま（VT でない材質の見た目と費用を変えない）。
     */
    template <typename... TexturePointers>
    inline bool AnySparseTexture(const TexturePointers&... textures)
    {
        return ((static_cast<bool>(textures) && textures->IsSparse()) || ...);
    }
} // namespace NorvesLib::Core::Rendering
