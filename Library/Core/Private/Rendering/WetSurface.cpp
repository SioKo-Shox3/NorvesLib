// GLSL と同じ演算順で濡れた面の材質と法線を求める。
#include "Rendering/WetSurface.h"

#include <cmath>

namespace
{
    float Mix(float x, float y, float a)
    {
        return x * (1.0f - a) + y * a;
    }

    NorvesLib::Math::Vector3 Mix(const NorvesLib::Math::Vector3& x, const NorvesLib::Math::Vector3& y, float a)
    {
        return NorvesLib::Math::Vector3(Mix(x.x, y.x, a), Mix(x.y, y.y, a), Mix(x.z, y.z, a));
    }

    float SmoothStep(float edge0, float edge1, float x)
    {
        float t = (x - edge0) / (edge1 - edge0);
        t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }
}

namespace NorvesLib::Core::Rendering
{
    void ApplyWetSurface(Math::Vector3& albedo, float& roughness, float& metallic, Math::Vector3& normal,
                         const Math::Vector3& geomNormal, float cavity, float wetness, float exposure,
                         float response, const WetSurfaceTuning& tuning)
    {
        float w = wetness * exposure * response;
        if (!(w > 0.0f))
        {
            return;
        }
        w = (w < 1.0f) ? w : 1.0f;
        const Math::Vector3 geomN = geomNormal;
        const WetSurfaceTuning t = tuning;

        // 金属度は [0,1] に収め、NaN は 0 とする。
        float m = (metallic >= 0.0f) ? ((metallic < 1.0f) ? metallic : 1.0f) : 0.0f;
        float darken = Mix(t.darkenDielectric, t.darkenMetal, m);
        albedo *= Mix(1.0f, darken, w);

        roughness = Mix(roughness, roughness * t.roughnessScale, w);

        // 凹みは [0,1] に収め、NaN は 0 とする。
        float c = (cavity >= 0.0f) ? ((cavity < 1.0f) ? cavity : 1.0f) : 0.0f;
        float pool = SmoothStep(t.poolStart, t.poolEnd,
                                w * (t.poolCavityBase + t.poolCavityScale * c));
        float up = geomN.y * t.poolUpScale - t.poolUpBias;
        up = (up > 0.0f) ? ((up < 1.0f) ? up : 1.0f) : 0.0f;
        pool *= up;
        if (pool > 0.0f)
        {
            roughness = Mix(roughness, t.poolRoughness, pool);
            Math::Vector3 n = Mix(normal, geomN, pool);
            float lenSq = n.x * n.x + n.y * n.y + n.z * n.z;
            normal = (lenSq > 1e-12f) ? n * (1.0f / std::sqrt(lenSq)) : geomN;
            metallic *= 1.0f - pool;
        }
    }
} // namespace NorvesLib::Core::Rendering
