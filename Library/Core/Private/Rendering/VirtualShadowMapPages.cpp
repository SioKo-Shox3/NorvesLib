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
            uint32_t screen[4];  // x = 幅、y = 高さ、z = スライスの数、w = 印付けが選ぶ太陽の段の、スライスの表での先頭の番号
            float tuning[4];     // x = PCF の核の半径のうち texel に比例する分（texel）、y = 影の範囲の奥の端（前方への距離 m）、z = 探索・PCF の半径の上限（m）
            uint32_t control[4]; // x = 段階、y = 物理ページの数、z = 間接 dispatch の x の上限、w = 印付けが段を選ぶ太陽のスライスの数（クリップマップの段の数）
            float thresholds[VirtualShadowMapMaxLevels];
            float view[4];       // x, y, z = カメラの前方（単位ベクトル）、w = 影の範囲の手前の端（前方への距離 m）
            // ここから下は vsm_allocate.comp だけが読む（vsm_mark.comp・vsm_clear.comp の VsmParams はここまでの前半と同じ並び）
            uint32_t cache[4];                                  // x = 印（CacheFlag*）、y = 持ち越すフレーム数、z = 無効化の矩形の数、w = 無効化の球の数
            float rects[VirtualShadowMap::MAX_INVALIDATION_RECTS][4]; // 無効化の矩形（ライト空間。x, y = 最小、z, w = 最大）
            float spheres[VirtualShadowMap::MAX_INVALIDATION_RECTS][4]; // 無効化の球（ワールド。xyz = 中心、w = 半径）
        };
        // ページの一辺・texel・範囲の原点（前フレームの分を含む）はスライスの表（GPUVsmSlice。binding BindSlices）にある
        static_assert(sizeof(GPUVsmParams) == 224 + 16 + 16 + VirtualShadowMap::MAX_INVALIDATION_RECTS * 16 * 2,
                      "vsm_*.comp の VsmParams と同じ大きさにすること");

        // vsm_mark.comp の VsmPointParams（std140）と同じ並び。点光源の印付けの入力
        struct GPUVsmPointParams
        {
            uint32_t header[4]; // x = 灯の数、y = 点光源のスライスの先頭の番号、z = 解像度の段の数、w = 面の段 0 の解像度（texel）
            float tuning[4];    // x = カメラからの距離 1 m あたりの画素の大きさ（m）、y = 目標 texel の係数（2^bias）、z = 核の半径の texel の分、w = 核の半径に足す長さ（m）
            float plane[4];     // x = 面の近い面（m）
            float lights[PointShadowMaxLights][4]; // xyz = 灯の位置、w = Range
        };
        static_assert(sizeof(GPUVsmPointParams) == 48 + PointShadowMaxLights * 16, "vsm_mark.comp の VsmPointParams と同じ大きさにすること");
        static_assert(PointShadowMaxLights == 4u, "vsm_mark.comp の MAX_POINT_LIGHTS と合わせること");

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
        constexpr uint32_t CacheFlagInvalidatePoint = 4u;
        // スライスの表の extra[3]。立っているスライスは、前フレームのページの内容を引き継げず、全ページに dirty を付ける
        constexpr int32_t SliceFlagInvalidate = 1;
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
        // vsm_mark.comp の点光源の入力
        constexpr uint32_t BindPointParams = 9;

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
            desc.bindings.push_back(MakeBinding(BindPointParams, RHI::ResourceBindType::ConstantBuffer));
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

        // 太陽と点光源に共通の印付けの入力（深度から位置を戻す行列・カメラの位置・画面の大きさ）を書く
        void FillCommonMarkParams(const VirtualShadowMapPagesDispatch& dispatch, uint32_t width, uint32_t height, GPUVsmParams& params)
        {
            for (uint32_t element = 0; element < 16u; ++element)
            {
                params.invViewProjection[element] = dispatch.InverseViewProjection[element];
            }
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                params.cameraPosition[axis] = dispatch.CameraPosition[axis];
            }
            params.screen[0] = width;
            params.screen[1] = height;
        }

        // 太陽の印付けに使える入力か。使えるなら params へクリップマップ由来の値を書く（太陽の段は先頭の LevelCount 件のスライス）。
        // 使えないときは太陽の段の数（control[3]）を 0 のままにする（シェーダーは太陽の印付けを飛ばす）
        bool FillMarkParams(const VirtualShadowMapPagesDispatch& dispatch, uint32_t sliceCount, uint32_t width, uint32_t height, GPUVsmParams& params)
        {
            const VirtualShadowMapClipmap* clipmap = dispatch.Clipmap;
            if (clipmap == nullptr || !clipmap->bEnabled || clipmap->LevelCount == 0u ||
                clipmap->LevelCount > VirtualShadowMap::LEVEL_COUNT || dispatch.MarkFirstSlice + clipmap->LevelCount > sliceCount ||
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
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                params.view[axis] = dispatch.CameraForward[axis] / forwardLength;
            }
            params.view[3] = shadowNear;
            params.lightRight[0] = clipmap->LightRight.x;
            params.lightRight[1] = clipmap->LightRight.y;
            params.lightRight[2] = clipmap->LightRight.z;
            params.lightUp[0] = clipmap->LightUp.x;
            params.lightUp[1] = clipmap->LightUp.y;
            params.lightUp[2] = clipmap->LightUp.z;
            params.control[3] = clipmap->LevelCount;
            params.screen[3] = dispatch.MarkFirstSlice;
            params.tuning[0] = dispatch.PcfRadiusTexels;
            params.tuning[1] = shadowFar;
            params.tuning[2] = dispatch.MaxFilterRadiusMeters;
            return true;
        }

        // 点光源の印付けに使える入力か。使えるなら params を書く（使えないときは灯の数 0 のまま）。
        // 点光源のスライスはページの表の 128 × 128 の枠に収まり（段 0 の一辺のページが 128 以下）、スライスの表の sliceCount 件に収まること
        bool FillPointParams(const VirtualShadowMapPagesDispatch& dispatch, uint32_t sliceCount, uint32_t height, GPUVsmPointParams& params)
        {
            const VirtualShadowMapPointLights* lights = dispatch.PointLights;
            if (lights == nullptr || lights->LightCount == 0u || lights->LightCount > PointShadowMaxLights ||
                !IsValidVirtualShadowMapPointSettings(lights->Settings) ||
                lights->Settings.PageResolution != VirtualShadowMap::PAGE_RESOLUTION ||
                lights->SlicesPerLight != PointShadowFaceCount * lights->Settings.MipCount ||
                VirtualShadowMapPointPagesPerAxis(lights->Settings, 0u) > VirtualShadowMap::TABLE_DIMENSION ||
                lights->FirstSlice + lights->SliceCount() > sliceCount)
            {
                return false;
            }
            const float pixelPerMeter = VirtualShadowMapScreenPixelMeters(1.0f, dispatch.FovYDegrees, static_cast<float>(height));
            if (!(pixelPerMeter > 0.0f) || !std::isfinite(pixelPerMeter) || !std::isfinite(dispatch.PointPcfRadiusTexels) ||
                !(dispatch.PointPcfRadiusTexels >= 0.0f) || !std::isfinite(dispatch.PointFilterRadiusMeters) ||
                !(dispatch.PointFilterRadiusMeters >= 0.0f))
            {
                return false;
            }
            params.header[0] = lights->LightCount;
            params.header[1] = lights->FirstSlice;
            params.header[2] = lights->Settings.MipCount;
            params.header[3] = lights->Settings.FaceResolution;
            params.tuning[0] = pixelPerMeter;
            params.tuning[1] = std::exp2(lights->Settings.BiasLevels);
            params.tuning[2] = dispatch.PointPcfRadiusTexels;
            params.tuning[3] = dispatch.PointFilterRadiusMeters;
            params.plane[0] = PointShadowNearPlane;
            for (uint32_t light = 0; light < lights->LightCount; ++light)
            {
                params.lights[light][0] = lights->Position[light].x;
                params.lights[light][1] = lights->Position[light].y;
                params.lights[light][2] = lights->Position[light].z;
                params.lights[light][3] = lights->Range[light];
            }
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
        m_RemapScratch.reset();
        m_Device = nullptr;
        m_bMarked = false;
        m_bCacheValid = false;
        m_bCacheContinued = false;
        m_bInvalidatedAll = false;
        m_PointInvalidatedSlices = 0;
        m_bPreviousSun = false;
        m_bPreviousPoint = false;
        m_CachedPageTable = nullptr;
        m_CachedPool = nullptr;
        m_CachedPoolPages = 0;
        m_CachedSliceCount = 0;
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
        if (!use.PointUniform)
        {
            use.PointUniform = m_Device->CreateBuffer(
                RHI::BufferDesc(sizeof(GPUVsmPointParams), RHI::ResourceUsage::ConstantBuffer, true, "VsmPointParams"));
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
        return use.Uniform && use.PointUniform && use.Slices && use.DescriptorSet;
    }

    void VirtualShadowMapPages::RemapPointPageTable(RHI::ICommandList* commandList,
                                                    const VirtualShadowMapPagesDispatch& dispatch,
                                                    const VirtualShadowMapPointRemap& remap)
    {
        const VirtualShadowMapPointLights& lights = *dispatch.PointLights;
        const uint32_t blockCount = std::max(m_PreviousPointLights.LightCount, lights.LightCount);
        const uint64_t blockBytes = VirtualShadowMap::PageTableBytes(lights.SlicesPerLight);
        const uint64_t firstByte = VirtualShadowMap::PageTableBytes(lights.FirstSlice);
        const uint64_t regionBytes = blockBytes * blockCount;
        const uint64_t scratchBytes = blockBytes * m_PreviousPointLights.LightCount;
        if (!m_RemapScratch || m_RemapScratch->GetSize() < scratchBytes)
        {
            m_RemapScratch = m_Device->CreateBuffer(RHI::BufferDesc(
                blockBytes * PointShadowMaxLights, RHI::ResourceUsage::TransferSrc | RHI::ResourceUsage::TransferDst, false, "VsmPointRemapScratch"));
        }
        if (!m_RemapScratch || blockBytes == 0u || regionBytes == 0u)
        {
            return;
        }
        // 前フレームの領域をすべて退避してから、今フレームの番号の領域へ書き戻す（入れ替えでも、書く前に読み終える）。
        // 退避先は直前のフレームの書き戻しが読み終わってから使う（CopySource → CopyDest の遷移が実行順を保証する）
        commandList->BufferBarrier(dispatch.PageTable, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopySource, firstByte, regionBytes);
        commandList->BufferBarrier(m_RemapScratch, RHI::ResourceState::CopySource, RHI::ResourceState::CopyDest, 0u, scratchBytes);
        commandList->CopyBuffer(dispatch.PageTable, m_RemapScratch, scratchBytes, firstByte, 0u);
        commandList->BufferBarrier(m_RemapScratch, RHI::ResourceState::CopyDest, RHI::ResourceState::CopySource, 0u, scratchBytes);
        commandList->BufferBarrier(dispatch.PageTable, RHI::ResourceState::CopySource, RHI::ResourceState::CopyDest, firstByte, regionBytes);
        for (uint32_t block = 0; block < blockCount; ++block)
        {
            const uint32_t source = remap.Source[block];
            if (source == block)
            {
                continue;
            }
            if (source == VirtualShadowMapPointNoSource)
            {
                commandList->FillBuffer(dispatch.PageTable, firstByte + blockBytes * block, blockBytes, 0u);
            }
            else
            {
                commandList->CopyBuffer(m_RemapScratch, dispatch.PageTable, blockBytes, blockBytes * source, firstByte + blockBytes * block);
            }
        }
        commandList->BufferBarrier(dispatch.PageTable, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess, firstByte, regionBytes);
    }

    bool VirtualShadowMapPages::Record(RHI::ICommandList* commandList, const VirtualShadowMapPagesDispatch& dispatch)
    {
        m_bMarked = false;
        m_bCacheContinued = false;
        m_bInvalidatedAll = false;
        // 記録の途中で戻ったとき、古い状態を次の記録が引き継がないよう先に無効にする（最後で入力を残して有効に戻す）
        const bool bCacheWasValid = m_bCacheValid;
        m_bCacheValid = false;
        const uint32_t sliceCount = dispatch.SliceCount != 0u ? dispatch.SliceCount : VirtualShadowMap::LEVEL_COUNT;
        if (!IsReady() || !commandList || dispatch.PoolPages == 0 || dispatch.PoolPages > VirtualShadowMap::MAX_POOL_PAGES ||
            sliceCount > VirtualShadowMap::MAX_SLICES || !dispatch.Pool || !dispatch.PageTable || !dispatch.RequestBits ||
            !dispatch.FreeList || !dispatch.Stats || !dispatch.DirtyList)
        {
            return false;
        }
        if (dispatch.Pool->GetSize() < VirtualShadowMap::PoolBytes(dispatch.PoolPages) ||
            dispatch.PageTable->GetSize() < VirtualShadowMap::PageTableBytes(sliceCount) ||
            dispatch.RequestBits->GetSize() < VirtualShadowMap::RequestBitsBytes(sliceCount) ||
            dispatch.FreeList->GetSize() < VirtualShadowMap::FreeListBytes(dispatch.PoolPages) ||
            dispatch.Stats->GetSize() < VirtualShadowMap::STATS_BYTES ||
            dispatch.DirtyList->GetSize() < VirtualShadowMap::DirtyListBytes(dispatch.PoolPages))
        {
            return false;
        }

        GPUVsmParams baseParams = {};
        baseParams.screen[2] = sliceCount;
        baseParams.control[1] = dispatch.PoolPages;
        baseParams.control[2] = VirtualShadowMap::GROUP_COUNT_X_LIMIT;

        const uint32_t width = dispatch.Depth ? dispatch.Depth->GetWidth() : 0u;
        const uint32_t height = dispatch.Depth ? dispatch.Depth->GetHeight() : 0u;
        GPUVsmParams markParams = baseParams;
        // 太陽と点光源は別々に使えるか決める（夜は太陽のクリップマップが無効で、点光源だけが印付けをする）
        const bool bHasDepth = dispatch.Depth && width != 0u && height != 0u;
        if (bHasDepth)
        {
            FillCommonMarkParams(dispatch, width, height, markParams);
        }
        const bool bSunMark = bHasDepth && FillMarkParams(dispatch, sliceCount, width, height, markParams);
        GPUVsmPointParams pointParams = {};
        const bool bPointMark = bHasDepth && FillPointParams(dispatch, sliceCount, height, pointParams);
        const bool bMark = bSunMark || bPointMark;
        // スライスの表。外から渡されなければクリップマップから作る（先頭 LevelCount 件が太陽の段）。
        // 印付けに使えないフレームは使わない（印付けを記録しない）
        GPUVsmSlice markSliceStorage[VirtualShadowMapMaxSlices];
        const GPUVsmSlice* markSlices = dispatch.Slices;
        if (bMark && markSlices == nullptr)
        {
            BuildVirtualShadowMapSlices(bSunMark ? dispatch.Clipmap : nullptr, nullptr, sliceCount, markSliceStorage);
            if (bPointMark)
            {
                BuildVirtualShadowMapPointSlices(*dispatch.PointLights, markSliceStorage);
            }
            markSlices = markSliceStorage;
        }
        const uint32_t sliceBytes = sliceCount * static_cast<uint32_t>(sizeof(GPUVsmSlice));

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
            ZeroFill(commandList, dispatch.RequestBits, VirtualShadowMap::RequestBitsBytes(sliceCount));
            if (bMark)
            {
                markUse->Uniform->Update(&markParams, sizeof(markParams));
                markUse->Slices->Update(markSlices, sliceBytes);
                markUse->PointUniform->Update(&pointParams, sizeof(pointParams));
                markUse->DescriptorSet->BindConstantBuffer(BindParams, markUse->Uniform, 0, sizeof(markParams));
                markUse->DescriptorSet->BindConstantBuffer(BindPointParams, markUse->PointUniform, 0, sizeof(pointParams));
                markUse->DescriptorSet->BindStorageBuffer(BindSlices, markUse->Slices, 0, sliceBytes);
                markUse->DescriptorSet->BindTexture(BindDepth, dispatch.Depth);
                markUse->DescriptorSet->BindSampler(BindDepth, m_PointSampler);
                markUse->DescriptorSet->BindStorageBuffer(BindRequestBits, dispatch.RequestBits, 0,
                                                          ClampBindSize(VirtualShadowMap::RequestBitsBytes(sliceCount)));
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
        // 太陽の段（正射影）と点光源の面（透視）は、それぞれ印付けをしたフレームだけが前フレームから引き継ぐ。
        // 前フレームと太陽の印付けの有無が変わった（昼夜の切り替え）フレームは、太陽の段の表の欄が指す範囲の意味が分からないので全部を割り当て直す。
        // 点光源の灯の有無・並びの変化は、灯ごとのスライスの印（下の pointInvalid）で無効にする
        const bool bCacheWanted = dispatch.bCacheEnabled && (bSunMark || bPointMark);
        const bool bContinue = bCacheWanted && bCacheWasValid && m_CachedPageTable == dispatch.PageTable.get() &&
                               m_CachedPool == dispatch.Pool.get() && m_CachedPoolPages == dispatch.PoolPages &&
                               m_CachedSliceCount == sliceCount && m_bPreviousSun == bSunMark &&
                               (!bSunMark || IsSameLevelLayout(m_PreviousClipmap, *dispatch.Clipmap));
        bool bInvalidateSun = false;
        bool bInvalidatePoint = false;
        if (bContinue)
        {
            const bool bTooMany = dispatch.InvalidationRectCount > VirtualShadowMap::MAX_INVALIDATION_RECTS ||
                                  dispatch.InvalidationSphereCount > VirtualShadowMap::MAX_INVALIDATION_RECTS;
            bInvalidateSun = dispatch.bInvalidateAll || bTooMany;
            bInvalidatePoint = dispatch.bInvalidateAll || bTooMany;
            if (bSunMark)
            {
                const VirtualShadowMapClipmap& current = *dispatch.Clipmap;
                bInvalidateSun = bInvalidateSun || !IsSameDirection(m_PreviousClipmap.Direction, current.Direction) ||
                                 !IsSameDirection(m_PreviousClipmap.LightRight, current.LightRight) ||
                                 !IsSameDirection(m_PreviousClipmap.LightUp, current.LightUp) ||
                                 m_PreviousClipmap.DepthCenter != current.DepthCenter;
            }
        }
        const bool bInvalidateAll = bInvalidateSun || bInvalidatePoint;
        m_bCacheContinued = bContinue;
        m_bInvalidatedAll = bInvalidateAll;

        // 灯の識別子・位置・Range・並びが前フレームの同じ番号の灯と違うスライスは、全ページを無効にする（スライスの印）
        bool pointInvalid[VirtualShadowMapMaxSlices] = {};
        VirtualShadowMapPointRemap pointRemap;
        m_PointInvalidatedSlices = 0;
        if (bContinue)
        {
            m_PointInvalidatedSlices = BuildVirtualShadowMapPointSliceInvalidation(
                m_bPreviousPoint ? &m_PreviousPointLights : nullptr, bPointMark ? dispatch.PointLights : nullptr, pointInvalid, &pointRemap);
        }

        // ----- 割り当て -----
        {
            ScopedGpuTimestamp timestamp(commandList, "VsmAllocate");
            if (!bContinue)
            {
                ZeroFill(commandList, dispatch.PageTable, VirtualShadowMap::PageTableBytes(sliceCount));
            }
            ZeroFill(commandList, dispatch.Stats, VirtualShadowMap::STATS_BYTES);
            if (bContinue && pointRemap.bMoves)
            {
                RemapPointPageTable(commandList, dispatch, pointRemap);
            }

            GPUVsmParams allocateParams = bMark ? markParams : baseParams;
            allocateParams.cache[0] = (bCacheWanted ? CacheFlagEnabled : 0u) | (bInvalidateSun ? CacheFlagInvalidateAll : 0u) |
                                      (bInvalidatePoint ? CacheFlagInvalidatePoint : 0u);
            allocateParams.cache[1] = VirtualShadowMap::CACHE_CARRY_FRAMES;
            // 矩形は太陽の段、球は点光源の面のページを無効にする。全ページを無効にするときは、その種類の矩形・球は要らない
            const uint32_t rectCount = bContinue && bSunMark && !bInvalidateSun ? dispatch.InvalidationRectCount : 0u;
            const uint32_t sphereCount = bContinue && bPointMark && !bInvalidatePoint ? dispatch.InvalidationSphereCount : 0u;
            allocateParams.cache[2] = rectCount;
            allocateParams.cache[3] = sphereCount;
            // 引き継がないときは、範囲が動いていない（前フレームも今フレームと同じ）ものとして扱う（前の原点が null なら今の原点と同じ）。
            // 外から渡されたスライスの表は、そのまま使う（前フレームの原点も呼び出し側が入れる）
            GPUVsmSlice allocateSliceStorage[VirtualShadowMapMaxSlices];
            const GPUVsmSlice* allocateSlices = dispatch.Slices;
            if (allocateSlices == nullptr)
            {
                BuildVirtualShadowMapSlices(bSunMark ? dispatch.Clipmap : nullptr,
                                            bContinue ? &m_PreviousClipmap : nullptr,
                                            sliceCount,
                                            allocateSliceStorage);
                if (bPointMark)
                {
                    BuildVirtualShadowMapPointSlices(*dispatch.PointLights, allocateSliceStorage);
                }
                // 引き継げないスライス（灯が動いた・入れ替わった・無くなった）の印。点光源の領域のスライスなので、使わない灯の分も透視の種類にして数える
                for (uint32_t index = 0; index < sliceCount; ++index)
                {
                    if (pointInvalid[index])
                    {
                        allocateSliceStorage[index].extra[2] = VirtualShadowMapSliceProjectionPerspective;
                        allocateSliceStorage[index].extra[3] = SliceFlagInvalidate;
                    }
                }
                allocateSlices = allocateSliceStorage;
            }
            for (uint32_t index = 0; index < sphereCount; ++index)
            {
                const float* sphere = dispatch.InvalidationSpheres + static_cast<size_t>(index) * 4u;
                allocateParams.spheres[index][0] = sphere[0];
                allocateParams.spheres[index][1] = sphere[1];
                allocateParams.spheres[index][2] = sphere[2];
                allocateParams.spheres[index][3] = sphere[3] + InvalidationMarginMeters;
            }
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
                use.Slices->Update(allocateSlices, sliceBytes);
                use.DescriptorSet->BindConstantBuffer(BindParams, use.Uniform, 0, sizeof(params));
                use.DescriptorSet->BindStorageBuffer(BindSlices, use.Slices, 0, sliceBytes);
                use.DescriptorSet->BindStorageBuffer(BindRequestBits, dispatch.RequestBits, 0,
                                                     ClampBindSize(VirtualShadowMap::RequestBitsBytes(sliceCount)));
                use.DescriptorSet->BindStorageBuffer(BindPageTable, dispatch.PageTable, 0,
                                                     ClampBindSize(VirtualShadowMap::PageTableBytes(sliceCount)));
                use.DescriptorSet->BindStorageBuffer(BindFreeList, dispatch.FreeList, 0,
                                                     ClampBindSize(VirtualShadowMap::FreeListBytes(dispatch.PoolPages)));
                use.DescriptorSet->BindStorageBuffer(BindStats, dispatch.Stats, 0, ClampBindSize(VirtualShadowMap::STATS_BYTES));
                use.DescriptorSet->BindStorageBuffer(BindDirtyList, dispatch.DirtyList, 0,
                                                     ClampBindSize(VirtualShadowMap::DirtyListBytes(dispatch.PoolPages)));
                use.DescriptorSet->Update();
            }

            const uint32_t entryGroups = GroupsFor(sliceCount * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL, GroupSize);
            const uint32_t pageGroups = GroupsFor(dispatch.PoolPages, GroupSize);
            const uint32_t rectGroups = GroupsFor(std::max(rectCount, sphereCount) * sliceCount, GroupSize);
            const uint32_t requestGroups = GroupsFor(VirtualShadowMap::RequestWords(sliceCount), GroupSize);
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
            m_CachedSliceCount = sliceCount;
            m_bPreviousSun = bSunMark;
            if (bSunMark)
            {
                m_PreviousClipmap = *dispatch.Clipmap;
            }
            m_bPreviousPoint = bPointMark;
            if (bPointMark)
            {
                m_PreviousPointLights = *dispatch.PointLights;
            }
        }
        return bCleared;
    }

} // namespace NorvesLib::Core::Rendering
