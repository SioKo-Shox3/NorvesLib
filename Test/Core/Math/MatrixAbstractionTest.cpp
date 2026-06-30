#include "Math/Matrix4x4.h"
#include "Math/MatrixUtils.h"
#include "Math/Transform.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <cassert>
#include <cmath>
#include <cstdint>

namespace
{
    bool IsNearlyEqual(float a, float b, float eps = 1.0e-4f)
    {
        return std::fabs(a - b) <= eps;
    }

    void AssertVector3NearlyEqual(const NorvesLib::Math::Vector3& actual,
                                  const NorvesLib::Math::Vector3& expected,
                                  float eps = 1.0e-4f)
    {
        assert(IsNearlyEqual(actual.x, expected.x, eps));
        assert(IsNearlyEqual(actual.y, expected.y, eps));
        assert(IsNearlyEqual(actual.z, expected.z, eps));
    }

    NorvesLib::Math::Matrix4x4 CreateExpectedWorldRowVectorFromTransform(
        const NorvesLib::Math::Vector3& translation,
        const NorvesLib::Math::Quaternion& rotation,
        const NorvesLib::Math::Vector3& scale)
    {
        const NorvesLib::Math::Transform transform(translation, rotation, scale);
        NorvesLib::Math::Matrix4x4 expected = transform.ToMatrix();
        expected.m30 = translation.x;
        expected.m31 = translation.y;
        expected.m32 = translation.z;
        expected.m03 = 0.0f;
        expected.m13 = 0.0f;
        expected.m23 = 0.0f;
        expected.m33 = 1.0f;
        return expected;
    }

    NorvesLib::Math::Matrix4x4 CreateExpectedNormalMatrixDrawCommandStyle(
        const NorvesLib::Math::Matrix4x4& world)
    {
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

        NorvesLib::Math::Matrix4x4 normalMatrix = NorvesLib::Math::Matrix4x4::Identity;
        if (std::abs(determinant) < NorvesLib::Math::Constants::EPSILON)
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

    void AssertWorldRowVectorMatchesTransform(const NorvesLib::Math::Vector3& translation,
                                              const NorvesLib::Math::Quaternion& rotation,
                                              const NorvesLib::Math::Vector3& scale)
    {
        const NorvesLib::Math::Matrix4x4 expected =
            CreateExpectedWorldRowVectorFromTransform(translation, rotation, scale);
        const NorvesLib::Math::Matrix4x4 actual =
            NorvesLib::Math::MatrixUtils::CreateWorldRowVector(translation, rotation, scale);
        assert(NorvesLib::Math::MatrixUtils::ApproxEqual(actual, expected, 1.0e-6f));
    }
}

int main()
{
    {
        const NorvesLib::Math::Matrix4x4 world =
            NorvesLib::Math::MatrixUtils::CreateWorldRowVector(
                NorvesLib::Math::Vector3(1.0f, 2.0f, 3.0f),
                NorvesLib::Math::Quaternion::Identity,
                NorvesLib::Math::Vector3(2.0f, 4.0f, 0.5f));
        const NorvesLib::Math::Matrix4x4 normal =
            NorvesLib::Math::MatrixUtils::CreateNormalMatrix(world);

        assert(IsNearlyEqual(normal.m00, 0.5f));
        assert(IsNearlyEqual(normal.m11, 0.25f));
        assert(IsNearlyEqual(normal.m22, 2.0f));
        assert(IsNearlyEqual(normal.m01, 0.0f));
        assert(IsNearlyEqual(normal.m02, 0.0f));
        assert(IsNearlyEqual(normal.m10, 0.0f));
        assert(IsNearlyEqual(normal.m12, 0.0f));
        assert(IsNearlyEqual(normal.m20, 0.0f));
        assert(IsNearlyEqual(normal.m21, 0.0f));
    }

    {
        const NorvesLib::Math::Quaternion rotation(
            NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f),
            NorvesLib::Math::Constants::PI * 0.25f);
        const NorvesLib::Math::Matrix4x4 world =
            NorvesLib::Math::MatrixUtils::CreateWorldRowVector(
                NorvesLib::Math::Vector3::Zero,
                rotation,
                NorvesLib::Math::Vector3(2.0f, 1.0f, 0.5f));
        const NorvesLib::Math::Matrix4x4 normal =
            NorvesLib::Math::MatrixUtils::CreateNormalMatrix(world);
        const NorvesLib::Math::Matrix4x4 expected =
            CreateExpectedNormalMatrixDrawCommandStyle(world);

        assert(NorvesLib::Math::MatrixUtils::ApproxEqual(normal, expected, 1.0e-4f));
    }

    {
        const NorvesLib::Math::Matrix4x4 world =
            NorvesLib::Math::MatrixUtils::CreateWorldRowVector(
                NorvesLib::Math::Vector3::Zero,
                NorvesLib::Math::Quaternion::Identity,
                NorvesLib::Math::Vector3(0.0f, 1.0f, 1.0f));
        const NorvesLib::Math::Matrix4x4 normal =
            NorvesLib::Math::MatrixUtils::CreateNormalMatrix(world);

        assert(NorvesLib::Math::MatrixUtils::ApproxEqual(
            normal,
            NorvesLib::Math::Matrix4x4::Identity));
    }

