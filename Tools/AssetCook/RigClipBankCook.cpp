#include "RigClipBankCook.h"
namespace NorvesLib::Tools::AssetCook
{
    bool CookRigClipBankV1NativePath(
        Core::Container::Span<const uint8_t> source, const std::filesystem::path& sourcePath,
        Core::Container::AnsiStringView format, RigClipBankCookResult& out, Core::Skeletal::RigV1Report& report,
        const Core::Skeletal::RigV1Limits& limits, const Core::AssetImport::LoadedImportSettings* settings,
        const Core::Skeletal::SkeletalGltfDecodeOptions* options, Core::Skeletal::RigImportProfile profile,
        const Core::Skeletal::RigClipSourceSelection* clipSource,
        const Core::Skeletal::RigClipAnalysisOptions* analysisOptions, Core::Skeletal::RigGltfImportCapture* capture,
        Core::Container::AnsiStringView analysisRootJoint, bool* outRootJointMissing)
    {
        report = {};
        if (outRootJointMissing)
        {
            *outRootJointMissing = false;
        }
        try
        {
            if (format != Core::Container::AnsiStringView("nvskel.v1.clips"))
            {
                report.Status = Core::Skeletal::RigV1Status::UnsupportedProfile;
                return false;
            }
            Core::Skeletal::RigAuthoringCpu sourceRig;
            if (!Core::Skeletal::DecodeRigAuthoringWithProfileNativePath(
                    source, sourcePath, profile, sourceRig, report, limits, settings, options, capture, clipSource))
            {
                return false;
            }
            Core::Skeletal::RigClipAnalysisOptions resolvedAnalysis;
            if (analysisOptions && !analysisRootJoint.empty())
            {
                resolvedAnalysis = *analysisOptions;
                const auto& topology = sourceRig.GetData()->Topology;
                bool bFound = false;
                for (size_t i = 0; i < topology.Joints.size(); ++i)
                {
                    if (Core::Container::AnsiStringView(topology.Joints[i].Name) == analysisRootJoint)
                    {
                        resolvedAnalysis.RootJoint = topology.CanonicalToSource[i];
                        bFound = true;
                        break;
                    }
                }
                if (!bFound)
                {
                    if (outRootJointMissing)
                    {
                        *outRootJointMissing = true;
                    }
                    report.Status = Core::Skeletal::RigV1Status::InvalidName;
                    return false;
                }
                analysisOptions = &resolvedAnalysis;
            }
            Core::Skeletal::ClipBankV1 bank;
            if (!Core::Skeletal::BuildClipBankV1({&sourceRig, 1}, bank, report, limits, profile, analysisOptions))
            {
                return false;
            }
            RigClipBankCookResult candidate;
            if (!Core::Skeletal::WriteClipBankV1(bank, candidate.Bytes, report, limits, profile))
            {
                return false;
            }
            candidate.Report = report;
            out = std::move(candidate);
            return true;
        }
        catch (...)
        {
            report.Status = Core::Skeletal::RigV1Status::Exception;
            return false;
        }
    }
} // namespace NorvesLib::Tools::AssetCook
