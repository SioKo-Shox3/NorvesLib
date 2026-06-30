#pragma once

#include "MathForward.h"
#include "MathTypes.h"
#include "Matrix4x4.h"
#include "Quaternion.h"
#include "Vector3.h"
#include "Vector4.h"
#include "VectorUtils.h"
#include <cmath>
#include <cstring>
#include <type_traits>

namespace NorvesLib::Math
{

    struct ClipSpaceSphereBounds
    {
        Vector4 Center;
        Vector4 Extents;
    };

    enum class ClipSpaceFrustumPlane
    {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far,
        Count
    };

    struct ClipSpaceFrustumPlanes
    {
        Vector4 Planes[6];

        const Vector4 &Get(ClipSpaceFrustumPlane plane) const
        {
            return Planes[static_cast<int>(plane)];
        }

        void CopyToShaderData(float outPlanes[6][4]) const
        {
            for (int i = 0; i < 6; ++i)
            {
                outPlanes[i][0] = Planes[i].x;
                outPlanes[i][1] = Planes[i].y;
                outPlanes[i][2] = Planes[i].z;
                outPlanes[i][3] = Planes[i].w;
            }
        }
    };

    /**
     * @brief テンプレート化された行列ユーティリティクラス
     * @tparam Layout 対象とする行列のレイアウト
     */
    template <MatrixLayout Layout = MatrixLayout::RowMajor>
    class MatrixUtilsT
    {
    public:
        using MatrixType = Matrix4x4T<Layout>;

        // 行列式
        static float Determinant(const MatrixType &matrix)
        {
            // 行列式の計算（レイアウトに依存しない）
            float det1 = matrix.m00 * (matrix.m11 * (matrix.m22 * matrix.m33 - matrix.m23 * matrix.m32) -
                                       matrix.m12 * (matrix.m21 * matrix.m33 - matrix.m23 * matrix.m31) +
                                       matrix.m13 * (matrix.m21 * matrix.m32 - matrix.m22 * matrix.m31));
            float det2 = matrix.m01 * (matrix.m10 * (matrix.m22 * matrix.m33 - matrix.m23 * matrix.m32) -
                                       matrix.m12 * (matrix.m20 * matrix.m33 - matrix.m23 * matrix.m30) +
                                       matrix.m13 * (matrix.m20 * matrix.m32 - matrix.m22 * matrix.m30));
            float det3 = matrix.m02 * (matrix.m10 * (matrix.m21 * matrix.m33 - matrix.m23 * matrix.m31) -
                                       matrix.m11 * (matrix.m20 * matrix.m33 - matrix.m23 * matrix.m30) +
                                       matrix.m13 * (matrix.m20 * matrix.m31 - matrix.m21 * matrix.m30));
            float det4 = matrix.m03 * (matrix.m10 * (matrix.m21 * matrix.m32 - matrix.m22 * matrix.m31) -
                                       matrix.m11 * (matrix.m20 * matrix.m32 - matrix.m22 * matrix.m30) +
                                       matrix.m12 * (matrix.m20 * matrix.m31 - matrix.m21 * matrix.m30));
            return det1 - det2 + det3 - det4;
        }

