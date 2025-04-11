#pragma once

namespace NorvesLib::RHI 
{

/**
 * @brief レンダリングAPIの種類を定義する列挙型
 */
enum class API 
{
    None,
    DirectX11,
    DirectX12,
    Vulkan,
    OpenGL
};

} // namespace NorvesLib::RHI