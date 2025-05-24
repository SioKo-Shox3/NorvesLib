// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Math\Public\Transform.h
#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include "Vector3.h"
#include "Quaternion.h"
#include "Matrix4x4.h"

namespace NorvesLib::Math 
{

/**
 * @brief トランスフォームクラス（位置、回転、拡縮）
 */
struct NORVES_ALIGN(16) Transform
{
public:
    Vector3 position;      // 位置
    Quaternion rotation;   // 回転
    Vector3 scale;         // 拡縮

    // コンストラクタ
    Transform() 
        : position(Vector3::Zero)
        , rotation(Quaternion::Identity)
        , scale(Vector3::One) 
    {}

    Transform(const Vector3& inPosition, const Quaternion& inRotation, const Vector3& inScale)
        : position(inPosition)
        , rotation(inRotation)
        , scale(inScale)
    {}

    // 位置のみ指定するコンストラクタ
    explicit Transform(const Vector3& inPosition)
        : position(inPosition)
        , rotation(Quaternion::Identity)
        , scale(Vector3::One)
    {}

    // 位置と回転を指定するコンストラクタ
    Transform(const Vector3& inPosition, const Quaternion& inRotation)
        : position(inPosition)
        , rotation(inRotation)
        , scale(Vector3::One)
    {}

    // 演算子オーバーロード
    bool operator==(const Transform& other) const 
    {
        return (position == other.position) && 
               (rotation == other.rotation) && 
               (scale == other.scale);
    }

    bool operator!=(const Transform& other) const 
    {
        return !(*this == other);
    }

    // トランスフォームの合成 (親 * 子)
    Transform operator*(const Transform& other) const 
    {
        Transform result;
        
        // スケールされた子の位置を親の空間に回転させ、親の位置に加算
        result.position = position + rotation * (scale * other.position);
        
        // 回転の合成
        result.rotation = rotation * other.rotation;
        
        // スケールの合成
        result.scale = Vector3(
            scale.x * other.scale.x,
            scale.y * other.scale.y,
            scale.z * other.scale.z
        );
        
        return result;
    }

    // 行列への変換
    Matrix4x4 ToMatrix() const
    {
        // スケーリング行列の要素
        float sx = scale.x;
        float sy = scale.y;
        float sz = scale.z;

        // 回転行列の要素（クォータニオンから計算）
        float xx = rotation.x * rotation.x;
        float xy = rotation.x * rotation.y;
        float xz = rotation.x * rotation.z;
        float xw = rotation.x * rotation.w;
        float yy = rotation.y * rotation.y;
        float yz = rotation.y * rotation.z;
        float yw = rotation.y * rotation.w;
        float zz = rotation.z * rotation.z;
        float zw = rotation.z * rotation.w;

        // クォータニオンからの回転行列要素
        float r00 = 1.0f - 2.0f * (yy + zz);
        float r01 = 2.0f * (xy - zw);
        float r02 = 2.0f * (xz + yw);

        float r10 = 2.0f * (xy + zw);
        float r11 = 1.0f - 2.0f * (xx + zz);
        float r12 = 2.0f * (yz - xw);

        float r20 = 2.0f * (xz - yw);
        float r21 = 2.0f * (yz + xw);
        float r22 = 1.0f - 2.0f * (xx + yy);

        // スケール適用
        r00 *= sx; r01 *= sy; r02 *= sz;
        r10 *= sx; r11 *= sy; r12 *= sz;
        r20 *= sx; r21 *= sy; r22 *= sz;

        // 変換行列の構築
        return Matrix4x4(
            r00, r01, r02, position.x,
            r10, r11, r12, position.y,
            r20, r21, r22, position.z,
            0.0f, 0.0f, 0.0f, 1.0f
        );
    }

    // ローカル空間のベクトルをワールド空間に変換
    Vector3 TransformPoint(const Vector3& point) const 
    {
        return position + rotation * (scale * point);
    }

    // ローカル空間の方向ベクトルをワールド空間に変換（位置の影響を受けない）
    Vector3 TransformDirection(const Vector3& direction) const 
    {
        return rotation * direction;
    }

    // ローカル空間のベクトルをワールド空間に変換（スケールの影響を受けるが、位置の影響を受けない）
    Vector3 TransformVector(const Vector3& vector) const 
    {
        return rotation * (scale * vector);
    }

    // 逆トランスフォームの取得
    Transform Inverse() const 
    {
        // 逆回転
        Quaternion invRotation = Quaternion(-rotation.x, -rotation.y, -rotation.z, rotation.w);
        
        // 逆スケール
        Vector3 invScale(
            scale.x != 0.0f ? 1.0f / scale.x : 0.0f,
            scale.y != 0.0f ? 1.0f / scale.y : 0.0f,
            scale.z != 0.0f ? 1.0f / scale.z : 0.0f
        );
        
        // 逆位置：-(R^-1 * P)
        Vector3 invPosition = invRotation * (position * -1.0f);
        
        return Transform(invPosition, invRotation, invScale);
    }

    // 静的定数
    static const Transform Identity;
};

// 静的メンバの初期化
inline const Transform Transform::Identity(
    Vector3::Zero,
    Quaternion::Identity,
    Vector3::One
);

} // namespace NorvesLib::Math