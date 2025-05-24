#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include "VectorUtils.h"
#include <cmath>

namespace NorvesLib::Math
{

    /**
     * @brief 2次元ベクトルクラス
     */
    struct NORVES_ALIGN(8) Vector2
    {
    public:
        float x;
        float y;

        // コンストラクタ
        Vector2() : x(0.0f), y(0.0f) {}
        Vector2(float inX, float inY) : x(inX), y(inY) {}
        explicit Vector2(float scalar) : x(scalar), y(scalar) {}

        // 演算子オーバーロード
        Vector2 operator+(const Vector2 &other) const
        {
            return Vector2(x + other.x, y + other.y);
        }

        Vector2 operator-(const Vector2 &other) const
        {
            return Vector2(x - other.x, y - other.y);
        }

        Vector2 operator*(float scalar) const
        {
            return Vector2(x * scalar, y * scalar);
        }

        Vector2 operator/(float scalar) const
        {
            float invScalar = 1.0f / scalar;
            return Vector2(x * invScalar, y * invScalar);
        }

        Vector2 &operator+=(const Vector2 &other)
        {
            x += other.x;
            y += other.y;
            return *this;
        }

        Vector2 &operator-=(const Vector2 &other)
        {
            x -= other.x;
            y -= other.y;
            return *this;
        }

        Vector2 &operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }

        Vector2 &operator/=(float scalar)
        {
            float invScalar = 1.0f / scalar;
            x *= invScalar;
            y *= invScalar;
            return *this;
        }

        bool operator==(const Vector2 &other) const
        {
            return (x == other.x) && (y == other.y);
        }

        bool operator!=(const Vector2 &other) const
        {
            return !(*this == other);
        }

        // インデックス操作
        float &operator[](int index) { return (&x)[index]; }
        const float &operator[](int index) const { return (&x)[index]; }

        // ユーティリティ関数
        float Length() const
        {
            return VectorUtils::Length(*this);
        }

        float LengthSquared() const
        {
            return VectorUtils::LengthSquared(*this);
        }

        Vector2 Normalized() const
        {
            return VectorUtils::Normalize(*this);
        }

        void Normalize()
        {
            *this = VectorUtils::Normalize(*this);
        }

        // 静的メソッド - VectorUtilsのラッパー
        static float Dot(const Vector2 &a, const Vector2 &b)
        {
            return VectorUtils::Dot(a, b);
        }

        static float Cross(const Vector2 &a, const Vector2 &b)
        {
            return a.x * b.y - a.y * b.x; // 2D外積はスカラー値（Z成分のみ）
        }

        static Vector2 Lerp(const Vector2 &a, const Vector2 &b, float t)
        {
            return VectorUtils::Lerp(a, b, t);
        }

        // 静的定数
        static const Vector2 Zero;
        static const Vector2 One;
        static const Vector2 UnitX;
        static const Vector2 UnitY;
        static const Vector2 Right;
        static const Vector2 Left;
        static const Vector2 Up;
        static const Vector2 Down;
    };

    // 静的メンバの初期化
    inline const Vector2 Vector2::Zero(0.0f, 0.0f);
    inline const Vector2 Vector2::One(1.0f, 1.0f);
    inline const Vector2 Vector2::UnitX(1.0f, 0.0f);
    inline const Vector2 Vector2::UnitY(0.0f, 1.0f);
    inline const Vector2 Vector2::Right(1.0f, 0.0f);
    inline const Vector2 Vector2::Left(-1.0f, 0.0f);
    inline const Vector2 Vector2::Up(0.0f, 1.0f);
    inline const Vector2 Vector2::Down(0.0f, -1.0f);

    // グローバル演算子のオーバーロード
    inline Vector2 operator*(float scalar, const Vector2 &vec)
    {
        return vec * scalar;
    }

} // namespace NorvesLib::Math