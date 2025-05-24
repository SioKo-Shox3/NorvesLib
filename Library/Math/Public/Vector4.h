#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include "VectorUtils.h"
#include "Vector3.h"
#include <cmath>

namespace NorvesLib::Math 
{

/**
 * @brief 4次元ベクトルクラス
 */
struct NORVES_ALIGN(16) Vector4
{
public:
    float x;
    float y;
    float z;
    float w;

    // コンストラクタ
    Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vector4(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}
    explicit Vector4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    Vector4(const Vector3& vec3, float inW) : x(vec3.x), y(vec3.y), z(vec3.z), w(inW) {}

    // 演算子オーバーロード
    Vector4 operator+(const Vector4& other) const 
    {
        return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    Vector4 operator-(const Vector4& other) const 
    {
        return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);
    }

    Vector4 operator*(float scalar) const 
    {
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    Vector4 operator/(float scalar) const 
    {
        float invScalar = 1.0f / scalar;
        return Vector4(x * invScalar, y * invScalar, z * invScalar, w * invScalar);
    }

    Vector4& operator+=(const Vector4& other) 
    {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }

    Vector4& operator-=(const Vector4& other) 
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }

    Vector4& operator*=(float scalar) 
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    Vector4& operator/=(float scalar) 
    {
        float invScalar = 1.0f / scalar;
        x *= invScalar;
        y *= invScalar;
        z *= invScalar;
        w *= invScalar;
        return *this;
    }

    bool operator==(const Vector4& other) const 
    {
        return (x == other.x) && (y == other.y) && (z == other.z) && (w == other.w);
    }

    bool operator!=(const Vector4& other) const 
    {
        return !(*this == other);
    }

    // Vector3への変換（wで割る）
    Vector3 ToVector3() const 
    {
        if (w != 0.0f) 
        {
            float invW = 1.0f / w;
            return Vector3(x * invW, y * invW, z * invW);
        }
        return Vector3(x, y, z);
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

    Vector4 Normalized() const 
    {
        return VectorUtils::Normalize(*this);
    }

    void Normalize() 
    {
        *this = VectorUtils::Normalize(*this);
    }

    // 静的メソッド - VectorUtilsのラッパー
    static float Dot(const Vector4& a, const Vector4& b) 
    {
        return VectorUtils::Dot(a, b);
    }

    static Vector4 Lerp(const Vector4& a, const Vector4& b, float t) 
    {
        return VectorUtils::Lerp(a, b, t);
    }

    // 静的定数
    static const Vector4 Zero;
    static const Vector4 One;
    static const Vector4 UnitX;
    static const Vector4 UnitY;
    static const Vector4 UnitZ;
    static const Vector4 UnitW;
};

// 静的メンバの初期化
inline const Vector4 Vector4::Zero(0.0f, 0.0f, 0.0f, 0.0f);
inline const Vector4 Vector4::One(1.0f, 1.0f, 1.0f, 1.0f);
inline const Vector4 Vector4::UnitX(1.0f, 0.0f, 0.0f, 0.0f);
inline const Vector4 Vector4::UnitY(0.0f, 1.0f, 0.0f, 0.0f);
inline const Vector4 Vector4::UnitZ(0.0f, 0.0f, 1.0f, 0.0f);
inline const Vector4 Vector4::UnitW(0.0f, 0.0f, 0.0f, 1.0f);

// グローバル演算子のオーバーロード
inline Vector4 operator*(float scalar, const Vector4& vec) 
{
    return vec * scalar;
}

} // namespace NorvesLib::Math