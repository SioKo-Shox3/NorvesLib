#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include <cmath>

namespace NorvesLib::Math 
{

// 前方宣言
struct Matrix4x4;

/**
 * @brief 四元数クラス（回転表現用）
 */
struct NORVES_ALIGN(16) Quaternion
{
    float x;
    float y;
    float z;
    float w;

    // コンストラクタ
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}  // 単位四元数（回転なし）
    Quaternion(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}
    
    // Vector3 + 回転角からの構築（軸角表現）
    explicit Quaternion(const Vector3& axis, float angleRadians)
    {
        float halfAngle = angleRadians * 0.5f;
        float sinHalfAngle = std::sin(halfAngle);
        
        Vector3 normalizedAxis = axis.Normalized();
        x = normalizedAxis.x * sinHalfAngle;
        y = normalizedAxis.y * sinHalfAngle;
        z = normalizedAxis.z * sinHalfAngle;
        w = std::cos(halfAngle);
    }

    // Vector4からの構築
    explicit Quaternion(const Vector4& vec) : x(vec.x), y(vec.y), z(vec.z), w(vec.w) {}

    // 操作
    Quaternion operator*(const Quaternion& rhs) const
    {
        return Quaternion(
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
            w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z
        );
    }

    Quaternion& operator*=(const Quaternion& rhs)
    {
        *this = *this * rhs;
        return *this;
    }

    Vector3 operator*(const Vector3& vector) const
    {
        // クォータニオンによるベクトルの回転
        Quaternion vectorQuat(vector.x, vector.y, vector.z, 0.0f);
        Quaternion conjugate(-x, -y, -z, w);
        Quaternion rotated = *this * vectorQuat * conjugate;
        
        return Vector3(rotated.x, rotated.y, rotated.z);
    }

    bool operator==(const Quaternion& rhs) const
    {
        return (x == rhs.x) && (y == rhs.y) && (z == rhs.z) && (w == rhs.w);
    }

    bool operator!=(const Quaternion& rhs) const
    {
        return !(*this == rhs);
    }

    // 静的定数
    static const Quaternion Identity;
};

// 静的メンバの初期化
inline const Quaternion Quaternion::Identity(0.0f, 0.0f, 0.0f, 1.0f);

} // namespace NorvesLib::Math