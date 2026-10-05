#include "Rendering/SkinningComputePass.h"

#include "Logging/LogMacros.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"

#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // シェーダーの SkinningParams（uvec4 counts）と同じ並び
        constexpr uint32_t ParamsBytes = 16;
        constexpr uint64_t OutputVertexBytes = sizeof(SkinnedOutputVertex);

        RHI::DescriptorSetDesc MakeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::ConstantBuffer,   // 0 パラメータ
                RHI::ResourceBindType::StructuredBuffer, // 1 入力の頂点
                RHI::ResourceBindType::StructuredBuffer, // 2 今のフレームの変換とパレット
                RHI::ResourceBindType::StructuredBuffer, // 3 直前のフレームの変換とパレット
                RHI::ResourceBindType::RWBuffer,         // 4 今のフレームの頂点
                RHI::ResourceBindType::RWBuffer,         // 5 直前のフレームの頂点
            };
            for (uint32_t bindingIndex = 0; bindingIndex < 6u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        uint32_t ClampBindSize(uint64_t size)
        {
            return size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                               : static_cast<uint32_t>(size);
        }

        // 書いた今・前の頂点のバッファを、宣言した最終の状態（GenericRead）へ遷移させる。RenderGraph は終わった状態を
        // 信じて、後のパスの読み取りの前にバリアを足さないので、書き込んだこのパスが dispatch の後に出す。
        void RecordFinalBarriers(RHI::ICommandList* commandList,
                                 const RHI::BufferPtr& currentVertices,
                                 const RHI::BufferPtr& previousVertices)
        {
            if (!commandList)
            {
                return;
            }
            for (const RHI::BufferPtr& buffer : {currentVertices, previousVertices})
            {
                if (buffer)
                {
                    commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess,
                                               RHI::ResourceState::GenericRead, 0u, buffer->GetSize());
                }
            }
        }
    } // namespace

    // ========================================
    // SkinningCompute
    // ========================================

    SkinningCompute::SkinningCompute() = default;

    SkinningCompute::~SkinningCompute()
    {
        Shutdown();
    }

    bool SkinningCompute::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }

        m_Shader = shaderManager->LoadShader("skinning_compute.comp", RHI::ShaderStage::Compute);
        if (!m_Shader)
        {
            NORVES_LOG_WARNING("SkinningCompute", "スキニングの計算シェーダーの読み込みに失敗");
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_Shader;
        pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
        m_Pipeline = device->CreateComputePipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("SkinningCompute", "スキニングの計算パイプラインの作成に失敗");
            Shutdown();
            return false;
        }

        m_Device = device;
        return true;
    }

    void SkinningCompute::Shutdown()
    {
        m_Uses.Clear();
        m_Pipeline.reset();
        m_Shader.reset();
        m_Device = nullptr;
    }

    void SkinningCompute::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool SkinningCompute::Record(RHI::ICommandList* commandList, const SkinningComputeDispatch& dispatch)
    {
        if (!m_Device || !m_Pipeline || !commandList || dispatch.VertexCount == 0 || !dispatch.SkinVertices ||
            !dispatch.Palette || !dispatch.PreviousPalette || !dispatch.CurrentVertices || !dispatch.PreviousVertices)
        {
            return false;
        }

        const uint64_t outputEnd =
            (static_cast<uint64_t>(dispatch.OutputVertexBase) + dispatch.VertexCount) * OutputVertexBytes;
        if (dispatch.CurrentVertices->GetSize() < outputEnd || dispatch.PreviousVertices->GetSize() < outputEnd ||
            dispatch.SkinVertices->GetSize() < static_cast<uint64_t>(dispatch.VertexCount) * sizeof(SkinnedMeshVertex))
        {
            return false;
        }

        // 資源の作成に失敗した Use は、次の Record が作り直す（位置は進める）
        Use& use = m_Uses.Acquire();
        if (!use.Uniform)
        {
            use.Uniform = m_Device->CreateBuffer(
                RHI::BufferDesc(ParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "SkinningComputeParams"));
        }
        if (!use.DescriptorSet)
        {
            use.DescriptorSet = m_Device->CreateDescriptorSet(MakeDescriptorSetDesc());
        }
        if (!use.Uniform || !use.DescriptorSet)
        {
            return false;
        }

        const uint32_t params[4] = {dispatch.VertexCount, dispatch.OutputVertexBase, 0u, 0u};
        use.Uniform->Update(params, ParamsBytes);

        use.DescriptorSet->BindConstantBuffer(0, use.Uniform, 0, ParamsBytes);
        use.DescriptorSet->BindStorageBuffer(1, dispatch.SkinVertices, 0, ClampBindSize(dispatch.SkinVertices->GetSize()));
        use.DescriptorSet->BindStorageBuffer(2, dispatch.Palette, 0, ClampBindSize(dispatch.Palette->GetSize()));
        use.DescriptorSet->BindStorageBuffer(3, dispatch.PreviousPalette, 0,
                                             ClampBindSize(dispatch.PreviousPalette->GetSize()));
        use.DescriptorSet->BindStorageBuffer(4, dispatch.CurrentVertices, 0,
                                             ClampBindSize(dispatch.CurrentVertices->GetSize()));
        use.DescriptorSet->BindStorageBuffer(5, dispatch.PreviousVertices, 0,
                                             ClampBindSize(dispatch.PreviousVertices->GetSize()));
        use.DescriptorSet->Update();

        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(use.DescriptorSet, 0);
        commandList->Dispatch((dispatch.VertexCount + ThreadsPerGroup - 1u) / ThreadsPerGroup, 1u, 1u);
        return true;
    }

    // ========================================
    // SkinningComputePass
    // ========================================

    SkinningComputePass::SkinningComputePass()
    {
        // 今の GBuffer の経路は頂点シェーダーのスキニングのままなので、ビジビリティバッファを使うときだけ有効にする。
        m_bEnabled = false;
    }

    SkinningComputePass::~SkinningComputePass()
    {
        Shutdown();
    }

    bool SkinningComputePass::Initialize(ViewRenderContext& context)
    {
        // 計算パイプラインを作れなくても描画全体は止めない（Declare が何も宣言せず、このパスは何もしない）。
        if (!m_Compute.Initialize(context.Device, context.ShaderMgr))
        {
            NORVES_LOG_WARNING("SkinningComputePass", "計算スキニングを使えないので、このパスは何もしない");
        }
        m_bInitialized = true;
        return true;
    }

    void SkinningComputePass::Shutdown()
    {
        m_Compute.Shutdown();
        m_CurrentHandle = {};
        m_PreviousHandle = {};
        m_Plan.clear();
        m_Instances.clear();
        m_bInitialized = false;
    }

    void SkinningComputePass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void SkinningComputePass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void SkinningComputePass::Declare(RenderGraphBuilder& builder)
    {
        m_CurrentHandle = {};
        m_PreviousHandle = {};
        m_Plan.clear();

        const ViewRenderContext* context = builder.GetContext();
        if (!context || !m_Compute.IsReady() || !context->SnapshotSkinnedMeshFrameLeases)
        {
            return;
        }

        // 不透明描画のうちスキニングの 1 描画（非インスタンス）を集め、頂点を詰めて出力の範囲を割り当てる。
        const DrawCommandView commands = context->GetActiveOpaqueCommands();
        uint64_t totalVertices = 0;
        for (uint32_t commandIndex = 0; commandIndex < commands.Count; ++commandIndex)
        {
            const DrawCommand& command = commands.Data[commandIndex];
            if (command.Draw.PayloadKind != DrawPayloadKind::Skinned || command.Draw.bInstanced ||
                command.Draw.InstanceCount != 1 ||
                command.Skinned.FrameLeaseIndex >= context->SnapshotSkinnedMeshFrameLeases->size())
            {
                continue;
            }
            const auto& frameLease = (*context->SnapshotSkinnedMeshFrameLeases)[command.Skinned.FrameLeaseIndex];
            if (!frameLease || !frameLease->IsValid() || command.Skinned.BonePalette.empty())
            {
                continue;
            }
            const uint64_t vertexCount = frameLease->AssetLease->GetVertices().size();
            if (totalVertices + vertexCount > std::numeric_limits<uint32_t>::max() / OutputVertexBytes)
            {
                break;
            }

            PlannedInstance planned;
            planned.CommandIndex = commandIndex;
            planned.VertexBase = static_cast<uint32_t>(totalVertices);
            planned.VertexCount = static_cast<uint32_t>(vertexCount);
            m_Plan.push_back(planned);
            totalVertices += vertexCount;
        }
        if (m_Plan.empty() || totalVertices == 0)
        {
            return;
        }

        // 今・前のフレームの頂点を詰めた 2 本のバッファ。計算シェーダーが書き、後のパスがアドレスか storage で読む。
        RGBufferDesc desc;
        desc.Size = totalVertices * OutputVertexBytes;
        desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead |
                     RHI::ResourceUsage::BufferDeviceAddress;
        desc.bCPUAccessible = false;

        desc.DebugName = "Skinning_CurrentVertices";
        m_CurrentHandle = builder.WriteBuffer(RenderGraphResourceNames::SkinningCurrentVertices,
                                              desc,
                                              RHI::ResourceState::UnorderedAccess,
                                              RHI::ResourceState::GenericRead);
        desc.DebugName = "Skinning_PreviousVertices";
        m_PreviousHandle = builder.WriteBuffer(RenderGraphResourceNames::SkinningPreviousVertices,
                                               desc,
                                               RHI::ResourceState::UnorderedAccess,
                                               RHI::ResourceState::GenericRead);
        if (!m_CurrentHandle.IsValid() || !m_PreviousHandle.IsValid())
        {
            m_CurrentHandle = {};
            m_PreviousHandle = {};
            m_Plan.clear();
            return;
        }
        builder.PreserveInsertionOrder();
    }

    void SkinningComputePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_Instances.clear();
        if (m_Plan.empty() || !m_CurrentHandle.IsValid() || !m_PreviousHandle.IsValid() || !context.CommandList)
        {
            return;
        }

        const RHI::BufferPtr currentVertices = resources.GetBuffer(m_CurrentHandle);
        const RHI::BufferPtr previousVertices = resources.GetBuffer(m_PreviousHandle);
        if (!currentVertices || !previousVertices)
        {
            return;
        }

        // 1 つも記録できないフレームでも、宣言したバッファは遷移させる（宣言した状態と実際の状態を合わせる）。
        if (m_Compute.IsReady() && context.SkinnedMeshes && context.SnapshotSkinnedMeshFrameLeases)
        {
            RecordInstances(context, currentVertices, previousVertices);
        }
        RecordFinalBarriers(context.CommandList, currentVertices, previousVertices);
    }

    void SkinningComputePass::RecordInstances(ViewRenderContext& context,
                                              const RHI::BufferPtr& currentVertices,
                                              const RHI::BufferPtr& previousVertices)
    {
        const DrawCommandView commands = context.GetActiveOpaqueCommands();
        m_Compute.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        for (const PlannedInstance& planned : m_Plan)
        {
            if (planned.CommandIndex >= commands.Count)
            {
                continue;
            }
            const DrawCommand& source = commands.Data[planned.CommandIndex];
            if (source.Draw.PayloadKind != DrawPayloadKind::Skinned ||
                source.Skinned.FrameLeaseIndex >= context.SnapshotSkinnedMeshFrameLeases->size())
            {
                continue;
            }
            const auto& frameLease = (*context.SnapshotSkinnedMeshFrameLeases)[source.Skinned.FrameLeaseIndex];
            if (!frameLease || !frameLease->IsValid())
            {
                continue;
            }

            // GBuffer の経路と同じく、直前のフレームの値が無ければ今の値で代用して動きを 0 にする。
            const bool bHasPrevious = source.Skinned.bHasPrevious &&
                                      source.Skinned.PreviousBonePalette.size() == source.Skinned.BonePalette.size();
            SkinnedMeshPreparedDraw prepared;
            if (!context.SkinnedMeshes->PrepareDraw(frameLease,
                                                    source.Skinned.BonePalette,
                                                    source.Draw.WorldMatrix,
                                                    prepared,
                                                    bHasPrevious ? &source.Skinned.PreviousBonePalette
                                                                 : &source.Skinned.BonePalette,
                                                    bHasPrevious ? &source.Skinned.PreviousWorldMatrix
                                                                 : &source.Draw.WorldMatrix) ||
                !prepared.PreviousPaletteBuffer)
            {
                continue;
            }
            // 提出するフレームの番号を結び、GPU が使い終わるまでパレットを保つ。
            if (!context.SkinnedMeshes->MarkLastUse(prepared, frameLease))
            {
                continue;
            }

            SkinningComputeDispatch dispatch;
            dispatch.SkinVertices = prepared.VertexBuffer;
            dispatch.Palette = prepared.PaletteBuffer;
            dispatch.PreviousPalette = prepared.PreviousPaletteBuffer;
            dispatch.CurrentVertices = currentVertices;
            dispatch.PreviousVertices = previousVertices;
            dispatch.VertexCount = planned.VertexCount;
            dispatch.OutputVertexBase = planned.VertexBase;
            if (!m_Compute.Record(context.CommandList, dispatch))
            {
                continue;
            }

            SkinningComputeInstance instance;
            instance.MeshHandle = prepared.MeshHandle;
            instance.ObjectId = source.Draw.ObjectId;
            instance.SourceMeshComponentId = source.Draw.SourceMeshComponentId;
            instance.MaterialIndex = source.Draw.MaterialIndex;
            instance.VertexBase = planned.VertexBase;
            instance.VertexCount = planned.VertexCount;
            instance.IndexCount = prepared.IndexCount;
            instance.IndexBuffer = prepared.IndexBuffer;
            const uint64_t currentAddress = currentVertices->GetDeviceAddress();
            const uint64_t previousAddress = previousVertices->GetDeviceAddress();
            const uint64_t byteOffset = static_cast<uint64_t>(planned.VertexBase) * OutputVertexBytes;
            instance.CurrentVertexAddress = currentAddress != 0 ? currentAddress + byteOffset : 0;
            instance.PreviousVertexAddress = previousAddress != 0 ? previousAddress + byteOffset : 0;
            m_Instances.push_back(instance);
        }
    }

} // namespace NorvesLib::Core::Rendering
