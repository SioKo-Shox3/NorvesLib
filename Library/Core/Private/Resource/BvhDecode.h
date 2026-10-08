#pragma once
// BVHの値と宣言順だけを所有する。軸・単位・回転の解釈は変換層に委ねる。
#include "Container/Span.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Bvh
{
    enum class Channel : uint8_t
    {
        Xposition,
        Yposition,
        Zposition,
        Xrotation,
        Yrotation,
        Zrotation
    };
    struct Vector3d
    {
        double X = 0, Y = 0, Z = 0;
    };
    struct Joint
    {
        Container::VariableArray<uint8_t> Name;
        uint32_t Parent = UINT32_MAX;
        Vector3d Offset;
        uint32_t ChannelOffset = 0;
        uint8_t ChannelCount = 0;
        bool bHasEndSite = false;
        Vector3d EndSiteOffset;
    };
    struct BvhDocument
    {
        Container::VariableArray<Joint> Joints;
        Container::VariableArray<Channel> Channels;
        // 1frameごとに全jointのCHANNELSを宣言順で並べる。角度は元のdegree値のまま。
        Container::VariableArray<double> Values;
        uint32_t FrameCount = 0;
        double FrameTimeSeconds = 0;
    };
    struct BvhDecodeLimits
    {
        size_t MaxInputBytes = 64u * 1024u * 1024u;
        uint32_t MaxJoints = 1024;
        uint32_t MaxDepth = 256;
        uint32_t MaxFrames = 1000000;
        size_t MaxValues = size_t{1} << 23;
        size_t MaxNameBytes = 512;
        size_t MaxNumberBytes = 128;
    };
    enum class BvhDecodeStatus : uint8_t
    {
        Success,
        InvalidArgument,
        InvalidUtf8,
        InvalidCharacter,
        InvalidHeader,
        InvalidHierarchy,
        DuplicateJointName,
        InvalidChannels,
        InvalidNumber,
        InvalidFrameTime,
        FrameCountMismatch,
        LimitExceeded,
        TrailingInput
    };
    struct BvhDecodeResult
    {
        BvhDecodeStatus Status = BvhDecodeStatus::InvalidArgument;
        size_t ByteOffset = 0;
        // 失敗時は1始まり、成功時はLine/Columnとも0。ColumnはUTF8
        // scalar数ではなく行内byte数で、CRLFは1改行として数える。
        size_t Line = 1, Column = 1;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == BvhDecodeStatus::Success;
        }
    };
    // 成功時だけoutを置換する。失敗と確保例外では既存outを保持し、例外は呼出元へ伝播する。
    [[nodiscard]] BvhDecodeResult DecodeBvh(Container::Span<const uint8_t> bytes, const BvhDecodeLimits& limits,
                                            BvhDocument& out);
} // namespace NorvesLib::Core::Bvh
