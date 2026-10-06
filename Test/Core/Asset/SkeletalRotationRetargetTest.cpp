// 1frame回転の独立literal。実kernelだけをhostでもリンクする。
#include "Animation/SkeletalRotationRetarget.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cfenv>
#include <initializer_list>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Rotation check failed %s:%d %s\n", __FILE__, __LINE__, #x);                          \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace A = NorvesLib::Core::Animation;
namespace B = NorvesLib::Core::Bvh;
namespace C = NorvesLib::Core::Container;
using Status = A::SkeletalRetargetStatus;
namespace
{
    B::Matrix3d Matrix(std::initializer_list<double> values)
    {
        CHECK(values.size() == 9);
        B::Matrix3d m;
        size_t i = 0;
        for (double v : values)
        {
            m.Values[i++] = v;
        }
        return m;
    }
    B::Matrix3d X()
    {
        return Matrix({1, 0, 0, 0, 0, -1, 0, 1, 0});
    }
    B::Matrix3d Y()
    {
        return Matrix({0, 0, 1, 0, 1, 0, -1, 0, 0});
    }
    B::Matrix3d Z()
    {
        return Matrix({0, -1, 0, 1, 0, 0, 0, 0, 1});
    }
    void Near(const B::Matrix3d& a, const B::Matrix3d& b, double tolerance = 1e-6)
    {
        for (size_t i = 0; i < 9; ++i)
        {
            CHECK(std::isfinite(a.Values[i]));
            CHECK(std::abs(a.Values[i] - b.Values[i]) <= tolerance);
        }
    }
    // 出力float quaternionを、テスト側の回転公式から再構成する。
    B::Matrix3d OutputMatrix(const A::SkeletalRetargetRotationValue& v)
    {
        const double length = std::sqrt(double(v.X) * v.X + double(v.Y) * v.Y + double(v.Z) * v.Z + double(v.W) * v.W);
        CHECK(std::abs(length - 1) < 1e-6);
        const double x = v.X / length, y = v.Y / length, z = v.Z / length, w = v.W / length;
        return Matrix({w * w + x * x - y * y - z * z, 2 * (x * y - w * z), 2 * (x * z + w * y), 2 * (x * y + w * z),
                       w * w - x * x + y * y - z * z, 2 * (y * z - w * x), 2 * (x * z - w * y), 2 * (y * z + w * x),
                       w * w - x * x - y * y + z * z});
    }
    struct One
    {
        A::SkeletalRetargetSourceRotation Source[1];
        A::SkeletalRetargetTargetRotation Target[1];
        A::SkeletalJointMappingPair Pairs[1]{{0, 0, 0}};
        B::Matrix3d Correction[1];
        A::SkeletalRetargetRotationWork Work[1];
        A::SkeletalRetargetRotationValue Out[1]{{77, 2, 3, 5, 7}};
        A::SkeletalRetargetRotationRequest Request()
        {
            return {C::Span<const A::SkeletalRetargetSourceRotation>(Source),
                    C::Span<const A::SkeletalRetargetTargetRotation>(Target),
                    C::Span<const A::SkeletalJointMappingPair>(Pairs),
                    C::Span<const B::Matrix3d>(Correction),
                    {0, 0},
                    A::SkeletalSourceReusePolicy::Reject,
                    A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations};
        }
        A::SkeletalRetargetResult Run()
        {
            return A::EvaluateSkeletalRotationFrame(Request(), C::Span<A::SkeletalRetargetRotationWork>(Work),
                                                    C::Span<A::SkeletalRetargetRotationValue>(Out));
        }
    };
    void TestLiteralBranches()
    {
        One f;
        CHECK(f.Run().Succeeded());
        Near(OutputMatrix(f.Out[0]), B::Matrix3d{});
        f.Source[0].WorldRotation = X();
        f.Correction[0] = Z();
        f.Target[0].BindLocalRotation = Y();
        f.Target[0].BindWorldRotation = Y();
        auto result = f.Run();
        CHECK(result.Succeeded());
        CHECK(!result.bFloatRealizationChecked);
        Near(OutputMatrix(f.Out[0]), Matrix({-1, 0, 0, 0, 1, 0, 0, 0, -1}));
        f = One{};
        const B::Matrix3d cases[] = {X(),
                                     Y(),
                                     Z(),
                                     Matrix({1, 0, 0, 0, -1, 0, 0, 0, -1}),
                                     Matrix({-1, 0, 0, 0, 1, 0, 0, 0, -1}),
                                     Matrix({-1, 0, 0, 0, -1, 0, 0, 0, 1}),
                                     Matrix({0, 0, 1, 1, 0, 0, 0, 1, 0})};
        for (const auto& value : cases)
        {
            f.Source[0].WorldRotation = value;
            CHECK(f.Run().Succeeded());
            Near(OutputMatrix(f.Out[0]), value);
            const auto previous = f.Out[0];
            CHECK(f.Run().Succeeded());
            CHECK(std::memcmp(&previous, &f.Out[0], sizeof(previous)) == 0);
        }
        f = One{};
        f.Source[0].WorldRotation.Values[0] += 1e-7;
        CHECK(f.Run().Succeeded());
        Near(OutputMatrix(f.Out[0]), B::Matrix3d{});
    }
    void TestForestAndUnmapped()
    {
        A::SkeletalRetargetSourceRotation source[3]{{-1, Z()}, {0, X()}, {-1, Y()}};
        const auto xy = Matrix({0, 0, 1, 1, 0, 0, 0, 1, 0});
        A::SkeletalRetargetTargetRotation target[6]{{3, Y(), Y()},
                                                    {0, B::Matrix3d{}, Y()},
                                                    {-1, B::Matrix3d{}, B::Matrix3d{}},
                                                    {-1, B::Matrix3d{}, B::Matrix3d{}},
                                                    {-1, X(), X()},
                                                    {4, Y(), xy}};
        A::SkeletalJointMappingPair pairs[3]{{1, 1, 0}, {2, 2, 1}, {0, 3, 2}};
        B::Matrix3d corrections[3];
        A::SkeletalRetargetRotationWork work[6];
        A::SkeletalRetargetRotationValue out[3];
        A::SkeletalRetargetRotationRequest request{C::Span<const A::SkeletalRetargetSourceRotation>(source),
                                                   C::Span<const A::SkeletalRetargetTargetRotation>(target),
                                                   C::Span<const A::SkeletalJointMappingPair>(pairs),
                                                   C::Span<const B::Matrix3d>(corrections),
                                                   {0, 3},
                                                   A::SkeletalSourceReusePolicy::Reject,
                                                   A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations};
        const auto run = [&]
        {
            return A::EvaluateSkeletalRotationFrame(request, C::Span<A::SkeletalRetargetRotationWork>(work),
                                                    C::Span<A::SkeletalRetargetRotationValue>(out));
        };
        CHECK(run().Succeeded());
        CHECK(out[0].TargetIndex == 1 && out[1].TargetIndex == 2 && out[2].TargetIndex == 3);
        Near(OutputMatrix(out[0]), Matrix({0, -1, 0, 0, 0, -1, 1, 0, 0}));
        Near(OutputMatrix(out[1]), Y());
        Near(OutputMatrix(out[2]), Z());
        Near(work[0].WorldRotation, Matrix({0, -1, 0, 0, 0, 1, -1, 0, 0}));
        Near(work[1].WorldRotation, xy);
        Near(work[4].WorldRotation, X());
        Near(work[5].WorldRotation, xy);
        CHECK(work[0].MappingIndex == -1 && work[4].MappingIndex == -1 && work[5].MappingIndex == -1);
        const auto a = pairs[0];
        pairs[0] = pairs[2];
        pairs[2] = a;
        CHECK(run().Succeeded());
        Near(OutputMatrix(out[2]), Matrix({0, -1, 0, 0, 0, -1, 1, 0, 0}));
        pairs[1].SourceIndex = 0;
        CHECK(run().Status == Status::DuplicateSource);
        request.SourceReuse = A::SkeletalSourceReusePolicy::Allow;
        CHECK(run().Succeeded());
        Near(OutputMatrix(out[1]), Z());
        A::SkeletalRetargetRotationValue saved[3];
        std::memcpy(saved, out, sizeof(out));
        target[5].BindLocalRotation.Values[8] = std::numeric_limits<double>::quiet_NaN();
        CHECK(run().Status == Status::InvalidRotation);
        CHECK(std::memcmp(saved, out, sizeof(out)) == 0);
    }
    void TestRefusals()
    {
        One f;
        const auto original = f.Out[0];
        auto reject = [&](Status expected)
        {
            CHECK(f.Run().Status == expected);
            CHECK(std::memcmp(&original, &f.Out[0], sizeof(original)) == 0);
        };
        f.Source[0].ParentIndex = 0;
        reject(Status::InvalidHierarchy);
        f = One{};
        f.Target[0].ParentIndex = 1;
        reject(Status::InvalidHierarchy);
        f = One{};
        f.Pairs[0].SourceIndex = 1;
        reject(Status::InvalidMapping);
        f = One{};
        for (double value : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 2.0})
        {
            for (size_t i = 0; i < 9; ++i)
            {
                f = One{};
                f.Source[0].WorldRotation.Values[i] = value;
                reject(Status::InvalidRotation);
            }
        }
        f = One{};
        f.Correction[0] = Matrix({-1, 0, 0, 0, 1, 0, 0, 0, 1});
        reject(Status::InvalidRotation);
        f = One{};
        f.Target[0].BindWorldRotation.Values[1] = 0.01;
        reject(Status::InvalidRotation);
        f = One{};
        auto r = f.Request();
        auto call = [&]
        {
            return A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(f.Work),
                                                    C::Span<A::SkeletalRetargetRotationValue>(f.Out));
        };
        r.Policy = A::SkeletalRetargetRotationPolicy::Unspecified;
        CHECK(call().Status == Status::InvalidPolicy);
        r = f.Request();
        r.SourceReuse = A::SkeletalSourceReusePolicy::Unspecified;
        CHECK(call().Status == Status::InvalidPolicy);
        r = f.Request();
        r.Root.TargetIndex = 1;
        CHECK(call().Status == Status::InvalidRoot);
        r = f.Request();
        r.Source = C::Span<const A::SkeletalRetargetSourceRotation>(f.Source, 1025);
        CHECK(call().Status == Status::LimitExceeded);
        r = f.Request();
        r.Source = C::Span<const A::SkeletalRetargetSourceRotation>(nullptr, 1);
        CHECK(call().Status == Status::InvalidInput);
        r = f.Request();
        r.Corrections = C::Span<const B::Matrix3d>();
        CHECK(call().Status == Status::InvalidInput);
        r = f.Request();
        r.Source = C::Span<const A::SkeletalRetargetSourceRotation>(
            reinterpret_cast<const A::SkeletalRetargetSourceRotation*>(reinterpret_cast<uintptr_t>(f.Source) + 1), 1);
        CHECK(call().Status == Status::InvalidInput);
        r = f.Request();
        CHECK(A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(f.Work),
                                               C::Span<A::SkeletalRetargetRotationValue>(
                                                   reinterpret_cast<A::SkeletalRetargetRotationValue*>(f.Work), 1))
                  .Status == Status::InvalidInput);
        CHECK(std::memcmp(&original, &f.Out[0], sizeof(original)) == 0);
        const int old = std::fegetround();
        CHECK(std::fesetround(FE_DOWNWARD) == 0);
        r = f.Request();
        CHECK(call().Status == Status::UnsupportedFloatEnvironment);
        CHECK(std::fegetround() == FE_DOWNWARD);
        CHECK(std::fesetround(old) == 0);
        CHECK(std::memcmp(&original, &f.Out[0], sizeof(original)) == 0);
    }
    void TestMappingAndRootRefusals()
    {
        A::SkeletalRetargetSourceRotation source[2]{{-1, B::Matrix3d{}}, {0, B::Matrix3d{}}};
        A::SkeletalRetargetTargetRotation target[2]{{-1, B::Matrix3d{}, B::Matrix3d{}},
                                                    {0, B::Matrix3d{}, B::Matrix3d{}}};
        A::SkeletalJointMappingPair pairs[2]{{0, 0, 0}, {1, 1, 1}};
        B::Matrix3d corrections[2];
        A::SkeletalRetargetRotationWork work[2];
        A::SkeletalRetargetRotationValue out[2]{{77, 2, 3, 5, 7}, {88, 11, 13, 17, 19}};
        A::SkeletalRetargetRotationValue saved[2];
        std::memcpy(saved, out, sizeof(out));
        A::SkeletalRetargetRotationRequest r{C::Span<const A::SkeletalRetargetSourceRotation>(source),
                                             C::Span<const A::SkeletalRetargetTargetRotation>(target),
                                             C::Span<const A::SkeletalJointMappingPair>(pairs),
                                             C::Span<const B::Matrix3d>(corrections),
                                             {0, 0},
                                             A::SkeletalSourceReusePolicy::Allow,
                                             A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations};
        const auto refuse = [&](Status status)
        {
            const auto result = A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(work),
                                                                 C::Span<A::SkeletalRetargetRotationValue>(out));
            CHECK(result.Status == status);
            CHECK(std::memcmp(saved, out, sizeof(out)) == 0);
            return result;
        };
        pairs[1].TargetIndex = 0;
        CHECK(refuse(Status::DuplicateTarget).EntryIndex == 1);
        pairs[1].TargetIndex = 1;
        pairs[0].SourceIndex = 1;
        CHECK(refuse(Status::InvalidRoot).JointIndex == 0);
        pairs[0].SourceIndex = 0;
        r.Root = {1, 0};
        CHECK(refuse(Status::InvalidRoot).Side == A::SkeletalJointMappingSide::Source);
        r.Root = {0, 1};
        CHECK(refuse(Status::InvalidRoot).Side == A::SkeletalJointMappingSide::Target);
        r.Root = {0, 0};
        r.Mappings = C::Span<const A::SkeletalJointMappingPair>(pairs + 1, 1);
        r.Corrections = C::Span<const B::Matrix3d>(corrections, 1);
        A::SkeletalRetargetRotationValue single = out[0];
        CHECK(A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(work),
                                               C::Span<A::SkeletalRetargetRotationValue>(&single, 1))
                  .Status == Status::InvalidRoot);
        CHECK(std::memcmp(&single, &saved[0], sizeof(single)) == 0);
        r.Mappings = C::Span<const A::SkeletalJointMappingPair>(pairs);
        r.Corrections = C::Span<const B::Matrix3d>(corrections);
        corrections[1].Values[0] = std::numeric_limits<double>::quiet_NaN();
        CHECK(refuse(Status::InvalidRotation).EntryIndex == 1);
        corrections[1] = B::Matrix3d{};
        source[0].ParentIndex = 1;
        CHECK(refuse(Status::InvalidHierarchy).Side == A::SkeletalJointMappingSide::Source);
        source[0].ParentIndex = -1;
        CHECK(A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(work, 1),
                                               C::Span<A::SkeletalRetargetRotationValue>(out))
                  .Status == Status::InvalidInput);
        CHECK(A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(work),
                                               C::Span<A::SkeletalRetargetRotationValue>(out, 1))
                  .Status == Status::InvalidInput);
        CHECK(std::memcmp(saved, out, sizeof(out)) == 0);
    }
    void TestBoundedDeepHierarchy()
    {
        // native stackを増やさず、許可した上限の逆順chainを解決する。
        A::SkeletalRetargetSourceRotation source[1];
        A::SkeletalRetargetTargetRotation target[1024];
        A::SkeletalRetargetRotationWork work[1024];
        for (size_t i = 0; i < 1023; ++i)
        {
            target[i].ParentIndex = static_cast<int32_t>(i + 1);
        }
        A::SkeletalJointMappingPair pairs[1]{{0, 1023, 0}};
        B::Matrix3d corrections[1];
        A::SkeletalRetargetRotationValue out[1];
        A::SkeletalRetargetRotationRequest r{C::Span<const A::SkeletalRetargetSourceRotation>(source),
                                             C::Span<const A::SkeletalRetargetTargetRotation>(target),
                                             C::Span<const A::SkeletalJointMappingPair>(pairs),
                                             C::Span<const B::Matrix3d>(corrections),
                                             {0, 1023},
                                             A::SkeletalSourceReusePolicy::Reject,
                                             A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations};
        CHECK(A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(work),
                                               C::Span<A::SkeletalRetargetRotationValue>(out))
                  .Succeeded());
        CHECK(work[0].bDone);
        Near(work[0].WorldRotation, B::Matrix3d{});
        target[1023].ParentIndex = 0;
        CHECK(A::EvaluateSkeletalRotationFrame(r, C::Span<A::SkeletalRetargetRotationWork>(work),
                                               C::Span<A::SkeletalRetargetRotationValue>(out))
                  .Status == Status::InvalidHierarchy);
    }
} // namespace
int main()
{
    TestLiteralBranches();
    TestForestAndUnmapped();
    TestRefusals();
    TestMappingAndRootRefusals();
    TestBoundedDeepHierarchy();
    std::puts(
        "SKELETAL_ROTATION_FRAME result=pass explicit_c_world_local_forest_unmapped_float_export_bounded_atomic_no_root_motion");
}
