#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include "Quaternion.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include "VectorUtils.h"
#include <cmath>
#include <type_traits>

namespace NorvesLib::Math 
{

/**
 * @brief クォータニオン演算のためのユーティリティ関数
 */
class QuaternionUtils
{
public:
    // クォータニオンの長さを計算
    static float Length(const Quaternion& q)
    {
        return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    }

    // クォータニオンの長さの二乗を計算
    static float LengthSquared(const Quaternion& q)
    {
        return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    }

    // クォータニオンの正規化
    static Quaternion Normalize(const Quaternion& q)
    {
        float len = Length(q);
        if (len > Constants::EPSILON)
        {
            float invLen = 1.0f / len;
            return Quaternion(
                q.x * invLen,
                q.y * invLen,
                q.z * invLen,
                q.w * invLen
            );
        }
        return Quaternion(); // 単位クォータニオンを返す
    }

    // クォータニオンの共役（複素共役）
    static Quaternion Conjugate(const Quaternion& q)
    {
        return Quaternion(-q.x, -q.y, -q.z, q.w);
    }

    // クォータニオンの逆数
    static Quaternion Inverse(const Quaternion& q)
    {
        float lenSquared = LengthSquared(q);
        if (lenSquared > Constants::EPSILON)
        {
            float invLenSquared = 1.0f / lenSquared;
            return Quaternion(
                -q.x * invLenSquared,
                -q.y * invLenSquared,
                -q.z * invLenSquared,
                q.w * invLenSquared
            );
        }
        return Quaternion(); // 単位クォータニオンを返す
    }

    // オイラー角への変換（ラジアン）
    static Vector3 ToEulerAngles(const Quaternion& q)
    {
        Vector3 angles;
        
        // ロール (x軸周りの回転)
        float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
        float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        angles.x = std::atan2(sinr_cosp, cosr_cosp);
        
        // ピッチ (y軸周りの回転)
        float sinp = 2.0f * (q.w * q.y - q.z * q.x);
        if (std::abs(sinp) >= 1.0f)
        {
            angles.y = std::copysign(Constants::PI / 2.0f, sinp); // 90度
        }
        else
        {
            angles.y = std::asin(sinp);
        }
        
        // ヨー (z軸周りの回転)
        float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
        float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        angles.z = std::atan2(siny_cosp, cosy_cosp);
        
        return angles;
    }

    // オイラー角からクォータニオンを生成（ラジアン）
    static Quaternion FromEulerAngles(const Vector3& angles)
    {
        float cy = std::cos(angles.z * 0.5f);
        float sy = std::sin(angles.z * 0.5f);
        float cp = std::cos(angles.y * 0.5f);
        float sp = std::sin(angles.y * 0.5f);
        float cr = std::cos(angles.x * 0.5f);
        float sr = std::sin(angles.x * 0.5f);

        return Quaternion(
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy
        );
    }

