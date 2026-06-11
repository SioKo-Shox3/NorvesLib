#include "Platform/PlatformGraphics.h"

namespace NorvesLib::Core::Platform
{

    RHI::GraphicsAPI GetDefaultGraphicsAPI()
    {
        // Windows プラットフォームの既定バックエンドは Vulkan
        return RHI::GraphicsAPI::Vulkan;
    }

    bool IsGraphicsAPISupported(RHI::GraphicsAPI api)
    {
        // 現時点で Windows が対応しているバックエンドは Vulkan のみ
        return api == RHI::GraphicsAPI::Vulkan;
    }

} // namespace NorvesLib::Core::Platform
