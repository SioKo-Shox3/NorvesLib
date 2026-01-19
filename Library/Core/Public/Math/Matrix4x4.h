#pragma once

#include "Vector4.h"
#include "MathTypes.h"
#include <cmath>

namespace NorvesLib::Math
{

    // 前方宣言
    struct Quaternion;

    /**
     * @brief 行列レイアウトの種類
     */
    enum class MatrixLayout
    {
        RowMajor,   // 行優先 - m[row][column]
        ColumnMajor // 列優先 - m[column][row]
    };

    /**
     * @brief 4x4行列クラス（テンプレート化されたレイアウト）
     * @tparam Layout 行列の内部レイアウト（行優先または列優先）
     */
    template <MatrixLayout Layout = MatrixLayout::RowMajor>
    struct NORVES_ALIGN(16) Matrix4x4T
    {
        // 4x4行列（m[i][j]の意味はレイアウトによって異なる）
        union
        {
            float m[4][4];
            float values[16];
            struct
            {
                float m00, m01, m02, m03;
                float m10, m11, m12, m13;
                float m20, m21, m22, m23;
                float m30, m31, m32, m33;
            };
            Vector4 vectors[4]; // 行優先の場合は行ベクトル、列優先の場合は列ベクトル
        };

        // コンストラクタ
        Matrix4x4T()
        {
            // 単位行列で初期化
            m[0][0] = 1.0f;
            m[0][1] = 0.0f;
            m[0][2] = 0.0f;
            m[0][3] = 0.0f;
            m[1][0] = 0.0f;
            m[1][1] = 1.0f;
            m[1][2] = 0.0f;
            m[1][3] = 0.0f;
            m[2][0] = 0.0f;
            m[2][1] = 0.0f;
            m[2][2] = 1.0f;
            m[2][3] = 0.0f;
            m[3][0] = 0.0f;
            m[3][1] = 0.0f;
            m[3][2] = 0.0f;
            m[3][3] = 1.0f;
        }

        // コンストラクタ - 行優先または列優先の解釈に応じてセマンティクスが変わる
        Matrix4x4T(float m00, float m01, float m02, float m03,
                   float m10, float m11, float m12, float m13,
                   float m20, float m21, float m22, float m23,
                   float m30, float m31, float m32, float m33)
        {
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                // 行優先の場合、引数の順序は行ごとに解釈される
                this->m00 = m00;
                this->m01 = m01;
                this->m02 = m02;
                this->m03 = m03;
                this->m10 = m10;
                this->m11 = m11;
                this->m12 = m12;
                this->m13 = m13;
                this->m20 = m20;
                this->m21 = m21;
                this->m22 = m22;
                this->m23 = m23;
                this->m30 = m30;
                this->m31 = m31;
                this->m32 = m32;
                this->m33 = m33;
            }
            else // ColumnMajor
            {
                // 列優先の場合、引数の順序は列ごとに解釈される
                this->m00 = m00;
                this->m10 = m01;
                this->m20 = m02;
                this->m30 = m03;
                this->m01 = m10;
                this->m11 = m11;
                this->m21 = m12;
                this->m31 = m13;
                this->m02 = m20;
                this->m12 = m21;
                this->m22 = m22;
                this->m32 = m23;
                this->m03 = m30;
                this->m13 = m31;
                this->m23 = m32;
                this->m33 = m33;
            }
        }

        // Vector4のベクトルから行列を構築
        Matrix4x4T(const Vector4 &vec0, const Vector4 &vec1, const Vector4 &vec2, const Vector4 &vec3)
        {
            vectors[0] = vec0;
            vectors[1] = vec1;
            vectors[2] = vec2;
            vectors[3] = vec3;
        }

        // 行列要素へのアクセス演算子 - レイアウトに応じた解釈をする
        Vector4 &operator[](int index)
        {
            return vectors[index];
        }

        const Vector4 &operator[](int index) const
        {
            return vectors[index];
        }

