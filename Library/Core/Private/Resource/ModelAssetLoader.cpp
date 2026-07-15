#include "Resource/ModelAssetLoader.h"

#include "Asset/AssetSystem.h"
#include "Container/StringView.h"
#include "Resource/ModelAssetResolver.h"

namespace NorvesLib::Core::Resource
{
    namespace
    {
        Container::String ToOwnedString(Container::AnsiStringView value)
        {
            return Container::String(Container::StringView(value.data(), value.size()));
        }
    } // namespace

    bool BuildModelStagingFromCookedMesh(
        const Asset::CookedMeshData& cooked,
        const Container::String& debugName,
        const Container::String& resolvedPath,
        ModelStaging::ModelStagingData& outStaging)
    {
        outStaging = {};
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

        if (!cooked.Materials.empty())
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
        Asset::AssetResolveResult resolveResult = ResolveCookedModel(assetSystem, logicalPath);
        if (!resolveResult.UsedCooked())
        {
            return Rendering::ModelHandle::Invalid();
        }

        Asset::CookedMeshParseResult parseResult = Asset::ParseCookedMesh(resolveResult.Blob);
        if (parseResult.Status != Asset::CookedMeshParseStatus::Success)
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
} // namespace NorvesLib::Core::Resource
