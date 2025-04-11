#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include "VectorUtils.h"
#include <cmath>

namespace NorvesLib::Math 
{

/**
 * @brief 3次元ベクトルクラス
 */
struct NORVES_ALIGN(16) Vector3
{
public:
    float x;
    float y;
    float z;

    // コンストラクタ
    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vector3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}
    explicit Vector3(float scalar) : x(scalar), y(scalar), z(scalar) {}

    // 演算子オーバーロード
    Vector3 operator+(const Vector3& other) const 
    {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    Vector3 operator-(const Vector3& other) const 
    {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    Vector3 operator*(float scalar) const 
    {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }

    Vector3 operator/(float scalar) const 
    {
        float invScalar = 1.0f / scalar;
        return Vector3(x * invScalar, y * invScalar, z * invScalar);
    }

    Vector3& operator+=(const Vector3& other) 
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vector3& operator-=(const Vector3& other) 
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vector3& operator*=(float scalar) 
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vector3& operator/=(float scalar) 
    {
        float invScalar = 1.0f / scalar;
        x *= invScalar;
        y *= invScalar;
        z *= invScalar;
        return *this;
    }

    bool operator==(const Vector3& other) const 
    {
        return (x == other.x) && (y == other.y) && (z == other.z);
    }

    bool operator!=(const Vector3& other) const 
    {
        return !(*this == other);
    }

    // ユーティリティ関数
    float Length() const 
    {
        return VectorUtils::Length(*this);
    }

    float LengthSquared() const 
    {
        return VectorUtils::LengthSquared(*this);
    }

    Vector3 Normalized() const 
    {
        return VectorUtils::Normalize(*this);
    }

    void Normalize() 
    {
        *this = VectorUtils::Normalize(*this);
    }

    // 静的メソッド - VectorUtilsのラッパー
    static float Dot(const Vector3& a, const Vector3& b) 
    {
        return VectorUtils::Dot(a, b);
    }

    static Vector3 Cross(const Vector3& a, const Vector3& b) 
    {
        return VectorUtils::Cross(a, b);
    }

    static Vector3 Lerp(const Vector3& a, const Vector3& b, float t) 
    {
        return VectorUtils::Lerp(a, b, t);
    }

    // 静的定数
    static const Vector3 Zero;
    static const Vector3 One;
    static const Vector3 UnitX;
    static const Vector3 UnitY;
    static const Vector3 UnitZ;
    static const Vector3 Right;
    static const Vector3 Left;
    static const Vector3 Up;
    static const Vector3 Down;
    static const Vector3 Forward;
    static const Vector3 Backward;
};

// 静的メンバの初期化
inline const Vector3 Vector3::Zero(0.0f, 0.0f, 0.0f);
inline const Vector3 Vector3::One(1.0f, 1.0f, 1.0f);
inline const Vector3 Vector3::UnitX(1.0f, 0.0f, 0.0f);
inline const Vector3 Vector3::UnitY(0.0f, 1.0f, 0.0f);
inline const Vector3 Vector3::UnitZ(0.0f, 0.0f, 1.0f);
inline const Vector3 Vector3::Right(1.0f, 0.0f, 0.0f);
inline const Vector3 Vector3::Left(-1.0f, 0.0f, 0.0f);
inline const Vector3 Vector3::Up(0.0f, 1.0f, 0.0f);
inline const Vector3 Vector3::Down(0.0f, -1.0f, 0.0f);
inline const Vector3 Vector3::Forward(0.0f, 0.0f, 1.0f);
inline const Vector3 Vector3::Backward(0.0f, 0.0f, -1.0f);

// グローバル演算子のオーバーロード
inline Vector3 operator*(float scalar, const Vector3& vec) 
{
    return vec * scalar;
}

} // namespace NorvesLib::Math