        // 逆行列
        static MatrixType Inverse(const MatrixType &matrix)
        {
            float det = Determinant(matrix);

            // 行列式がゼロに近い場合は逆行列が存在しない
            if (std::abs(det) < Constants::EPSILON)
            {
                return MatrixType::Identity;
            }

            float invDet = 1.0f / det;

            // 余因子行列の計算
            MatrixType result;
            result.m00 = invDet * ((matrix.m11 * matrix.m22 * matrix.m33 + matrix.m12 * matrix.m23 * matrix.m31 + matrix.m13 * matrix.m21 * matrix.m32) -
                                   (matrix.m11 * matrix.m23 * matrix.m32 + matrix.m12 * matrix.m21 * matrix.m33 + matrix.m13 * matrix.m22 * matrix.m31));
            result.m01 = invDet * ((matrix.m01 * matrix.m23 * matrix.m32 + matrix.m02 * matrix.m21 * matrix.m33 + matrix.m03 * matrix.m22 * matrix.m31) -
                                   (matrix.m01 * matrix.m22 * matrix.m33 + matrix.m02 * matrix.m23 * matrix.m31 + matrix.m03 * matrix.m21 * matrix.m32));
            result.m02 = invDet * ((matrix.m01 * matrix.m12 * matrix.m33 + matrix.m02 * matrix.m13 * matrix.m31 + matrix.m03 * matrix.m11 * matrix.m32) -
                                   (matrix.m01 * matrix.m13 * matrix.m32 + matrix.m02 * matrix.m11 * matrix.m33 + matrix.m03 * matrix.m12 * matrix.m31));
            result.m03 = invDet * ((matrix.m01 * matrix.m13 * matrix.m22 + matrix.m02 * matrix.m11 * matrix.m23 + matrix.m03 * matrix.m12 * matrix.m21) -
                                   (matrix.m01 * matrix.m12 * matrix.m23 + matrix.m02 * matrix.m13 * matrix.m21 + matrix.m03 * matrix.m11 * matrix.m22));

            result.m10 = invDet * ((matrix.m10 * matrix.m23 * matrix.m32 + matrix.m12 * matrix.m20 * matrix.m33 + matrix.m13 * matrix.m22 * matrix.m30) -
                                   (matrix.m10 * matrix.m22 * matrix.m33 + matrix.m12 * matrix.m23 * matrix.m30 + matrix.m13 * matrix.m20 * matrix.m32));
            result.m11 = invDet * ((matrix.m00 * matrix.m22 * matrix.m33 + matrix.m02 * matrix.m23 * matrix.m30 + matrix.m03 * matrix.m20 * matrix.m32) -
                                   (matrix.m00 * matrix.m23 * matrix.m32 + matrix.m02 * matrix.m20 * matrix.m33 + matrix.m03 * matrix.m22 * matrix.m30));
            result.m12 = invDet * ((matrix.m00 * matrix.m13 * matrix.m32 + matrix.m02 * matrix.m10 * matrix.m33 + matrix.m03 * matrix.m12 * matrix.m30) -
                                   (matrix.m00 * matrix.m12 * matrix.m33 + matrix.m02 * matrix.m13 * matrix.m30 + matrix.m03 * matrix.m10 * matrix.m32));
            result.m13 = invDet * ((matrix.m00 * matrix.m12 * matrix.m23 + matrix.m02 * matrix.m13 * matrix.m20 + matrix.m03 * matrix.m10 * matrix.m22) -
                                   (matrix.m00 * matrix.m13 * matrix.m22 + matrix.m02 * matrix.m10 * matrix.m23 + matrix.m03 * matrix.m12 * matrix.m20));

            result.m20 = invDet * ((matrix.m10 * matrix.m21 * matrix.m33 + matrix.m11 * matrix.m23 * matrix.m30 + matrix.m13 * matrix.m20 * matrix.m31) -
                                   (matrix.m10 * matrix.m23 * matrix.m31 + matrix.m11 * matrix.m20 * matrix.m33 + matrix.m13 * matrix.m21 * matrix.m30));
            result.m21 = invDet * ((matrix.m00 * matrix.m23 * matrix.m31 + matrix.m01 * matrix.m20 * matrix.m33 + matrix.m03 * matrix.m21 * matrix.m30) -
                                   (matrix.m00 * matrix.m21 * matrix.m33 + matrix.m01 * matrix.m23 * matrix.m30 + matrix.m03 * matrix.m20 * matrix.m31));
            result.m22 = invDet * ((matrix.m00 * matrix.m11 * matrix.m33 + matrix.m01 * matrix.m13 * matrix.m30 + matrix.m03 * matrix.m10 * matrix.m31) -
                                   (matrix.m00 * matrix.m13 * matrix.m31 + matrix.m01 * matrix.m10 * matrix.m33 + matrix.m03 * matrix.m11 * matrix.m30));
            result.m23 = invDet * ((matrix.m00 * matrix.m13 * matrix.m21 + matrix.m01 * matrix.m10 * matrix.m23 + matrix.m03 * matrix.m11 * matrix.m20) -
                                   (matrix.m00 * matrix.m11 * matrix.m23 + matrix.m01 * matrix.m13 * matrix.m20 + matrix.m03 * matrix.m10 * matrix.m21));

            result.m30 = invDet * ((matrix.m10 * matrix.m22 * matrix.m31 + matrix.m11 * matrix.m20 * matrix.m32 + matrix.m12 * matrix.m21 * matrix.m30) -
                                   (matrix.m10 * matrix.m21 * matrix.m32 + matrix.m11 * matrix.m22 * matrix.m30 + matrix.m12 * matrix.m20 * matrix.m31));
            result.m31 = invDet * ((matrix.m00 * matrix.m21 * matrix.m32 + matrix.m01 * matrix.m22 * matrix.m30 + matrix.m02 * matrix.m20 * matrix.m31) -
                                   (matrix.m00 * matrix.m22 * matrix.m31 + matrix.m01 * matrix.m20 * matrix.m32 + matrix.m02 * matrix.m21 * matrix.m30));
            result.m32 = invDet * ((matrix.m00 * matrix.m12 * matrix.m31 + matrix.m01 * matrix.m10 * matrix.m32 + matrix.m02 * matrix.m11 * matrix.m30) -
                                   (matrix.m00 * matrix.m11 * matrix.m32 + matrix.m01 * matrix.m12 * matrix.m30 + matrix.m02 * matrix.m10 * matrix.m31));
            result.m33 = invDet * ((matrix.m00 * matrix.m11 * matrix.m22 + matrix.m01 * matrix.m12 * matrix.m20 + matrix.m02 * matrix.m10 * matrix.m21) -
                                   (matrix.m00 * matrix.m12 * matrix.m21 + matrix.m01 * matrix.m10 * matrix.m22 + matrix.m02 * matrix.m11 * matrix.m20));

            return result;
        }

