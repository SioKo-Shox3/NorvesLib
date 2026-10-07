#include "RigClipBankCook.h"
namespace NorvesLib::Tools::AssetCook
{
    bool CookRigClipBankV1NativePath(Core::Container::Span<const uint8_t> source,
                                     const std::filesystem::path& sourcePath, Core::Container::AnsiStringView format,
                                     RigClipBankCookResult& out, Core::Skeletal::RigV1Report& report,
                                     const Core::Skeletal::RigV1Limits& limits,
                                     const Core::AssetImport::LoadedImportSettings* settings,
                                     const Core::Skeletal::SkeletalGltfDecodeOptions* options,
                                     Core::Skeletal::RigImportProfile profile,
                                     const Core::Skeletal::RigClipSourceSelection* clipSource,
                                     const Core::Skeletal::RigClipAnalysisOptions* analysisOptions)
    {
        report = {};
        try
        {
            if (format != Core::Container::AnsiStringView("nvskel.v1.clips"))
            {
                report.Status = Core::Skeletal::RigV1Status::UnsupportedProfile;
                return false;
            }
            Core::Skeletal::RigAuthoringCpu sourceRig;
            if (!Core::Skeletal::DecodeRigAuthoringWithProfileNativePath(
                    source, sourcePath, profile, sourceRig, report, limits, settings, options, nullptr, clipSource))
            {
                return false;
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
