#pragma once

#include "Rendering/RenderTypes.h"
#include "Rendering/ImpostorMath.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// GBuffer へ書くインポスターの束。同じアトラスの組を使う、InstanceData の連続した個体の範囲。
    struct ImpostorBatch
    {
        TextureHandle AlbedoAtlas = TextureHandle::Invalid(); // 色と被覆（A）。必須
        TextureHandle NormalAtlas = TextureHandle::Invalid(); // オブジェクト空間の法線。無効なら視線へ向く法線
        TextureHandle OrmAtlas = TextureHandle::Invalid();    // R=AO、G=粗さ、B=金属度。無効なら既定の材質
        TextureHandle DepthAtlas = TextureHandle::Invalid();  // 0 = 見る人の側、1 = 奥。無効なら板の面の深度
        ImpostorOctahedralLayout Layout = ImpostorOctahedralLayout::HemiOctahedral;
        uint32_t InstanceDataOffset = 0; // FramePacket::InstanceData の先頭の添字
        uint32_t InstanceCount = 0;
    };
} // namespace NorvesLib::Core::Rendering
