#include "Resource/SkeletalAssetLoader.h"
#include "Resource/SkeletalAssetLoaderTestAccess.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Resource/SkeletalSubmeshBounds.h"
#include "Resource/SkeletalSubmeshLayout.h"
#include "Resource/SkeletalLimits.h"
#include "Asset/AssetSystem.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Object/ResourceRegistry.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <exception>
#include <filesystem>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core::ResourceIO
{
    namespace C = Container;
    struct CookedSkeletalCpuAsset::State
    {
        Asset::CookedSkeletalData Data;
        C::AnsiString LogicalPath;
        C::String ResourcePath;
        Asset::AssetResolveSource Source = Asset::AssetResolveSource::None;
    };
    CookedSkeletalCpuAsset::CookedSkeletalCpuAsset() = default;
    CookedSkeletalCpuAsset::~CookedSkeletalCpuAsset() = default;
    CookedSkeletalCpuAsset::CookedSkeletalCpuAsset(CookedSkeletalCpuAsset&&) noexcept = default;
    CookedSkeletalCpuAsset& CookedSkeletalCpuAsset::operator=(CookedSkeletalCpuAsset&&) noexcept = default;
    const Asset::CookedSkeletalData* CookedSkeletalCpuAsset::GetData() const noexcept
    {
        return m_State ? &m_State->Data : nullptr;
    }
    C::AnsiStringView CookedSkeletalCpuAsset::GetLogicalPath() const noexcept
    {
        return m_State ? C::AnsiStringView(m_State->LogicalPath) : C::AnsiStringView{};
    }
    const C::String* CookedSkeletalCpuAsset::GetResourcePath() const noexcept
    {
        return m_State ? &m_State->ResourcePath : nullptr;
    }
    Asset::AssetResolveSource CookedSkeletalCpuAsset::GetSource() const noexcept
    {
        return m_State ? m_State->Source : Asset::AssetResolveSource::None;
    }
    namespace
    {
        bool SetFailure(SkeletalAssetLoadReport& report, SkeletalAssetLoadStatus status)
        {
            report.Status = status;
            return false;
        }
        void LogStage(const char* stage, const char* role, C::AnsiStringView path,
                      const SkeletalAssetLoadReport& report, bool success)
        {
            NORVES_LOG_INFO(
                "AssetLoadProfile",
                "stage=%s role=%s source=%s normalized_path=\"%.*s\" status=%u resolve_status=%u parse_status=%u success=%d",
                stage, role,
                report.Source == Asset::AssetResolveSource::Cooked ? "cooked"
                : report.Source == Asset::AssetResolveSource::None ? "none"
                                                                   : "loose",
                static_cast<int>(path.size()), path.data() ? path.data() : "", static_cast<unsigned>(report.Status),
                static_cast<unsigned>(report.ResolveStatus), static_cast<unsigned>(report.ParseStatus),
                success ? 1 : 0);
        }
        bool ResourcePath(C::AnsiStringView utf8, C::String& out)
        {
            using Char = C::String::value_type;
            const C::Span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size()};
            const auto measured = Asset::MeasureSkeletalNameDecoding<Char>(2, bytes);
            if (!measured.Succeeded() || measured.CodeUnitCount == SIZE_MAX)
            {
                return false;
            }
            C::VariableArray<Char> units(measured.CodeUnitCount + 1, Char{});
            if (!Asset::DecodeSkeletalWireName<Char>(2, bytes, {units.data(), measured.CodeUnitCount}).Succeeded())
            {
                return false;
            }
            out = C::String(units.data());
            return true;
        }
        // NVSKELの書き出しで付与・検査される値を、同じCore関数で揃える。
        bool PrepareLooseSkeletal(Skeletal::SkeletalGltfData& data)
        {
            if (data.Vertices.empty() || data.Vertices.size() > UINT32_MAX || data.Indices.empty() ||
                data.Indices.size() > UINT32_MAX || data.Joints.empty() ||
                data.Joints.size() > Skeletal::LegacyMaximumJointCount || data.Clips.empty() ||
                data.Clips.size() > UINT32_MAX)
            {
                return false;
            }
            const auto layout = Skeletal::ResolveSkeletalSubmeshLayout(
                {data.SubMeshes.data(), data.SubMeshes.size()}, data.Indices.size(), data.MaterialSlots.size());
            if (!layout.Succeeded())
            {
                return false;
            }
            if (layout.bUsesImplicitSingleSubmesh)
            {
                data.SubMeshes.push_back(layout.ImplicitSubmesh);
                Skeletal::SkeletalMaterialSlot slot;
                slot.Name = "Default";
                data.MaterialSlots.push_back(std::move(slot));
            }
            for (auto& submesh : data.SubMeshes)
            {
                if (!Skeletal::ComputeSkeletalSubmeshBounds(
                        {data.Indices.data() + submesh.IndexStart, submesh.IndexCount}, data.Vertices.size(),
                        [&data](uint32_t index) { return data.Vertices[index].Position; }, submesh))
                {
                    return false;
                }
            }
            if (!Skeletal::ValidateSkeletalSubmeshData({data.SubMeshes.data(), data.SubMeshes.size()},
                    {data.Indices.data(), data.Indices.size()}, data.Vertices.size(), data.MaterialSlots.size())
                     .Succeeded())
            {
                return false;
            }
            uint64_t nameBytes = 0;
            const auto validName = [&nameBytes](const C::String& name)
            {
                const auto measured = Asset::MeasureSkeletalNameEncoding(
                    2, C::Span<const C::String::value_type>{name.data(), name.size()});
                if (!measured.Succeeded() || measured.ByteCount > UINT32_MAX - nameBytes)
                {
                    return false;
                }
                nameBytes += measured.ByteCount;
                return true;
            };
            for (const auto& joint : data.Joints)
            {
                if (!validName(joint.Name))
                {
                    return false;
                }
            }
            uint64_t channelCount = 0, sampleCount = 0;
            for (auto& clip : data.Clips)
            {
                if (!validName(clip.Name) || !clip.RootMotion.empty() || clip.Channels.empty() ||
                    clip.Channels.size() > UINT32_MAX - channelCount)
                {
                    return false;
                }
                channelCount += clip.Channels.size();
                for (const auto& channel : clip.Channels)
                {
                    if (channel.Samples.empty() || channel.Samples.size() > UINT32_MAX - sampleCount)
                    {
                        return false;
                    }
                    sampleCount += channel.Samples.size();
                }
                // NVSKEL v0にはclip metadataが保存されない。
                clip.Metadata = {};
            }
            for (const auto& slot : data.MaterialSlots)
            {
                if (!validName(slot.Name))
                {
                    return false;
                }
            }
            return true;
        }
        bool MetadataMatches(const Asset::AssetCookedReference& reference, const Skeletal::SkeletalGltfData& data)
        {
            if (!reference.bHasSkeletalMetadata)
            {
                return true;
            }
            const auto& m = reference.SkeletalMetadata;
            return m.VertexCount == data.Vertices.size() && m.IndexCount == data.Indices.size() &&
                   m.JointCount == data.Joints.size() && m.ClipCount == data.Clips.size() &&
                   (!m.bHasSubmeshCounts ||
                    (m.SubmeshCount == data.SubMeshes.size() && m.MaterialSlotCount == data.MaterialSlots.size()));
        }
    } // namespace
    bool LoadCookedSkeletalForWorker(const CookedSkeletalLoadPlan& plan, CookedSkeletalCpuAsset& out,
                                     SkeletalAssetLoadReport& report)
    {
        report = {};
        try
        {
            if (!plan.Assets || plan.LogicalPath.empty() ||
                !Asset::MeasureSkeletalNameEncoding(
                     2, C::Span<const char>{plan.LogicalPath.data(), plan.LogicalPath.size()})
                     .Succeeded())
            {
                LogStage("skeletal_asset_resolve", "worker", plan.LogicalPath, report, false);
                return false;
            }
            Asset::AssetResolveResult resolved;
            report.bResolveAttempted = true;
            {
                NORVES_STAT_SCOPE_CATEGORY("SkeletalAsset.Resolve", "AssetLoad");
                resolved = plan.Assets->ResolveAsset(plan.LogicalPath, Asset::AssetKind::Model,
                                                     Asset::AssetManifest::DefaultVariant,
                                                     Asset::AssetFallbackMode::FailOnCookedFailure);
            }
            report.ResolveStatus = resolved.Status;
            report.Source = resolved.Source;
            const bool bCooked = resolved.UsedCooked();
            const bool bLoose = plan.bAllowLoose && resolved.Succeeded() &&
                                resolved.Source == Asset::AssetResolveSource::Loose;
            if (!bCooked && !bLoose)
            {
                if (!plan.bAllowLoose && report.Source == Asset::AssetResolveSource::Loose)
                {
                    report.Source = Asset::AssetResolveSource::None;
                }
                report.Status = SkeletalAssetLoadStatus::ResolveRejected;
                LogStage("skeletal_asset_resolve", "worker", resolved.NormalizedLogicalPath, report, false);
                return false;
            }
            const auto& reference = resolved.CookedReference;
            if (bCooked && (reference.Kind != Asset::AssetKind::Model ||
                reference.EntryType != Asset::MakeAssetPackageFourCC('S', 'k', 'l', '0') ||
                reference.Format != "nvskel.v0.skinned.pnujiw.u32" || reference.CookedVersion != 0))
            {
                report.Status = SkeletalAssetLoadStatus::FormatRejected;
                LogStage("skeletal_asset_resolve", "worker", resolved.NormalizedLogicalPath, report, false);
                return false;
            }
            report.Status = SkeletalAssetLoadStatus::Success;
            LogStage("skeletal_asset_resolve", "worker", resolved.NormalizedLogicalPath, report, true);
            Asset::CookedSkeletalParseResult parsed;
            report.bParseAttempted = true;
            if (bCooked)
            {
                NORVES_STAT_SCOPE_CATEGORY("SkeletalAsset.ParseCooked", "AssetLoad");
                parsed = Asset::ParseCookedSkeletal(resolved.Blob);
            }
            else
            {
                NORVES_STAT_SCOPE_CATEGORY("SkeletalAsset.DecodeLoose", "AssetLoad");
                parsed.Status = Asset::CookedSkeletalParseStatus::InvalidRecord;
                if (resolved.LoosePath.HasResolvedPath())
                {
                    const auto& sourcePath = resolved.LoosePath.GetResolvedPath();
                    const auto decode = [&](const auto& path)
                    {
                        return Skeletal::DecodeRigGltfNativePath(
                            resolved.Blob.GetSpan(), std::filesystem::path(path.begin(), path.end()));
                    };
#if defined(UNICODE)
                    // AssetFileReaderと同様にUTF-8を優先し、失敗時はローカルコードページを使う。
                    C::String widePath;
                    auto decoded = ResourcePath(sourcePath, widePath) ? decode(widePath) : decode(sourcePath);
#else
                    // narrow版のAssetFileReaderは、読み取りに使ったbytesをそのまま渡す。
                    auto decoded = decode(sourcePath);
#endif
                    if (decoded.Succeeded() && PrepareLooseSkeletal(decoded.Data))
                    {
                        parsed.Status = Asset::CookedSkeletalParseStatus::Success;
                        parsed.Data.SourceBlob = resolved.Blob;
                        parsed.Data.Skeletal = std::move(decoded.Data);
                    }
                }
            }
            report.ParseStatus = parsed.Status;
            if (!parsed.Succeeded())
            {
                report.Status = SkeletalAssetLoadStatus::ParseRejected;
            }
            else if (bCooked && !MetadataMatches(reference, parsed.Data.Skeletal))
            {
                report.Status = SkeletalAssetLoadStatus::MetadataMismatch;
            }
            if (report.Status != SkeletalAssetLoadStatus::Success)
            {
                LogStage("skeletal_cooked_parse", "worker", resolved.NormalizedLogicalPath, report, false);
                return false;
            }
            auto candidate = C::MakeUnique<CookedSkeletalCpuAsset::State>();
            candidate->LogicalPath = resolved.NormalizedLogicalPath;
            if (!ResourcePath(candidate->LogicalPath, candidate->ResourcePath))
            {
                report.Status = SkeletalAssetLoadStatus::InvalidRequest;
                LogStage("skeletal_cooked_parse", "worker", resolved.NormalizedLogicalPath, report, false);
                return false;
            }
            candidate->Data = std::move(parsed.Data);
            candidate->Source = report.Source;
            LogStage("skeletal_cooked_parse", "worker", resolved.NormalizedLogicalPath, report, true);
            out.m_State = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            report.Status = SkeletalAssetLoadStatus::Exception;
            LogStage(report.bParseAttempted ? "skeletal_cooked_parse" : "skeletal_asset_resolve", "worker",
                     plan.LogicalPath, report, false);
            return false;
        }
    }
    bool Detail::AssembleCookedSkeletalAssetWithProbe(const CookedSkeletalCpuAsset& cpu,
                                                      const SkeletalAssetCreateContext& context,
                                                      C::TSharedPtr<SkeletalAssetResource>& out,
                                                      SkeletalAssetLoadReport& report, SkeletalCreateProbe probe,
                                                      void* probeContext)
    {
        report = {};
        if (cpu.GetSource() == Asset::AssetResolveSource::Loose)
        {
            report.Source = Asset::AssetResolveSource::Loose;
        }
        const auto finish = [&](SkeletalAssetLoadStatus status)
        {
            report.Status = status;
            LogStage("skeletal_resource_create", "owner", cpu.GetLogicalPath(), report,
                     status == SkeletalAssetLoadStatus::Success);
            return status == SkeletalAssetLoadStatus::Success;
        };
        try
        {
            if (context.OwnerThread == NorvesLib::Thread::Thread::ThreadId{} ||
                context.OwnerThread != NorvesLib::Thread::Thread::GetCurrentThreadId())
            {
                return finish(SkeletalAssetLoadStatus::WrongOwnerThread);
            }
            if (!context.Registry || !context.Registry->IsInitialized())
            {
                return finish(SkeletalAssetLoadStatus::RegistryNotReady);
            }
            if (!cpu.GetData() || !cpu.GetResourcePath())
            {
                return finish(SkeletalAssetLoadStatus::InvalidCpuResult);
            }
            report.Source = cpu.GetSource();
            NORVES_STAT_SCOPE_CATEGORY("SkeletalAsset.CreateResources", "AssetLoad");
            // CPU結果を保持し、途中の通常失敗/確保例外でも再試行できる所有copyを使う。
            auto data = cpu.GetData()->Skeletal;
            report.bCreateAttempted = true;
            auto& registry = *context.Registry;
            const auto observe = [&](SkeletalCreatePoint point, uint32_t ordinal, const C::TSharedPtr<Resource>& value)
            {
                if (!value || value->GetResourceId() == 0)
                {
                    return SetFailure(report, SkeletalAssetLoadStatus::ResourceCreateFailed);
                }
                ++report.CreatedResources;
                return !probe || probe(point, ordinal, value, probeContext) ||
                       SetFailure(report, SkeletalAssetLoadStatus::InjectedFailure);
            };
            auto mesh = registry.CreateResource<SkinnedMeshResource>(*cpu.GetResourcePath());
            if (!observe(SkeletalCreatePoint::Mesh, 0, mesh))
            {
                return finish(report.Status);
            }
            mesh->SetVertices(std::move(data.Vertices));
            mesh->SetIndices(std::move(data.Indices));
            mesh->SetSubmeshTables(std::move(data.SubMeshes), std::move(data.MaterialSlots));
            mesh->SetMeshNodeGlobalTransform(data.MeshNodeGlobalTransform);
            if (!mesh->Load())
            {
                return finish(SkeletalAssetLoadStatus::ResourceLoadFailed);
            }
            auto skeleton = registry.CreateResource<SkeletonResource>(*cpu.GetResourcePath());
            if (!observe(SkeletalCreatePoint::Skeleton, 0, skeleton))
            {
                return finish(report.Status);
            }
            skeleton->SetJoints(std::move(data.Joints));
            if (!skeleton->Load())
            {
                return finish(SkeletalAssetLoadStatus::ResourceLoadFailed);
            }
            C::VariableArray<C::TSharedPtr<AnimationClipResource>> clips;
            clips.reserve(data.Clips.size());
            for (size_t i = 0; i < data.Clips.size(); ++i)
            {
                auto clip = registry.CreateResource<AnimationClipResource>(*cpu.GetResourcePath());
                if (!observe(SkeletalCreatePoint::Clip, static_cast<uint32_t>(i), clip))
                {
                    return finish(report.Status);
                }
                clip->SetClip(std::move(data.Clips[i]));
                if (!clip->Load())
                {
                    return finish(SkeletalAssetLoadStatus::ResourceLoadFailed);
                }
                clips.push_back(std::move(clip));
            }
            auto asset = registry.CreateResource<SkeletalAssetResource>(*cpu.GetResourcePath());
            if (!observe(SkeletalCreatePoint::Aggregate, 0, asset))
            {
                return finish(report.Status);
            }
            asset->SetClipResources(mesh, skeleton, clips);
            if (!asset->Load())
            {
                return finish(SkeletalAssetLoadStatus::ResourceLoadFailed);
            }
            // ログも成功公開より前に行い、ここから先はnoexceptな共有pointer代入だけにする。
            finish(SkeletalAssetLoadStatus::Success);
            out = std::move(asset);
            return true;
        }
        catch (const std::exception&)
        {
            return finish(SkeletalAssetLoadStatus::Exception);
        }
    }
    bool AssembleCookedSkeletalAsset(const CookedSkeletalCpuAsset& cpu, const SkeletalAssetCreateContext& context,
                                     C::TSharedPtr<SkeletalAssetResource>& out, SkeletalAssetLoadReport& report)
    {
        return Detail::AssembleCookedSkeletalAssetWithProbe(cpu, context, out, report, nullptr, nullptr);
    }
} // namespace NorvesLib::Core::ResourceIO
