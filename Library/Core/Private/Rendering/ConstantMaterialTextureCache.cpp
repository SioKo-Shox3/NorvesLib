#include "Rendering/ConstantMaterialTextureCache.h"
#include "RHI/ITexture.h"
#include <cmath>
namespace NorvesLib::Core::Rendering
{
    RHI::TexturePtr ConstantMaterialTextureCache::GetOrCreate(RHI::IDevice* device, float value)
    {
        if (!device || !std::isfinite(value))
        {
            return nullptr;
        }
        if (m_Device != device)
        {
            Clear();
            m_Device = device;
        }
        const float clamped = value < 0 ? 0 : (value > 1 ? 1 : value);
        const auto level = static_cast<uint32_t>(clamped * 255.0f + 0.5f);
        const auto found = m_Textures.find(level);
        if (found != m_Textures.end())
        {
            return found->second;
        }
        RHI::TextureDesc desc;
        desc.Width = 1;
        desc.Height = 1;
        desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.Usage = RHI::ResourceUsage::ShaderRead;
        desc.DebugName = "MegaMaterialConstantGray1x1";
        auto texture = device->CreateTexture(desc);
        if (!texture)
        {
            return nullptr;
        }
        const auto gray = static_cast<uint8_t>(level);
        const uint8_t pixel[] = {gray, gray, gray, 255};
        texture->Update(pixel, sizeof(pixel), sizeof(pixel));
        m_Textures.emplace(level, texture);
        return texture;
    }
    void ConstantMaterialTextureCache::Clear()
    {
        m_Textures.clear();
        m_Device = nullptr;
    }
} // namespace NorvesLib::Core::Rendering
