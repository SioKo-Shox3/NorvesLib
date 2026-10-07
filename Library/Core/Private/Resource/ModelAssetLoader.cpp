#include "Resource/ModelAssetLoader.h"
#include "Resource/ImportedOpaqueRuntime.h"

#include "Asset/AssetSystem.h"
#include "Container/StringView.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include "Resource/ModelAssetResolver.h"
#include <utility>

namespace NorvesLib::Core::ResourceIO
{
    namespace
    {
        Container::String ToOwnedString(Container::AnsiStringView value)
        {
            return Container::String(Container::StringView(value.data(), value.size()));
        }
        bool MatchesImportedVersion(const Asset::AssetResolveResult& resolved, const Asset::CookedMeshData& mesh)
        {
            const auto& reference = resolved.CookedReference;
            const bool bV1 = mesh.VersionMajor == 1 || reference.CookedVersion != 0 ||
                             reference.Format == "nvmesh.v1.mesh3d.pnt.u32.clustered" ||
                             reference.Format == "nvmesh.v1.mesh3d.pnt.u32.lodgraph";
            if (!bV1)
            {
                return true;
            }
            const char *format = mesh.Layout == Asset::CookedMeshLayout::ClusteredV1
                                     ? "nvmesh.v1.mesh3d.pnt.u32.clustered"
                                     : "nvmesh.v1.mesh3d.pnt.u32.lodgraph";
            return mesh.VersionMajor == 1 && mesh.Layout != Asset::CookedMeshLayout::LegacyV0 &&
                   reference.CookedVersion == 1 && reference.Format == format &&
                   reference.EntryType == Asset::MakeAssetPackageFourCC('M', 's', 'h', '0');
        }
    } // namespace

