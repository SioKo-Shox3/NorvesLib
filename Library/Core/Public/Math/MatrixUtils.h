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
    };

    // デフォルトのMatrixUtilsの定義
    using MatrixUtils = MatrixUtilsT<MatrixLayout::RowMajor>;

    // 明示的なレイアウト指定のための型エイリアス
    using RowMajorMatrixUtils = MatrixUtilsT<MatrixLayout::RowMajor>;
    using ColumnMajorMatrixUtils = MatrixUtilsT<MatrixLayout::ColumnMajor>;

} // namespace NorvesLib::Math