    // 回転行列からクォータニオンを生成
    static Quaternion FromRotationMatrix(const Matrix4x4& m)
    {
        float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
        
        if (trace > 0.0f)
        {
            float s = 0.5f / std::sqrt(trace + 1.0f);
            return Quaternion(
                (m.m[2][1] - m.m[1][2]) * s,
                (m.m[0][2] - m.m[2][0]) * s,
                (m.m[1][0] - m.m[0][1]) * s,
                0.25f / s
            );
        }
        else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2])
        {
            float s = 2.0f * std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]);
            return Quaternion(
                0.25f * s,
                (m.m[0][1] + m.m[1][0]) / s,
                (m.m[0][2] + m.m[2][0]) / s,
                (m.m[2][1] - m.m[1][2]) / s
            );
        }
        else if (m.m[1][1] > m.m[2][2])
        {
            float s = 2.0f * std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]);
            return Quaternion(
                (m.m[0][1] + m.m[1][0]) / s,
                0.25f * s,
                (m.m[1][2] + m.m[2][1]) / s,
                (m.m[0][2] - m.m[2][0]) / s
            );
        }
        else
        {
            float s = 2.0f * std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]);
            return Quaternion(
                (m.m[0][2] + m.m[2][0]) / s,
                (m.m[1][2] + m.m[2][1]) / s,
                0.25f * s,
                (m.m[1][0] - m.m[0][1]) / s
            );
        }
    }

    // 2つのベクトル間の回転を表すクォータニオンを生成
    static Quaternion FromToRotation(const Vector3& from, const Vector3& to)
    {
        Vector3 normalizedFrom = VectorUtils::Normalize(from);
        Vector3 normalizedTo = VectorUtils::Normalize(to);
        
        float cosTheta = VectorUtils::Dot(normalizedFrom, normalizedTo);
        
        // ベクトルが平行の場合
        if (cosTheta > 0.99999f)
        {
            return Quaternion(); // 単位クォータニオン（回転なし）
        }
        
        // ベクトルが反対方向の場合
        if (cosTheta < -0.99999f)
        {
            // 任意の垂直軸を見つける
            Vector3 axis = VectorUtils::Cross(Vector3::UnitX, normalizedFrom);
            if (VectorUtils::LengthSquared(axis) < 0.00001f)
            {
                axis = VectorUtils::Cross(Vector3::UnitY, normalizedFrom);
            }
            axis = VectorUtils::Normalize(axis);
            return Quaternion(axis, Constants::PI);
        }
        
        // 通常のケース
        Vector3 axis = VectorUtils::Cross(normalizedFrom, normalizedTo);
        float s = std::sqrt((1.0f + cosTheta) * 2.0f);
        float invs = 1.0f / s;
        
        return Normalize(Quaternion(
            axis.x * invs,
            axis.y * invs,
            axis.z * invs,
            s * 0.5f
        ));
    }

    // 球面線形補間
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t)
    {
        // 最短経路の確保
        float cosHalfTheta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        
        // 必要に応じて反転
        Quaternion end = b;
        if (cosHalfTheta < 0.0f)
        {
            end.x = -end.x;
            end.y = -end.y;
            end.z = -end.z;
            end.w = -end.w;
            cosHalfTheta = -cosHalfTheta;
        }
        
        // クォータニオンが非常に近い場合は線形補間
        if (cosHalfTheta > 0.99999f)
        {
            return Normalize(Quaternion(
                a.x + t * (end.x - a.x),
                a.y + t * (end.y - a.y),
                a.z + t * (end.z - a.z),
                a.w + t * (end.w - a.w)
            ));
        }
        
        // 球面線形補間の実行
        float halfTheta = std::acos(cosHalfTheta);
        float sinHalfTheta = std::sqrt(1.0f - cosHalfTheta * cosHalfTheta);
        
        if (std::abs(sinHalfTheta) < 0.001f)
        {
            // 角度がゼロに近い場合は線形補間
            return Normalize(Quaternion(
                a.x * 0.5f + end.x * 0.5f,
                a.y * 0.5f + end.y * 0.5f,
                a.z * 0.5f + end.z * 0.5f,
                a.w * 0.5f + end.w * 0.5f
            ));
        }
        
        float ratioA = std::sin((1.0f - t) * halfTheta) / sinHalfTheta;
        float ratioB = std::sin(t * halfTheta) / sinHalfTheta;
        
        return Quaternion(
            a.x * ratioA + end.x * ratioB,
            a.y * ratioA + end.y * ratioB,
            a.z * ratioA + end.z * ratioB,
            a.w * ratioA + end.w * ratioB
        );
    }

    // 線形補間
    static Quaternion Lerp(const Quaternion& a, const Quaternion& b, float t)
    {
        return Normalize(Quaternion(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        ));
    }

    // 軸と角度からクォータニオンを生成
    static Quaternion FromAxisAngle(const Vector3& axis, float angleRadians)
    {
        float halfAngle = angleRadians * 0.5f;
        float sinHalfAngle = std::sin(halfAngle);
        
        Vector3 normalizedAxis = VectorUtils::Normalize(axis);
        return Quaternion(
            normalizedAxis.x * sinHalfAngle,
            normalizedAxis.y * sinHalfAngle,
            normalizedAxis.z * sinHalfAngle,
            std::cos(halfAngle)
        );
    }

    // オブジェクトの前方向から回転を構築
    static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3::Up)
    {
        Vector3 normalizedForward = VectorUtils::Normalize(forward);
        
        // 前方向と上方向が平行でないことを確認
        if (VectorUtils::LengthSquared(normalizedForward) < Constants::EPSILON)
        {
            return Quaternion();  // デフォルト（単位クォータニオン）
        }
        
        // 前方軸、右軸、上軸から回転行列を構築
        Vector3 normalizedRight = VectorUtils::Normalize(VectorUtils::Cross(up, normalizedForward));
        Vector3 normalizedUp = VectorUtils::Cross(normalizedForward, normalizedRight);
        
        Matrix4x4 rotMatrix(
            normalizedRight.x, normalizedUp.x, normalizedForward.x, 0.0f,
            normalizedRight.y, normalizedUp.y, normalizedForward.y, 0.0f,
            normalizedRight.z, normalizedUp.z, normalizedForward.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        
        return FromRotationMatrix(rotMatrix);
    }

    // 2つのクォータニオンの内積
    static float Dot(const Quaternion& a, const Quaternion& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    // クォータニオンの角度を取得（ラジアン）
    static float Angle(const Quaternion& a, const Quaternion& b)
    {
        float dot = std::min(std::abs(Dot(a, b)), 1.0f);
        return 2.0f * std::acos(dot);
    }

    // 指定した方向を向くクォータニオンを生成
    static Quaternion RotateTowards(const Quaternion& from, const Quaternion& to, float maxDegreesDelta)
    {
        float angle = Angle(from, to);
        
        if (angle == 0.0f)
        {
            return to;
        }
        
        float t = std::min(1.0f, maxDegreesDelta / angle);
        return Slerp(from, to, t);
    }
};

} // namespace NorvesLib::Math