        // 平行移動行列の作成
        static MatrixType CreateTranslation(float x, float y, float z)
        {
            MatrixType result;
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[0][3] = x;
                result.m[1][3] = y;
                result.m[2][3] = z;
            }
            else // ColumnMajor
            {
                result.m[3][0] = x;
                result.m[3][1] = y;
                result.m[3][2] = z;
            }
            return result;
        }

        static MatrixType CreateTranslation(const Vector3 &translation)
        {
            return CreateTranslation(translation.x, translation.y, translation.z);
        }

        // スケーリング行列の作成
        static MatrixType CreateScale(float x, float y, float z)
        {
            MatrixType result;
            result.m[0][0] = x;
            result.m[1][1] = y;
            result.m[2][2] = z;
            return result;
        }

        static MatrixType CreateScale(const Vector3 &scale)
        {
            return CreateScale(scale.x, scale.y, scale.z);
        }

        static MatrixType CreateScale(float scale)
        {
            return CreateScale(scale, scale, scale);
        }

        // X軸回転行列の作成
        static MatrixType CreateRotationX(float radians)
        {
            float cosTheta = std::cos(radians);
            float sinTheta = std::sin(radians);

            MatrixType result;
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[1][1] = cosTheta;
                result.m[1][2] = -sinTheta;
                result.m[2][1] = sinTheta;
                result.m[2][2] = cosTheta;
            }
            else // ColumnMajor
            {
                result.m[1][1] = cosTheta;
                result.m[2][1] = -sinTheta;
                result.m[1][2] = sinTheta;
                result.m[2][2] = cosTheta;
            }
            return result;
        }

        // Y軸回転行列の作成
        static MatrixType CreateRotationY(float radians)
        {
            float cosTheta = std::cos(radians);
            float sinTheta = std::sin(radians);

            MatrixType result;
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[0][0] = cosTheta;
                result.m[0][2] = sinTheta;
                result.m[2][0] = -sinTheta;
                result.m[2][2] = cosTheta;
            }
            else // ColumnMajor
            {
                result.m[0][0] = cosTheta;
                result.m[2][0] = sinTheta;
                result.m[0][2] = -sinTheta;
                result.m[2][2] = cosTheta;
            }
            return result;
        }

        // Z軸回転行列の作成
        static MatrixType CreateRotationZ(float radians)
        {
            float cosTheta = std::cos(radians);
            float sinTheta = std::sin(radians);

            MatrixType result;
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[0][0] = cosTheta;
                result.m[0][1] = -sinTheta;
                result.m[1][0] = sinTheta;
                result.m[1][1] = cosTheta;
            }
            else // ColumnMajor
            {
                result.m[0][0] = cosTheta;
                result.m[1][0] = -sinTheta;
                result.m[0][1] = sinTheta;
                result.m[1][1] = cosTheta;
            }
            return result;
        }

        // 任意軸回転行列の作成
        static MatrixType CreateFromAxisAngle(const Vector3 &axis, float radians)
        {
            Vector3 normalizedAxis = VectorUtils::Normalize(axis);
            float x = normalizedAxis.x;
            float y = normalizedAxis.y;
            float z = normalizedAxis.z;
            float cosTheta = std::cos(radians);
            float sinTheta = std::sin(radians);
            float t = 1.0f - cosTheta;

            MatrixType result;
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[0][0] = t * x * x + cosTheta;
                result.m[0][1] = t * x * y - sinTheta * z;
                result.m[0][2] = t * x * z + sinTheta * y;

                result.m[1][0] = t * x * y + sinTheta * z;
                result.m[1][1] = t * y * y + cosTheta;
                result.m[1][2] = t * y * z - sinTheta * x;

                result.m[2][0] = t * x * z - sinTheta * y;
                result.m[2][1] = t * y * z + sinTheta * x;
                result.m[2][2] = t * z * z + cosTheta;
            }
            else // ColumnMajor
            {
                result.m[0][0] = t * x * x + cosTheta;
                result.m[1][0] = t * x * y - sinTheta * z;
                result.m[2][0] = t * x * z + sinTheta * y;

                result.m[0][1] = t * x * y + sinTheta * z;
                result.m[1][1] = t * y * y + cosTheta;
                result.m[2][1] = t * y * z - sinTheta * x;

                result.m[0][2] = t * x * z - sinTheta * y;
                result.m[1][2] = t * y * z + sinTheta * x;
                result.m[2][2] = t * z * z + cosTheta;
            }

            return result;
        }

        // ビュー行列の作成
        static MatrixType CreateLookAt(const Vector3 &eye, const Vector3 &target, const Vector3 &up)
        {
            Vector3 zaxis = VectorUtils::Normalize(eye - target);
            Vector3 xaxis = VectorUtils::Normalize(VectorUtils::Cross(up, zaxis));
            Vector3 yaxis = VectorUtils::Cross(zaxis, xaxis);

            MatrixType result;

            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[0][0] = xaxis.x;
                result.m[0][1] = xaxis.y;
                result.m[0][2] = xaxis.z;
                result.m[0][3] = -VectorUtils::Dot(xaxis, eye);

                result.m[1][0] = yaxis.x;
                result.m[1][1] = yaxis.y;
                result.m[1][2] = yaxis.z;
                result.m[1][3] = -VectorUtils::Dot(yaxis, eye);

                result.m[2][0] = zaxis.x;
                result.m[2][1] = zaxis.y;
                result.m[2][2] = zaxis.z;
                result.m[2][3] = -VectorUtils::Dot(zaxis, eye);
            }
            else // ColumnMajor
            {
                result.m[0][0] = xaxis.x;
                result.m[0][1] = yaxis.x;
                result.m[0][2] = zaxis.x;
                result.m[3][0] = -VectorUtils::Dot(xaxis, eye);

                result.m[1][0] = xaxis.y;
                result.m[1][1] = yaxis.y;
                result.m[1][2] = zaxis.y;
                result.m[3][1] = -VectorUtils::Dot(yaxis, eye);

                result.m[2][0] = xaxis.z;
                result.m[2][1] = yaxis.z;
                result.m[2][2] = zaxis.z;
                result.m[3][2] = -VectorUtils::Dot(zaxis, eye);
            }

            return result;
        }

        // 透視投影行列の作成
        static MatrixType CreatePerspectiveFieldOfView(float fovY, float aspectRatio, float nearPlane, float farPlane)
        {
            float yScale = 1.0f / std::tan(fovY * 0.5f);
            float xScale = yScale / aspectRatio;
            float farMinusNear = farPlane - nearPlane;

            MatrixType result;
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[0][0] = xScale;
                result.m[1][1] = yScale;
                result.m[2][2] = farPlane / farMinusNear;
                result.m[2][3] = -nearPlane * farPlane / farMinusNear;
                result.m[3][2] = 1.0f;
                result.m[3][3] = 0.0f;
            }
            else // ColumnMajor
            {
                result.m[0][0] = xScale;
                result.m[1][1] = yScale;
                result.m[2][2] = farPlane / farMinusNear;
                result.m[3][2] = 1.0f;
                result.m[2][3] = -nearPlane * farPlane / farMinusNear;
                result.m[3][3] = 0.0f;
            }

            return result;
        }

        // 正射影行列の作成
        static MatrixType CreateOrthographic(float width, float height, float nearPlane, float farPlane)
        {
            float farMinusNear = farPlane - nearPlane;

            MatrixType result;
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                result.m[0][0] = 2.0f / width;
                result.m[1][1] = 2.0f / height;
                result.m[2][2] = 1.0f / farMinusNear;
                result.m[2][3] = -nearPlane / farMinusNear;
            }
            else // ColumnMajor
            {
                result.m[0][0] = 2.0f / width;
                result.m[1][1] = 2.0f / height;
                result.m[2][2] = 1.0f / farMinusNear;
                result.m[3][2] = -nearPlane / farMinusNear;
            }

            return result;
        }

        // クォータニオンから回転行列を作成
        static MatrixType CreateFromQuaternion(const Quaternion &rotation)
        {
            Quaternion q = VectorUtils::Normalize(rotation);
            float xx = q.x * q.x;
            float yy = q.y * q.y;
            float zz = q.z * q.z;
            float xy = q.x * q.y;
            float xz = q.x * q.z;
            float yz = q.y * q.z;
            float wx = q.w * q.x;
            float wy = q.w * q.y;
            float wz = q.w * q.z;

            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                return MatrixType(
                    1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy), 0.0f,
                    2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx), 0.0f,
                    2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy), 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
            }
            else // ColumnMajor
            {
                return MatrixType(
                    1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f,
                    2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f,
                    2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
            }
        }

        // TRS（移動*回転*スケーリング）行列の作成
        static MatrixType CreateTRS(const Vector3 &translation, const Quaternion &rotation, const Vector3 &scale)
        {
            MatrixType S = CreateScale(scale);
            MatrixType R = CreateFromQuaternion(rotation);
            MatrixType T = CreateTranslation(translation);

            // 順序：スケール→回転→移動
            return T * R * S;
        }

        // 行列間の線形補間
        static MatrixType Lerp(const MatrixType &a, const MatrixType &b, float t)
        {
            MatrixType result;

            for (int i = 0; i < 16; ++i)
            {
                result.values[i] = a.values[i] + (b.values[i] - a.values[i]) * t;
            }

            return result;
        }

        // 2つの行列の要素ごとの乗算
        static MatrixType ElementwiseMultiply(const MatrixType &a, const MatrixType &b)
        {
            MatrixType result;

            for (int i = 0; i < 16; ++i)
            {
                result.values[i] = a.values[i] * b.values[i];
            }

            return result;
        }

        static Vector4 TransformPoint(const MatrixType &matrix, const Vector3 &point)
        {
            return matrix * Vector4(point, 1.0f);
        }

        static Vector4 TransformVector(const MatrixType &matrix, const Vector3 &vector)
        {
            return matrix * Vector4(vector, 0.0f);
        }

        static ClipSpaceSphereBounds TransformSphereToClipSpace(const MatrixType &matrix,
                                                                const Vector3 &center,
                                                                float radius)
        {
            const Vector4 centerClip = TransformPoint(matrix, center);
            const Vector4 axisX = TransformVector(matrix, Vector3(radius, 0.0f, 0.0f));
            const Vector4 axisY = TransformVector(matrix, Vector3(0.0f, radius, 0.0f));
            const Vector4 axisZ = TransformVector(matrix, Vector3(0.0f, 0.0f, radius));

            ClipSpaceSphereBounds bounds;
            bounds.Center = centerClip;
            bounds.Extents = Vector4(
                std::abs(axisX.x) + std::abs(axisY.x) + std::abs(axisZ.x),
                std::abs(axisX.y) + std::abs(axisY.y) + std::abs(axisZ.y),
                std::abs(axisX.z) + std::abs(axisY.z) + std::abs(axisZ.z),
                std::abs(axisX.w) + std::abs(axisY.w) + std::abs(axisZ.w));
            return bounds;
        }

        static bool IntersectsClipSpace(const ClipSpaceSphereBounds &bounds,
                                        ClipSpaceDepthRange depthRange = ClipSpaceDepthRange::ZeroToOne)
        {
            const Vector4 &center = bounds.Center;
            const Vector4 &extent = bounds.Extents;
            const float xwExtent = extent.x + extent.w;
            const float ywExtent = extent.y + extent.w;
            const float zwExtent = extent.z + extent.w;

            if (center.x + center.w + xwExtent < 0.0f)
            {
                return false;
            }
            if (-center.x + center.w + xwExtent < 0.0f)
            {
                return false;
            }
            if (center.y + center.w + ywExtent < 0.0f)
            {
                return false;
            }
            if (-center.y + center.w + ywExtent < 0.0f)
            {
                return false;
            }

            if (depthRange == ClipSpaceDepthRange::ZeroToOne)
            {
                if (center.z + extent.z < 0.0f)
                {
                    return false;
                }
            }
            else if (center.z + center.w + zwExtent < 0.0f)
            {
                return false;
            }

            if (-center.z + center.w + zwExtent < 0.0f)
            {
                return false;
            }

            return true;
        }

        static Vector4 NormalizeClipSpacePlane(const Vector4 &plane)
        {
            const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
            if (length > 1e-8f)
            {
                return plane / length;
            }
            return plane;
        }

        static ClipSpaceFrustumPlanes ExtractClipSpaceFrustumPlanes(
            const MatrixType &viewProjection,
            ClipSpaceDepthRange depthRange = ClipSpaceDepthRange::NegativeOneToOne)
        {
            const Vector4 row0 = viewProjection.GetRow(0);
            const Vector4 row1 = viewProjection.GetRow(1);
            const Vector4 row2 = viewProjection.GetRow(2);
            const Vector4 row3 = viewProjection.GetRow(3);

            ClipSpaceFrustumPlanes result;
            result.Planes[static_cast<int>(ClipSpaceFrustumPlane::Left)] =
                NormalizeClipSpacePlane(row3 + row0);
            result.Planes[static_cast<int>(ClipSpaceFrustumPlane::Right)] =
                NormalizeClipSpacePlane(row3 - row0);
            result.Planes[static_cast<int>(ClipSpaceFrustumPlane::Bottom)] =
                NormalizeClipSpacePlane(row3 + row1);
            result.Planes[static_cast<int>(ClipSpaceFrustumPlane::Top)] =
                NormalizeClipSpacePlane(row3 - row1);
            result.Planes[static_cast<int>(ClipSpaceFrustumPlane::Near)] =
                NormalizeClipSpacePlane(depthRange == ClipSpaceDepthRange::ZeroToOne ? row2 : row3 + row2);
            result.Planes[static_cast<int>(ClipSpaceFrustumPlane::Far)] =
                NormalizeClipSpacePlane(row3 - row2);
            return result;
        }

        /**
         * @brief 列ベクトル規約の行列をGPUシェーダー用データに変換（RowMajor→ColumnMajor転置）
         *
         * View、Projection、InverseViewProjection等の列ベクトル規約行列に使用します。
         * C++ RowMajor配列をGLSL column-major（std140）レイアウトに変換します。
         *
         * @param mat 変換元の行列
         * @param out 出力先のfloat[16]配列
         */
        static void TransposeToShaderData(const MatrixType &mat, float *out)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    out[col * 4 + row] = mat.m[row][col];
                }
            }
        }

        /**
         * @brief 行ベクトル規約のワールド行列をGPUシェーダー用データにコピー
         *
         * 行ベクトル規約（平行移動がRow3）のワールド行列は、
         * 直接コピーでGLSL column-majorとして正しく解釈されます。
         *
         * @param mat 変換元のワールド行列
         * @param out 出力先のfloat[16]配列
         */
        static void CopyToShaderData(const MatrixType &mat, float *out)
        {
            std::memcpy(out, mat.values, sizeof(float) * 16);
        }

        // 行ベクトル規約ワールド行列のスケール成分(各論理行ベクトルの長さ)を抽出する。
        // 前提: 行ベクトル規約ワールド行列(上3x3=回転×スケール, 並進=行3, 列3=0)。
        static Vector3 ExtractScale(const MatrixType& matrix)
        {
            const Vector4 row0 = matrix.GetRow(0);
            const Vector4 row1 = matrix.GetRow(1);
            const Vector4 row2 = matrix.GetRow(2);
            const float scaleX = std::sqrt(row0.x * row0.x + row0.y * row0.y + row0.z * row0.z);
            const float scaleY = std::sqrt(row1.x * row1.x + row1.y * row1.y + row1.z * row1.z);
            const float scaleZ = std::sqrt(row2.x * row2.x + row2.y * row2.y + row2.z * row2.z);
            return Vector3(scaleX, scaleY, scaleZ);
        }

        // 行ベクトル規約ワールド行列を構築する(並進=論理行3, 列3=0, m33=1)。
        // Transform::ToMatrix()と同一の非正規化クォータニオン展開を用いる(正規化しない)。
        // 既存3サイト(MeshComponent/BoardComponent/ComponentDataRegistryのCalculateWorldMatrix)と数値一致。
        static MatrixType CreateWorldRowVector(const Vector3& translation, const Quaternion& rotation, const Vector3& scale)
        {
            static_assert(Layout == MatrixLayout::RowMajor,
                "CreateWorldRowVector assumes row-vector (RowMajor physical) layout");

            const float sx = scale.x;
            const float sy = scale.y;
            const float sz = scale.z;

            const float xx = rotation.x * rotation.x;
            const float xy = rotation.x * rotation.y;
            const float xz = rotation.x * rotation.z;
            const float xw = rotation.x * rotation.w;
            const float yy = rotation.y * rotation.y;
            const float yz = rotation.y * rotation.z;
            const float yw = rotation.y * rotation.w;
            const float zz = rotation.z * rotation.z;
            const float zw = rotation.z * rotation.w;

            float r00 = 1.0f - 2.0f * (yy + zz);
            float r01 = 2.0f * (xy - zw);
            float r02 = 2.0f * (xz + yw);
            float r10 = 2.0f * (xy + zw);
            float r11 = 1.0f - 2.0f * (xx + zz);
            float r12 = 2.0f * (yz - xw);
            float r20 = 2.0f * (xz - yw);
            float r21 = 2.0f * (yz + xw);
            float r22 = 1.0f - 2.0f * (xx + yy);

            r00 *= sx;
            r01 *= sy;
            r02 *= sz;
            r10 *= sx;
            r11 *= sy;
            r12 *= sz;
            r20 *= sx;
            r21 *= sy;
            r22 *= sz;

            MatrixType result;
            result.m00 = r00;
            result.m01 = r01;
            result.m02 = r02;
            result.m03 = 0.0f;
            result.m10 = r10;
            result.m11 = r11;
            result.m12 = r12;
            result.m13 = 0.0f;
            result.m20 = r20;
            result.m21 = r21;
            result.m22 = r22;
            result.m23 = 0.0f;
            result.m30 = translation.x;
            result.m31 = translation.y;
            result.m32 = translation.z;
            result.m33 = 1.0f;
            return result;
        }

        // 法線行列(ワールド上3x3の逆転置)を構築する。DrawCommandの既存手書きロジックを逐語移設。
        static MatrixType CreateNormalMatrix(const MatrixType& world)
        {
            static_assert(Layout == MatrixLayout::RowMajor,
                "CreateNormalMatrix assumes RowMajor (named == logical) layout");

            const float a00 = world.m00;
            const float a01 = world.m01;
            const float a02 = world.m02;
            const float a10 = world.m10;
            const float a11 = world.m11;
            const float a12 = world.m12;
            const float a20 = world.m20;
            const float a21 = world.m21;
            const float a22 = world.m22;

            const float determinant =
                a00 * (a11 * a22 - a12 * a21) -
                a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);

            MatrixType normalMatrix = MatrixType::Identity;
            if (std::abs(determinant) < Constants::EPSILON)
            {
                return normalMatrix;
            }

            const float invDeterminant = 1.0f / determinant;

            const float inv00 = (a11 * a22 - a12 * a21) * invDeterminant;
            const float inv01 = (a02 * a21 - a01 * a22) * invDeterminant;
            const float inv02 = (a01 * a12 - a02 * a11) * invDeterminant;
            const float inv10 = (a12 * a20 - a10 * a22) * invDeterminant;
            const float inv11 = (a00 * a22 - a02 * a20) * invDeterminant;
            const float inv12 = (a02 * a10 - a00 * a12) * invDeterminant;
            const float inv20 = (a10 * a21 - a11 * a20) * invDeterminant;
            const float inv21 = (a01 * a20 - a00 * a21) * invDeterminant;
            const float inv22 = (a00 * a11 - a01 * a10) * invDeterminant;

            normalMatrix.m00 = inv00;
            normalMatrix.m01 = inv10;
            normalMatrix.m02 = inv20;
            normalMatrix.m10 = inv01;
            normalMatrix.m11 = inv11;
            normalMatrix.m12 = inv21;
            normalMatrix.m20 = inv02;
            normalMatrix.m21 = inv12;
            normalMatrix.m22 = inv22;

            return normalMatrix;
        }

        // 上3x3を3つのvec4(各行末w=0)へ非転置でパックする(GPUアップロード用, float[12])。
        // DrawCommandの法線行列パックを共通化。out は12要素以上を指すこと。
        static void CopyUpper3x3ToShaderData(const MatrixType& matrix, float* out)
        {
            const Vector4 row0 = matrix.GetRow(0);
            const Vector4 row1 = matrix.GetRow(1);
            const Vector4 row2 = matrix.GetRow(2);

            out[0] = row0.x;
            out[1] = row0.y;
            out[2] = row0.z;
            out[3] = 0.0f;
            out[4] = row1.x;
            out[5] = row1.y;
            out[6] = row1.z;
            out[7] = 0.0f;
            out[8] = row2.x;
            out[9] = row2.y;
            out[10] = row2.z;
            out[11] = 0.0f;
        }

        // 2つの行列の全16要素を近似比較する。
        static bool ApproxEqual(const MatrixType& a, const MatrixType& b, float epsilon = 1.0e-4f)
        {
            for (int i = 0; i < 16; ++i)
            {
                if (std::abs(a.values[i] - b.values[i]) > epsilon)
                {
                    return false;
                }
            }
            return true;
        }

        // 上3x3(回転×スケール)のみを近似比較する(並進の行/列規約差を無視)。
        static bool ApproxEqualUpperLeft3x3(const MatrixType& a, const MatrixType& b, float epsilon = 1.0e-4f)
        {
            return std::abs(a.m00 - b.m00) <= epsilon
                && std::abs(a.m01 - b.m01) <= epsilon
                && std::abs(a.m02 - b.m02) <= epsilon
                && std::abs(a.m10 - b.m10) <= epsilon
                && std::abs(a.m11 - b.m11) <= epsilon
                && std::abs(a.m12 - b.m12) <= epsilon
                && std::abs(a.m20 - b.m20) <= epsilon
                && std::abs(a.m21 - b.m21) <= epsilon
                && std::abs(a.m22 - b.m22) <= epsilon;
        }
    };

    // デフォルトのMatrixUtilsの定義
    using MatrixUtils = MatrixUtilsT<MatrixLayout::RowMajor>;

    // 明示的なレイアウト指定のための型エイリアス
    using RowMajorMatrixUtils = MatrixUtilsT<MatrixLayout::RowMajor>;
    using ColumnMajorMatrixUtils = MatrixUtilsT<MatrixLayout::ColumnMajor>;

} // namespace NorvesLib::Math
