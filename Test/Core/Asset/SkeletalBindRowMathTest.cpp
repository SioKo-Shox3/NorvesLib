// bindの共有算術をliteralと失敗時出力保持で検証する。実Sampler同値は別の凍結bit列で確認する。
#include "Animation/SkeletalBindRowMath.h"
#include <cmath>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Bind check failed: %s:%d %s\n", __FILE__, __LINE__, #value);                         \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace M = NorvesLib::Math;
namespace D = NorvesLib::Core::Animation::Detail;
namespace
{
    void Near(float actual, float expected)
    {
        CHECK(std::isfinite(actual));
        CHECK(std::fabs(actual - expected) <= 1e-5f);
    }
    void MatrixNear(const M::Matrix4x4& actual, const M::Matrix4x4& expected)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            Near(actual.values[i], expected.values[i]);
        }
    }
    void MatrixExact(const M::Matrix4x4& actual, const M::Matrix4x4& expected)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            CHECK(std::bit_cast<uint32_t>(actual.values[i]) == std::bit_cast<uint32_t>(expected.values[i]));
        }
    }
    M::Matrix4x4 Marker()
    {
        auto value = M::Matrix4x4::Identity;
        value.m30 = 71;
        value.m31 = 83;
        value.m32 = 97;
        return value;
    }
    void TestLiteralAndAliases()
    {
        const M::Matrix4x4 parent(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1);
        const M::Matrix4x4 child(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 7, 22, 34, 1);
        auto inverseBind = M::Matrix4x4::Identity;
        inverseBind.m30 = -2;
        inverseBind.m31 = -3;
        inverseBind.m32 = -4;
        auto local = M::Matrix4x4::Identity;
        local.m30 = 2;
        local.m31 = 3;
        local.m32 = 4;
        auto output = Marker();
        CHECK(D::TryBuildBindGlobalRow(inverseBind, parent, output));
        MatrixNear(output, child);
        CHECK(D::TryBuildBindLocalRow(child, &parent, output));
        MatrixNear(output, local);
        CHECK(D::TryBuildBindLocalRow(child, nullptr, output));
        MatrixNear(output, child);
        auto alias = inverseBind;
        CHECK(D::TryBuildBindGlobalRow(alias, parent, alias));
        MatrixNear(alias, child);
        alias = parent;
        CHECK(D::TryBuildBindGlobalRow(inverseBind, alias, alias));
        MatrixNear(alias, child);
        alias = child;
        CHECK(D::TryBuildBindLocalRow(alias, &parent, alias));
        MatrixNear(alias, local);
        alias = parent;
        CHECK(D::TryBuildBindLocalRow(child, &alias, alias));
        MatrixNear(alias, local);
        alias = child;
        CHECK(D::TryBuildBindLocalRow(alias, &alias, alias));
        MatrixNear(alias, M::Matrix4x4::Identity);
        alias = inverseBind;
        CHECK(D::TryInverseMatrix(alias, alias));
        MatrixNear(alias, local);
    }
    void TestFailures()
    {
        const auto identity = M::Matrix4x4::Identity;
        auto output = Marker();
        auto singular = identity;
        singular.m00 = 0;
        CHECK(!D::TryInverseMatrix(singular, output));
        MatrixExact(output, Marker());
        CHECK(!D::TryBuildBindGlobalRow(singular, identity, output));
        MatrixExact(output, Marker());
        CHECK(!D::TryBuildBindLocalRow(identity, &singular, output));
        MatrixExact(output, Marker());
        auto alias = singular;
        CHECK(!D::TryInverseMatrix(alias, alias));
        MatrixExact(alias, singular);
        for (float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            auto invalid = identity;
            invalid.m00 = bad;
            CHECK(!D::IsFiniteMatrix(invalid));
            CHECK(!D::TryInverseMatrix(invalid, output));
            MatrixExact(output, Marker());
            CHECK(!D::TryBuildBindGlobalRow(invalid, identity, output));
            MatrixExact(output, Marker());
            CHECK(!D::TryBuildBindGlobalRow(identity, invalid, output));
            MatrixExact(output, Marker());
            CHECK(!D::TryBuildBindLocalRow(invalid, nullptr, output));
            MatrixExact(output, Marker());
            CHECK(!D::TryBuildBindLocalRow(identity, &invalid, output));
            MatrixExact(output, Marker());
        }
        auto half = identity;
        half.m00 = 0.5f;
        auto huge = identity;
        huge.m00 = std::numeric_limits<float>::max();
        CHECK(!D::TryBuildBindGlobalRow(half, huge, output));
        MatrixExact(output, Marker());
        CHECK(!D::TryBuildBindLocalRow(huge, &half, output));
        MatrixExact(output, Marker());
        auto determinantOverflow = huge;
        determinantOverflow.m11 = std::numeric_limits<float>::max();
        CHECK(!D::TryInverseMatrix(determinantOverflow, output));
        MatrixExact(output, Marker());
        auto below = identity;
        below.m00 = std::nextafter(M::Constants::EPSILON, 0.0f);
        CHECK(!D::TryInverseMatrix(below, output));
        MatrixExact(output, Marker());
        // determinantは境界で有限だが、逆行列の並進だけがoverflowする後段失敗。
        auto inverseOverflow = identity;
        inverseOverflow.m00 = M::Constants::EPSILON;
        inverseOverflow.m30 = std::numeric_limits<float>::max();
        CHECK(!D::TryInverseMatrix(inverseOverflow, output));
        MatrixExact(output, Marker());
        alias = inverseOverflow;
        CHECK(!D::TryInverseMatrix(alias, alias));
        MatrixExact(alias, inverseOverflow);
        below.m00 = M::Constants::EPSILON;
        CHECK(D::TryInverseMatrix(below, output));
        CHECK(D::IsFiniteMatrix(output));
    }
    void TestLegacyDecomposition()
    {
        const M::Matrix4x4 local(0, 2, 0, 0, -3, 0, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1);
        const auto trs = D::DecomposeRowTransform(local);
        Near(trs.Translation.x, 5);
        Near(trs.Translation.y, 6);
        Near(trs.Translation.z, 7);
        Near(trs.Scale.x, 2);
        Near(trs.Scale.y, 3);
        Near(trs.Scale.z, 4);
        Near(trs.Rotation.x * trs.Rotation.x + trs.Rotation.y * trs.Rotation.y + trs.Rotation.z * trs.Rotation.z +
                 trs.Rotation.w * trs.Rotation.w,
             1);
        auto reflected = M::Matrix4x4::Identity;
        reflected.m00 = -2;
        Near(D::DecomposeRowTransform(reflected).Scale.x, 2);
        auto extreme = M::Matrix4x4::Identity;
        extreme.m00 = 1e20f;
        extreme.m11 = 1e-20f;
        CHECK(std::isinf(D::DecomposeRowTransform(extreme).Scale.x));
    }
} // namespace
int main()
{
    TestLiteralAndAliases();
    TestFailures();
    TestLegacyDecomposition();
    std::puts(
        "SKELETAL_BIND_ROW_MATH result=pass literal_row_order_alias_failure_output_legacy_decomposition_no_new_trs_refusal");
    return 0;
}
