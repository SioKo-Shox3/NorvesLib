#include "Resource/RigAuthoring.h"
#include "Resource/SkeletalGltfDecode.h"
#include <cmath>
namespace NorvesLib::Core::Skeletal
{
    bool DecodeRigAuthoringNativePath(Container::Span<const uint8_t> source, const std::filesystem::path& path,
                                      RigAuthoringCpu& out, RigV1Report& report, const RigV1Limits& limits,
                                      const AssetImport::LoadedImportSettings* settings,
                                      const SkeletalGltfDecodeOptions* options)
    {
        report = {};
        try
        {
            if (!IsValidRigV1Limits(limits) || source.empty() || !source.data())
            {
                return false;
            }
            if (source.size() > limits.MaxSourceBytes)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            const auto nativeLabel = path.generic_u8string();
            if (nativeLabel.size() > limits.MaxNameBytes)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            auto data = Container::MakeShared<RigAuthoringData>();
            auto decoded = DecodeRigAuthorRestGltfNativePath(source, path, data->LocalRest, data->ResolvedImportScale,
                                                             limits, settings, options);
            report.DecodeStatus = decoded.Status;
            if (!decoded.Succeeded())
            {
                report.Status = decoded.Status == SkeletalGltfDecodeStatus::ImportLimitExceeded
                                    ? RigV1Status::LimitExceeded
                                    : RigV1Status::DecodeRejected;
                return false;
            }
            if (data->LocalRest.size() != decoded.Data.Joints.size())
            {
                report.Status = RigV1Status::InvalidRest;
                return false;
            }
            for (const auto& rest : data->LocalRest)
            {
                if (!IsValidSkeletalRestTransform(rest))
                {
                    report.Status = RigV1Status::InvalidRest;
                    return false;
                }
            }
            report.Status =
                BuildRigTopology({decoded.Data.Joints.data(), decoded.Data.Joints.size()}, limits, data->Topology);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            // v1 B1は単一rootを要求する。topology正準列自体のforest表現とは別のprofile。
            size_t roots = 0;
            for (const auto& joint : data->Topology.Joints)
            {
                if (joint.ParentIndex < 0)
                {
                    ++roots;
                }
            }
            if (roots != 1)
            {
                report.Status = RigV1Status::UnsupportedProfile;
                return false;
            }
            if (decoded.Data.Clips.size() > limits.MaxClips)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            data->SourceLabel = nativeLabel.empty()
                                    ? Container::AnsiString("memory")
                                    : Container::AnsiString(Container::AnsiStringView(
                                          reinterpret_cast<const char*>(nativeLabel.data()), nativeLabel.size()));
            data->Geometry = std::move(decoded.Data);
            report.SkeletonId = data->Topology.SkeletonId;
            RigAuthoringCpu candidate;
            candidate.m_Data = std::move(data);
            out = std::move(candidate);
            report.Status = RigV1Status::Success;
            return true;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
} // namespace NorvesLib::Core::Skeletal
