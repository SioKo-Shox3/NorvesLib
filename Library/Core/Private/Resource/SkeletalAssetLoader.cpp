#include "Resource/SkeletalAssetLoader.h"
#include "Resource/SkeletalAssetLoaderTestAccess.h"
#include "Asset/AssetSystem.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Object/ResourceRegistry.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <exception>
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
            if (!resolved.UsedCooked())
            {
                report.Status = SkeletalAssetLoadStatus::ResolveRejected;
                LogStage("skeletal_asset_resolve", "worker", resolved.NormalizedLogicalPath, report, false);
                return false;
            }
            const auto& reference = resolved.CookedReference;
            if (reference.Kind != Asset::AssetKind::Model ||
                reference.EntryType != Asset::MakeAssetPackageFourCC('S', 'k', 'l', '0') ||
                reference.Format != "nvskel.v0.skinned.pnujiw.u32" || reference.CookedVersion != 0)
            {
                report.Status = SkeletalAssetLoadStatus::FormatRejected;
                LogStage("skeletal_asset_resolve", "worker", resolved.NormalizedLogicalPath, report, false);
                return false;
            }
            report.Status = SkeletalAssetLoadStatus::Success;
            LogStage("skeletal_asset_resolve", "worker", resolved.NormalizedLogicalPath, report, true);
            Asset::CookedSkeletalParseResult parsed;
            report.bParseAttempted = true;
            {
                NORVES_STAT_SCOPE_CATEGORY("SkeletalAsset.ParseCooked", "AssetLoad");
                parsed = Asset::ParseCookedSkeletal(resolved.Blob);
            }
            report.ParseStatus = parsed.Status;
            if (!parsed.Succeeded())
            {
                report.Status = SkeletalAssetLoadStatus::ParseRejected;
            }
            else if (!MetadataMatches(reference, parsed.Data.Skeletal))
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
            report.Source = Asset::AssetResolveSource::Cooked;
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
