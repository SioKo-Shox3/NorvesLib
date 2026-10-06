// schema非依存の名前対応と所有結果を検証する。role/階層/rest互換は対象外。
#include "Animation/SkeletalJointMapping.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Mapping check failed: %s:%d %s\n", __FILE__, __LINE__, #value);                      \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace A = NorvesLib::Core::Animation;
namespace C = NorvesLib::Core::Container;
namespace Asset = NorvesLib::Core::Asset;
using Status = A::SkeletalJointMappingStatus;
using Side = A::SkeletalJointMappingSide;
using Policy = A::SkeletalSourceReusePolicy;
using View = A::SkeletalJointMappingNameView;
using Set = A::SkeletalJointMappingSet;
namespace
{
    template <class Char, size_t N> C::Span<const uint8_t> Bytes(const Char (&text)[N])
    {
        static_assert(sizeof(Char) == 1);
        return {reinterpret_cast<const uint8_t*>(text), N - 1};
    }
    void Same(const Set& a, const Set& b)
    {
        CHECK(a.SourceJointCount == b.SourceJointCount && a.TargetJointCount == b.TargetJointCount);
        CHECK(a.RootMappingIndex == b.RootMappingIndex && a.Pairs.size() == b.Pairs.size());
        CHECK(a.UnmappedSourceIndices == b.UnmappedSourceIndices && a.UnmappedTargetIndices == b.UnmappedTargetIndices);
        for (size_t i = 0; i < a.Pairs.size(); ++i)
        {
            CHECK(a.Pairs[i].SourceIndex == b.Pairs[i].SourceIndex && a.Pairs[i].TargetIndex == b.Pairs[i].TargetIndex);
            CHECK(a.Pairs[i].InputEntryIndex == b.Pairs[i].InputEntryIndex);
        }
    }
    void Empty(const Set& set)
    {
        CHECK(set.SourceJointCount == 0 && set.TargetJointCount == 0 && set.RootMappingIndex == UINT32_MAX);
        CHECK(set.Pairs.empty() && set.UnmappedSourceIndices.empty() && set.UnmappedTargetIndices.empty());
    }
    struct Fixture
    {
        A::SkeletalJointIndex Source, Target;
        C::VariableArray<View> Pairs;
        A::SkeletalJointMappingRoot Root{2, 1};
        A::SkeletalJointMappingLimits Limits;
        Policy Reuse = Policy::Reject;
        Fixture()
        {
            const A::SkeletalJointNameView source[] = {{Bytes("Arm")},    {Bytes(u8"骨🐕")}, {Bytes("Root")},
                                                       {Bytes("Unused")}, {Bytes(u8"é")},    {Bytes(u8"e\u0301")}};
            const A::SkeletalJointNameView target[] = {{Bytes("unused")}, {Bytes("root")}, {Bytes("Arm")},
                                                       {Bytes(u8"骨🐕")}, {Bytes(u8"é")},  {Bytes(u8"e\u0301")}};
            CHECK(A::BuildSkeletalJointIndex(source, {}, Source).Succeeded());
            CHECK(A::BuildSkeletalJointIndex(target, {}, Target).Succeeded());
            Pairs = {{Bytes(u8"骨🐕"), Bytes(u8"骨🐕")}, {Bytes("Root"), Bytes("root")}, {Bytes("Arm"), Bytes("Arm")}};
        }
        A::SkeletalJointMappingResult Resolve(Set& out) const
        {
            return A::ResolveSkeletalJointMappings(Source, Target, Pairs, Root, Reuse, Limits, out);
        }
    };
    void Preserved(A::SkeletalJointMappingResult result, Status status, Side side, const Set& out, const Set& before)
    {
        CHECK(!result.Succeeded() && result.Status == status && result.Side == side);
        Same(out, before);
    }
    void TestOrderAndPolicy()
    {
        Fixture f;
        Set result;
        CHECK(f.Resolve(result).Succeeded());
        CHECK(result.SourceJointCount == 6 && result.TargetJointCount == 6 && result.RootMappingIndex == 1);
        CHECK(result.Pairs.size() == 3);
        const uint32_t expectedSource[] = {1, 2, 0}, expectedTarget[] = {3, 1, 2};
        for (size_t i = 0; i < 3; ++i)
        {
            CHECK(result.Pairs[i].SourceIndex == expectedSource[i] && result.Pairs[i].TargetIndex == expectedTarget[i]);
            CHECK(result.Pairs[i].InputEntryIndex == i);
        }
        CHECK((result.UnmappedSourceIndices == C::VariableArray<uint32_t>{3, 4, 5}));
        CHECK((result.UnmappedTargetIndices == C::VariableArray<uint32_t>{0, 4, 5}));
        const Set held = result;
        f.Pairs.push_back({Bytes("Arm"), Bytes("unused")});
        auto failure = f.Resolve(result);
        Preserved(failure, Status::DuplicateSource, Side::Source, result, held);
        CHECK(failure.EntryIndex == 3 && failure.OtherEntryIndex == 2 && failure.JointIndex == 0);
        f.Reuse = Policy::Allow;
        CHECK(f.Resolve(result).Succeeded());
        CHECK(result.Pairs.size() == 4 && result.Pairs[3].SourceIndex == 0 && result.Pairs[3].TargetIndex == 0);
        CHECK((result.UnmappedTargetIndices == C::VariableArray<uint32_t>{4, 5}));
        result = held;
        f.Pairs[3] = f.Pairs[2];
        failure = f.Resolve(result);
        Preserved(failure, Status::DuplicateTarget, Side::Target, result, held);
        CHECK(failure.EntryIndex == 3 && failure.OtherEntryIndex == 2 && failure.JointIndex == 2);
        f.Reuse = Policy::Reject;
        Preserved(f.Resolve(result), Status::DuplicateTarget, Side::Target, result, held);
        f.Pairs.pop_back();
        f.Reuse = Policy::Unspecified;
        Preserved(f.Resolve(result), Status::InvalidPolicy, Side::None, result, held);
        f.Reuse = static_cast<Policy>(255);
        Preserved(f.Resolve(result), Status::InvalidPolicy, Side::None, result, held);
    }
    void TestRootAndNames()
    {
        Fixture f;
        Set out;
        CHECK(f.Resolve(out).Succeeded());
        const Set held = out;
        f.Root.SourceIndex = UINT32_MAX;
        Preserved(f.Resolve(out), Status::InvalidRoot, Side::Source, out, held);
        f.Root = {2, UINT32_MAX};
        Preserved(f.Resolve(out), Status::InvalidRoot, Side::Target, out, held);
        f.Root = {};
        Preserved(f.Resolve(out), Status::InvalidRoot, Side::Source, out, held);
        f.Root = {2, 1};
        const View root = f.Pairs[1];
        f.Pairs[1] = {Bytes("Arm"), Bytes("root")};
        auto error = f.Resolve(out);
        Preserved(error, Status::RootConflict, Side::Target, out, held);
        CHECK(error.EntryIndex == 1 && error.JointIndex == 1);
        f.Pairs.erase(f.Pairs.begin() + 1);
        Preserved(f.Resolve(out), Status::RootMissing, Side::None, out, held);
        f.Pairs.insert(f.Pairs.begin() + 1, root);
        const View original = f.Pairs[0];
        f.Pairs[0].SourceName = Bytes("missing");
        error = f.Resolve(out);
        Preserved(error, Status::UnknownName, Side::Source, out, held);
        CHECK(error.EntryIndex == 0 && error.NameResult.Status == A::SkeletalJointIndexStatus::NotFound);
        f.Pairs[0] = original;
        f.Pairs[0].TargetName = Bytes("ROOT");
        Preserved(f.Resolve(out), Status::UnknownName, Side::Target, out, held);
        f.Pairs[0].TargetName = Bytes("ns:Arm");
        Preserved(f.Resolve(out), Status::UnknownName, Side::Target, out, held);
        f.Pairs[0] = original;
        f.Pairs[0].SourceName = {};
        error = f.Resolve(out);
        Preserved(error, Status::InvalidName, Side::Source, out, held);
        CHECK(error.NameResult.Status == A::SkeletalJointIndexStatus::EmptyName);
        f.Pairs[0].SourceName = Bytes("A\0B");
        error = f.Resolve(out);
        Preserved(error, Status::InvalidName, Side::Source, out, held);
        CHECK(error.NameResult.EncodingStatus == Asset::SkeletalNameStatus::EmbeddedNul);
        const uint8_t invalid[] = {0xff};
        f.Pairs[0].SourceName = {invalid, 1};
        error = f.Resolve(out);
        Preserved(error, Status::InvalidName, Side::Source, out, held);
        CHECK(error.NameResult.EncodingStatus == Asset::SkeletalNameStatus::InvalidUtf8);
        f.Pairs[0] = original;
        // 正規化せず、別名として元番号4→5を対応させる。
        f.Pairs.push_back({Bytes(u8"é"), Bytes(u8"e\u0301")});
        CHECK(f.Resolve(out).Succeeded());
        CHECK(out.Pairs.back().SourceIndex == 4 && out.Pairs.back().TargetIndex == 5);
    }
    void TestLimitsAndSpans()
    {
        Fixture f;
        Set out;
        CHECK(f.Resolve(out).Succeeded());
        const Set held = out;
        f.Limits.MaxMappings = 3;
        f.Limits.MaxCatalogJoints = 6;
        f.Limits.MaxTotalNameBytes = 28;
        CHECK(f.Resolve(out).Succeeded());
        Same(out, held);
        f.Limits.MaxMappings = 2;
        Preserved(f.Resolve(out), Status::LimitExceeded, Side::None, out, held);
        f.Limits.MaxMappings = 3;
        f.Limits.MaxCatalogJoints = 5;
        Preserved(f.Resolve(out), Status::LimitExceeded, Side::Source, out, held);
        f.Limits.MaxCatalogJoints = 6;
        f.Limits.MaxTotalNameBytes = 27;
        auto error = f.Resolve(out);
        Preserved(error, Status::LimitExceeded, Side::Target, out, held);
        CHECK(error.EntryIndex == 2);
        {
            Fixture larger;
            const A::SkeletalJointNameView target[] = {{Bytes("unused")}, {Bytes("root")}, {Bytes("Arm")},
                                                       {Bytes(u8"骨🐕")}, {Bytes(u8"é")},  {Bytes(u8"e\u0301")},
                                                       {Bytes("extra")}};
            CHECK(A::BuildSkeletalJointIndex(target, {}, larger.Target).Succeeded());
            larger.Limits.MaxCatalogJoints = 6;
            Preserved(larger.Resolve(out), Status::LimitExceeded, Side::Target, out, held);
        }
        f.Limits = {};
        C::VariableArray<uint8_t> longName(4097, static_cast<uint8_t>('x'));
        const auto name = f.Pairs[0].SourceName;
        f.Pairs[0].SourceName = {longName.data(), longName.size()};
        error = f.Resolve(out);
        Preserved(error, Status::LimitExceeded, Side::Source, out, held);
        CHECK(error.NameResult.Status == A::SkeletalJointIndexStatus::LimitExceeded);
        f.Pairs[0].SourceName = {reinterpret_cast<const uint8_t*>(UINTPTR_MAX - 1), 4};
        error = f.Resolve(out);
        Preserved(error, Status::InvalidName, Side::Source, out, held);
        CHECK(error.NameResult.Status == A::SkeletalJointIndexStatus::InvalidInput);
        f.Pairs[0].SourceName = name;
        const auto resolve = [&](C::Span<const View> input)
        {
            return A::ResolveSkeletalJointMappings(f.Source, f.Target, input, f.Root, f.Reuse, f.Limits, out);
        };
        Preserved(resolve({}), Status::InvalidInput, Side::None, out, held);
        Preserved(resolve({nullptr, 1}), Status::InvalidInput, Side::None, out, held);
        alignas(View) uint8_t misaligned[sizeof(View) + 1]{};
        Preserved(resolve({reinterpret_cast<const View*>(misaligned + 1), 1}), Status::InvalidInput, Side::None, out,
                  held);
        const uintptr_t end = UINTPTR_MAX & ~(static_cast<uintptr_t>(alignof(View)) - 1);
        Preserved(resolve({reinterpret_cast<const View*>(end), 1}), Status::InvalidInput, Side::None, out, held);
        A::SkeletalJointIndex empty;
        Preserved(A::ResolveSkeletalJointMappings(empty, f.Target, f.Pairs, f.Root, f.Reuse, f.Limits, out),
                  Status::EmptyIndex, Side::Source, out, held);
        Preserved(A::ResolveSkeletalJointMappings(f.Source, empty, f.Pairs, f.Root, f.Reuse, f.Limits, out),
                  Status::EmptyIndex, Side::Target, out, held);
    }
    Set OwnedAfterInputsDie()
    {
        C::VariableArray<uint8_t> root = {'R', 'o', 'o', 't'}, arm = {'A', 'r', 'm'};
        const C::Span<const uint8_t> r(root), a(arm);
        const A::SkeletalJointNameView source[] = {{r}, {a}}, target[] = {{a}, {r}};
        A::SkeletalJointIndex s, t;
        CHECK(A::BuildSkeletalJointIndex(source, {}, s).Succeeded());
        CHECK(A::BuildSkeletalJointIndex(target, {}, t).Succeeded());
        const View pairs[] = {{r, r}, {a, a}};
        Set out;
        CHECK(A::ResolveSkeletalJointMappings(s, t, pairs, {0, 1}, Policy::Reject, {}, out).Succeeded());
        root[0] = 'X';
        arm.clear();
        s = {};
        t = {};
        return out;
    }
    void TestOwnedCopyMove()
    {
        Set out = OwnedAfterInputsDie();
        CHECK(out.SourceJointCount == 2 && out.TargetJointCount == 2 && out.RootMappingIndex == 0);
        CHECK(out.Pairs[0].SourceIndex == 0 && out.Pairs[0].TargetIndex == 1);
        CHECK(out.Pairs[1].SourceIndex == 1 && out.Pairs[1].TargetIndex == 0);
        CHECK(out.UnmappedSourceIndices.empty() && out.UnmappedTargetIndices.empty());
        const Set original = out;
        Set copy(out);
        copy.Pairs[0].SourceIndex = 99;
        CHECK(out.Pairs[0].SourceIndex == 0);
        copy = out;
        Same(copy, out);
        Set moved(std::move(copy));
        Same(moved, original);
        Empty(copy);
        Set assigned;
        assigned = std::move(moved);
        Same(assigned, original);
        Empty(moved);
        Set* self = &assigned;
        assigned = *self;
        Same(assigned, original);
        assigned = std::move(*self);
        Same(assigned, original);
    }
} // namespace
int main()
{
    TestOrderAndPolicy();
    TestRootAndNames();
    TestLimitsAndSpans();
    TestOwnedCopyMove();
    std::puts(
        "SKELETAL_JOINT_MAPPING result=pass strict_names_original_indices_explicit_reuse_root_unmapped_owned_atomic_no_schema");
    return 0;
}
