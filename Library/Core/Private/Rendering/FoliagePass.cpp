#include "Rendering/FoliagePass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FoliageTypes.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        /// Common/FoliageData.glsl の FoliageData と同じ並び（std140）
        struct FoliageUniform
        {
            float view[16];
            float projection[16];
            float previousView[16];
            float previousProjection[16];
            float cameraPosition[4];
            float wind[4];
            float windParams[4];
            float stemColor[4];
            float tipColorA[4];
            float tipColorB[4];
            float frameParams[4];
        };
        static_assert(sizeof(FoliageUniform) == 368u, "Common/FoliageData.glsl の FoliageData と合わせる");

        static_assert(offsetof(FoliageVertex, Normal) == 12);
        static_assert(offsetof(FoliageVertex, Side) == 24);
        static_assert(offsetof(FoliageVertex, Flex) == 36);
        static_assert(offsetof(FoliageInstance, WidthScale) == 16);
        static_assert(offsetof(FoliageInstance, FadeThreshold) == 32);

        /// foliage.vert の kFadeShrinkBand と同じ値（しきい値が残す割合をこの幅だけ下回るまでは、縮みながら描く）
        constexpr float kFadeShrinkBand = 0.08f;

        /// カメラから距離 distance[m] の所で残す割合（foliage.vert の FoliageFadeScale と同じ規則）
        float FadeKeepFraction(const FoliageBatch &batch, float distance)
        {
            if (batch.FadeEndDistance <= batch.FadeStartDistance)
            {
                return 1.0f;
            }
            const float t = (distance - batch.FadeStartDistance) / (batch.FadeEndDistance - batch.FadeStartDistance);
            return 1.0f - std::clamp(t, 0.0f, 1.0f);
        }

        /// 箱が視錐台の外か（どれかの面の外側に箱の全部があれば外。面は内側が正）
        bool IsBoxOutsideFrustum(const Math::ClipSpaceFrustumPlanes &planes, const float (&boundsMin)[3],
                                 const float (&boundsMax)[3])
        {
            for (const Math::Vector4 &plane : planes.Planes)
            {
                // 面の法線の向きに最も進んだ箱の角
                const float x = plane.x >= 0.0f ? boundsMax[0] : boundsMin[0];
                const float y = plane.y >= 0.0f ? boundsMax[1] : boundsMin[1];
                const float z = plane.z >= 0.0f ? boundsMax[2] : boundsMin[2];
                if (plane.x * x + plane.y * y + plane.z * z + plane.w < 0.0f)
                {
                    return true;
                }
            }
            return false;
        }

        /// 点 (x,y,z) から箱までの最短の距離[m]（箱の中なら 0）
        float DistanceToBox(const float (&point)[3], const float (&boundsMin)[3], const float (&boundsMax)[3])
        {
            float squared = 0.0f;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const float outside = std::max({boundsMin[axis] - point[axis], 0.0f, point[axis] - boundsMax[axis]});
                squared += outside * outside;
            }
            return std::sqrt(squared);
        }

        /// 区画の中で描く株の数（FadeThreshold の小さい順に並んだ範囲のうち、しきい値が残す割合 keep を下回る株の数）
        uint32_t CountKeptInstances(const Container::VariableArray<FoliageInstance> &instances, const FoliageCell &cell,
                                    float keep)
        {
            if (keep >= 1.0f)
            {
                return cell.InstanceCount;
            }
            uint32_t low = 0;
            uint32_t high = cell.InstanceCount;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2u;
                if (instances[cell.FirstInstance + middle].FadeThreshold < keep)
                {
                    low = middle + 1u;
                }
                else
                {
                    high = middle;
                }
            }
            return low;
        }

        /// 一覧から外れたバッファを捨てるまでに待つ Execute の回数（飛行中のフレームとビューの数より十分に多い）
        constexpr uint64_t kStaticBufferReleaseDelay = 64;

        /// 書き換えない値を置くバッファ（作るときに一度だけ書く）
        RHI::BufferPtr CreateStaticBuffer(RHI::IDevice *device, const void *data, uint64_t size, RHI::ResourceUsage usage,
                                          const char *name)
        {
            if (!device || !data || size == 0)
            {
                return {};
            }
            RHI::BufferDesc desc(size, usage, true, name);
            RHI::BufferPtr buffer = device->CreateBuffer(desc);
            if (!buffer)
            {
                NORVES_LOG_ERROR("FoliagePass", "FOLIAGE_BUFFER_FAILED name=%s bytes=%llu", name,
                                 static_cast<unsigned long long>(size));
                return {};
            }
            buffer->Update(data, static_cast<size_t>(size));
            return buffer;
        }

        /// 風の時計の折り返し[s]（float の精度を保つ。折り返しの瞬間だけ揺れが飛ぶ）
        constexpr double kWindClockWrapSeconds = 4096.0;

        RHI::DescriptorSetDesc CreateFoliageDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc descriptorSetDesc;
            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            descriptorSetDesc.bindings.push_back(uboBinding);
            return descriptorSetDesc;
        }

        /// GBuffer の 5 枚の名前（gbuffer.frag の出力の順）
        const Identity &ColorTargetName(uint32_t index)
        {
            static const Identity kNames[] = {
                RenderGraphResourceNames::GBufferAlbedo,   RenderGraphResourceNames::GBufferNormal,
                RenderGraphResourceNames::GBufferMaterial, RenderGraphResourceNames::GBufferEmissive,
                RenderGraphResourceNames::GBufferVelocity,
            };
            return kNames[index];
        }

        void CopyColor(const float (&source)[3], float alpha, float (&destination)[4])
        {
            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2];
            destination[3] = alpha;
        }
    } // namespace

    FoliagePass::~FoliagePass()
    {
        Shutdown();
    }

    bool FoliagePass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("FoliagePass", "Device か ShaderManager がありません");
            return false;
        }
        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("foliage.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("foliage.frag", RHI::ShaderStage::Pixel);
        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateFoliageDescriptorSetDesc());
        if (!m_VertexShader || !m_FragmentShader || !m_DescriptorSetLayout)
        {
            NORVES_LOG_ERROR("FoliagePass", "FOLIAGE_INIT_FAILED 草・低木のシェーダーか記述子のレイアウトを作れません");
            m_VertexShader.reset();
            m_FragmentShader.reset();
            m_DescriptorSetLayout.reset();
            m_Device = nullptr;
            return false;
        }
        m_bInitialized = true;
        return true;
    }

    void FoliagePass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_StaticBuffers.clear();
        m_ExecuteCount = 0;
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_DescriptorSetLayout.reset();
        for (RGResourceHandle &handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};
        for (const RHI::ITexture *&target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        for (RHI::Format &format : m_TargetFormats)
        {
            format = RHI::Format::UNKNOWN;
        }
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void FoliagePass::Setup(ViewRenderContext &context)
    {
        (void)context;
    }

    void FoliagePass::Execute(ViewRenderContext &context)
    {
        (void)context;
    }

    void FoliagePass::Declare(RenderGraphBuilder &builder)
    {
        for (RGResourceHandle &handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};

        const ViewRenderContext *context = builder.GetContext();
        if (!context || !context->SnapshotFoliage || context->SnapshotFoliage->IsEmpty() || !context->GetActiveCamera())
        {
            builder.PreserveInsertionOrder();
            return;
        }
        // GBuffer の 5 枚と深度がそろう構成でだけ描く（どれかが無い構成では何も宣言しない）
        RGTextureHandle available;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            if (!builder.TryGetTexture(ColorTargetName(index), available))
            {
                builder.PreserveInsertionOrder();
                return;
            }
        }
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, available))
        {
            builder.PreserveInsertionOrder();
            return;
        }

        // 前のパス（解決・GBuffer の描画）の内容へ重ねる
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RGTextureHandle handle;
            if (builder.TryUseAttachment(ColorTargetName(index), handle, RGAttachmentKind::Color, RGAttachmentMutability::Write,
                                         RHI::AttachmentLoadOp::Load, RHI::AttachmentStoreOp::Store,
                                         RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource))
            {
                m_ColorHandles[index] = handle.ToResourceHandle();
            }
        }
        RGTextureHandle depthHandle;
        if (builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth, depthHandle, RGAttachmentKind::DepthStencil,
                                     RGAttachmentMutability::Write, RHI::AttachmentLoadOp::Load,
                                     RHI::AttachmentStoreOp::Store, RHI::ResourceState::DepthWrite,
                                     RHI::ResourceState::ShaderResource))
        {
            m_DepthHandle = depthHandle.ToResourceHandle();
        }
        builder.PreserveInsertionOrder();
    }

    void FoliagePass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        const FoliageScene *scene = context.SnapshotFoliage;
        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (!scene || scene->IsEmpty() || !activeCamera || !context.CommandList || !context.FrameResources)
        {
            return;
        }
        if (!m_DepthHandle.IsValid())
        {
            return;
        }
        for (const RGResourceHandle &handle : m_ColorHandles)
        {
            if (!handle.IsValid())
            {
                return;
            }
        }
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }

        RHI::TexturePtr colorTargets[kColorTargetCount];
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            colorTargets[index] = resources.GetTexture(m_ColorHandles[index]);
            if (!colorTargets[index])
            {
                return;
            }
        }
        RHI::TexturePtr depthTarget = resources.GetTexture(m_DepthHandle);
        if (!depthTarget || !EnsureTargets(colorTargets, depthTarget))
        {
            return;
        }

        // カメラ（GBufferPass と同じ組み方。前のカメラが無いときは速度を 0 にする）
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
        const CameraProxy *previousCamera = context.GetPreviousCamera();
        const CameraViewConstants previousConstants =
            previousCamera ? CameraViewConstants::BuildForDevice(*previousCamera, context.GetActiveAspectRatio(), context.Device)
                           : cameraConstants;

        FoliageUniform frameTemplate{};
        cameraConstants.CopyShaderView(frameTemplate.view);
        cameraConstants.CopyShaderProjection(frameTemplate.projection);
        previousConstants.CopyShaderView(frameTemplate.previousView);
        previousConstants.CopyShaderProjection(frameTemplate.previousProjection);
        cameraConstants.CopyCameraPosition(frameTemplate.cameraPosition);
        // 距離 1 m あたりの 1 画素の大きさ（縦の画角と描画の高さから）
        const uint32_t renderHeight = context.GetActiveRenderHeight();
        const float halfFovRadians = activeCamera->FieldOfView * 0.5f * 3.14159265f / 180.0f;
        frameTemplate.cameraPosition[3] =
            renderHeight > 0 ? 2.0f * std::tan(halfFovRadians) / static_cast<float>(renderHeight) : 0.0f;

        // 風の向きと時計（決定的な撮影では 1/60 秒の固定刻みで、エポックの始めから数える）
        const FoliageWindParameters &wind = scene->Wind;
        const float directionLength = std::sqrt(wind.DirectionX * wind.DirectionX + wind.DirectionZ * wind.DirectionZ);
        frameTemplate.wind[0] = directionLength > 1.0e-6f ? wind.DirectionX / directionLength : 1.0f;
        frameTemplate.wind[1] = directionLength > 1.0e-6f ? wind.DirectionZ / directionLength : 0.0f;
        const float windTime =
            static_cast<float>(std::fmod(context.TotalTime, kWindClockWrapSeconds)) + wind.TimeOffsetSeconds;
        frameTemplate.wind[2] = windTime;
        frameTemplate.wind[3] = windTime - context.DeltaTime;
        frameTemplate.windParams[0] = wind.GustSpeed;
        frameTemplate.windParams[1] = wind.GustScale;
        frameTemplate.frameParams[0] = previousCamera ? 1.0f : 0.0f;

        // 区画のカリング: 視錐台の面（GBufferPass と同じビュー・投影）とカメラの位置
        const Math::ClipSpaceFrustumPlanes frustumPlanes = Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(
            cameraConstants.ViewProjectionMatrix, Math::ClipSpaceDepthRange::ZeroToOne);
        const float cameraPosition[3] = {frameTemplate.cameraPosition[0], frameTemplate.cameraPosition[1],
                                         frameTemplate.cameraPosition[2]};

        // メッシュとインスタンスは一度だけ置いたバッファを使い、全部の束の定数を先に書いてから記録する
        // （記録済みのコマンドが参照するアップロードのページへ、同じフレームの後から書き込まない）
        ++m_ExecuteCount;
        /// 1 回のインスタンス描画（束の中の連続したインスタンスの範囲）
        struct DrawRange
        {
            uint32_t FirstInstance = 0;
            uint32_t InstanceCount = 0;
        };
        struct PreparedBatch
        {
            RHI::BufferPtr Vertex;
            RHI::BufferPtr Index;
            RHI::BufferPtr Instance;
            RHI::DescriptorSetPtr DescriptorSet;
            uint32_t IndexCount = 0;
            // drawRanges の中のこの束の範囲
            uint32_t FirstRange = 0;
            uint32_t RangeCount = 0;
        };
        FrameResourceLane *frameResources = context.FrameResources;
        Container::VariableArray<PreparedBatch> prepared;
        Container::VariableArray<DrawRange> drawRanges;
        prepared.reserve(scene->Batches.size());
        uint32_t totalCells = 0;
        uint32_t frustumCulledCells = 0;
        for (const FoliageBatch &batch : scene->Batches)
        {
            if (!batch.Mesh || !batch.Instances || batch.Mesh->Vertices.empty() || batch.Mesh->Indices.empty() ||
                batch.Instances->empty())
            {
                continue;
            }
            const FoliageMeshData &mesh = *batch.Mesh;
            const auto &instances = *batch.Instances;

            // 区画ごとに、視錐台の外を外し、残す割合で切った株の数を描く（区画が無ければ全部を 1 回で描く）
            PreparedBatch entry;
            entry.FirstRange = static_cast<uint32_t>(drawRanges.size());
            if (!batch.Cells || batch.Cells->empty())
            {
                drawRanges.push_back(DrawRange{0u, static_cast<uint32_t>(instances.size())});
            }
            else
            {
                for (const FoliageCell &cell : *batch.Cells)
                {
                    if (cell.InstanceCount == 0 ||
                        static_cast<uint64_t>(cell.FirstInstance) + cell.InstanceCount > instances.size())
                    {
                        continue;
                    }
                    ++totalCells;
                    if (IsBoxOutsideFrustum(frustumPlanes, cell.BoundsMin, cell.BoundsMax))
                    {
                        ++frustumCulledCells;
                        continue;
                    }
                    const float keep = FadeKeepFraction(batch, DistanceToBox(cameraPosition, cell.BoundsMin, cell.BoundsMax));
                    const uint32_t kept = CountKeptInstances(instances, cell, keep);
                    if (kept > 0)
                    {
                        drawRanges.push_back(DrawRange{cell.FirstInstance, kept});
                    }
                }
            }
            entry.RangeCount = static_cast<uint32_t>(drawRanges.size()) - entry.FirstRange;
            if (entry.RangeCount == 0)
            {
                continue;
            }

            entry.Instance = AcquireInstanceBuffer(batch.Instances);
            if (!AcquireMeshBuffers(batch.Mesh, entry.Vertex, entry.Index) || !entry.Instance)
            {
                drawRanges.resize(entry.FirstRange);
                continue;
            }

            FoliageUniform uniform = frameTemplate;
            uniform.windParams[2] = batch.WindStrength;
            uniform.windParams[3] = batch.WindSwayFrequency;
            CopyColor(batch.StemColor, batch.Roughness, uniform.stemColor);
            CopyColor(batch.TipColorA, batch.MinPixelWidth, uniform.tipColorA);
            CopyColor(batch.TipColorB, batch.MaxWidenRatio, uniform.tipColorB);
            uniform.frameParams[1] = mesh.Height;
            uniform.frameParams[2] = batch.FadeStartDistance;
            uniform.frameParams[3] = batch.FadeEndDistance;
            const FrameUploadAllocation uniformAllocation = frameResources->AllocateConstants(uniform);
            const FrameDescriptorAllocation descriptorAllocation = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout);
            if (!uniformAllocation.IsValid() || !descriptorAllocation.IsValid())
            {
                drawRanges.resize(entry.FirstRange);
                continue;
            }
            entry.DescriptorSet = descriptorAllocation.Set;
            entry.DescriptorSet->BindConstantBuffer(0, uniformAllocation.Buffer, uniformAllocation.GetOffset32(),
                                                    uniformAllocation.GetSize32());
            entry.DescriptorSet->Update();
            entry.IndexCount = static_cast<uint32_t>(mesh.Indices.size());
            prepared.push_back(entry);
        }
        ReleaseUnusedStaticBuffers();
        if (prepared.empty())
        {
            return;
        }

        RHI::ICommandList *commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(context.GetActiveLocalViewport());
        commandList->SetScissor(context.GetActiveLocalScissor());
        commandList->SetPipeline(m_Pipeline);

        uint32_t drawnBatches = 0;
        uint32_t drawCalls = 0;
        uint64_t drawnInstances = 0;
        for (const PreparedBatch &entry : prepared)
        {
            commandList->SetDescriptorSet(entry.DescriptorSet, 0);
            commandList->SetVertexBuffer(entry.Vertex, 0, 0);
            commandList->SetVertexBuffer(entry.Instance, 0, 1);
            commandList->SetIndexBuffer(entry.Index, 0, RHI::IndexType::Uint32);
            for (uint32_t rangeIndex = entry.FirstRange; rangeIndex < entry.FirstRange + entry.RangeCount; ++rangeIndex)
            {
                const DrawRange &range = drawRanges[rangeIndex];
                commandList->DrawIndexedInstanced(entry.IndexCount, range.InstanceCount, 0, 0, range.FirstInstance);
                ++drawCalls;
                drawnInstances += range.InstanceCount;
            }
            ++drawnBatches;
        }
        commandList->EndRenderPass();

        // 描いた数が変わったときだけ 1 行出す（カメラが動くと区画の数・切った株の数が変わる）
        if (drawnBatches != m_LoggedBatchCount || drawnInstances != m_LoggedInstanceCount)
        {
            m_LoggedBatchCount = drawnBatches;
            m_LoggedInstanceCount = drawnInstances;
            NORVES_LOG_INFO("FoliagePass", "FOLIAGE_DRAW batches=%u draws=%u instances=%llu cells=%u frustum_culled=%u",
                            drawnBatches, drawCalls, static_cast<unsigned long long>(drawnInstances), totalCells,
                            frustumCulledCells);
        }
    }

    bool FoliagePass::AcquireMeshBuffers(const Container::TSharedPtr<const FoliageMeshData> &mesh,
                                         RHI::BufferPtr &outVertex, RHI::BufferPtr &outIndex)
    {
        for (StaticBuffer &entry : m_StaticBuffers)
        {
            if (entry.MeshSource && entry.MeshSource.get() == mesh.get())
            {
                entry.LastUsedExecute = m_ExecuteCount;
                outVertex = entry.VertexBuffer;
                outIndex = entry.IndexBuffer;
                return true;
            }
        }
        StaticBuffer entry;
        entry.MeshSource = mesh;
        entry.VertexBuffer = CreateStaticBuffer(m_Device, mesh->Vertices.data(),
                                                static_cast<uint64_t>(mesh->Vertices.size()) * sizeof(FoliageVertex),
                                                RHI::ResourceUsage::VertexBuffer, "FoliageVB");
        entry.IndexBuffer = CreateStaticBuffer(m_Device, mesh->Indices.data(),
                                               static_cast<uint64_t>(mesh->Indices.size()) * sizeof(uint32_t),
                                               RHI::ResourceUsage::IndexBuffer, "FoliageIB");
        if (!entry.VertexBuffer || !entry.IndexBuffer)
        {
            return false;
        }
        entry.LastUsedExecute = m_ExecuteCount;
        outVertex = entry.VertexBuffer;
        outIndex = entry.IndexBuffer;
        m_StaticBuffers.push_back(entry);
        return true;
    }

    RHI::BufferPtr FoliagePass::AcquireInstanceBuffer(
        const Container::TSharedPtr<const Container::VariableArray<FoliageInstance>> &instances)
    {
        for (StaticBuffer &entry : m_StaticBuffers)
        {
            if (entry.InstanceSource && entry.InstanceSource.get() == instances.get())
            {
                entry.LastUsedExecute = m_ExecuteCount;
                return entry.InstanceBuffer;
            }
        }
        StaticBuffer entry;
        entry.InstanceSource = instances;
        entry.InstanceBuffer = CreateStaticBuffer(m_Device, instances->data(),
                                                  static_cast<uint64_t>(instances->size()) * sizeof(FoliageInstance),
                                                  RHI::ResourceUsage::VertexBuffer, "FoliageInstances");
        if (!entry.InstanceBuffer)
        {
            return {};
        }
        entry.LastUsedExecute = m_ExecuteCount;
        m_StaticBuffers.push_back(entry);
        return entry.InstanceBuffer;
    }

    void FoliagePass::ReleaseUnusedStaticBuffers()
    {
        // 一覧から外れてから kStaticBufferReleaseDelay 回の描画を待つ（飛行中のフレームの記録がまだ参照している）
        for (auto it = m_StaticBuffers.begin(); it != m_StaticBuffers.end();)
        {
            if (m_ExecuteCount - it->LastUsedExecute > kStaticBufferReleaseDelay)
            {
                it = m_StaticBuffers.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool FoliagePass::EnsureTargets(const RHI::TexturePtr (&colorTargets)[kColorTargetCount],
                                    const RHI::TexturePtr &depthTarget)
    {
        // render pass とパイプラインは描き先の形式だけで決まる。framebuffer は描き先のテクスチャが替わったら作り直す
        bool bSameFormats = m_RenderPass && m_Pipeline && m_TargetFormats[kColorTargetCount] == depthTarget->GetFormat();
        for (uint32_t index = 0; index < kColorTargetCount && bSameFormats; ++index)
        {
            bSameFormats = m_TargetFormats[index] == colorTargets[index]->GetFormat();
        }
        bool bSameTargets = bSameFormats && m_Framebuffer && m_FramebufferTargets[kColorTargetCount] == depthTarget.get();
        for (uint32_t index = 0; index < kColorTargetCount && bSameTargets; ++index)
        {
            bSameTargets = m_FramebufferTargets[index] == colorTargets[index].get();
        }
        if (bSameTargets)
        {
            return true;
        }
        m_Framebuffer.reset();
        for (const RHI::ITexture *&target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        if (!bSameFormats && !CreatePipeline(colorTargets, depthTarget))
        {
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        for (const RHI::TexturePtr &target : colorTargets)
        {
            framebufferDesc.colorTargets.push_back(target);
        }
        framebufferDesc.depthStencilTarget = depthTarget;
        framebufferDesc.width = depthTarget->GetWidth();
        framebufferDesc.height = depthTarget->GetHeight();
        m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("FoliagePass", "FOLIAGE_INIT_FAILED framebuffer を作れません");
            return false;
        }
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            m_FramebufferTargets[index] = colorTargets[index].get();
        }
        m_FramebufferTargets[kColorTargetCount] = depthTarget.get();
        return true;
    }

    bool FoliagePass::CreatePipeline(const RHI::TexturePtr (&colorTargets)[kColorTargetCount],
                                     const RHI::TexturePtr &depthTarget)
    {
        m_RenderPass.reset();
        m_Pipeline.reset();
        for (RHI::Format &format : m_TargetFormats)
        {
            format = RHI::Format::UNKNOWN;
        }

        RHI::RenderPassDesc renderPassDesc;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = colorTargets[index]->GetFormat();
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(colorAttachment);
        }
        renderPassDesc.hasDepthStencil = true;
        renderPassDesc.depthStencilAttachment.format = depthTarget->GetFormat();
        renderPassDesc.depthStencilAttachment.isDepthStencil = true;
        renderPassDesc.depthStencilAttachment.clear = false;
        renderPassDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        renderPassDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        renderPassDesc.depthStencilAttachment.initialState = RHI::ResourceState::DepthWrite;
        renderPassDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("FoliagePass", "FOLIAGE_INIT_FAILED render pass を作れません");
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_VertexShader;
        pipelineDesc.pixelShader = m_FragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(FoliageVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);
        RHI::VertexBindingDesc instanceBinding;
        instanceBinding.binding = 1;
        instanceBinding.stride = sizeof(FoliageInstance);
        instanceBinding.inputRate = RHI::VertexInputRate::Instance;
        pipelineDesc.vertexBindings.push_back(instanceBinding);

        struct AttributeLayout
        {
            uint32_t Location;
            uint32_t Binding;
            RHI::Format Format;
            uint32_t Offset;
        };
        const AttributeLayout attributes[] = {
            {0, 0, RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(FoliageVertex, Position))},
            {1, 0, RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(FoliageVertex, Normal))},
            {2, 0, RHI::Format::R32G32B32_FLOAT, static_cast<uint32_t>(offsetof(FoliageVertex, Side))},
            {3, 0, RHI::Format::R32G32B32A32_FLOAT, static_cast<uint32_t>(offsetof(FoliageVertex, Flex))},
            {4, 1, RHI::Format::R32G32B32A32_FLOAT, static_cast<uint32_t>(offsetof(FoliageInstance, Position))},
            {5, 1, RHI::Format::R32G32B32A32_FLOAT, static_cast<uint32_t>(offsetof(FoliageInstance, WidthScale))},
            {6, 1, RHI::Format::R32_FLOAT, static_cast<uint32_t>(offsetof(FoliageInstance, FadeThreshold))},
        };
        for (const AttributeLayout &layout : attributes)
        {
            RHI::VertexAttributeDesc attribute;
            attribute.location = layout.Location;
            attribute.binding = layout.Binding;
            attribute.format = layout.Format;
            attribute.offset = layout.Offset;
            pipelineDesc.vertexAttributes.push_back(attribute);
        }

        // 葉の帯は両面を描く（裏の法線はフラグメントが面で鏡に映す）
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        // ビジビリティバッファのラスタと同じ深度の比較
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::LessOrEqual;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
        }
        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(CreateFoliageDescriptorSetDesc());
        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("FoliagePass", "FOLIAGE_INIT_FAILED パイプラインを作れません");
            m_RenderPass.reset();
            return false;
        }

        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            m_TargetFormats[index] = colorTargets[index]->GetFormat();
        }
        m_TargetFormats[kColorTargetCount] = depthTarget->GetFormat();
        return true;
    }

} // namespace NorvesLib::Core::Rendering
