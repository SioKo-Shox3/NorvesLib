// 空の遮蔽クリップマップの更新範囲を求め、計算シェーダーを記録する。
#include "Rendering/SkyOcclusionMapPass.h"

#include "Logging/LogMacros.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr int64_t kTileSize = 8;
        constexpr int64_t kCoordinateLimit = int64_t{1} << 50;
        // Vulkan が保証する X 方向のワークグループ数を超えないように分割する。
        constexpr uint32_t kMaxDispatchTiles = 65535u;

        struct Rect
        {
            int64_t MinX = 0;
            int64_t MinZ = 0;
            int64_t MaxX = 0;
            int64_t MaxZ = 0;
        };

        struct WorldBounds
        {
            double MinX = 0.0;
            double MinZ = 0.0;
            double MaxX = 0.0;
            double MaxZ = 0.0;
        };

        struct TileGrid
        {
            Rect Window;
            int64_t MinTileX = 0;
            int64_t MinTileZ = 0;
            uint32_t Width = 0u;
            uint32_t Height = 0u;
            Container::VariableArray<uint8_t> Splat;
            Container::VariableArray<uint8_t> Integrate;
            Container::VariableArray<Rect> SplatRects;
        };

        struct BinnedOccluder
        {
            WorldBounds Bounds;
            uint32_t PackedIndex = 0u;
            float Opacity = 0.0f;
        };

        int64_t FloorDivTile(int64_t value)
        {
            const int64_t quotient = value / kTileSize;
            return quotient - (value % kTileSize < 0 ? 1 : 0);
        }

        int64_t FloorCoordinate(double value)
        {
            return static_cast<int64_t>(std::floor(std::clamp(
                value, -static_cast<double>(kCoordinateLimit), static_cast<double>(kCoordinateLimit))));
        }

        int64_t CeilCoordinate(double value)
        {
            return static_cast<int64_t>(std::ceil(std::clamp(
                value, -static_cast<double>(kCoordinateLimit), static_cast<double>(kCoordinateLimit))));
        }

        Rect Intersect(const Rect& lhs, const Rect& rhs)
        {
            return {std::max(lhs.MinX, rhs.MinX), std::max(lhs.MinZ, rhs.MinZ),
                    std::min(lhs.MaxX, rhs.MaxX), std::min(lhs.MaxZ, rhs.MaxZ)};
        }

        bool IsEmpty(const Rect& rect)
        {
            return rect.MinX >= rect.MaxX || rect.MinZ >= rect.MaxZ;
        }

        Rect Expand(const Rect& rect, int64_t margin)
        {
            return {rect.MinX - margin, rect.MinZ - margin,
                    rect.MaxX + margin, rect.MaxZ + margin};
        }

        TileGrid MakeTileGrid(int32_t originX, int32_t originZ, uint32_t resolution)
        {
            TileGrid grid;
            grid.Window = {originX, originZ, static_cast<int64_t>(originX) + resolution,
                           static_cast<int64_t>(originZ) + resolution};
            grid.MinTileX = FloorDivTile(grid.Window.MinX);
            grid.MinTileZ = FloorDivTile(grid.Window.MinZ);
            grid.Width = static_cast<uint32_t>(FloorDivTile(grid.Window.MaxX - 1) - grid.MinTileX + 1);
            grid.Height = static_cast<uint32_t>(FloorDivTile(grid.Window.MaxZ - 1) - grid.MinTileZ + 1);
            const size_t count = static_cast<size_t>(grid.Width) * grid.Height;
            grid.Splat.resize(count, 0u);
            grid.Integrate.resize(count, 0u);
            return grid;
        }

        void MarkTiles(TileGrid& grid, const Rect& bounds, Container::VariableArray<uint8_t>& mask)
        {
            const Rect clipped = Intersect(grid.Window, bounds);
            if (IsEmpty(clipped))
            {
                return;
            }
            const int64_t x0 = FloorDivTile(clipped.MinX) - grid.MinTileX;
            const int64_t z0 = FloorDivTile(clipped.MinZ) - grid.MinTileZ;
            const int64_t x1 = FloorDivTile(clipped.MaxX - 1) - grid.MinTileX;
            const int64_t z1 = FloorDivTile(clipped.MaxZ - 1) - grid.MinTileZ;
            for (int64_t z = z0; z <= z1; ++z)
            {
                for (int64_t x = x0; x <= x1; ++x)
                {
                    mask[static_cast<size_t>(z) * grid.Width + static_cast<size_t>(x)] = 1u;
                }
            }
        }

        void AddSplatRect(TileGrid& grid, const Rect& bounds)
        {
            const Rect clipped = Intersect(grid.Window, bounds);
            if (!IsEmpty(clipped))
            {
                grid.SplatRects.push_back(clipped);
                MarkTiles(grid, clipped, grid.Splat);
            }
        }

        void AddExposedWindow(TileGrid& grid, const Rect& previous)
        {
            const Rect& window = grid.Window;
            AddSplatRect(grid, {window.MinX, window.MinZ, previous.MinX, window.MaxZ});
            AddSplatRect(grid, {previous.MaxX, window.MinZ, window.MaxX, window.MaxZ});
            AddSplatRect(grid, {window.MinX, window.MinZ, window.MaxX, previous.MinZ});
            AddSplatRect(grid, {window.MinX, previous.MaxZ, window.MaxX, window.MaxZ});
        }

        WorldBounds GetBounds(const SkyOccluderProxy& proxy)
        {
            const double cosine = std::abs(std::cos(static_cast<double>(proxy.Yaw)));
            const double sine = std::abs(std::sin(static_cast<double>(proxy.Yaw)));
            // GPU 側の三角関数の丸め差を含め、ビンから交差物を落とさない。
            const double extentX = (cosine * proxy.HalfExtentX + sine * proxy.HalfExtentZ) * 1.000001;
            const double extentZ = (sine * proxy.HalfExtentX + cosine * proxy.HalfExtentZ) * 1.000001;
            return {proxy.CenterX - extentX, proxy.CenterZ - extentZ,
                    proxy.CenterX + extentX, proxy.CenterZ + extentZ};
        }

        void AddOccluderRect(TileGrid& grid, const SkyOccluderProxy& proxy, float texelMeters)
        {
            if (!IsValidSkyOccluderProxy(proxy))
            {
                return;
            }
            const WorldBounds bounds = GetBounds(proxy);
            const double scale = static_cast<double>(texelMeters);
            AddSplatRect(grid, {FloorCoordinate(bounds.MinX / scale) - 1,
                                FloorCoordinate(bounds.MinZ / scale) - 1,
                                FloorCoordinate(bounds.MaxX / scale) + 2,
                                FloorCoordinate(bounds.MaxZ / scale) + 2});
        }

        void AddChangedOccluders(TileGrid& grid, float texelMeters,
                                 const Container::VariableArray<SkyOccluderProxy>& previous,
                                 const Container::VariableArray<SkyOccluderProxy>& current)
        {
            const size_t count = std::max(previous.size(), current.size());
            for (size_t index = 0u; index < count; ++index)
            {
                const bool bHadPrevious = index < previous.size();
                const bool bHasCurrent = index < current.size();
                if (bHadPrevious && bHasCurrent &&
                    AreEqualSkyOccluderProxy(previous[index], current[index]))
                {
                    continue;
                }
                if (bHadPrevious)
                {
                    AddOccluderRect(grid, previous[index], texelMeters);
                }
                if (bHasCurrent)
                {
                    AddOccluderRect(grid, current[index], texelMeters);
                }
            }
        }

        void AddWindowBands(TileGrid& grid, const Rect& previous, float texelMeters,
                            float reachMeters, bool bCoarsest, float maxHorizonMeters)
        {
            const int64_t dx = grid.Window.MinX - previous.MinX;
            const int64_t dz = grid.Window.MinZ - previous.MinZ;
            const double distance = bCoarsest ? maxHorizonMeters : reachMeters;
            const int64_t reach = CeilCoordinate(distance / texelMeters) + 1;
            if (dx != 0)
            {
                const int64_t width = reach + std::abs(dx);
                MarkTiles(grid, {grid.Window.MinX, grid.Window.MinZ,
                                 grid.Window.MinX + width, grid.Window.MaxZ}, grid.Integrate);
                MarkTiles(grid, {grid.Window.MaxX - width, grid.Window.MinZ,
                                 grid.Window.MaxX, grid.Window.MaxZ}, grid.Integrate);
            }
            if (dz != 0)
            {
                const int64_t width = reach + std::abs(dz);
                MarkTiles(grid, {grid.Window.MinX, grid.Window.MinZ,
                                 grid.Window.MaxX, grid.Window.MinZ + width}, grid.Integrate);
                MarkTiles(grid, {grid.Window.MinX, grid.Window.MaxZ - width,
                                 grid.Window.MaxX, grid.Window.MaxZ}, grid.Integrate);
            }
        }

        void AddIntegrationFromSplat(TileGrid* grids, const float* texelMeters,
                                     uint32_t levelCount, float maxHorizonMeters)
        {
            for (uint32_t source = 0u; source < levelCount; ++source)
            {
                for (const Rect& rect : grids[source].SplatRects)
                {
                    // タイル境界まで広げた実際の書き込みを、標本し得る他の段にも反映する。
                    const Rect written = {FloorDivTile(rect.MinX) * kTileSize,
                                          FloorDivTile(rect.MinZ) * kTileSize,
                                          (FloorDivTile(rect.MaxX - 1) + 1) * kTileSize,
                                          (FloorDivTile(rect.MaxZ - 1) + 1) * kTileSize};
                    const Rect clipped = Intersect(written, grids[source].Window);
                    for (uint32_t target = 0u; target < levelCount; ++target)
                    {
                        const double ratio = static_cast<double>(texelMeters[source]) / texelMeters[target];
                        const Rect projected = {FloorCoordinate(clipped.MinX * ratio),
                                                FloorCoordinate(clipped.MinZ * ratio),
                                                CeilCoordinate(clipped.MaxX * ratio),
                                                CeilCoordinate(clipped.MaxZ * ratio)};
                        const int64_t margin = CeilCoordinate(
                            static_cast<double>(maxHorizonMeters) / texelMeters[target]) + 1;
                        MarkTiles(grids[target], Expand(projected, margin), grids[target].Integrate);
                    }
                }
            }
        }

        void CollectTiles(const TileGrid& grid, uint32_t level,
                          const Container::VariableArray<uint8_t>& mask,
                          Container::VariableArray<SkyOcclusionWorkTile>& out)
        {
            for (uint32_t z = 0u; z < grid.Height; ++z)
            {
                for (uint32_t x = 0u; x < grid.Width; ++x)
                {
                    if (mask[static_cast<size_t>(z) * grid.Width + x] != 0u)
                    {
                        out.push_back({static_cast<int32_t>(grid.MinTileX + x),
                                       static_cast<int32_t>(grid.MinTileZ + z), level, 0u});
                    }
                }
            }
        }

        bool BuildBins(Container::VariableArray<SkyOcclusionWorkTile>& tiles,
                       const Container::VariableArray<SkyOccluderProxy>& input,
                       const float* texelMeters,
                       Container::VariableArray<SkyOccluderProxy>& packed,
                       Container::VariableArray<uint32_t>& indices,
                       uint64_t maxStorageBytes)
        {
            Container::VariableArray<BinnedOccluder> sorted;
            for (const SkyOccluderProxy& proxy : input)
            {
                if (IsValidSkyOccluderProxy(proxy))
                {
                    if (packed.size() >= maxStorageBytes / sizeof(SkyOccluderProxy))
                    {
                        return false;
                    }
                    const uint32_t packedIndex = static_cast<uint32_t>(packed.size());
                    packed.push_back(proxy);
                    sorted.push_back({GetBounds(proxy), packedIndex, std::clamp(proxy.Opacity, 0.0f, 1.0f)});
                }
            }
            std::sort(sorted.begin(), sorted.end(), [](const BinnedOccluder& lhs, const BinnedOccluder& rhs)
            {
                return lhs.Opacity != rhs.Opacity ? lhs.Opacity < rhs.Opacity : lhs.PackedIndex < rhs.PackedIndex;
            });
            for (SkyOcclusionWorkTile& tile : tiles)
            {
                if (indices.size() >= maxStorageBytes / sizeof(uint32_t))
                {
                    return false;
                }
                tile.ListOffset = static_cast<uint32_t>(indices.size());
                indices.push_back(0u);
                const double scale = texelMeters[tile.Level];
                const double minX = static_cast<double>(tile.TileX) * kTileSize * scale;
                const double minZ = static_cast<double>(tile.TileZ) * kTileSize * scale;
                const double maxX = (static_cast<double>(tile.TileX) * kTileSize + kTileSize) * scale;
                const double maxZ = (static_cast<double>(tile.TileZ) * kTileSize + kTileSize) * scale;
                for (const BinnedOccluder& item : sorted)
                {
                    if (item.Bounds.MaxX >= minX && item.Bounds.MinX <= maxX &&
                        item.Bounds.MaxZ >= minZ && item.Bounds.MinZ <= maxZ)
                    {
                        if (indices.size() >= maxStorageBytes / sizeof(uint32_t))
                        {
                            return false;
                        }
                        indices.push_back(item.PackedIndex);
                        ++indices[tile.ListOffset];
                    }
                }
            }
            return true;
        }

        bool NeedsFullRebuild(const SkyOcclusionParameters& previous, const SkyOcclusionParameters& current)
        {
            return previous.GroundHeightMeters != current.GroundHeightMeters ||
                   previous.ReceiverHeightMeters != current.ReceiverHeightMeters ||
                   previous.LevelCount != current.LevelCount ||
                   previous.BaseTexelMeters != current.BaseTexelMeters ||
                   previous.Resolution != current.Resolution ||
                   previous.MaxHorizonMeters != current.MaxHorizonMeters;
        }

        bool IsAnchorRepresentable(const SkyOcclusionParameters& parameters, float x, float z)
        {
            if (!std::isfinite(x) || !std::isfinite(z))
            {
                return false;
            }
            const double minimum = static_cast<double>(std::numeric_limits<int32_t>::min()) + 16.0;
            const double maximum = static_cast<double>(std::numeric_limits<int32_t>::max()) - parameters.Resolution - 16.0;
            for (uint32_t level = 0u; level < parameters.LevelCount; ++level)
            {
                const double scale = GetSkyOcclusionLevelTexelMeters(parameters, level);
                const double ox = std::floor(static_cast<double>(x) / scale) - parameters.Resolution / 2u;
                const double oz = std::floor(static_cast<double>(z) / scale) - parameters.Resolution / 2u;
                if (ox < minimum || ox > maximum || oz < minimum || oz > maximum)
                {
                    return false;
                }
            }
            return true;
        }

        RHI::DescriptorSetDesc MakeLayout(bool bSplat)
        {
            RHI::DescriptorSetDesc desc;
            for (uint32_t binding = 0u; binding < 5u; ++binding)
            {
                RHI::DescriptorBinding entry;
                entry.binding = binding;
                entry.stages = RHI::ShaderStage::Compute;
                entry.type = binding == 0u ? RHI::ResourceBindType::ConstantBuffer :
                             (bSplat ? binding < 4u : binding == 1u)
                                 ? RHI::ResourceBindType::StructuredBuffer : RHI::ResourceBindType::RWTexture;
                desc.bindings.push_back(entry);
            }
            return desc;
        }

        RHI::ResourceState GetTextureState(const ViewRenderContext& context, const RHI::TexturePtr& texture)
        {
            switch (context.CommandList->GetTextureLayout(texture.get()))
            {
            case RHI::TextureLayout::ShaderReadOnly:
                return RHI::ResourceState::ShaderResource;
            case RHI::TextureLayout::General:
                return RHI::ResourceState::UnorderedAccess;
            case RHI::TextureLayout::TransferSource:
                return RHI::ResourceState::CopySource;
            case RHI::TextureLayout::TransferDest:
                return RHI::ResourceState::CopyDest;
            default:
                return RHI::ResourceState::Undefined;
            }
        }

        void BindStorage(const RHI::DescriptorSetPtr& set, uint32_t binding,
                         const FrameUploadAllocation& allocation)
        {
            set->BindStorageBuffer(binding, allocation.Buffer, allocation.GetOffset32(), allocation.GetSize32());
        }
    } // 無名名前空間

    float GetSkyOcclusionLevelTexelMeters(const SkyOcclusionParameters& params, uint32_t level)
    {
        float width = params.BaseTexelMeters;
        for (uint32_t index = 0u; index < level; ++index)
        {
            width *= 4.0f;
        }
        return width;
    }

    void ComputeSkyOcclusionWindowOrigin(const SkyOcclusionParameters& params, uint32_t level,
                                         float anchorX, float anchorZ, int32_t& outX, int32_t& outZ)
    {
        const double width = GetSkyOcclusionLevelTexelMeters(params, level);
        const double minimum = static_cast<double>(std::numeric_limits<int32_t>::min()) + 16.0;
        const double maximum = static_cast<double>(std::numeric_limits<int32_t>::max()) - params.Resolution - 16.0;
        const auto origin = [&](float anchor)
        {
            const double value = std::floor(static_cast<double>(anchor) / width) - params.Resolution / 2u;
            return static_cast<int32_t>(std::clamp(value, minimum, maximum));
        };
        outX = origin(anchorX);
        outZ = origin(anchorZ);
    }

    uint32_t WrapSkyOcclusionTexel(int32_t worldTexel, uint32_t resolution)
    {
        if (resolution == 0u)
        {
            return 0u;
        }
        const int64_t remainder = static_cast<int64_t>(worldTexel) % resolution;
        return static_cast<uint32_t>(remainder < 0 ? remainder + resolution : remainder);
    }

    float GetSkyOcclusionOwnLevelReach(const SkyOcclusionParameters& params, uint32_t level)
    {
        return static_cast<float>(params.Resolution) * GetSkyOcclusionLevelTexelMeters(params, level) / 8.0f;
    }

    SkyOcclusionMapPass::SkyOcclusionMapPass(const SkyOcclusionMapPassSettings& settings)
        : m_Settings(settings)
    {
    }

    SkyOcclusionMapPass::~SkyOcclusionMapPass()
    {
        Shutdown();
    }

    bool SkyOcclusionMapPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            return false;
        }
        try
        {
            m_Device = context.Device;
            const RHI::DescriptorSetDesc splatLayout = MakeLayout(true);
            const RHI::DescriptorSetDesc integrateLayout = MakeLayout(false);
            m_SplatShader = context.ShaderMgr->LoadShader("SkyOcclusion/Splat.comp", RHI::ShaderStage::Compute);
            m_IntegrateShader = context.ShaderMgr->LoadShader("SkyOcclusion/Integrate.comp", RHI::ShaderStage::Compute);
            m_SplatDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(splatLayout);
            m_IntegrateDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(integrateLayout);
            if (m_SplatShader && m_IntegrateShader && m_SplatDescriptorSetLayout && m_IntegrateDescriptorSetLayout)
            {
                RHI::ComputePipelineDesc desc;
                desc.computeShader = m_SplatShader;
                desc.descriptorSetLayouts.push_back(splatLayout);
                m_SplatPipeline = m_Device->CreateComputePipeline(desc);
                desc.computeShader = m_IntegrateShader;
                desc.descriptorSetLayouts.clear();
                desc.descriptorSetLayouts.push_back(integrateLayout);
                m_IntegratePipeline = m_Device->CreateComputePipeline(desc);
                RHI::SamplerDesc samplerDesc;
                samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
                samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
                samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
                m_Sampler = m_Device->CreateSampler(samplerDesc);
            }
            m_bInitialized = m_SplatPipeline && m_IntegratePipeline && m_Sampler;
        }
        catch (const std::exception&)
        {
            m_bInitialized = false;
        }
        if (!m_bInitialized)
        {
            NORVES_LOG_WARNING("SkyOcclusionMapPass", "空の遮蔽パスの初期化に失敗しました");
            Shutdown();
            return false;
        }
        return true;
    }

    void SkyOcclusionMapPass::Shutdown()
    {
        m_SplatDispatches.clear();
        m_IntegrateDispatches.clear();
        m_LastOccluders.clear();
        m_PreparedOccluders.clear();
        m_OccludersTexture.reset();
        m_VisibilityTexture.reset();
        m_AuxTexture.reset();
        m_Sampler.reset();
        m_SplatPipeline.reset();
        m_IntegratePipeline.reset();
        m_SplatShader.reset();
        m_IntegrateShader.reset();
        m_SplatDescriptorSetLayout.reset();
        m_IntegrateDescriptorSetLayout.reset();
        m_OccludersHandle = {};
        m_VisibilityHandle = {};
        m_AuxHandle = {};
        m_Device = nullptr;
        m_LastParameters = {};
        m_LastUpdateSubmission = {};
        m_LastSkyOcclusionRevision = 0u;
        m_bHasAnchor = false;
        m_bHasValidData = false;
        m_bPrepared = false;
        m_bInitialized = false;
    }

    bool SkyOcclusionMapPass::EnsureTextures(const SkyOcclusionParameters& parameters)
    {
        const uint32_t arraySize = std::max(parameters.LevelCount, 2u);
        if (m_OccludersTexture && m_VisibilityTexture && m_AuxTexture &&
            m_OccludersTexture->GetWidth() == parameters.Resolution &&
            m_OccludersTexture->GetArraySize() == arraySize)
        {
            return true;
        }
        // RHI の 2D 配列ビューには最低 2 層が必要で、未使用の層には書き込まない。
        RHI::TextureDesc desc;
        desc.Width = parameters.Resolution;
        desc.Height = parameters.Resolution;
        desc.ArraySize = arraySize;
        desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite |
                     RHI::ResourceUsage::TransferSrc;
        desc.TextureFormat = RHI::Format::R32G32B32A32_FLOAT;
        desc.DebugName = "SkyOcclusion.Occluders";
        RHI::TexturePtr occluders = m_Device->CreateTexture(desc);
        desc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        desc.DebugName = "SkyOcclusion.Visibility";
        RHI::TexturePtr visibility = m_Device->CreateTexture(desc);
        desc.DebugName = "SkyOcclusion.Aux";
        RHI::TexturePtr aux = m_Device->CreateTexture(desc);
        if (!occluders || !visibility || !aux)
        {
            return false;
        }
        m_OccludersTexture = occluders;
        m_VisibilityTexture = visibility;
        m_AuxTexture = aux;
        m_bHasValidData = false;
        return true;
    }

    bool SkyOcclusionMapPass::PrepareUpdate(const ViewRenderContext& context)
    {
        const SceneProxy& scene = *context.SnapshotScene;
        const SkyOcclusionParameters& parameters = scene.SkyOcclusion;
        if (!EnsureTextures(parameters))
        {
            return false;
        }
        float anchorX = m_AnchorX;
        float anchorZ = m_AnchorZ;
        if (parameters.bUseCenterOverride)
        {
            anchorX = parameters.CenterOverrideX;
            anchorZ = parameters.CenterOverrideZ;
        }
        else
        {
            const CameraProxy& camera = *context.MainCamera;
            const double distance = std::max(std::abs(static_cast<double>(camera.PositionX) - anchorX),
                                             std::abs(static_cast<double>(camera.PositionZ) - anchorZ));
            if (!m_bHasAnchor || distance > static_cast<double>(m_Settings.RecenterThresholdTexels) * parameters.BaseTexelMeters)
            {
                anchorX = camera.PositionX;
                anchorZ = camera.PositionZ;
            }
        }
        if (!IsAnchorRepresentable(parameters, anchorX, anchorZ))
        {
            NORVES_LOG_WARNING("SkyOcclusionMapPass", "追従位置をワールドテクセルで表せません");
            return false;
        }

        m_PreparedParameters = parameters;
        m_PreparedAnchorX = anchorX;
        m_PreparedAnchorZ = anchorZ;
        m_PreparedRevision = scene.SkyOcclusionRevision;
        m_PreparedOccluders = scene.SkyOccluders;
        SkyOcclusionMapGpuParams constants;
        constants.counts[0] = parameters.LevelCount;
        constants.counts[1] = parameters.Resolution;
        constants.heights[0] = parameters.GroundHeightMeters;
        constants.heights[1] = parameters.ReceiverHeightMeters;
        constants.heights[2] = parameters.MaxHorizonMeters;
        TileGrid grids[SkyOcclusionMaxLevelCount];
        const bool bFull = !m_bHasValidData || NeedsFullRebuild(m_LastParameters, parameters);
        for (uint32_t level = 0u; level < SkyOcclusionMaxLevelCount; ++level)
        {
            m_PreparedWindowOrigin[level][0] = 0;
            m_PreparedWindowOrigin[level][1] = 0;
            m_PreparedTexelMeters[level] = 0.0f;
            if (level >= parameters.LevelCount)
            {
                continue;
            }
            int32_t& x = m_PreparedWindowOrigin[level][0];
            int32_t& z = m_PreparedWindowOrigin[level][1];
            ComputeSkyOcclusionWindowOrigin(parameters, level, anchorX, anchorZ, x, z);
            const float scale = GetSkyOcclusionLevelTexelMeters(parameters, level);
            m_PreparedTexelMeters[level] = scale;
            constants.windowOrigin[level][0] = x;
            constants.windowOrigin[level][1] = z;
            constants.texelMeters[level] = scale;
            grids[level] = MakeTileGrid(x, z, parameters.Resolution);
            if (bFull)
            {
                AddSplatRect(grids[level], grids[level].Window);
                MarkTiles(grids[level], grids[level].Window, grids[level].Integrate);
                continue;
            }
            const Rect previous = {m_WindowOrigin[level][0], m_WindowOrigin[level][1],
                                   static_cast<int64_t>(m_WindowOrigin[level][0]) + parameters.Resolution,
                                   static_cast<int64_t>(m_WindowOrigin[level][1]) + parameters.Resolution};
            AddExposedWindow(grids[level], previous);
            if (m_LastSkyOcclusionRevision != scene.SkyOcclusionRevision)
            {
                AddChangedOccluders(grids[level], scale, m_LastOccluders, scene.SkyOccluders);
            }
            AddWindowBands(grids[level], previous, scale, GetSkyOcclusionOwnLevelReach(parameters, level),
                           level + 1u == parameters.LevelCount, parameters.MaxHorizonMeters);
        }
        if (!bFull)
        {
            AddIntegrationFromSplat(grids, m_PreparedTexelMeters, parameters.LevelCount, parameters.MaxHorizonMeters);
        }

        Container::VariableArray<SkyOcclusionWorkTile> splatTiles;
        Container::VariableArray<SkyOcclusionWorkTile> integrateTiles;
        for (uint32_t level = 0u; level < parameters.LevelCount; ++level)
        {
            CollectTiles(grids[level], level, grids[level].Splat, splatTiles);
            CollectTiles(grids[level], level, grids[level].Integrate, integrateTiles);
        }
        if (splatTiles.empty() && integrateTiles.empty())
        {
            return true;
        }
        if (!context.FrameResources)
        {
            return false;
        }

        FrameResourceLane& lane = *context.FrameResources;
        FrameUploadAllocation proxiesAllocation;
        FrameUploadAllocation indicesAllocation;
        if (!splatTiles.empty())
        {
            Container::VariableArray<SkyOccluderProxy> packed;
            Container::VariableArray<uint32_t> indices;
            const uint64_t deviceLimit = m_Device->GetCapabilities().MaxStorageBufferRange;
            const uint64_t storageLimit = deviceLimit == 0u ? UINT32_MAX : std::min<uint64_t>(deviceLimit, UINT32_MAX);
            if (!BuildBins(splatTiles, scene.SkyOccluders, m_PreparedTexelMeters, packed, indices, storageLimit))
            {
                NORVES_LOG_WARNING("SkyOcclusionMapPass", "遮蔽物の一覧がストレージバッファの上限を超えました");
                return false;
            }
            // 遮蔽物が 0 個でも未参照のストレージ記述子には領域が必要。
            const SkyOccluderProxy emptyProxy{};
            proxiesAllocation = lane.AllocateUploadData(packed.empty() ? &emptyProxy : packed.data(),
                packed.empty() ? sizeof(emptyProxy) : static_cast<uint64_t>(packed.size()) * sizeof(SkyOccluderProxy),
                FrameUploadUsage::Storage);
            indicesAllocation = lane.AllocateUploadData(indices.data(),
                static_cast<uint64_t>(indices.size()) * sizeof(uint32_t), FrameUploadUsage::Storage);
            if (!proxiesAllocation.IsValid() || !indicesAllocation.IsValid())
            {
                return false;
            }
        }

        const auto prepareDispatches = [&](const Container::VariableArray<SkyOcclusionWorkTile>& tiles,
                                           bool bSplat, Container::VariableArray<PreparedDispatch>& dispatches)
        {
            for (size_t offset = 0u; offset < tiles.size(); offset += kMaxDispatchTiles)
            {
                const uint32_t count = static_cast<uint32_t>(std::min<size_t>(kMaxDispatchTiles, tiles.size() - offset));
                constants.counts[2] = count;
                const FrameUploadAllocation uniform = lane.AllocateConstants(constants);
                const FrameUploadAllocation work = lane.AllocateUploadData(tiles.data() + offset,
                    static_cast<uint64_t>(count) * sizeof(SkyOcclusionWorkTile), FrameUploadUsage::Storage);
                const FrameDescriptorAllocation descriptor = lane.AllocateDescriptorSet(
                    bSplat ? m_SplatDescriptorSetLayout : m_IntegrateDescriptorSetLayout);
                if (!uniform.IsValid() || !work.IsValid() || !descriptor.IsValid())
                {
                    return false;
                }
                const RHI::DescriptorSetPtr& set = descriptor.Set;
                set->BindConstantBuffer(0u, uniform.Buffer, uniform.GetOffset32(), uniform.GetSize32());
                if (bSplat)
                {
                    BindStorage(set, 1u, proxiesAllocation);
                    BindStorage(set, 2u, work);
                    BindStorage(set, 3u, indicesAllocation);
                    set->BindStorageTexture(4u, m_OccludersTexture);
                }
                else
                {
                    BindStorage(set, 1u, work);
                    set->BindStorageTexture(2u, m_OccludersTexture);
                    set->BindStorageTexture(3u, m_VisibilityTexture);
                    set->BindStorageTexture(4u, m_AuxTexture);
                }
                set->Update();
                dispatches.push_back({set, count});
            }
            return true;
        };
        return prepareDispatches(splatTiles, true, m_SplatDispatches) &&
               prepareDispatches(integrateTiles, false, m_IntegrateDispatches);
    }

    void SkyOcclusionMapPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void SkyOcclusionMapPass::Execute(ViewRenderContext& /*context*/)
    {
        // 記録と公開はレンダーグラフ経由で行う。
    }

    void SkyOcclusionMapPass::Declare(RenderGraphBuilder& builder)
    {
        m_bPrepared = false;
        m_SplatDispatches.clear();
        m_IntegrateDispatches.clear();
        m_OccludersHandle = {};
        m_VisibilityHandle = {};
        m_AuxHandle = {};
        const ViewRenderContext* context = builder.GetContext();
        if (!m_bInitialized || !m_bEnabled || !context || !context->CommandList || !context->SnapshotScene ||
            !context->SnapshotScene->SkyOcclusion.bEnabled ||
            (!context->SnapshotScene->SkyOcclusion.bUseCenterOverride && !context->MainCamera))
        {
            return;
        }
        if (m_LastUpdateSubmission.IsSet())
        {
            const FrameSubmissionOutcome outcome = context->FrameResources
                ? context->FrameResources->GetSubmissionOutcome(m_LastUpdateSubmission)
                : FrameSubmissionOutcome::Unknown;
            if (outcome == FrameSubmissionOutcome::Aborted || outcome == FrameSubmissionOutcome::Unknown)
            {
                // 提出されなかった記録は layout も実行されていないため、画像ごと作り直す。
                m_OccludersTexture.reset();
                m_VisibilityTexture.reset();
                m_AuxTexture.reset();
                m_bHasValidData = false;
                m_bHasAnchor = false;
            }
            if (outcome != FrameSubmissionOutcome::Recording)
            {
                m_LastUpdateSubmission = {};
            }
        }
        try
        {
            m_bPrepared = PrepareUpdate(*context);
        }
        catch (const std::exception&)
        {
            m_bPrepared = false;
        }
        if (!m_bPrepared)
        {
            NORVES_LOG_WARNING("SkyOcclusionMapPass", "空の遮蔽マップの更新を準備できませんでした");
            return;
        }
        const RGResourceHandle occluders = builder.ImportTexture(m_OccludersTexture,
            GetTextureState(*context, m_OccludersTexture), "SkyOcclusion.Occluders");
        const RGResourceHandle visibility = builder.ImportTexture(m_VisibilityTexture,
            GetTextureState(*context, m_VisibilityTexture), "SkyOcclusion.Visibility");
        const RGResourceHandle aux = builder.ImportTexture(m_AuxTexture,
            GetTextureState(*context, m_AuxTexture), "SkyOcclusion.Aux");
        if (!occluders.IsValid() || !visibility.IsValid() || !aux.IsValid() ||
            !builder.PublishTexture(RenderGraphResourceNames::SkyOcclusionOccluders, occluders) ||
            !builder.PublishTexture(RenderGraphResourceNames::SkyOcclusionVisibility, visibility) ||
            !builder.PublishTexture(RenderGraphResourceNames::SkyOcclusionAux, aux) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SkyOcclusionOccluders, m_OccludersHandle) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SkyOcclusionVisibility, m_VisibilityHandle) ||
            !builder.TryGetTexture(RenderGraphResourceNames::SkyOcclusionAux, m_AuxHandle))
        {
            m_bPrepared = false;
            return;
        }
        if (!m_SplatDispatches.empty() || !m_IntegrateDispatches.empty())
        {
            builder.Write(occluders, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
            builder.Write(visibility, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
            builder.Write(aux, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
        }
        else
        {
            builder.Read(occluders, RHI::ResourceState::ShaderResource);
            builder.Read(visibility, RHI::ResourceState::ShaderResource);
            builder.Read(aux, RHI::ResourceState::ShaderResource);
        }
        builder.PreserveInsertionOrder();
    }

    void SkyOcclusionMapPass::PublishContextResources(RenderGraphResources& resources, ViewRenderContext& context) const
    {
        if (!m_bPrepared || !m_OccludersHandle.IsValid() || !m_VisibilityHandle.IsValid() || !m_AuxHandle.IsValid() ||
            !resources.GetTexture(m_OccludersHandle) || !resources.GetTexture(m_VisibilityHandle) ||
            !resources.GetTexture(m_AuxHandle))
        {
            return;
        }
        context.SkyOcclusion.Publish(m_PreparedParameters, m_VisibilityTexture, m_AuxTexture, m_Sampler,
                                     m_PreparedWindowOrigin, m_PreparedTexelMeters,
                                     m_PreparedParameters.LevelCount, m_PreparedParameters.Resolution, true);
    }

    void SkyOcclusionMapPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bPrepared || !context.CommandList || !resources.GetTexture(m_OccludersHandle) ||
            !resources.GetTexture(m_VisibilityHandle) || !resources.GetTexture(m_AuxHandle) ||
            ((!m_SplatDispatches.empty() || !m_IntegrateDispatches.empty()) && !context.FrameResources))
        {
            return;
        }
        RHI::ICommandList& commands = *context.CommandList;
        if (!m_SplatDispatches.empty() || !m_IntegrateDispatches.empty())
        {
            commands.SetPipeline(m_SplatPipeline);
            for (const PreparedDispatch& dispatch : m_SplatDispatches)
            {
                commands.SetDescriptorSet(dispatch.DescriptorSet, 0u);
                commands.Dispatch(dispatch.TileCount, 1u, 1u);
            }
            if (!m_SplatDispatches.empty())
            {
                commands.TextureBarrier(m_OccludersTexture, RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::UnorderedAccess);
            }
            commands.SetPipeline(m_IntegratePipeline);
            for (const PreparedDispatch& dispatch : m_IntegrateDispatches)
            {
                commands.SetDescriptorSet(dispatch.DescriptorSet, 0u);
                commands.Dispatch(dispatch.TileCount, 1u, 1u);
            }
            commands.TextureBarrier(m_OccludersTexture, RHI::ResourceState::UnorderedAccess,
                                    RHI::ResourceState::ShaderResource);
            commands.TextureBarrier(m_VisibilityTexture, RHI::ResourceState::UnorderedAccess,
                                    RHI::ResourceState::ShaderResource);
            commands.TextureBarrier(m_AuxTexture, RHI::ResourceState::UnorderedAccess,
                                    RHI::ResourceState::ShaderResource);
            // 借りる口が提出の成否を保持し、中止された更新の再利用を防ぐ。
            m_LastUpdateSubmission = context.FrameResources->GetGeneration();
        }
        m_AnchorX = m_PreparedAnchorX;
        m_AnchorZ = m_PreparedAnchorZ;
        for (uint32_t level = 0u; level < SkyOcclusionMaxLevelCount; ++level)
        {
            m_WindowOrigin[level][0] = m_PreparedWindowOrigin[level][0];
            m_WindowOrigin[level][1] = m_PreparedWindowOrigin[level][1];
        }
        m_LastParameters = m_PreparedParameters;
        m_LastSkyOcclusionRevision = m_PreparedRevision;
        m_LastOccluders = std::move(m_PreparedOccluders);
        m_bHasAnchor = true;
        m_bHasValidData = true;
        PublishContextResources(resources, context);
    }

    bool SkyOcclusionMapPass::DeclaresAllLayoutChanges() const
    {
        return true;
    }

    RGRecordPolicy SkyOcclusionMapPass::GetRecordPolicy() const
    {
        return RGRecordPolicy::Parallel;
    }

    void SkyOcclusionMapPass::PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context)
    {
        PublishContextResources(resources, context);
    }
} // namespace NorvesLib::Core::Rendering
