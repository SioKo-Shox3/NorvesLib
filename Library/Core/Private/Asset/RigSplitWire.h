#pragma once
// 分離role専用の有限wire外枠。受入済みClipBank codecの意味とbytesは変更しない。
#include "Animation/RigV1Types.h"
namespace NorvesLib::Core::Skeletal::SplitWire
{
    using Bytes = Container::VariableArray<uint8_t>;
    using View = Container::Span<const uint8_t>;
    constexpr uint32_t Four(char a, char b, char c, char d)
    {
        return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
               (uint32_t(uint8_t(d)) << 24);
    }
    struct Section
    {
        uint32_t Code = 0, Record = 0, Count = 0;
        uint64_t Offset = 0, Size = 0;
        bool Required = true;
    };
    struct OutputSection
    {
        uint32_t Code = 0, Record = 0;
        Bytes Data;
        bool Required = true;
    };
    uint32_t U32(View, size_t);
    uint64_t U64(View, size_t);
    float F32(View, size_t);
    double F64(View, size_t);
    void W32(Bytes&, size_t, uint32_t);
    void W64(Bytes&, size_t, uint64_t);
    void WF(Bytes&, size_t, float);
    bool Zero(View, size_t first, size_t last);
    View ViewOf(const Bytes&);
    View NameView(const Container::AnsiString&);
    bool NameLess(const Container::AnsiString&, const Container::AnsiString&);
    bool NativeName(const Container::AnsiString&, Container::String&);
    bool ValidName(const Container::AnsiString&, const RigV1Limits&);
    bool ReadName(View strings, uint64_t offset, uint32_t size, Container::AnsiString&, const RigV1Limits&);
    // 借用viewだけで個別長/UTF8/参照所有量を検査し、成功時のみ残量を減らす。
    RigV1Status PreflightName(View strings, uint64_t offset, uint32_t size, const RigV1Limits&, uint64_t& remaining,
                              bool bAllowEmpty = false);
    uint64_t AppendName(Bytes&, const Container::AnsiString&);
    void WriteRest(Bytes&, size_t, const SkeletalRestTransform&);
    SkeletalRestTransform ReadRest(View, size_t);
    // expectedのCode/Recordを入力とし、成功時だけOffset/Size/Countを設定する。
    RigV1Status ReadEnvelope(View, uint32_t role, Container::Span<Section> expected, const RigV1Limits&,
                             RigImportProfile = RigImportProfile::DirectTrs128);
    RigV1Status WriteEnvelope(uint32_t role, uint64_t skeletonId, Container::Span<const OutputSection>, Bytes&,
                              const RigV1Limits&, RigImportProfile = RigImportProfile::DirectTrs128);
    RigV1Status ReadTopology(View, View strings, const Section&, const RigV1Limits&, RigTopology&);
    void WriteTopology(const RigTopology&, Bytes& strings, Bytes& joints);
    uint64_t RootIdentityHash();
} // namespace NorvesLib::Core::Skeletal::SplitWire
