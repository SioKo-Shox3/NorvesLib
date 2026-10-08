#include "Rendering/VirtualShadowMapPages.h"

#include "Logging/LogMacros.h"
#include "Rendering/ScopedGpuTimestamp.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VirtualShadowMapSample.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ITexture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // シェーダー（vsm_mark.comp・vsm_allocate.comp・vsm_clear.comp）の VsmParams（std140）と同じ並び
        struct GPUVsmParams
        {
            float invViewProjection[16];
            float cameraPosition[4];
            float lightRight[4];
            float lightUp[4];
            uint32_t screen[4];  // x = 幅、y = 高さ、z = スライス（段）の数、w = 1 段の一辺のページ数
            float tuning[4];     // x = PCF の核の半径のうち texel に比例する分（texel）、y = 影の範囲の奥の端（前方への距離 m）、z = 探索・PCF の半径の上限（m）
            uint32_t control[4]; // x = 段階、y = 物理ページの数、z = 間接 dispatch の x の上限
            float thresholds[VirtualShadowMapMaxLevels];
            float view[4];       // x, y, z = カメラの前方（単位ベクトル）、w = 影の範囲の手前の端（前方への距離 m）
            // ここから下は vsm_allocate.comp だけが読む（vsm_mark.comp・vsm_clear.comp の VsmParams はここまでの前半と同じ並び）
            uint32_t cache[4];                                  // x = 印（CacheFlag*）、y = 持ち越すフレーム数、z = 無効化の矩形の数
            float rects[VirtualShadowMap::MAX_INVALIDATION_RECTS][4]; // 無効化の矩形（ライト空間。x, y = 最小、z, w = 最大）
        };
        // ページの一辺・texel・範囲の原点（前フレームの分を含む）はスライスの表（GPUVsmSlice。binding BindSlices）にある
        static_assert(sizeof(GPUVsmParams) == 224 + 16 + 16 + VirtualShadowMap::MAX_INVALIDATION_RECTS * 16,
                      "vsm_*.comp の VsmParams と同じ大きさにすること");

        constexpr uint32_t GroupSize = 256;
        constexpr uint32_t MarkGroupSize = 8;
        // vsm_allocate.comp の段階（control.x）。この順に 1 回ずつ dispatch する
        enum AllocateStage : uint32_t
        {
            StageScroll = 0,
            StageRects,
            StageAge,
            StageEvictPlan,
            StageEvict,
            StageFreeReset,
            StageFreeMark,
            StageFreeCompact,
            StageAllocate,
            StageDirtyList,
            StageFinalize,
            StageCount,
        };
        constexpr uint32_t CacheFlagEnabled = 1u;
        constexpr uint32_t CacheFlagInvalidateAll = 2u;
        // 無効化の矩形の外側へ足す余白（m）。展開の範囲の計算（float）との丸めの差でページを取りこぼさないため
        constexpr float InvalidationMarginMeters = 1.0e-3f;
        // 絶対のページの番号を int32 でシェーダーへ渡せる範囲（範囲の端 + 128 ページが溢れない余裕を持つ）
        constexpr int64_t MaxOriginMagnitude = 1ll << 30;

        constexpr uint32_t BindParams = 0;
        constexpr uint32_t BindDepth = 1;
        constexpr uint32_t BindRequestBits = 2;
        constexpr uint32_t BindPageTable = 3;
        constexpr uint32_t BindFreeList = 4;
        constexpr uint32_t BindStats = 5;
        constexpr uint32_t BindDirtyList = 6;
        constexpr uint32_t BindPool = 7;
        // vsm_mark.comp・vsm_allocate.comp のスライスの表
        constexpr uint32_t BindSlices = 8;

        RHI::DescriptorBinding MakeBinding(uint32_t binding, RHI::ResourceBindType type)
        {
            RHI::DescriptorBinding result;
            result.binding = binding;
            result.type = type;
            result.stages = RHI::ShaderStage::Compute;
            return result;
        }

        RHI::DescriptorSetDesc MakeMarkLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(BindParams, RHI::ResourceBindType::ConstantBuffer));
            desc.bindings.push_back(MakeBinding(BindDepth, RHI::ResourceBindType::CombinedImageSampler));
            desc.bindings.push_back(MakeBinding(BindRequestBits, RHI::ResourceBindType::RWBuffer));
            desc.bindings.push_back(MakeBinding(BindSlices, RHI::ResourceBindType::StructuredBuffer));
            return desc;
        }

        RHI::DescriptorSetDesc MakeAllocateLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(BindParams, RHI::ResourceBindType::ConstantBuffer));
            for (const uint32_t binding : {BindRequestBits, BindPageTable, BindFreeList, BindStats, BindDirtyList})
            {
                desc.bindings.push_back(MakeBinding(binding, RHI::ResourceBindType::RWBuffer));
            }
            desc.bindings.push_back(MakeBinding(BindSlices, RHI::ResourceBindType::StructuredBuffer));
            return desc;
        }

        RHI::DescriptorSetDesc MakeClearLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(BindParams, RHI::ResourceBindType::ConstantBuffer));
            desc.bindings.push_back(MakeBinding(BindDirtyList, RHI::ResourceBindType::RWBuffer));
            desc.bindings.push_back(MakeBinding(BindPool, RHI::ResourceBindType::RWBuffer));
            return desc;
        }

        uint32_t ClampBindSize(uint64_t size)
        {
            return size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                               : static_cast<uint32_t>(size);
        }

        uint32_t GroupsFor(uint32_t count, uint32_t groupSize)
        {
            return (count + groupSize - 1u) / groupSize;
        }

        // バッファは呼び出し前に UnorderedAccess の状態で、終わっても UnorderedAccess
        void ZeroFill(RHI::ICommandList* commandList, const RHI::BufferPtr& buffer, uint64_t size)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopyDest, 0u, size);
            commandList->FillBuffer(buffer, 0u, size, 0u);
            commandList->BufferBarrier(buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess, 0u, size);
        }

        void BarrierWrites(RHI::ICommandList* commandList, std::initializer_list<RHI::BufferPtr> buffers)
        {
            for (const RHI::BufferPtr& buffer : buffers)
            {
                commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            }
        }

        bool IsOriginInRange(int64_t origin)
        {
            return origin > -MaxOriginMagnitude && origin < MaxOriginMagnitude;
        }

        // 向きの成分が 1 つでも違えばページの中身は古い。前フレームとの差に許容を設けると、
        // 許容未満の微小な回転が毎フレーム続いたとき（比較元も毎フレーム更新される）に無効化されないまま累積する
        bool IsSameDirection(const Math::Vector3& a, const Math::Vector3& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        // 段の設定（ページの大きさ・段の数）が前フレームと同じか。違えば、ページの表の欄が指すページの意味が変わるので引き継げない
        bool IsSameLevelLayout(const VirtualShadowMapClipmap& previous, const VirtualShadowMapClipmap& current)
        {
            if (previous.LevelCount != current.LevelCount || previous.PagesPerAxis != current.PagesPerAxis)
            {
                return false;
            }
            for (uint32_t level = 0; level < current.LevelCount; ++level)
            {
                if (previous.Levels[level].PageMeters != current.Levels[level].PageMeters ||
                    previous.Levels[level].TexelMeters != current.Levels[level].TexelMeters)
                {
                    return false;
                }
            }
            return true;
        }

        // 印付けに使える入力か。使えるなら params へクリップマップ由来の値を書く
        bool FillMarkParams(const VirtualShadowMapPagesDispatch& dispatch, uint32_t width, uint32_t height, GPUVsmParams& params)
        {
            const VirtualShadowMapClipmap* clipmap = dispatch.Clipmap;
            if (clipmap == nullptr || !clipmap->bEnabled || clipmap->LevelCount == 0u ||
                clipmap->LevelCount > VirtualShadowMap::LEVEL_COUNT ||
                clipmap->PagesPerAxis != VirtualShadowMap::TABLE_DIMENSION ||
                clipmap->Settings.PageResolution != VirtualShadowMap::PAGE_RESOLUTION)
            {
                return false;
            }
            if (!VirtualShadowMapLevelDistanceThresholds(clipmap->Settings,
                                                         dispatch.FovYDegrees,
                                                         static_cast<float>(height),
                                                         params.thresholds))
            {
                return false;
            }
            for (uint32_t level = 0; level < clipmap->LevelCount; ++level)
            {
                const VirtualShadowMapClipmapLevel& data = clipmap->Levels[level];
                if (!IsOriginInRange(data.OriginPageX) || !IsOriginInRange(data.OriginPageY) || !(data.PageMeters > 0.0f))
                {
                    return false;
                }
            }
            const float forwardLength = std::sqrt(dispatch.CameraForward[0] * dispatch.CameraForward[0] +
                                                  dispatch.CameraForward[1] * dispatch.CameraForward[1] +
                                                  dispatch.CameraForward[2] * dispatch.CameraForward[2]);
            if (!std::isfinite(forwardLength) || !(forwardLength > 1.0e-5f))
            {
                return false;
            }
            float shadowNear = dispatch.ShadowNearMeters;
            float shadowFar = dispatch.ShadowFarMeters;
            if (!(std::isfinite(shadowNear) && std::isfinite(shadowFar) && shadowFar > shadowNear))
            {
                shadowNear = 0.0f;
                shadowFar = clipmap->Settings.MaxShadowDistance;
            }
            for (uint32_t element = 0; element < 16u; ++element)
            {
                params.invViewProjection[element] = dispatch.InverseViewProjection[element];
            }
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                params.view[axis] = dispatch.CameraForward[axis] / forwardLength;
            }
            params.view[3] = shadowNear;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                params.cameraPosition[axis] = dispatch.CameraPosition[axis];
            }
            params.lightRight[0] = clipmap->LightRight.x;
            params.lightRight[1] = clipmap->LightRight.y;
            params.lightRight[2] = clipmap->LightRight.z;
            params.lightUp[0] = clipmap->LightUp.x;
            params.lightUp[1] = clipmap->LightUp.y;
            params.lightUp[2] = clipmap->LightUp.z;
            params.screen[0] = width;
            params.screen[1] = height;
            params.screen[2] = clipmap->LevelCount;
            params.tuning[0] = dispatch.PcfRadiusTexels;
            params.tuning[1] = shadowFar;
            params.tuning[2] = dispatch.MaxFilterRadiusMeters;
            return true;
        }
    } // namespace

    VirtualShadowMapPages::VirtualShadowMapPages() = default;

    VirtualShadowMapPages::~VirtualShadowMapPages()
    {
        Shutdown();
    }

    bool VirtualShadowMapPages::CreatePipeline(ShaderManager* shaderManager,
                                               const char* shaderName,
                                               const RHI::DescriptorSetDesc& layout,
                                               RHI::ShaderPtr& outShader,
                                               RHI::PipelinePtr& outPipeline)
    {
        outShader = shaderManager->LoadShader(shaderName, RHI::ShaderStage::Compute);
        if (!outShader)
        {
            NORVES_LOG_WARNING("VirtualShadowMapPages", "VSM の計算シェーダーの読み込みに失敗: %s", shaderName);
            return false;
        }
        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = outShader;
        pipelineDesc.descriptorSetLayouts.push_back(layout);
        outPipeline = m_Device->CreateComputePipeline(pipelineDesc);
        if (!outPipeline)
        {
            NORVES_LOG_WARNING("VirtualShadowMapPages", "VSM の計算パイプラインの作成に失敗: %s", shaderName);
            return false;
        }
        return true;
    }

    bool VirtualShadowMapPages::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }
        m_Device = device;

        // 深度は texelFetch で読むのでフィルターしない
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_PointSampler = device->CreateSampler(samplerDesc);
        if (!m_PointSampler)
        {
            NORVES_LOG_WARNING("VirtualShadowMapPages", "VSM のサンプラーの作成に失敗");
            Shutdown();
            return false;
        }

        if (!CreatePipeline(shaderManager, "vsm_mark.comp", MakeMarkLayout(), m_MarkShader, m_MarkPipeline) ||
            !CreatePipeline(shaderManager, "vsm_allocate.comp", MakeAllocateLayout(), m_AllocateShader, m_AllocatePipeline) ||
            !CreatePipeline(shaderManager, "vsm_clear.comp", MakeClearLayout(), m_ClearShader, m_ClearPipeline))
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void VirtualShadowMapPages::Shutdown()
    {
        m_MarkUses.Clear();
        m_AllocateUses.Clear();
        m_ClearUses.Clear();
        m_MarkPipeline.reset();
        m_AllocatePipeline.reset();
        m_ClearPipeline.reset();
        m_MarkShader.reset();
        m_AllocateShader.reset();
        m_ClearShader.reset();
        m_PointSampler.reset();
        m_Device = nullptr;
        m_bMarked = false;
        m_bCacheValid = false;
        m_bCacheContinued = false;
        m_bInvalidatedAll = false;
        m_CachedPageTable = nullptr;
        m_CachedPool = nullptr;
        m_CachedPoolPages = 0;
    }

    void VirtualShadowMapPages::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_MarkUses.BeginFrame(inFlightIndex, frameSerial);
        m_AllocateUses.BeginFrame(inFlightIndex, frameSerial);
        m_ClearUses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool VirtualShadowMapPages::AcquireUse(FrameUseRing<Use>& ring, const RHI::DescriptorSetDesc& layout, Use*& outUse)
    {
        Use& use = ring.Acquire();
        if (!use.Uniform)
        {
            use.Uniform = m_Device->CreateBuffer(
                RHI::BufferDesc(sizeof(GPUVsmParams), RHI::ResourceUsage::ConstantBuffer, true, "VsmParams"));
        }
        if (!use.Slices)
        {
            use.Slices = m_Device->CreateBuffer(RHI::BufferDesc(
                sizeof(GPUVsmSlice) * VirtualShadowMapMaxSlices, RHI::ResourceUsage::StorageBuffer, true, "VsmSlices"));
        }
        if (!use.DescriptorSet)
        {
            use.DescriptorSet = m_Device->CreateDescriptorSet(layout);
        }
        outUse = &use;
        return use.Uniform && use.Slices && use.DescriptorSet;
    }

    bool VirtualShadowMapPages::Record(RHI::ICommandList* commandList, const VirtualShadowMapPagesDispatch& dispatch)
    {
        m_bMarked = false;
        m_bCacheContinued = false;
        m_bInvalidatedAll = false;
        // 記録の途中で戻ったとき、古い状態を次の記録が引き継がないよう先に無効にする（最後で入力を残して有効に戻す）
        const bool bCacheWasValid = m_bCacheValid;
        m_bCacheValid = false;
        if (!IsReady() || !commandList || dispatch.PoolPages == 0 || dispatch.PoolPages > VirtualShadowMap::MAX_POOL_PAGES ||
            !dispatch.Pool || !dispatch.PageTable || !dispatch.RequestBits || !dispatch.FreeList || !dispatch.Stats ||
            !dispatch.DirtyList)
        {
            return false;
        }
        if (dispatch.Pool->GetSize() < VirtualShadowMap::PoolBytes(dispatch.PoolPages) ||
            dispatch.PageTable->GetSize() < VirtualShadowMap::PageTableBytes() ||
            dispatch.RequestBits->GetSize() < VirtualShadowMap::RequestBitsBytes() ||
            dispatch.FreeList->GetSize() < VirtualShadowMap::FreeListBytes(dispatch.PoolPages) ||
            dispatch.Stats->GetSize() < VirtualShadowMap::STATS_BYTES ||
            dispatch.DirtyList->GetSize() < VirtualShadowMap::DirtyListBytes(dispatch.PoolPages))
        {
            return false;
        }

        GPUVsmParams baseParams = {};
        baseParams.screen[3] = VirtualShadowMap::TABLE_DIMENSION;
        baseParams.screen[2] = VirtualShadowMap::LEVEL_COUNT;
        baseParams.control[1] = dispatch.PoolPages;
        baseParams.control[2] = VirtualShadowMap::GROUP_COUNT_X_LIMIT;

        const uint32_t width = dispatch.Depth ? dispatch.Depth->GetWidth() : 0u;
        const uint32_t height = dispatch.Depth ? dispatch.Depth->GetHeight() : 0u;
        GPUVsmParams markParams = baseParams;
        const bool bMark = dispatch.Depth && width != 0u && height != 0u &&
                           FillMarkParams(dispatch, width, height, markParams);
        // スライスの表。印付けに使えないフレームは、ページの表の先頭・一辺だけを持つ表（ページの一辺 0 = 何も無い）を渡す
        GPUVsmSlice markSlices[VirtualShadowMapMaxSlices];
        BuildVirtualShadowMapSlices(bMark ? dispatch.Clipmap : nullptr, nullptr, markSlices);

        Use* markUse = nullptr;
        Use* allocateUses[StageCount] = {};
        Use* clearUse = nullptr;
        if (bMark && !AcquireUse(m_MarkUses, MakeMarkLayout(), markUse))
        {
            return false;
        }
        for (Use*& use : allocateUses)
        {
            if (!AcquireUse(m_AllocateUses, MakeAllocateLayout(), use))
            {
                return false;
            }
        }
        if (!AcquireUse(m_ClearUses, MakeClearLayout(), clearUse))
        {
            return false;
        }

        // ----- 印付け -----
        {
            ScopedGpuTimestamp timestamp(commandList, "VsmMark");
            ZeroFill(commandList, dispatch.RequestBits, VirtualShadowMap::RequestBitsBytes());
            if (bMark)
            {
                markUse->Uniform->Update(&markParams, sizeof(markParams));
                markUse->Slices->Update(markSlices, sizeof(markSlices));
                markUse->DescriptorSet->BindConstantBuffer(BindParams, markUse->Uniform, 0, sizeof(markParams));
                markUse->DescriptorSet->BindStorageBuffer(BindSlices, markUse->Slices, 0, sizeof(markSlices));
                markUse->DescriptorSet->BindTexture(BindDepth, dispatch.Depth);
                markUse->DescriptorSet->BindSampler(BindDepth, m_PointSampler);
                markUse->DescriptorSet->BindStorageBuffer(BindRequestBits, dispatch.RequestBits, 0,
                                                          ClampBindSize(VirtualShadowMap::RequestBitsBytes()));
                markUse->DescriptorSet->Update();

                commandList->SetPipeline(m_MarkPipeline);
                commandList->SetDescriptorSet(markUse->DescriptorSet, 0);
                commandList->Dispatch(GroupsFor(width, MarkGroupSize), GroupsFor(height, MarkGroupSize), 1u);
                m_bMarked = true;
            }
            BarrierWrites(commandList, {dispatch.RequestBits});
        }

        // ----- 前フレームのページの表を引き継げるか -----
        // 引き継ぐには、キャッシュを使い、印付けをして、同じ資源・同じ段の設定で前フレームも記録していること。
        // 引き継がないときは表を 0 にして全部を割り当て直す（資源は未初期化・見張りの値でもよい）
        const bool bCacheWanted = dispatch.bCacheEnabled && bMark;
        const bool bContinue = bCacheWanted && bCacheWasValid && m_CachedPageTable == dispatch.PageTable.get() &&
                               m_CachedPool == dispatch.Pool.get() && m_CachedPoolPages == dispatch.PoolPages &&
                               IsSameLevelLayout(m_PreviousClipmap, *dispatch.Clipmap);
        bool bInvalidateAll = false;
        if (bContinue)
        {
            const VirtualShadowMapClipmap& current = *dispatch.Clipmap;
            bInvalidateAll = dispatch.bInvalidateAll || !IsSameDirection(m_PreviousClipmap.Direction, current.Direction) ||
                             !IsSameDirection(m_PreviousClipmap.LightRight, current.LightRight) ||
                             !IsSameDirection(m_PreviousClipmap.LightUp, current.LightUp) ||
                             m_PreviousClipmap.DepthCenter != current.DepthCenter ||
                             dispatch.InvalidationRectCount > VirtualShadowMap::MAX_INVALIDATION_RECTS;
        }
        m_bCacheContinued = bContinue;
        m_bInvalidatedAll = bInvalidateAll;

        // ----- 割り当て -----
        {
            ScopedGpuTimestamp timestamp(commandList, "VsmAllocate");
            if (!bContinue)
            {
                ZeroFill(commandList, dispatch.PageTable, VirtualShadowMap::PageTableBytes());
            }
            ZeroFill(commandList, dispatch.Stats, VirtualShadowMap::STATS_BYTES);

            GPUVsmParams allocateParams = bMark ? markParams : baseParams;
            allocateParams.cache[0] = (bCacheWanted ? CacheFlagEnabled : 0u) | (bInvalidateAll ? CacheFlagInvalidateAll : 0u);
            allocateParams.cache[1] = VirtualShadowMap::CACHE_CARRY_FRAMES;
            const uint32_t rectCount = bContinue && !bInvalidateAll ? dispatch.InvalidationRectCount : 0u;
            allocateParams.cache[2] = rectCount;
            // 引き継がないときは、範囲が動いていない（前フレームも今フレームと同じ）ものとして扱う（前の原点が null なら今の原点と同じ）
            GPUVsmSlice allocateSlices[VirtualShadowMapMaxSlices];
            BuildVirtualShadowMapSlices(bMark ? dispatch.Clipmap : nullptr, bContinue ? &m_PreviousClipmap : nullptr, allocateSlices);
            for (uint32_t index = 0; index < rectCount; ++index)
            {
                const float* rect = dispatch.InvalidationRects + static_cast<size_t>(index) * 4u;
                allocateParams.rects[index][0] = rect[0] - InvalidationMarginMeters;
                allocateParams.rects[index][1] = rect[1] - InvalidationMarginMeters;
                allocateParams.rects[index][2] = rect[2] + InvalidationMarginMeters;
                allocateParams.rects[index][3] = rect[3] + InvalidationMarginMeters;
            }

            for (uint32_t index = 0; index < StageCount; ++index)
            {
                GPUVsmParams params = allocateParams;
                params.control[0] = index;
                Use& use = *allocateUses[index];
                use.Uniform->Update(&params, sizeof(params));
                use.Slices->Update(allocateSlices, sizeof(allocateSlices));
                use.DescriptorSet->BindConstantBuffer(BindParams, use.Uniform, 0, sizeof(params));
                use.DescriptorSet->BindStorageBuffer(BindSlices, use.Slices, 0, sizeof(allocateSlices));
                use.DescriptorSet->BindStorageBuffer(BindRequestBits, dispatch.RequestBits, 0,
                                                     ClampBindSize(VirtualShadowMap::RequestBitsBytes()));
                use.DescriptorSet->BindStorageBuffer(BindPageTable, dispatch.PageTable, 0,
                                                     ClampBindSize(VirtualShadowMap::PageTableBytes()));
                use.DescriptorSet->BindStorageBuffer(BindFreeList, dispatch.FreeList, 0,
                                                     ClampBindSize(VirtualShadowMap::FreeListBytes(dispatch.PoolPages)));
                use.DescriptorSet->BindStorageBuffer(BindStats, dispatch.Stats, 0, ClampBindSize(VirtualShadowMap::STATS_BYTES));
                use.DescriptorSet->BindStorageBuffer(BindDirtyList, dispatch.DirtyList, 0,
                                                     ClampBindSize(VirtualShadowMap::DirtyListBytes(dispatch.PoolPages)));
                use.DescriptorSet->Update();
            }

            const uint32_t entryGroups = GroupsFor(VirtualShadowMap::LEVEL_COUNT * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL, GroupSize);
            const uint32_t pageGroups = GroupsFor(dispatch.PoolPages, GroupSize);
            const uint32_t rectGroups = GroupsFor(rectCount * VirtualShadowMap::LEVEL_COUNT, GroupSize);
            const uint32_t requestGroups = GroupsFor(VirtualShadowMap::REQUEST_WORDS, GroupSize);
            // 段階ごとの dispatch の大きさ（0 は記録しない）
            const uint32_t groups[StageCount] = {
                entryGroups,   // StageScroll
                rectGroups,    // StageRects
                entryGroups,   // StageAge
                1u,            // StageEvictPlan
                entryGroups,   // StageEvict
                pageGroups,    // StageFreeReset
                entryGroups,   // StageFreeMark
                pageGroups,    // StageFreeCompact
                requestGroups, // StageAllocate
                entryGroups,   // StageDirtyList
                1u,            // StageFinalize
            };
            commandList->SetPipeline(m_AllocatePipeline);
            for (uint32_t index = 0; index < StageCount; ++index)
            {
                if (groups[index] == 0u)
                {
                    continue;
                }
                commandList->SetDescriptorSet(allocateUses[index]->DescriptorSet, 0);
                commandList->Dispatch(groups[index], 1u, 1u);
                BarrierWrites(commandList, {dispatch.FreeList, dispatch.PageTable, dispatch.Stats, dispatch.DirtyList});
            }
        }

        // ----- 消去 -----
        bool bCleared = false;
        {
            ScopedGpuTimestamp timestamp(commandList, "VsmClear");
            GPUVsmParams params = baseParams;
            clearUse->Uniform->Update(&params, sizeof(params));
            clearUse->DescriptorSet->BindConstantBuffer(BindParams, clearUse->Uniform, 0, sizeof(params));
            clearUse->DescriptorSet->BindStorageBuffer(BindDirtyList, dispatch.DirtyList, 0,
                                                       ClampBindSize(VirtualShadowMap::DirtyListBytes(dispatch.PoolPages)));
            clearUse->DescriptorSet->BindStorageBuffer(BindPool, dispatch.Pool, 0,
                                                       ClampBindSize(VirtualShadowMap::PoolBytes(dispatch.PoolPages)));
            clearUse->DescriptorSet->Update();

            // 一覧は間接 dispatch の引数としてもシェーダーの読み取りとしても読まれる（GenericRead）
            commandList->BufferBarrier(dispatch.DirtyList, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
            commandList->SetPipeline(m_ClearPipeline);
            commandList->SetDescriptorSet(clearUse->DescriptorSet, 0);
            bCleared = commandList->DispatchIndirect(dispatch.DirtyList, 0u);
            BarrierWrites(commandList, {dispatch.Pool});
            commandList->BufferBarrier(dispatch.DirtyList, RHI::ResourceState::GenericRead, RHI::ResourceState::UnorderedAccess);
        }
        if (!bCleared)
        {
            NORVES_LOG_WARNING("VirtualShadowMapPages", "間接 dispatch を記録できないので、VSM のページを消去できない");
        }

        // 次の記録が引き継ぐ入力を残す。キャッシュを使わない・印付けをしなかった記録は、引き継がない
        m_bCacheValid = bCacheWanted;
        if (bCacheWanted)
        {
            m_CachedPageTable = dispatch.PageTable.get();
            m_CachedPool = dispatch.Pool.get();
            m_CachedPoolPages = dispatch.PoolPages;
            m_PreviousClipmap = *dispatch.Clipmap;
        }
        return bCleared;
    }

} // namespace NorvesLib::Core::Rendering
