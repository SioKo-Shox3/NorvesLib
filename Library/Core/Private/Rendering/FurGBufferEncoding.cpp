// Common/FurShading.glsl と同じ式で毛の流れの角を符号化・復号する。
#include "FurGBufferEncoding.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    void FurFlowBasis(const Math::Vector3& n, Math::Vector3& outB1, Math::Vector3& outB2)
    {
        // z = 0 を境に基底の符号が反転する継ぎ目がある。書き込みと読み出しで同じ法線を使う。
        const float signZ = n.z >= 0.0f ? 1.0f : -1.0f;
        const float a = -1.0f / (signZ + n.z);
        const float b = n.x * n.y * a;
        outB1 = Math::Vector3(1.0f + signZ * n.x * n.x * a, signZ * b, -signZ * n.x);
        outB2 = Math::Vector3(b, signZ + n.y * n.y * a, -n.y);
    }

    float FurEncodeFlowAngle(const Math::Vector3& n, const Math::Vector3& t)
    {
        const Math::Vector3 projected = t - n * Math::Vector3::Dot(n, t);
        if (Math::Vector3::Dot(projected, projected) <= 1.0e-12f)
        {
            return 0.0f;
        }
        Math::Vector3 b1;
        Math::Vector3 b2;
        FurFlowBasis(n, b1, b2);
        const float angle = std::atan2(Math::Vector3::Dot(projected, b2), Math::Vector3::Dot(projected, b1));
        const float normalizedAngle = angle / FurFlowTwoPi;
        // GLSL の fract(x) と同じく x - floor(x) で負の角も折り返す。
        const float fraction = normalizedAngle - std::floor(normalizedAngle);
        return std::min(fraction, 1.0f - 1.0f / 2048.0f);
    }

    Math::Vector3 FurDecodeFlowTangent(const Math::Vector3& n, float p1)
    {
        Math::Vector3 b1;
        Math::Vector3 b2;
        FurFlowBasis(n, b1, b2);
        const float angle = (p1 - std::floor(p1)) * FurFlowTwoPi;
        return std::cos(angle) * b1 + std::sin(angle) * b2;
    }
} // namespace NorvesLib::Core::Rendering
