#pragma once

namespace NorvesLib::Math 
{

/**
 * @brief 基本的な数学定数
 */
struct Constants 
{
    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
    static constexpr float HALF_PI = 0.5f * PI;
    static constexpr float EPSILON = 1.192092896e-07f;
};

/**
 * @brief クリップ空間の深度レンジ規約（Vulkan系=ZeroToOne / OpenGL系=NegativeOneToOne）
 */
enum class ClipSpaceDepthRange
{
    ZeroToOne,
    NegativeOneToOne
};

} // namespace NorvesLib::Math
