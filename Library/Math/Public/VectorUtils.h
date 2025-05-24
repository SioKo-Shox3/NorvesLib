#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include <cmath>
#include <type_traits>
#include <concepts>

namespace NorvesLib::Math
{

    // ベクトル型制約のためのコンセプト
    template <typename T>
    concept FloatingPointType = std::is_floating_point_v<T>;

    template <typename T>
    concept HasX = requires(T t) {
        t.x;
        requires std::is_floating_point_v<decltype(t.x)>;
    };

    template <typename T>
    concept HasXY = HasX<T> && requires(T t) {
        t.y;
        requires std::is_floating_point_v<decltype(t.y)>;
    };

    template <typename T>
    concept HasXYZ = HasXY<T> && requires(T t) {
        t.z;
        requires std::is_floating_point_v<decltype(t.z)>;
    };

    template <typename T>
    concept HasXYZW = HasXYZ<T> && requires(T t) {
        t.w;
        requires std::is_floating_point_v<decltype(t.w)>;
    };

    // 浮動小数点ベクトル型の定義
    template <typename T>
    concept FloatVector = HasX<T>;

    template <typename T>
    concept FloatVector2D = HasXY<T>;

    template <typename T>
    concept FloatVector3D = HasXYZ<T>;

    template <typename T>
    concept FloatVector4D = HasXYZW<T>;

    /**
     * @brief テンプレート化されたベクトル演算のためのユーティリティ関数
     */
    class VectorUtils
    {
    public:
        // ベクトルの長さを計算
        template <FloatVector VectorT>
        static auto Length(const VectorT &v)
        {
            return std::sqrt(LengthSquared(v));
        }

        // ベクトルの長さの二乗を計算
        template <FloatVector VectorT>
        static auto LengthSquared(const VectorT &v)
        {
            return Dot(v, v);
        }

        // ベクトルの正規化
        template <FloatVector VectorT>
        static auto Normalize(const VectorT &v)
        {
            auto len = Length(v);
            if (len > Constants::EPSILON)
            {
                auto invLen = static_cast<decltype(len)>(1.0) / len;
                return v * invLen;
            }
            return v; // 長さゼロのベクトルは変更しない
        }

        // 2次元ベクトルの内積
        template <FloatVector2D VectorT>
        static auto Dot(const VectorT &a, const VectorT &b)
        {
            return a.x * b.x + a.y * b.y;
        }

        // 3次元ベクトルの内積
        template <FloatVector3D VectorT>
        static auto Dot(const VectorT &a, const VectorT &b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        // 4次元ベクトルの内積
        template <FloatVector4D VectorT>
        static auto Dot(const VectorT &a, const VectorT &b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        }

        // 3次元ベクトルの外積
        template <FloatVector3D VectorT>
        static auto Cross(const VectorT &a, const VectorT &b)
        {
            return VectorT(
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
        }

        // ベクトル間の線形補間
        template <FloatVector VectorT>
        static auto Lerp(const VectorT &a, const VectorT &b, decltype(VectorT::x) t)
        {
            return a + (b - a) * t;
        }

        // 2つのベクトル間の距離
        template <FloatVector VectorT>
        static auto Distance(const VectorT &a, const VectorT &b)
        {
            return Length(b - a);
        }

        // 2つのベクトル間の距離の二乗
        template <FloatVector VectorT>
        static auto DistanceSquared(const VectorT &a, const VectorT &b)
        {
            return LengthSquared(b - a);
        }

        // ベクトルの反射
        template <FloatVector VectorT>
        static auto Reflect(const VectorT &incident, const VectorT &normal)
        {
            auto dot2 = static_cast<decltype(incident.x)>(2.0) * Dot(incident, normal);
            return incident - (normal * dot2);
        }

        // ベクトルの屈折
        template <FloatVector VectorT>
        static auto Refract(const VectorT &incident, const VectorT &normal, decltype(VectorT::x) ior)
        {
            auto dot = Dot(incident, normal);
            auto k = static_cast<decltype(ior)>(1.0) - ior * ior * (static_cast<decltype(ior)>(1.0) - dot * dot);

            if (k < static_cast<decltype(k)>(0.0))
            {
                return VectorT(); // 全反射
            }

            auto factor = ior * dot + std::sqrt(k);
            return incident * ior - normal * factor;
        }

        // 成分ごとの最小値
        template <FloatVector VectorT>
        static auto Min(const VectorT &a, const VectorT &b)
        {
            using ScalarT = decltype(a.x);

            if constexpr (FloatVector2D<VectorT>)
            {
                return VectorT(
                    std::min<ScalarT>(a.x, b.x),
                    std::min<ScalarT>(a.y, b.y));
            }
            else if constexpr (FloatVector3D<VectorT>)
            {
                return VectorT(
                    std::min<ScalarT>(a.x, b.x),
                    std::min<ScalarT>(a.y, b.y),
                    std::min<ScalarT>(a.z, b.z));
            }
            else if constexpr (FloatVector4D<VectorT>)
            {
                return VectorT(
                    std::min<ScalarT>(a.x, b.x),
                    std::min<ScalarT>(a.y, b.y),
                    std::min<ScalarT>(a.z, b.z),
                    std::min<ScalarT>(a.w, b.w));
            }
        }

        // 成分ごとの最大値
        template <FloatVector VectorT>
        static auto Max(const VectorT &a, const VectorT &b)
        {
            using ScalarT = decltype(a.x);

            if constexpr (FloatVector2D<VectorT>)
            {
                return VectorT(
                    std::max<ScalarT>(a.x, b.x),
                    std::max<ScalarT>(a.y, b.y));
            }
            else if constexpr (FloatVector3D<VectorT>)
            {
                return VectorT(
                    std::max<ScalarT>(a.x, b.x),
                    std::max<ScalarT>(a.y, b.y),
                    std::max<ScalarT>(a.z, b.z));
            }
            else if constexpr (FloatVector4D<VectorT>)
            {
                return VectorT(
                    std::max<ScalarT>(a.x, b.x),
                    std::max<ScalarT>(a.y, b.y),
                    std::max<ScalarT>(a.z, b.z),
                    std::max<ScalarT>(a.w, b.w));
            }
        }

        // ベクトルの値をクランプ
        template <FloatVector VectorT>
        static auto Clamp(const VectorT &v, const VectorT &min, const VectorT &max)
        {
            return Max(Min(v, max), min);
        }
    };

} // namespace NorvesLib::Math