    {
        AssertWorldRowVectorMatchesTransform(
            NorvesLib::Math::Vector3::Zero,
            NorvesLib::Math::Quaternion::Identity,
            NorvesLib::Math::Vector3::One);

        AssertWorldRowVectorMatchesTransform(
            NorvesLib::Math::Vector3(1.0f, -2.0f, 3.0f),
            NorvesLib::Math::Quaternion(
                NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f),
                NorvesLib::Math::Constants::PI * 0.25f),
            NorvesLib::Math::Vector3(2.0f, 3.0f, 4.0f));

        const NorvesLib::Math::Quaternion unitRotation(
            NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f),
            NorvesLib::Math::Constants::PI / 6.0f);
        const NorvesLib::Math::Quaternion nonUnitRotation(
            unitRotation.x * 2.0f,
            unitRotation.y * 2.0f,
            unitRotation.z * 2.0f,
            unitRotation.w * 2.0f);
        AssertWorldRowVectorMatchesTransform(
            NorvesLib::Math::Vector3(-5.0f, 6.0f, 7.0f),
            nonUnitRotation,
            NorvesLib::Math::Vector3(0.5f, 2.0f, 3.0f));
    }

    {
        NorvesLib::Math::Matrix4x4 matrix =
            NorvesLib::Math::MatrixUtils::CreateWorldRowVector(
                NorvesLib::Math::Vector3(3.0f, -4.0f, 5.0f),
                NorvesLib::Math::Quaternion::Identity,
                NorvesLib::Math::Vector3::One);
        AssertVector3NearlyEqual(
            matrix.GetTranslationRow(),
            NorvesLib::Math::Vector3(3.0f, -4.0f, 5.0f));

        matrix.SetTranslationRow(NorvesLib::Math::Vector3(7.0f, 8.0f, 9.0f));
        AssertVector3NearlyEqual(
            matrix.GetTranslationRow(),
            NorvesLib::Math::Vector3(7.0f, 8.0f, 9.0f));
    }

    {
        const NorvesLib::Math::Matrix4x4 matrix =
            NorvesLib::Math::MatrixUtils::CreateWorldRowVector(
                NorvesLib::Math::Vector3::Zero,
                NorvesLib::Math::Quaternion::Identity,
                NorvesLib::Math::Vector3(2.0f, 3.0f, 4.0f));
        const NorvesLib::Math::Vector3 scale =
            NorvesLib::Math::MatrixUtils::ExtractScale(matrix);

        AssertVector3NearlyEqual(scale, NorvesLib::Math::Vector3(2.0f, 3.0f, 4.0f));
    }

    {
        const float eps = 1.0e-4f;
        const NorvesLib::Math::Matrix4x4 matrix = NorvesLib::Math::Matrix4x4::Identity;
        assert(NorvesLib::Math::MatrixUtils::ApproxEqual(matrix, matrix, eps));
        assert(NorvesLib::Math::MatrixUtils::ApproxEqualUpperLeft3x3(matrix, matrix, eps));

        NorvesLib::Math::Matrix4x4 changed = matrix;
        changed.m00 += eps * 2.0f;
        assert(!NorvesLib::Math::MatrixUtils::ApproxEqual(matrix, changed, eps));

        NorvesLib::Math::Matrix4x4 translatedA =
            NorvesLib::Math::MatrixUtils::CreateWorldRowVector(
                NorvesLib::Math::Vector3(1.0f, 2.0f, 3.0f),
                NorvesLib::Math::Quaternion::Identity,
                NorvesLib::Math::Vector3::One);
        NorvesLib::Math::Matrix4x4 translatedB = translatedA;
        translatedB.SetTranslationRow(NorvesLib::Math::Vector3(4.0f, 5.0f, 6.0f));
        assert(NorvesLib::Math::MatrixUtils::ApproxEqualUpperLeft3x3(translatedA, translatedB, eps));
        assert(!NorvesLib::Math::MatrixUtils::ApproxEqual(translatedA, translatedB, eps));
    }

    {
        NorvesLib::Math::Matrix4x4 matrix = NorvesLib::Math::Matrix4x4::Identity;
        matrix.m00 = 1.0f;
        matrix.m01 = 2.0f;
        matrix.m02 = 3.0f;
        matrix.m10 = 4.0f;
        matrix.m11 = 5.0f;
        matrix.m12 = 6.0f;
        matrix.m20 = 7.0f;
        matrix.m21 = 8.0f;
        matrix.m22 = 9.0f;

        float out[12] = {};
        NorvesLib::Math::MatrixUtils::CopyUpper3x3ToShaderData(matrix, out);

        assert(IsNearlyEqual(out[0], 1.0f));
        assert(IsNearlyEqual(out[1], 2.0f));
        assert(IsNearlyEqual(out[2], 3.0f));
        assert(IsNearlyEqual(out[3], 0.0f));
        assert(IsNearlyEqual(out[4], 4.0f));
        assert(IsNearlyEqual(out[5], 5.0f));
        assert(IsNearlyEqual(out[6], 6.0f));
        assert(IsNearlyEqual(out[7], 0.0f));
        assert(IsNearlyEqual(out[8], 7.0f));
        assert(IsNearlyEqual(out[9], 8.0f));
        assert(IsNearlyEqual(out[10], 9.0f));
        assert(IsNearlyEqual(out[11], 0.0f));
    }

    return 0;
}