    bool BuildModelStagingFromCookedMesh(
        const Asset::CookedMeshData& cooked,
        const Container::String& debugName,
        const Container::String& resolvedPath,
        ModelStaging::ModelStagingData& outStaging)
    {
        outStaging = {};
        // 手組みv0の空表互換を保ち、v1は対応する1材質subsetだけを受理する。
        if (cooked.VersionMajor > 1 || cooked.Layout == Asset::CookedMeshLayout::LodGraphV1 ||
            cooked.Submeshes.size() > 1 || cooked.Materials.size() > 1)
        {
            NORVES_LOG_ERROR("ModelAsset",
                             "NVMESH "
                             "v%u・submesh=%zu・material=%zuのruntime材質接続は未対応です",
                             static_cast<unsigned>(cooked.VersionMajor), cooked.Submeshes.size(),
                             cooked.Materials.size());
            return false;
        }

        ModelStaging::ImportedMaterialStaging imported;
        if (cooked.VersionMajor == 1)
        {
            Container::AnsiString reason;
            if (cooked.Submeshes.size() != 1 || cooked.Materials.size() != 1 ||
                cooked.Submeshes[0].MaterialIndex != 0 || cooked.Vertices.empty() || cooked.Indices.empty() ||
                cooked.Clusters.empty() ||
                ModelStaging::BuildImportedMaterialStaging(cooked, 0, imported) !=
                    ModelStaging::MaterialStagingStatus::Success ||
                !ModelStaging::ValidateImportedOpaqueMaterial(imported, reason))
            {
                NORVES_LOG_ERROR("ModelAsset", "asset=%s material=0 unsupported_v1_profile %s", debugName.c_str(),
                                 reason.c_str());
                return false;
            }
            for (const auto& cluster : cooked.Clusters)
            {
                if (cluster.MaterialIndex != 0 || cluster.LODLevel != 0 || cluster.LODError != 0 ||
                    cluster.ParentStart != 0 || cluster.ParentCount != 0)
                {
                    NORVES_LOG_ERROR("ModelAsset", "asset=%s material=0 unsupported_cluster_profile",
                                     debugName.c_str());
                    return false;
                }
            }
        }

        outStaging.Vertices.reserve(cooked.Vertices.size());
        for (const Asset::CookedMeshVertex& cookedVertex : cooked.Vertices)
        {
            Rendering::Mesh3DVertex vertex{};
            vertex.Position[0] = cookedVertex.Position.X;
            vertex.Position[1] = cookedVertex.Position.Y;
            vertex.Position[2] = cookedVertex.Position.Z;
            vertex.Normal[0] = cookedVertex.Normal.X;
            vertex.Normal[1] = cookedVertex.Normal.Y;
            vertex.Normal[2] = cookedVertex.Normal.Z;
            vertex.TexCoord[0] = cookedVertex.TexCoord.U;
            vertex.TexCoord[1] = cookedVertex.TexCoord.V;
            outStaging.Vertices.push_back(vertex);
        }

        outStaging.ClusterizedIndices = cooked.Indices;
        outStaging.Clusters.reserve(cooked.Clusters.size());
        for (const Asset::CookedMeshCluster& cookedCluster : cooked.Clusters)
        {
            Rendering::MegaGeometry::MeshCluster cluster;
            cluster.IndexOffset = cookedCluster.IndexOffset;
            cluster.IndexCount = cookedCluster.IndexCount;
            cluster.VertexOffset = static_cast<int32_t>(cookedCluster.VertexOffset);
            cluster.VertexCount = cookedCluster.VertexCount;
            cluster.Bounds.CenterX = cookedCluster.BoundsCenter.X;
            cluster.Bounds.CenterY = cookedCluster.BoundsCenter.Y;
            cluster.Bounds.CenterZ = cookedCluster.BoundsCenter.Z;
            cluster.Bounds.Radius = cookedCluster.BoundsRadius;
            cluster.ConeAxisX = cookedCluster.ConeAxis.X;
            cluster.ConeAxisY = cookedCluster.ConeAxis.Y;
            cluster.ConeAxisZ = cookedCluster.ConeAxis.Z;
            cluster.ConeCutoff = cookedCluster.ConeCutoff;
            cluster.LODLevel = cookedCluster.LODLevel;
            cluster.LODError = cookedCluster.LODError;
            cluster.ParentStart = cookedCluster.ParentStart;
            cluster.ParentCount = cookedCluster.ParentCount;
            cluster.MaterialIndex = cookedCluster.MaterialIndex;
            outStaging.Clusters.push_back(cluster);
        }

        outStaging.TotalBounds.CenterX = cooked.TotalBoundsCenter.X;
        outStaging.TotalBounds.CenterY = cooked.TotalBoundsCenter.Y;
        outStaging.TotalBounds.CenterZ = cooked.TotalBoundsCenter.Z;
        outStaging.TotalBounds.Radius = cooked.TotalBoundsRadius;
        outStaging.DebugName = debugName;
        outStaging.ResolvedPath = resolvedPath;

        outStaging.ImportedMaterial = std::move(imported);
        if (cooked.VersionMajor == 0 && !cooked.Materials.empty())
        {
            const Asset::CookedMeshMaterial& material = cooked.Materials[0];
            outStaging.TextureReferences.Albedo.RequestPath = ToOwnedString(cooked.GetString(material.AlbedoTexture));
            outStaging.TextureReferences.Normal.RequestPath = ToOwnedString(cooked.GetString(material.NormalTexture));
            outStaging.TextureReferences.Arm.RequestPath = ToOwnedString(cooked.GetString(material.ArmTexture));
        }

        return true;
    }

