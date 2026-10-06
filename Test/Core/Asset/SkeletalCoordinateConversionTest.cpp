// double基底変換のliteralと全48設定を、実helperだけで検査する。
#include "Animation/SkeletalCoordinateConversion.h"
#include <bit>
#include <cfenv>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <xmmintrin.h>
#define NORVES_COORDINATE_TEST_MXCSR 1
#else
#define NORVES_COORDINATE_TEST_MXCSR 0
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Coordinate check failed: %s:%d %s\n", __FILE__, __LINE__, #value);                   \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace A = NorvesLib::Core::Animation;
namespace B = NorvesLib::Core::Bvh;
using Axis = NorvesLib::Core::AssetImport::SignedAxis;
using Hand = A::SkeletalSourceHandedness;
using Status = A::SkeletalCoordinateStatus;
using Conversion = A::SkeletalCoordinateConversion;
namespace
{
    static_assert(std::numeric_limits<double>::is_iec559);
    void Near(double a, double b)
    {
        CHECK(std::isfinite(a));
        CHECK(std::fabs(a - b) <= 1e-12);
    }
    void Near(const B::Vector3d& a, const B::Vector3d& b)
    {
        Near(a.X, b.X);
        Near(a.Y, b.Y);
        Near(a.Z, b.Z);
    }
    void Near(const B::Matrix3d& a, const B::Matrix3d& b)
    {
        for (size_t i = 0; i < 9; ++i)
        {
            Near(a.Values[i], b.Values[i]);
        }
    }
    void Exact(double a, double b)
    {
        CHECK(std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b));
    }
    void Exact(const B::Vector3d& a, const B::Vector3d& b)
    {
        Exact(a.X, b.X);
        Exact(a.Y, b.Y);
        Exact(a.Z, b.Z);
    }
    void Exact(const B::Matrix3d& a, const B::Matrix3d& b)
    {
        for (size_t i = 0; i < 9; ++i)
        {
            Exact(a.Values[i], b.Values[i]);
        }
    }
    void Exact(const B::RigidTransformd& a, const B::RigidTransformd& b)
    {
        Exact(a.Rotation, b.Rotation);
        Exact(a.Translation, b.Translation);
    }
    B::Matrix3d Matrix(std::initializer_list<double> values)
    {
        CHECK(values.size() == 9);
        B::Matrix3d m;
        size_t i = 0;
        for (double value : values)
        {
            m.Values[i++] = value;
        }
        return m;
    }
    B::Matrix3d Multiply(const B::Matrix3d& a, const B::Matrix3d& b)
    {
        B::Matrix3d out;
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < 3; ++j)
            {
                out.Values[i * 3 + j] = 0;
                for (size_t k = 0; k < 3; ++k)
                {
                    out.Values[i * 3 + j] += a.Values[i * 3 + k] * b.Values[k * 3 + j];
                }
            }
        }
        return out;
    }
    B::Matrix3d Transpose(const B::Matrix3d& a)
    {
        B::Matrix3d out;
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < 3; ++j)
            {
                out.Values[i * 3 + j] = a.Values[j * 3 + i];
            }
        }
        return out;
    }
    B::Vector3d Apply(const B::Matrix3d& a, const B::Vector3d& v)
    {
        return {a.Values[0] * v.X + a.Values[1] * v.Y + a.Values[2] * v.Z,
                a.Values[3] * v.X + a.Values[4] * v.Y + a.Values[5] * v.Z,
                a.Values[6] * v.X + a.Values[7] * v.Y + a.Values[8] * v.Z};
    }
    double Determinant(const B::Matrix3d& a)
    {
        const auto& x = a.Values;
        return x[0] * (x[4] * x[8] - x[5] * x[7]) - x[1] * (x[3] * x[8] - x[5] * x[6]) +
               x[2] * (x[3] * x[7] - x[4] * x[6]);
    }
    Conversion Build(Axis up, Axis forward, Hand hand, double scale = 1)
    {
        Conversion c;
        CHECK(A::BuildSkeletalCoordinateConversion(up, forward, hand, scale, c) == Status::Success);
        CHECK(c.IsValid());
        return c;
    }
    void TestLiteralsAndAliases()
    {
        const auto rz = Matrix({0, -1, 0, 1, 0, 0, 0, 0, 1});
        const auto dense = Matrix({1, 2, 3, 4, 5, 6, 7, 8, 9});
        const B::Vector3d translation{2, -3, 5};
        auto canonical = Build(Axis::PositiveY, Axis::PositiveZ, Hand::Right, 2);
        B::Vector3d v;
        B::Matrix3d m;
        CHECK(A::ConvertSkeletalTranslation(canonical, translation, v) == Status::Success);
        Near(v, {4, -6, 10});
        CHECK(A::ConvertSkeletalMatrixBasis(canonical, dense, m) == Status::Success);
        Exact(m, dense);
        auto proper = Build(Axis::PositiveZ, Axis::NegativeY, Hand::Right, 2);
        CHECK(A::ConvertSkeletalTranslation(proper, translation, v) == Status::Success);
        Near(v, {4, 10, 6});
        CHECK(A::ConvertSkeletalMatrixBasis(proper, rz, m) == Status::Success);
        Near(m, Matrix({0, 0, 1, 0, 1, 0, -1, 0, 0}));
        CHECK(A::ConvertSkeletalMatrixBasis(proper, dense, m) == Status::Success);
        Near(m, Matrix({1, 3, -2, 7, 9, -8, -4, -6, 5}));
        auto improper = Build(Axis::PositiveZ, Axis::NegativeY, Hand::Left, 2);
        CHECK(A::ConvertSkeletalTranslation(improper, translation, v) == Status::Success);
        Near(v, {-4, 10, 6});
        CHECK(A::ConvertSkeletalMatrixBasis(improper, rz, m) == Status::Success);
        Near(m, Matrix({0, 0, -1, 0, 1, 0, 1, 0, 0}));
        Near(Determinant(m), 1);
        CHECK(A::ConvertSkeletalMatrixBasis(improper, dense, m) == Status::Success);
        Near(m, Matrix({1, -3, 2, -7, 9, -8, 4, -6, 5}));
        CHECK(A::ConvertSkeletalMatrixBasis(improper, B::Matrix3d{}, m) == Status::Success);
        Near(m, B::Matrix3d{});
        auto left = Build(Axis::PositiveY, Axis::PositiveZ, Hand::Left);
        CHECK(A::ConvertSkeletalMatrixBasis(left, rz, m) == Status::Success);
        Near(m, Matrix({0, 1, 0, -1, 0, 0, 0, 0, 1}));
        v = translation;
        CHECK(A::ConvertSkeletalTranslation(proper, v, v) == Status::Success);
        Near(v, {4, 10, 6});
        m = dense;
        CHECK(A::ConvertSkeletalMatrixBasis(proper, m, m) == Status::Success);
        Near(m, Matrix({1, 3, -2, 7, 9, -8, -4, -6, 5}));
        const auto maxScale = Build(Axis::PositiveZ, Axis::NegativeY, Hand::Right, std::numeric_limits<double>::max());
        B::Matrix3d scaled;
        CHECK(A::ConvertSkeletalMatrixBasis(maxScale, dense, scaled) == Status::Success);
        Exact(scaled, m);
        auto singular = Matrix({0, 2, 0, 0, 0, 0, 0, 0, -3});
        CHECK(A::ConvertSkeletalMatrixBasis(improper, singular, m) == Status::Success);
        Near(Determinant(m), 0);
        for (double& value : singular.Values)
        {
            value = std::numeric_limits<double>::max();
        }
        CHECK(A::ConvertSkeletalMatrixBasis(improper, singular, m) == Status::Success);
        for (double value : m.Values)
        {
            CHECK(std::isfinite(value) && std::abs(value) == std::numeric_limits<double>::max());
        }
    }
    void TestAllBases()
    {
        const B::Vector3d axisVectors[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const B::Vector3d units[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const auto dense = Matrix({1, 2, 3, 4, 5, 6, 7, 8, 9});
        B::Matrix3d seen[48];
        size_t count = 0;
        for (uint8_t up = 0; up < 6; ++up)
        {
            for (uint8_t forward = 0; forward < 6; ++forward)
            {
                const auto& u = axisVectors[up];
                const auto& f = axisVectors[forward];
                if (u.X * f.X + u.Y * f.Y + u.Z * f.Z != 0)
                {
                    continue;
                }
                for (Hand hand : {Hand::Right, Hand::Left})
                {
                    auto conversion = Build(static_cast<Axis>(up), static_cast<Axis>(forward), hand);
                    B::Vector3d value;
                    CHECK(A::ConvertSkeletalTranslation(conversion, u, value) == Status::Success);
                    Near(value, {0, 1, 0});
                    CHECK(A::ConvertSkeletalTranslation(conversion, f, value) == Status::Success);
                    Near(value, {0, 0, 1});
                    B::Matrix3d basis;
                    for (size_t column = 0; column < 3; ++column)
                    {
                        CHECK(A::ConvertSkeletalTranslation(conversion, units[column], value) == Status::Success);
                        basis.Values[column] = value.X;
                        basis.Values[3 + column] = value.Y;
                        basis.Values[6 + column] = value.Z;
                    }
                    Near(Determinant(basis), hand == Hand::Right ? 1 : -1);
                    Near(Multiply(basis, Transpose(basis)), B::Matrix3d{});
                    for (size_t n = 0; n < count; ++n)
                    {
                        bool same = true;
                        for (size_t i = 0; i < 9; ++i)
                        {
                            same = same && basis.Values[i] == seen[n].Values[i];
                        }
                        CHECK(!same);
                    }
                    CHECK(count < 48);
                    seen[count++] = basis;
                    const B::Vector3d original{2, -3, 5};
                    CHECK(A::ConvertSkeletalTranslation(conversion, original, value) == Status::Success);
                    Near(Apply(Transpose(basis), value), original);
                    B::Matrix3d converted;
                    CHECK(A::ConvertSkeletalMatrixBasis(conversion, dense, converted) == Status::Success);
                    Near(converted, Multiply(Multiply(basis, dense), Transpose(basis)));
                    Near(Multiply(Multiply(Transpose(basis), converted), basis), dense);
                }
            }
        }
        CHECK(count == 48);
    }
    void TestBuildFailures()
    {
        auto c = Build(Axis::PositiveY, Axis::PositiveZ, Hand::Right, 2);
        const auto unchanged = [&]()
        {
            B::Vector3d out;
            CHECK(c.IsValid());
            CHECK(A::ConvertSkeletalTranslation(c, {2, -3, 5}, out) == Status::Success);
            Near(out, {4, -6, 10});
        };
        for (uint8_t up = 0; up < 6; ++up)
        {
            for (uint8_t forward = 0; forward < 6; ++forward)
            {
                if (up / 2 == forward / 2)
                {
                    CHECK(A::BuildSkeletalCoordinateConversion(static_cast<Axis>(up), static_cast<Axis>(forward),
                                                               Hand::Right, 1, c) == Status::InvalidAxes);
                    unchanged();
                }
            }
        }
        CHECK(A::BuildSkeletalCoordinateConversion(static_cast<Axis>(255), Axis::PositiveZ, Hand::Right, 1, c) ==
              Status::InvalidAxes);
        unchanged();
        CHECK(A::BuildSkeletalCoordinateConversion(Axis::PositiveY, static_cast<Axis>(255), Hand::Right, 1, c) ==
              Status::InvalidAxes);
        unchanged();
        for (Hand hand : {Hand::Unspecified, static_cast<Hand>(255)})
        {
            CHECK(A::BuildSkeletalCoordinateConversion(Axis::PositiveY, Axis::PositiveZ, hand, 1, c) ==
                  Status::InvalidHandedness);
            unchanged();
        }
        for (double scale : {0.0, -0.0, -1.0, std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        {
            CHECK(A::BuildSkeletalCoordinateConversion(Axis::PositiveY, Axis::PositiveZ, Hand::Right, scale, c) ==
                  Status::InvalidScale);
            unchanged();
        }
    }
    void TestFailurePreservation()
    {
        const B::Vector3d marker{71, 83, 97};
        const auto matrixMarker = Matrix({11, 12, 13, 14, 15, 16, 17, 18, 19});
        B::Vector3d out = marker;
        B::Matrix3d matrix = matrixMarker;
        Conversion unset;
        CHECK(!unset.IsValid());
        CHECK(A::ConvertSkeletalTranslation(unset, {1, 2, 3}, out) == Status::InvalidConversion);
        Exact(out, marker);
        CHECK(A::ConvertSkeletalMatrixBasis(unset, B::Matrix3d{}, matrix) == Status::InvalidConversion);
        Exact(matrix, matrixMarker);
        const auto c = Build(Axis::PositiveY, Axis::PositiveZ, Hand::Right, 2);
        for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()})
        {
            for (size_t i = 0; i < 9; ++i)
            {
                auto input = matrixMarker;
                input.Values[i] = bad;
                CHECK(A::ConvertSkeletalMatrixBasis(c, input, matrix) == Status::NonFiniteInput);
                Exact(matrix, matrixMarker);
                const auto held = input;
                CHECK(A::ConvertSkeletalMatrixBasis(c, input, input) == Status::NonFiniteInput);
                Exact(input, held);
            }
            for (size_t i = 0; i < 3; ++i)
            {
                B::Vector3d input{1, 2, 3};
                if (i == 0)
                {
                    input.X = bad;
                }
                else if (i == 1)
                {
                    input.Y = bad;
                }
                else
                {
                    input.Z = bad;
                }
                CHECK(A::ConvertSkeletalTranslation(c, input, out) == Status::NonFiniteInput);
                Exact(out, marker);
                const auto held = input;
                CHECK(A::ConvertSkeletalTranslation(c, input, input) == Status::NonFiniteInput);
                Exact(input, held);
            }
        }
        B::Vector3d huge{1, 2, std::numeric_limits<double>::max()};
        CHECK(A::ConvertSkeletalTranslation(c, huge, out) == Status::UnrepresentableOutput);
        Exact(out, marker);
        const auto heldHuge = huge;
        CHECK(A::ConvertSkeletalTranslation(c, huge, huge) == Status::UnrepresentableOutput);
        Exact(huge, heldHuge);
    }
    void TestUnsupportedEnvironment()
    {
        const auto c = Build(Axis::PositiveY, Axis::PositiveZ, Hand::Right, 2);
        const B::Vector3d marker{71, 83, 97};
        const auto matrixMarker = Matrix({11, 12, 13, 14, 15, 16, 17, 18, 19});
        const B::RigidTransformd held{matrixMarker, marker};
        auto unchangedConversion = c;
        const auto rejected = [&]()
        {
            B::Vector3d v = marker;
            B::Matrix3d m = matrixMarker;
            B::RigidTransformd t = held;
            CHECK(A::BuildSkeletalCoordinateConversion(Axis::PositiveZ, Axis::NegativeY, Hand::Left, 7,
                                                       unchangedConversion) == Status::UnsupportedFloatEnvironment);
            CHECK(A::ConvertSkeletalTranslation(c, {std::numeric_limits<double>::max(), 2, 3}, v) ==
                  Status::UnsupportedFloatEnvironment);
            Exact(v, marker);
            CHECK(A::ConvertSkeletalMatrixBasis(c, B::Matrix3d{}, m) == Status::UnsupportedFloatEnvironment);
            Exact(m, matrixMarker);
            CHECK(A::ConvertSkeletalTransform(c, held, t) == Status::UnsupportedFloatEnvironment);
            Exact(t, held);
        };
        const int rounding = std::fegetround();
        CHECK(rounding == FE_TONEAREST);
        for (int mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
        {
            CHECK(std::fesetround(mode) == 0);
            rejected();
            CHECK(std::fegetround() == mode);
            CHECK(std::fesetround(rounding) == 0);
        }
#if NORVES_COORDINATE_TEST_MXCSR
        const unsigned old = _mm_getcsr();
        for (unsigned flags : {0x8000u, 0x0040u, 0x8040u, 0x2000u, 0x4000u, 0x6000u})
        {
            _mm_setcsr(old | flags);
            rejected();
            CHECK((_mm_getcsr() & flags) == flags);
            _mm_setcsr(old);
        }
#endif
        B::Vector3d v;
        CHECK(A::ConvertSkeletalTranslation(unchangedConversion, {1, 2, 3}, v) == Status::Success);
        Near(v, {2, 4, 6});
    }
    void TestSubnormalsAndTransform()
    {
        const double tiny = std::numeric_limits<double>::denorm_min();
        auto identity = Build(Axis::PositiveY, Axis::PositiveZ, Hand::Right);
        B::Vector3d original{tiny, tiny * 2, -tiny}, value;
        CHECK(A::ConvertSkeletalTranslation(identity, original, value) == Status::Success);
        Exact(value, original);
        auto half = Build(Axis::PositiveY, Axis::PositiveZ, Hand::Right, 0.5);
        CHECK(A::ConvertSkeletalTranslation(half, {tiny * 2, 0, 0}, value) == Status::Success);
        Exact(value, {tiny, 0, 0});
        const auto held = value;
        CHECK(A::ConvertSkeletalTranslation(half, {tiny, 0, 0}, value) == Status::UnrepresentableOutput);
        Exact(value, held);
        B::Vector3d zero{-0.0, 0.0, -0.0};
        CHECK(A::ConvertSkeletalTranslation(identity, zero, value) == Status::Success);
        Exact(value, zero);
        auto proper = Build(Axis::PositiveZ, Axis::NegativeY, Hand::Right, 2);
        const B::RigidTransformd parent{Matrix({0, -1, 0, 1, 0, 0, 0, 0, 1}), {1, 2, 3}};
        const B::RigidTransformd child{Matrix({1, 0, 0, 0, 0, -1, 0, 1, 0}), {4, 5, 6}};
        const B::RigidTransformd world{Matrix({0, 0, 1, 1, 0, 0, 0, 1, 0}), {-4, 6, 9}};
        B::RigidTransformd p, c, w;
        CHECK(A::ConvertSkeletalTransform(proper, parent, p) == Status::Success);
        CHECK(A::ConvertSkeletalTransform(proper, child, c) == Status::Success);
        CHECK(A::ConvertSkeletalTransform(proper, world, w) == Status::Success);
        Near(w.Rotation, Matrix({0, 1, 0, 0, 0, -1, -1, 0, 0}));
        Near(w.Translation, {-8, 18, -12});
        Near(Multiply(p.Rotation, c.Rotation), w.Rotation);
        auto translated = Apply(p.Rotation, c.Translation);
        translated.X += p.Translation.X;
        translated.Y += p.Translation.Y;
        translated.Z += p.Translation.Z;
        Near(translated, w.Translation);
        auto alias = world;
        CHECK(A::ConvertSkeletalTransform(proper, alias, alias) == Status::Success);
        Exact(alias, w);
        const auto heldWorld = w;
        auto bad = world;
        bad.Translation.Z = std::numeric_limits<double>::max();
        CHECK(A::ConvertSkeletalTransform(proper, bad, w) == Status::UnrepresentableOutput);
        Exact(w, heldWorld);
        const auto heldBad = bad;
        CHECK(A::ConvertSkeletalTransform(proper, bad, bad) == Status::UnrepresentableOutput);
        Exact(bad, heldBad);
        bad = world;
        bad.Translation.Y = std::numeric_limits<double>::quiet_NaN();
        CHECK(A::ConvertSkeletalTransform(proper, bad, w) == Status::NonFiniteInput);
        Exact(w, heldWorld);
        bad = world;
        bad.Rotation.Values[8] = std::numeric_limits<double>::infinity();
        CHECK(A::ConvertSkeletalTransform(proper, bad, w) == Status::NonFiniteInput);
        Exact(w, heldWorld);
        Conversion unset;
        CHECK(A::ConvertSkeletalTransform(unset, world, w) == Status::InvalidConversion);
        Exact(w, heldWorld);
    }
} // namespace
int main()
{
    TestLiteralsAndAliases();
    TestAllBases();
    TestBuildFailures();
    TestFailurePreservation();
    TestSubnormalsAndTransform();
    TestUnsupportedEnvironment();
    std::puts(
        "SKELETAL_COORDINATE_CONVERSION result=pass explicit_48_bases_handedness_units_general_matrix_finite_subnormal_fp_environment_alias_atomic_no_inference");
    return 0;
}