        // 行または列に対応するVector4を取得（レイアウトに依存）
        Vector4 GetRow(int row) const
        {
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                return vectors[row];
            }
            else // ColumnMajor
            {
                return Vector4(m[0][row], m[1][row], m[2][row], m[3][row]);
            }
        }

        Vector4 GetColumn(int column) const
        {
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                return Vector4(m[0][column], m[1][column], m[2][column], m[3][column]);
            }
            else // ColumnMajor
            {
                return vectors[column];
            }
        }

        // 行列演算 - レイアウトに応じた実装
        template <MatrixLayout OtherLayout>
        Matrix4x4T operator*(const Matrix4x4T<OtherLayout> &other) const
        {
            Matrix4x4T result;

            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                // 行優先(M1) * 任意レイアウト(M2)
                for (int i = 0; i < 4; ++i)
                {
                    for (int j = 0; j < 4; ++j)
                    {
                        float sum = 0.0f;
                        for (int k = 0; k < 4; ++k)
                        {
                            if constexpr (OtherLayout == MatrixLayout::RowMajor)
                                sum += m[i][k] * other.m[k][j]; // 行優先 * 行優先
                            else
                                sum += m[i][k] * other.m[j][k]; // 行優先 * 列優先
                        }
                        result.m[i][j] = sum;
                    }
                }
            }
            else // このオブジェクトが列優先
            {
                // 列優先(M1) * 任意レイアウト(M2)
                for (int i = 0; i < 4; ++i)
                {
                    for (int j = 0; j < 4; ++j)
                    {
                        float sum = 0.0f;
                        for (int k = 0; k < 4; ++k)
                        {
                            if constexpr (OtherLayout == MatrixLayout::RowMajor)
                                sum += m[k][i] * other.m[k][j]; // 列優先 * 行優先
                            else
                                sum += m[k][i] * other.m[j][k]; // 列優先 * 列優先
                        }
                        result.m[i][j] = sum;
                    }
                }
            }

            return result;
        }

        // 行列とベクトルの乗算 - レイアウトに応じた実装
        Vector4 operator*(const Vector4 &vector) const
        {
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                // 行優先で行ベクトルとの内積
                return Vector4(
                    m00 * vector.x + m01 * vector.y + m02 * vector.z + m03 * vector.w,
                    m10 * vector.x + m11 * vector.y + m12 * vector.z + m13 * vector.w,
                    m20 * vector.x + m21 * vector.y + m22 * vector.z + m23 * vector.w,
                    m30 * vector.x + m31 * vector.y + m32 * vector.z + m33 * vector.w);
            }
            else // ColumnMajor
            {
                // 列優先で行ベクトルとの内積
                return Vector4(
                    m00 * vector.x + m10 * vector.y + m20 * vector.z + m30 * vector.w,
                    m01 * vector.x + m11 * vector.y + m21 * vector.z + m31 * vector.w,
                    m02 * vector.x + m12 * vector.y + m22 * vector.z + m32 * vector.w,
                    m03 * vector.x + m13 * vector.y + m23 * vector.z + m33 * vector.w);
            }
        }

        // 転置行列を取得
        Matrix4x4T<Layout == MatrixLayout::RowMajor ? MatrixLayout::ColumnMajor : MatrixLayout::RowMajor> Transpose() const
        {
            using TransposedMatrix = Matrix4x4T<Layout == MatrixLayout::RowMajor ? MatrixLayout::ColumnMajor : MatrixLayout::RowMajor>;

            // 転置は行と列を入れ替えるだけ
            return TransposedMatrix(
                m00, m10, m20, m30,
                m01, m11, m21, m31,
                m02, m12, m22, m32,
                m03, m13, m23, m33);
        }

        // 行優先形式に変換
        Matrix4x4T<MatrixLayout::RowMajor> ToRowMajor() const
        {
            if constexpr (Layout == MatrixLayout::RowMajor)
            {
                return *this;
            }
            else // ColumnMajor
            {
                return Transpose();
            }
        }

        // 列優先形式に変換
        Matrix4x4T<MatrixLayout::ColumnMajor> ToColumnMajor() const
        {
            if constexpr (Layout == MatrixLayout::ColumnMajor)
            {
                return *this;
            }
            else // RowMajor
            {
                return Transpose();
            }
        }

        // メンバ変数
        static const Matrix4x4T Identity;
        static const Matrix4x4T Zero;
    };

    // 静的メンバの初期化 (行優先版)
    template <MatrixLayout Layout>
    inline const Matrix4x4T<Layout> Matrix4x4T<Layout>::Identity; // デフォルトコンストラクタで単位行列

    template <MatrixLayout Layout>
    inline const Matrix4x4T<Layout> Matrix4x4T<Layout>::Zero(
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f);

    // デフォルトのMatrix4x4型の定義 - 現在は行優先をデフォルトとしている
    // これを変更することで、プロジェクト全体のデフォルト行列レイアウトを変更できる
    using Matrix4x4 = Matrix4x4T<MatrixLayout::RowMajor>;

    // 明示的なレイアウト指定のための型エイリアス
    using RowMajorMatrix4x4 = Matrix4x4T<MatrixLayout::RowMajor>;
    using ColumnMajorMatrix4x4 = Matrix4x4T<MatrixLayout::ColumnMajor>;

} // namespace NorvesLib::Math
