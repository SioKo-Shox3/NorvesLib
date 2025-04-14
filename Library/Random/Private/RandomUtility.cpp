#include "RandomUtility.h"
#include <cmath>

namespace NorvesLib::Random
{

Vector2 GetRandomVector2(float minX, float maxX, float minY, float maxY)
{
    return Vector2(
        GetRandomFloat(minX, maxX),
        GetRandomFloat(minY, maxY)
    );
}

Vector3 GetRandomVector3(float minX, float maxX, float minY, float maxY, float minZ, float maxZ)
{
    return Vector3(
        GetRandomFloat(minX, maxX),
        GetRandomFloat(minY, maxY),
        GetRandomFloat(minZ, maxZ)
    );
}

Vector2 GetRandomUnitVector2()
{
    // 単位円上のランダムな点を生成（角度を使用）
    float angle = GetRandomAngle();
    return Vector2(
        std::cos(angle),
        std::sin(angle)
    );
}

Vector3 GetRandomUnitVector3()
{
    // 単位球上のランダムな点を生成（球面座標を使用）
    float phi = GetRandomAngle();
    float cosTheta = GetRandomFloat(-1.0f, 1.0f);
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    
    return Vector3(
        sinTheta * std::cos(phi),
        sinTheta * std::sin(phi),
        cosTheta
    );
}

float GetRandomAngle(float min, float max)
{
    return GetRandomFloat(min, max);
}

} // namespace NorvesLib::Random