// 正距円筒の放射輝度から IBL 用の資源を生成する。
#pragma once

#include "Container/VariableArray.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t IblWorkingWidth = 256u;
    inline constexpr uint32_t IblWorkingHeight = 128u;
    inline constexpr uint32_t IblDiffuseWidth = 64u;
    inline constexpr uint32_t IblDiffuseHeight = 32u;
    inline constexpr uint32_t IblPrefilterMipCount = 9u;

    /// 正距円筒の線形放射輝度（RGBA32F、行 y = 0 が天頂）から IBL 資源を生成する。
    /// 出力は RGBA16F の環境（入力と同じ大きさ、1 ミップ）、
    /// 拡散照度（IblDiffuseWidth × IblDiffuseHeight、余弦で重み付けした全球の積分）、
    /// 鏡面の前フィルタ（IblWorkingWidth × IblWorkingHeight、IblPrefilterMipCount 段、GGX）、
    /// 強度を掛ける前の全球の平均照度である。
    /// 一定の入力では、拡散が π × 放射輝度、鏡面が放射輝度となることを照合する。
    /// 失敗した場合は出力を空、平均照度を 0 にして false を返す。
    /// ITexture::Update で同期的にアップロードするため、RenderThread でコマンド記録の外から呼ぶ。
    bool CreateIblResources(RHI::IDevice* device,
                            const Container::VariableArray<float>& sourceData,
                            uint32_t sourceWidth,
                            uint32_t sourceHeight,
                            const char* environmentName,
                            const char* diffuseName,
                            const char* prefilterName,
                            RHI::TexturePtr& outEnvironment,
                            RHI::TexturePtr& outDiffuse,
                            RHI::TexturePtr& outPrefilter,
                            float outAverageIrradiance[3]);
}
