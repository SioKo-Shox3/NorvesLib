// BVHの生の順序・値・所有と拒否境界を、解析値から独立したliteralで反証する。
#include "Resource/BvhDecode.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "BVH line %d: %s\n", __LINE__, #value);                                               \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace
{
    namespace B = NorvesLib::Core::Bvh;
    namespace C = NorvesLib::Core::Container;
    using Buffer = C::VariableArray<uint8_t>;
    using View = C::Span<const uint8_t>;
    using Status = B::BvhDecodeStatus;
    View Text(const char* text)
    {
        return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
    }
    Buffer Copy(const char* text)
    {
        const auto bytes = Text(text);
        return Buffer(bytes.begin(), bytes.end());
    }
    void Append(Buffer& target, const char* text)
    {
        const auto bytes = Text(text);
        target.insert(target.end(), bytes.begin(), bytes.end());
    }
    bool Equal(View bytes, const char* text)
    {
        const auto expected = Text(text);
        return bytes.size() == expected.size() && std::equal(bytes.begin(), bytes.end(), expected.begin());
    }
    Buffer Replace(Buffer bytes, const char* from, const char* to)
    {
        const auto needle = Text(from), replacement = Text(to);
        const auto at = std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
        CHECK(at != bytes.end());
        const size_t offset = at - bytes.begin();
        Buffer out(bytes.begin(), at);
        out.insert(out.end(), replacement.begin(), replacement.end());
        out.insert(out.end(), bytes.begin() + offset + needle.size(), bytes.end());
        return out;
    }
    constexpr const char* Fixture = "HIERARCHY\nROOT Hips {\nOFFSET +0 1e0 -2.5\n"
                                    "CHANNELS 6 Xposition Zrotation Yposition Xrotation Zposition Yrotation\n"
                                    "JOINT Bone { OFFSET 0 2 0 CHANNELS 3 Zrotation Yrotation Xrotation\n"
                                    "End Site { OFFSET 0 .5 0 }\n}\n"
                                    "JOINT Static { OFFSET -1 0 0 CHANNELS 0 }\n}\n"
                                    "MOTION\nFrames: 2\nFrame Time: .0625\n"
                                    "10 20 30 40 50 60 70 80 90\n11 21 31 41 51 61 71 81 91";
    constexpr const char* Single =
        "HIERARCHY ROOT Root { OFFSET 0 0 0 CHANNELS 1 Xrotation }\nMOTION\nFrames: 1\nFrame Time: 1\n-0";
    B::BvhDocument Parse(View bytes, const B::BvhDecodeLimits& limits = {})
    {
        B::BvhDocument out;
        const auto result = B::DecodeBvh(bytes, limits, out);
        if (!result.Succeeded())
        {
            std::fprintf(stderr, "BVH status=%u byte=%zu line=%zu column=%zu\n", static_cast<unsigned>(result.Status),
                         result.ByteOffset, result.Line, result.Column);
        }
        CHECK(result.Succeeded());
        return out;
    }
    void Reject(View bytes, Status expected, const B::BvhDecodeLimits& limits = {})
    {
        auto held = Parse(Text(Single));
        const auto result = B::DecodeBvh(bytes, limits, held);
        CHECK(result.Status == expected && !result.Succeeded());
        CHECK(result.ByteOffset <= bytes.size() && result.Line >= 1 && result.Column >= 1);
        CHECK(held.FrameCount == 1 && held.FrameTimeSeconds == 1 && held.Values.size() == 1 && held.Values[0] == 0 &&
              std::signbit(held.Values[0]));
        CHECK(held.Joints.size() == 1 && Equal(held.Joints[0].Name, "Root"));
    }
    void ValuesAndOwnership()
    {
        auto bytes = Copy(Fixture);
        auto document = Parse(bytes);
        CHECK(document.Joints.size() == 3 && document.Channels.size() == 9 && document.Values.size() == 18);
        CHECK(document.FrameCount == 2 && document.FrameTimeSeconds == .0625);
        CHECK(Equal(document.Joints[0].Name, "Hips") && document.Joints[0].Parent == UINT32_MAX);
        CHECK(Equal(document.Joints[1].Name, "Bone") && document.Joints[1].Parent == 0);
        CHECK(Equal(document.Joints[2].Name, "Static") && document.Joints[2].Parent == 0);
        CHECK(document.Joints[0].Offset.X == 0 && document.Joints[0].Offset.Y == 1 &&
              document.Joints[0].Offset.Z == -2.5);
        CHECK(document.Joints[1].Offset.Y == 2 && document.Joints[2].Offset.X == -1);
        CHECK(document.Joints[0].ChannelOffset == 0 && document.Joints[0].ChannelCount == 6);
        CHECK(document.Joints[1].ChannelOffset == 6 && document.Joints[1].ChannelCount == 3);
        CHECK(document.Joints[2].ChannelOffset == 9 && document.Joints[2].ChannelCount == 0);
        CHECK(!document.Joints[0].bHasEndSite && document.Joints[1].bHasEndSite && !document.Joints[2].bHasEndSite);
        CHECK(document.Joints[1].EndSiteOffset.Y == .5);
        const B::Channel channels[] = {B::Channel::Xposition, B::Channel::Zrotation, B::Channel::Yposition,
                                       B::Channel::Xrotation, B::Channel::Zposition, B::Channel::Yrotation,
                                       B::Channel::Zrotation, B::Channel::Yrotation, B::Channel::Xrotation};
        for (size_t i = 0; i < 9; ++i)
        {
            CHECK(document.Channels[i] == channels[i] && document.Values[i] == (i + 1) * 10 &&
                  document.Values[i + 9] == (i + 1) * 10 + 1);
        }
        auto copied = document;
        bytes.assign(1, 0xff);
        bytes.clear();
        bytes.shrink_to_fit();
        document = {};
        CHECK(copied.Values[17] == 91 && Equal(copied.Joints[2].Name, "Static"));
        auto moved = std::move(copied);
        CHECK(moved.Values[0] == 10 && Equal(moved.Joints[1].Name, "Bone"));
        auto positioned = Replace(Copy(Fixture), "CHANNELS 3 Zrotation Yrotation Xrotation",
                                  "CHANNELS 3 Xposition Zposition Yposition");
        const auto local = Parse(positioned);
        CHECK(local.Channels[6] == B::Channel::Xposition && local.Channels[7] == B::Channel::Zposition &&
              local.Values[8] == 90);
        auto unicode = Replace(Copy(Fixture), "Hips", "mixamorig:\xe7\x8a\xac");
        Buffer crlf = {0xef, 0xbb, 0xbf};
        for (uint8_t c : unicode)
        {
            if (c == '\n')
            {
                crlf.push_back('\r');
                crlf.push_back('\n');
            }
            else
            {
                crlf.push_back(c == ' ' ? '\t' : c);
            }
        }
        crlf = Replace(std::move(crlf), "Frames:", "Frames :");
        crlf = Replace(std::move(crlf), "Time:", "Time :");
        crlf = Replace(std::move(crlf), "HIERARCHY", "hierarchy");
        const auto decoded = Parse(crlf);
        CHECK(Equal(decoded.Joints[0].Name, "mixamorig:\xe7\x8a\xac") && decoded.Values[17] == 91);
        const auto nonBmp = Parse(Replace(Copy(Fixture), "Hips", "\xf0\x9f\x90\x95"));
        CHECK(Equal(nonBmp.Joints[0].Name, "\xf0\x9f\x90\x95"));
        const auto blankLines = Parse(Replace(Copy(Fixture), "\n10 20", "\n\n\t\n10 20"));
        CHECK(blankLines.Values.size() == 18 && blankLines.Values[17] == 91);
        const auto exponent = Parse(Replace(Copy(Single), "\n-0", "\n+1.25E+2"));
        CHECK(exponent.Values[0] == 125);
    }
    void ChannelOrders()
    {
        const char* orders[] = {"Xrotation Yrotation Zrotation", "Xrotation Zrotation Yrotation",
                                "Yrotation Xrotation Zrotation", "Yrotation Zrotation Xrotation",
                                "Zrotation Xrotation Yrotation", "Zrotation Yrotation Xrotation"};
        const unsigned expected[][3] = {{3, 4, 5}, {3, 5, 4}, {4, 3, 5}, {4, 5, 3}, {5, 3, 4}, {5, 4, 3}};
        for (size_t order = 0; order < 6; ++order)
        {
            auto bytes = Copy("HIERARCHY ROOT R { OFFSET 0 0 0 CHANNELS 3 ");
            Append(bytes, orders[order]);
            Append(bytes, " }\nMOTION\nFrames: 1\nFrame Time: 0.1\n17 29 43\n");
            const auto document = Parse(bytes);
            for (size_t i = 0; i < 3; ++i)
            {
                CHECK(static_cast<unsigned>(document.Channels[i]) == expected[order][i]);
            }
            CHECK(document.Values[0] == 17 && document.Values[1] == 29 && document.Values[2] == 43);
        }
    }
    void Refusals()
    {
        Reject({}, Status::InvalidHeader);
        Reject(Text("HIERARCHY"), Status::InvalidHierarchy);
        Reject(Replace(Copy(Fixture), "MOTION", ""), Status::InvalidHeader);
        Reject(Replace(Copy(Fixture), "JOINT Bone", "JOINT Hips"), Status::DuplicateJointName);
        Reject(Replace(Copy(Fixture), "Xposition Zrotation", "Xposition Xposition"), Status::InvalidChannels);
        Reject(Replace(Copy(Fixture), "Xposition", "Qrotation"), Status::InvalidChannels);
        Reject(Replace(Copy(Fixture), "CHANNELS 6", "CHANNELS 7"), Status::InvalidChannels);
        Reject(Replace(Copy(Fixture), "OFFSET +0 1e0 -2.5", "OFFSET +0 1e0 -2.5 OFFSET 0 0 0"),
               Status::InvalidHierarchy);
        Reject(Replace(Copy(Fixture), "End Site { OFFSET 0 .5 0 }", "End Site { OFFSET 0 .5 0 CHANNELS 0 }"),
               Status::InvalidHierarchy);
        Reject(Replace(Copy(Fixture), "End Site { OFFSET 0 .5 0 }", "End Site { OFFSET 0 .5 0 JOINT E {} }"),
               Status::InvalidHierarchy);
        Reject(Replace(Copy(Fixture), "End Site { OFFSET 0 .5 0 }",
                       "End Site { OFFSET 0 .5 0 } End Site { OFFSET 0 0 0 }"),
               Status::InvalidHierarchy);
        Reject(Replace(Copy(Fixture), "End Site {", "End Site Named {"), Status::InvalidHierarchy);
        Reject(Replace(Copy(Fixture), "ROOT Hips", "ROOT \"Hips\""), Status::InvalidHierarchy);
        Reject(Replace(Copy(Fixture), "Frames: 2", "Frames: 0"), Status::FrameCountMismatch);
        Reject(Replace(Copy(Fixture), "Frames: 2", "Frames: 3"), Status::FrameCountMismatch);
        Reject(Replace(Copy(Fixture), "Frames: 2", "Frames: 1"), Status::TrailingInput);
        Reject(Replace(Copy(Fixture), "60 70", "60\n70"), Status::FrameCountMismatch);
        Reject(Replace(Copy(Fixture), "80 90\n", "80 90 99\n"), Status::FrameCountMismatch);
        Reject(Replace(Copy(Fixture), "Frame Time: .0625", "Frame Time: 0"), Status::InvalidFrameTime);
        Reject(Replace(Copy(Fixture), "Frame Time: .0625", "Frame Time: -1"), Status::InvalidFrameTime);
        Reject(Replace(Copy(Fixture), "Frame Time: .0625", "Frame Time: NaN"), Status::InvalidFrameTime);
        Reject(Replace(Copy(Fixture), "Frame Time: .0625", "Frame Time: 1e999"), Status::InvalidFrameTime);
        const char* invalid[] = {"NaN", "Inf", "0x10", "1,2", "1e", "+", "1e999", "1e-999"};
        for (const char* value : invalid)
        {
            Reject(Replace(Copy(Single), "-0", value), Status::InvalidNumber);
        }
        auto trailing = Copy(Fixture);
        Append(trailing, "\nMOTION");
        Reject(trailing, Status::TrailingInput);
        auto noChannels = Replace(Copy(Single), "CHANNELS 1 Xrotation", "CHANNELS 0");
        Reject(noChannels, Status::InvalidChannels);
        Reject(Replace(Copy(Fixture), "Frames: 2", "Frames: 18446744073709551616"), Status::LimitExceeded);
        Reject(Replace(Copy(Fixture), "Frames: 2", "Frames: 4294967295"), Status::LimitExceeded);
        auto bytes = Copy(Fixture);
        bytes[1] = 0;
        Reject(bytes, Status::InvalidCharacter);
        Reject(Replace(Copy(Fixture), "Hips", "\xc2\x80"), Status::InvalidCharacter);
        bytes = Copy(Fixture);
        bytes[1] = 0xc0;
        Reject(bytes, Status::InvalidUtf8);
        bytes = Copy(Fixture);
        bytes[1] = 0xed;
        bytes[2] = 0xa0;
        bytes[3] = 0x80;
        Reject(bytes, Status::InvalidUtf8);
        bytes = Copy(Fixture);
        bytes.back() = 0xf0;
        Reject(bytes, Status::InvalidUtf8);
        bytes = Copy(Fixture);
        bytes[1] = 0xef;
        bytes[2] = 0xbb;
        bytes[3] = 0xbf;
        Reject(bytes, Status::InvalidCharacter);
        const auto missingBrace = Replace(Copy(Fixture), "}\nMOTION", "MOTION");
        Reject(missingBrace, Status::InvalidHierarchy);
        auto bad = Copy("HIERARCHY\r\nROOT R {\r\nOFFSET 0 0 nope\r\n}");
        B::BvhDocument held;
        const auto where = B::DecodeBvh(bad, {}, held);
        CHECK(where.Status == Status::InvalidNumber && where.Line == 3 && where.Column == 12);
        CHECK(Equal({bad.data() + where.ByteOffset, 4}, "nope"));
    }
    void LargeHierarchy()
    {
        for (bool bDeep : {false, true})
        {
            auto bytes = Copy("HIERARCHY ROOT R { OFFSET 0 0 0 CHANNELS 1 Xrotation\n");
            for (uint32_t i = 0; i < 256; ++i)
            {
                Append(bytes, "JOINT J");
                char number[16];
                const auto converted = std::to_chars(number, number + sizeof(number), i);
                CHECK(converted.ec == std::errc{});
                bytes.insert(bytes.end(), number, converted.ptr);
                Append(bytes, " { OFFSET 0 1 0 CHANNELS 0\n");
                if (!bDeep)
                {
                    Append(bytes, "}\n");
                }
            }
            if (bDeep)
            {
                for (size_t i = 0; i < 256; ++i)
                {
                    Append(bytes, "}\n");
                }
            }
            Append(bytes, "}\nMOTION\nFrames: 1\nFrame Time: 1\n0");
            B::BvhDecodeLimits limits;
            if (bDeep)
            {
                Reject(bytes, Status::LimitExceeded);
                limits.MaxDepth = 257;
            }
            const auto parsed = Parse(bytes, limits);
            CHECK(parsed.Joints.size() == 257 && parsed.Values.size() == 1);
            for (size_t i = 1; i < parsed.Joints.size(); ++i)
            {
                CHECK(parsed.Joints[i].Parent == (bDeep ? i - 1 : 0));
            }
            limits.MaxJoints = 256;
            Reject(bytes, Status::LimitExceeded, limits);
        }
    }
    void Limits()
    {
        const auto bytes = Copy(Fixture);
        B::BvhDecodeLimits limits;
        limits.MaxInputBytes = bytes.size();
        limits.MaxJoints = 3;
        limits.MaxDepth = 2;
        limits.MaxFrames = 2;
        limits.MaxValues = 18;
        limits.MaxNameBytes = 6;
        limits.MaxNumberBytes = 5;
        CHECK(Parse(bytes, limits).Values.size() == 18);
        auto small = limits;
        --small.MaxInputBytes;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        --small.MaxJoints;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        --small.MaxDepth;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        --small.MaxFrames;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        --small.MaxValues;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        --small.MaxNameBytes;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        --small.MaxNumberBytes;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        small.MaxDepth = 0;
        Reject(bytes, Status::LimitExceeded, small);
        small = limits;
        small.MaxJoints = 0;
        Reject(bytes, Status::LimitExceeded, small);
        small = {};
        small.MaxFrames = UINT32_MAX;
        small.MaxValues = SIZE_MAX;
        Reject(Replace(Replace(Copy(Fixture), "Frames: 2", "Frames: 3"), "Frame Time: .0625", "Frame Time: 1e308"),
               Status::InvalidFrameTime, small);
        B::BvhDocument held = Parse(Text(Single));
        const auto invalid = B::DecodeBvh({nullptr, size_t{1}}, {}, held);
        CHECK(invalid.Status == Status::InvalidArgument && held.Values.size() == 1 && std::signbit(held.Values[0]));
    }
} // namespace
int main()
{
    ValuesAndOwnership();
    ChannelOrders();
    LargeHierarchy();
    Refusals();
    Limits();
    std::puts("BVH_DECODE result=pass owned_utf8_hierarchy_channel_order_frame_rows_finite_limits_atomic_no_retarget");
    return 0;
}
