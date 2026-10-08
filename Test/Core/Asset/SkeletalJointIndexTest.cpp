// 元joint番号とUTF8完全一致・所有・native変換をliteralで検査する。
#include "Animation/SkeletalJointIndex.h"
#include "Resource/SkeletalGltfData.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "JOINT_INDEX line %d: %s\n", __LINE__, #value);                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace
{
    namespace A = NorvesLib::Core::Animation;
    namespace S = NorvesLib::Core::Skeletal;
    namespace C = NorvesLib::Core::Container;
    namespace Codec = NorvesLib::Core::Asset;
    using Bytes = C::VariableArray<uint8_t>;
    using View = C::Span<const uint8_t>;
    using Status = A::SkeletalJointIndexStatus;
    View Text(const char* text)
    {
        return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
    }
    A::SkeletalJointIndex Build(C::Span<const A::SkeletalJointNameView> names,
                                const A::SkeletalJointIndexLimits& limits = {})
    {
        A::SkeletalJointIndex out;
        const auto result = A::BuildSkeletalJointIndex(names, limits, out);
        CHECK(result.Succeeded() && out.GetCount() == names.size());
        return out;
    }
    A::SkeletalJointIndex Held()
    {
        const A::SkeletalJointNameView names[] = {{Text("Keep")}};
        return Build(names);
    }
    void Find(const A::SkeletalJointIndex& index, View name, uint32_t expected)
    {
        uint32_t value = 77;
        CHECK(A::FindSkeletalJointIndex(index, name, value).Succeeded() && value == expected);
    }
    void Miss(const A::SkeletalJointIndex& index, View name, Status expected)
    {
        uint32_t value = 77;
        CHECK(A::FindSkeletalJointIndex(index, name, value).Status == expected && value == 77);
    }
    A::SkeletalJointIndexResult Reject(C::Span<const A::SkeletalJointNameView> names, Status expected,
                                       const A::SkeletalJointIndexLimits& limits = {})
    {
        auto held = Held();
        const auto result = A::BuildSkeletalJointIndex(names, limits, held);
        CHECK(result.Status == expected && !result.Succeeded() && held.GetCount() == 1);
        Find(held, Text("Keep"), 0);
        return result;
    }
    void ExactAndOwnership()
    {
        Bytes first = {'Z', 'e', 'b', 'r', 'a'}, second = {'R', 'o', 'o', 't'};
        const A::SkeletalJointNameView names[] = {{first},
                                                  {second},
                                                  {Text("root")},
                                                  {Text("Root2")},
                                                  {Text("mixamorig:Root")},
                                                  {Text("Joint A")},
                                                  {Text("Joint\nA")},
                                                  {Text("\xc3\xa9")},
                                                  {Text("e\xcc\x81")},
                                                  {Text("\xe9\xaa\xa8\xf0\x9f\x90\xba")}};
        auto index = Build(names);
        for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            Find(index, names[i].Utf8, i);
        }
        const A::SkeletalJointNameView reordered[] = {{Text("Root")}, {Text("Zebra")}};
        const auto other = Build(reordered);
        Find(other, Text("Root"), 0);
        Find(index, Text("Root"), 1);
        Miss(index, Text("Roo"), Status::NotFound);
        Miss(index, Text("Root20"), Status::NotFound);
        Miss(index, Text("ROOT"), Status::NotFound);
        Miss(index, Text("unknown"), Status::NotFound);
        first.assign(1, '!');
        second.clear();
        second.shrink_to_fit();
        Find(index, Text("Zebra"), 0);
        Find(index, Text("Root"), 1);
        auto copy = index;
        index = A::SkeletalJointIndex{};
        Find(copy, Text("Root"), 1);
        A::SkeletalJointIndex assigned;
        assigned = copy;
        copy = copy;
        Find(copy, Text("Root"), 1);
        A::SkeletalJointIndex moved(std::move(copy));
        CHECK(copy.GetCount() == 0);
        Miss(copy, Text("Root"), Status::NotFound);
        Find(moved, Text("Root"), 1);
        auto destination = Held();
        destination = std::move(moved);
        CHECK(moved.GetCount() == 0);
        Miss(moved, Text("Root"), Status::NotFound);
        Find(destination, Text("Root"), 1);
        auto* const sameDestination = &destination;
        destination = std::move(*sameDestination);
        Find(destination, Text("Zebra"), 0);
        Find(assigned, Text("\xe9\xaa\xa8\xf0\x9f\x90\xba"), 9);
    }
    void InvalidAndLimits()
    {
        Reject({}, Status::InvalidInput);
        Reject({nullptr, size_t{1}}, Status::InvalidInput);
        alignas(A::SkeletalJointNameView) uint8_t unaligned[sizeof(A::SkeletalJointNameView) + 1]{};
        Reject({reinterpret_cast<const A::SkeletalJointNameView*>(unaligned + 1), size_t{1}}, Status::InvalidInput);
        const auto top = UINTPTR_MAX - (alignof(A::SkeletalJointNameView) - 1);
        Reject({reinterpret_cast<const A::SkeletalJointNameView*>(top), size_t{1}}, Status::InvalidInput);
        const A::SkeletalJointNameView empty[] = {{{}}};
        Reject(empty, Status::EmptyName);
        const A::SkeletalJointNameView missing[] = {{{nullptr, size_t{1}}}};
        Reject(missing, Status::InvalidInput);
        const uint8_t nul[] = {'A', 0, 'B'};
        const A::SkeletalJointNameView embedded[] = {{nul}};
        CHECK(Reject(embedded, Status::InvalidName).EncodingStatus == Codec::SkeletalNameStatus::EmbeddedNul);
        const uint8_t invalid[] = {0xed, 0xa0, 0x80};
        const A::SkeletalJointNameView malformed[] = {{invalid}};
        CHECK(Reject(malformed, Status::InvalidName).EncodingStatus == Codec::SkeletalNameStatus::InvalidUtf8);
        const uint8_t badPrefix[] = {0xc0, 0xaf};
        const A::SkeletalJointNameView overlong[] = {{badPrefix}};
        Reject(overlong, Status::InvalidName);
        const A::SkeletalJointNameView duplicates[] = {{Text("Root")}, {Text("Other")}, {Text("Root")}};
        const auto duplicate = Reject(duplicates, Status::DuplicateName);
        CHECK(duplicate.NameIndex == 2 && duplicate.OtherNameIndex == 0);
        const A::SkeletalJointNameView names[] = {{Text("Root")}, {Text("Bone")}};
        A::SkeletalJointIndexLimits limits;
        limits.MaxJoints = 2;
        limits.MaxNameBytes = 4;
        limits.MaxTotalBytes = 8;
        const auto index = Build(names, limits);
        Find(index, Text("Root"), 0);
        auto smaller = limits;
        smaller.MaxJoints = 1;
        Reject(names, Status::LimitExceeded, smaller);
        smaller = limits;
        smaller.MaxNameBytes = 3;
        Reject(names, Status::LimitExceeded, smaller);
        smaller = limits;
        smaller.MaxTotalBytes = 7;
        Reject(names, Status::LimitExceeded, smaller);
        smaller = limits;
        smaller.MaxJoints = 0;
        Reject(names, Status::LimitExceeded, smaller);
        Miss(index, Text("Longer"), Status::LimitExceeded);
        Miss(index, {}, Status::EmptyName);
        Miss(index, nul, Status::InvalidName);
        Miss(index, invalid, Status::InvalidName);
        Miss(index, {nullptr, size_t{1}}, Status::InvalidInput);
        const View badExtent{reinterpret_cast<const uint8_t*>(UINTPTR_MAX - 1), size_t{4}};
        Miss(index, badExtent, Status::InvalidInput);
        const A::SkeletalJointNameView invalidExtent[] = {{badExtent}};
        Reject(invalidExtent, Status::InvalidInput);
        const A::SkeletalJointNameView oversized[] = {{{reinterpret_cast<const uint8_t*>("X"), SIZE_MAX}}};
        Reject(oversized, Status::LimitExceeded);
    }
    template <class Char> Bytes Encode(C::Span<const Char> name)
    {
        const auto measured = Codec::MeasureSkeletalNameEncoding<Char>(2, name);
        CHECK(measured.Succeeded());
        Bytes encoded(measured.ByteCount);
        CHECK(Codec::EncodeSkeletalWireName<Char>(2, name, encoded).Succeeded());
        return encoded;
    }
    void UnicodeWidths()
    {
        const uint8_t literal[] = {0xe9, 0xaa, 0xa8, 0xf0, 0x9f, 0x90, 0xba};
        const char16_t utf16[] = {0x9aa8, 0xd83d, 0xdc3a};
        const char32_t utf32[] = {0x9aa8, 0x1f43a};
        const auto a = Encode<char16_t>(utf16), b = Encode<char32_t>(utf32);
        CHECK(a.size() == 7 && a == b && std::memcmp(a.data(), literal, 7) == 0);
        const A::SkeletalJointNameView source[] = {{a}, {Text("Root")}};
        const auto index = Build(source);
        Find(index, literal, 0);
        const char16_t bad16[] = {0xd800};
        const char32_t bad32[] = {0x110000};
        CHECK(Codec::MeasureSkeletalNameEncoding<char16_t>(2, bad16).Status ==
              Codec::SkeletalNameStatus::InvalidUnicode);
        CHECK(Codec::MeasureSkeletalNameEncoding<char32_t>(2, bad32).Status ==
              Codec::SkeletalNameStatus::InvalidUnicode);
        const A::SkeletalJointNameView duplicate[] = {{a}, {b}};
        Reject(duplicate, Status::DuplicateName);
    }
    C::String Native(View utf8)
    {
        using Char = C::String::value_type;
        const auto measured = Codec::MeasureSkeletalNameDecoding<Char>(2, utf8);
        CHECK(measured.Succeeded());
        C::VariableArray<Char> units(measured.CodeUnitCount);
        CHECK(Codec::DecodeSkeletalWireName<Char>(2, utf8, units).Succeeded());
        C::String result;
        result.append(units.data(), units.size());
        return result;
    }
    void NativeAdapter()
    {
        C::VariableArray<S::SkeletalJoint> joints(3);
        joints[0].Name = Native(Text("\xe9\xaa\xa8\xf0\x9f\x90\xba"));
        joints[1].Name = Native(Text("Root"));
        joints[2].Name = Native(Text("Joint A"));
        A::SkeletalJointIndex index;
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, {}, index).Succeeded());
        Find(index, Text("Root"), 1);
        Find(index, Text("\xe9\xaa\xa8\xf0\x9f\x90\xba"), 0);
        A::SkeletalJointIndexLimits limits;
        limits.MaxJoints = 3;
        limits.MaxNameBytes = 7;
        limits.MaxTotalBytes = 18;
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, limits, index).Succeeded());
        limits.MaxNameBytes = 6;
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, limits, index).Status == Status::LimitExceeded);
        Find(index, Text("Root"), 1);
        limits.MaxNameBytes = 7;
        limits.MaxTotalBytes = 17;
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, limits, index).Status == Status::LimitExceeded);
        joints[2].Name = joints[1].Name;
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, {}, index).Status == Status::DuplicateName);
        Find(index, Text("Root"), 1);
        joints[2].Name.clear();
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, {}, index).Status == Status::EmptyName);
        using Char = C::String::value_type;
        joints[2].Name.push_back(static_cast<Char>(0));
        const auto nul = A::BuildSkeletalJointIndexFromJoints(joints, {}, index);
        CHECK(nul.Status == Status::InvalidName && nul.EncodingStatus == Codec::SkeletalNameStatus::EmbeddedNul);
        joints[2].Name.clear();
        if constexpr (sizeof(Char) == 1)
        {
            joints[2].Name.push_back(static_cast<Char>(0xff));
        }
        else
        {
            joints[2].Name.push_back(static_cast<Char>(0xd800));
        }
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, {}, index).Status == Status::InvalidName);
        Find(index, Text("Root"), 1);
        CHECK(A::BuildSkeletalJointIndexFromJoints({}, {}, index).Status == Status::InvalidInput);
        CHECK(A::BuildSkeletalJointIndexFromJoints({nullptr, size_t{1}}, {}, index).Status == Status::InvalidInput);
        A::SkeletalJointIndexLimits countLimit;
        countLimit.MaxJoints = 2;
        CHECK(A::BuildSkeletalJointIndexFromJoints(joints, countLimit, index).Status == Status::LimitExceeded);
        alignas(S::SkeletalJoint) uint8_t nativeMisaligned[sizeof(S::SkeletalJoint) + 1]{};
        CHECK(A::BuildSkeletalJointIndexFromJoints(
                  {reinterpret_cast<const S::SkeletalJoint*>(nativeMisaligned + 1), size_t{1}}, {}, index)
                  .Status == Status::InvalidInput);
        const auto top = UINTPTR_MAX - (alignof(S::SkeletalJoint) - 1);
        CHECK(
            A::BuildSkeletalJointIndexFromJoints({reinterpret_cast<const S::SkeletalJoint*>(top), size_t{1}}, {}, index)
                .Status == Status::InvalidInput);
        joints[0].Name = Native(Text("Changed"));
        Find(index, Text("\xe9\xaa\xa8\xf0\x9f\x90\xba"), 0);
        Miss(index, Text("TooLongName"), Status::LimitExceeded);
        joints.clear();
        joints.shrink_to_fit();
        Find(index, Text("\xe9\xaa\xa8\xf0\x9f\x90\xba"), 0);
    }
} // namespace
int main()
{
    ExactAndOwnership();
    InvalidAndLimits();
    UnicodeWidths();
    NativeAdapter();
    std::puts(
        "SKELETAL_JOINT_INDEX result=pass strict_utf8_original_indices_owned_copy_move_limits_native_codec_atomic_no_runtime_change");
    return 0;
}
