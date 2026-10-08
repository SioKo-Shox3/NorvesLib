#pragma once
// author-rest必須の独立ClipBank。validated所有結果だけwriter/binderへ渡す。
#include "Resource/RigAuthoring.h"
#include "Animation/RigClipAnalysis.h"
namespace NorvesLib::Core::Skeletal
{
    struct RigClipSnapshot
    {
        Container::AnsiString Label;
        Container::VariableArray<SkeletalRestTransform> Rest;
        uint64_t RestHash = 0;
        double ResolvedImportScale = 1;
        RigRootFrame RootFrame = IdentityRigRootFrame();
    };
    struct ClipBankV1Data
    {
        RigImportProfile Profile = RigImportProfile::DirectTrs128;
        RigTopology Topology;
        Container::VariableArray<RigClipSnapshot> Snapshots;
        Container::VariableArray<SkeletalAnimationClip> Clips;
        Container::VariableArray<uint32_t> ClipSnapshots;
        Container::VariableArray<RigClipAnalysis> Analyses;
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
                                    const RigV1Limits&, RigImportProfile, const RigClipAnalysisOptions*,
                                    Container::Span<const SkeletalAnimationClip>);
        friend bool ParseClipBankV1(Container::Span<const uint8_t>, ClipBankV1&, RigV1Report&, const RigV1Limits&,
                                    RigImportProfile);
    };
    [[nodiscard]] bool BuildClipBankV1(Container::Span<const RigAuthoringCpu> sources, ClipBankV1& out,
                                       RigV1Report& report, const RigV1Limits& limits = {},
                                       RigImportProfile profile = RigImportProfile::DirectTrs128,
                                       const RigClipAnalysisOptions* analysisOptions = nullptr,
                                       Container::Span<const SkeletalAnimationClip> replacementClips = {});
    [[nodiscard]] bool WriteClipBankV1(const ClipBankV1& bank, Container::VariableArray<uint8_t>& out,
                                       RigV1Report& report, const RigV1Limits& limits = {},
                                       RigImportProfile profile = RigImportProfile::DirectTrs128);
    [[nodiscard]] bool ParseClipBankV1(Container::Span<const uint8_t> bytes, ClipBankV1& out, RigV1Report& report,
                                       const RigV1Limits& limits = {},
                                       RigImportProfile profile = RigImportProfile::DirectTrs128);
} // namespace NorvesLib::Core::Skeletal