    Rendering::ModelHandle LoadCookedModel(
        const Asset::AssetSystem& assetSystem,
        const Container::String& logicalPath,
        Rendering::ModelLoadResourceContext resources)
    {
        Asset::AssetResolveResult resolveResult;
        {
            NORVES_STAT_SCOPE_CATEGORY("ModelAsset.Resolve", "AssetLoad");
            resolveResult = ResolveCookedModel(assetSystem, logicalPath);
        }
        NORVES_LOG_INFO(
            "AssetLoadProfile",
            "stage=model_asset_resolve role=caller source=%s request_id=0 normalized_path=\"%.*s\" status=%u success=%d",
            resolveResult.UsedCooked() ? "cooked_nvmesh" : "none",
            static_cast<int>(resolveResult.NormalizedLogicalPath.size()),
            resolveResult.NormalizedLogicalPath.data(),
            static_cast<unsigned int>(resolveResult.Status),
            resolveResult.UsedCooked() ? 1 : 0);
        if (!resolveResult.UsedCooked())
        {
            return Rendering::ModelHandle::Invalid();
        }

        Asset::CookedMeshParseResult parseResult;
        {
            NORVES_STAT_SCOPE_CATEGORY("ModelAsset.ParseCooked", "AssetLoad");
            parseResult = Asset::ParseCookedMesh(resolveResult.Blob);
        }
        NORVES_LOG_INFO(
            "AssetLoadProfile",
            "stage=model_cooked_parse role=caller source=cooked_nvmesh request_id=0 normalized_path=\"%.*s\" status=%u success=%d",
            static_cast<int>(resolveResult.NormalizedLogicalPath.size()),
            resolveResult.NormalizedLogicalPath.data(),
            static_cast<unsigned int>(parseResult.Status),
            parseResult.Status == Asset::CookedMeshParseStatus::Success ? 1 : 0);
        if (parseResult.Status != Asset::CookedMeshParseStatus::Success ||
            !MatchesImportedVersion(resolveResult, parseResult.Mesh))
        {
            return Rendering::ModelHandle::Invalid();
        }

        const Container::AnsiStringView normalizedLogicalPath(
            resolveResult.NormalizedLogicalPath.data(),
            resolveResult.NormalizedLogicalPath.size());
        ModelStaging::ModelStagingData staging;
        if (!BuildModelStagingFromCookedMesh(
                parseResult.Mesh,
                logicalPath,
                ToOwnedString(normalizedLogicalPath),
                staging))
        {
            return Rendering::ModelHandle::Invalid();
        }

        return ModelStaging::FinalizeModelStaging(staging, resources, "main_render", 0);
    }

    bool LoadCookedModelForWorker(const CookedModelLoadPlan& plan,
                                  uint32_t requestId,
                                  CookedModelCpuLoadResult& outResult)
    {
        outResult = {};
        outResult.CacheKey = plan.CacheKey;
        outResult.Generation = plan.Generation;
        if (!plan.AssetSystem)
        {
            return false;
        }

        Asset::AssetResolveResult resolveResult;
        {
            NORVES_STAT_SCOPE_CATEGORY("ModelAsset.Resolve", "AssetLoad");
            resolveResult = ResolveCookedModel(*plan.AssetSystem, plan.RequestPath);
        }
        NORVES_LOG_INFO(
            "AssetLoadProfile",
            "stage=model_asset_resolve role=worker source=%s request_id=%u normalized_path=\"%.*s\" status=%u success=%d",
            resolveResult.UsedCooked() ? "cooked_nvmesh" : "none",
            static_cast<unsigned int>(requestId),
            static_cast<int>(resolveResult.NormalizedLogicalPath.size()),
            resolveResult.NormalizedLogicalPath.data(),
            static_cast<unsigned int>(resolveResult.Status),
            resolveResult.UsedCooked() ? 1 : 0);
        if (!resolveResult.UsedCooked() ||
            resolveResult.NormalizedLogicalPath != plan.NormalizedLogicalPath)
        {
            return false;
        }

        Asset::CookedMeshParseResult parseResult;
        {
            NORVES_STAT_SCOPE_CATEGORY("ModelAsset.ParseCooked", "AssetLoad");
            parseResult = Asset::ParseCookedMesh(resolveResult.Blob);
        }
        NORVES_LOG_INFO(
            "AssetLoadProfile",
            "stage=model_cooked_parse role=worker source=cooked_nvmesh request_id=%u normalized_path=\"%.*s\" status=%u success=%d",
            static_cast<unsigned int>(requestId),
            static_cast<int>(resolveResult.NormalizedLogicalPath.size()),
            resolveResult.NormalizedLogicalPath.data(),
            static_cast<unsigned int>(parseResult.Status),
            parseResult.Status == Asset::CookedMeshParseStatus::Success ? 1 : 0);
        if (parseResult.Status != Asset::CookedMeshParseStatus::Success ||
            !MatchesImportedVersion(resolveResult, parseResult.Mesh))
        {
            return false;
        }

        const Container::AnsiStringView normalizedLogicalPath(
            resolveResult.NormalizedLogicalPath.data(),
            resolveResult.NormalizedLogicalPath.size());
        outResult.bSuccess = BuildModelStagingFromCookedMesh(
            parseResult.Mesh,
            plan.RequestPath,
            ToOwnedString(normalizedLogicalPath),
            outResult.Staging);
        return outResult.bSuccess;
    }
} // namespace NorvesLib::Core::ResourceIO
