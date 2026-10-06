#pragma once
// author-rest必須の独立ClipBank。validated所有結果だけwriter/binderへ渡す。
#include "Resource/RigAuthoring.h"
namespace NorvesLib::Core::Skeletal
{
    struct RigClipSnapshot
    {
        Container::AnsiString Label;
        Container::VariableArray<SkeletalRestTransform> Rest;
        uint64_t RestHash = 0;
        double ResolvedImportScale = 1;
    };
    struct ClipBankV1Data
    {
        RigTopology Topology;
        Container::VariableArray<RigClipSnapshot> Snapshots;
        Container::VariableArray<SkeletalAnimationClip> Clips;
        Container::VariableArray<uint32_t> ClipSnapshots;
        uint64_t PayloadHash = 0;
        bool bParsedFromWire = false;
    };
    class ClipBankV1
    {
      public:
        [[nodiscard]] const ClipBankV1Data* GetData() const noexcept
        {
            return m_Data.get();
        }

      private:
        Container::TSharedPtr<const ClipBankV1Data> m_Data;
        friend bool BuildClipBankV1(Container::Span<const RigAuthoringCpu>, ClipBankV1&, RigV1Report&,
                                    const RigV1Limits&);
        friend bool ParseClipBankV1(Container::Span<const uint8_t>, ClipBankV1&, RigV1Report&, const RigV1Limits&);
    };
    [[nodiscard]] bool BuildClipBankV1(Container::Span<const RigAuthoringCpu> sources, ClipBankV1& out,
                                       RigV1Report& report, const RigV1Limits& limits = {});
    [[nodiscard]] bool WriteClipBankV1(const ClipBankV1& bank, Container::VariableArray<uint8_t>& out,
                                       RigV1Report& report, const RigV1Limits& limits = {});
    [[nodiscard]] bool ParseClipBankV1(Container::Span<const uint8_t> bytes, ClipBankV1& out, RigV1Report& report,
                                       const RigV1Limits& limits = {});
} // namespace NorvesLib::Core::Skeletal
