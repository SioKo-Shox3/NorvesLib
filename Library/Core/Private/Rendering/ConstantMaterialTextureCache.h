#pragma once
#include "Container/Containers.h"
#include "RHI/IDevice.h"
namespace NorvesLib::Core::Rendering
{
    // pass所有・単一thread用。deviceはcacheより長寿命。device破棄前にClearする。
    class ConstantMaterialTextureCache
    {
      public:
        [[nodiscard]] RHI::TexturePtr GetOrCreate(RHI::IDevice* device, float value);
        void Clear();

      private:
        RHI::IDevice* m_Device = nullptr;
        Container::UnorderedMap<uint32_t, RHI::TexturePtr> m_Textures;
    };
} // namespace NorvesLib::Core::Rendering
