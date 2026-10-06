#include "Rendering/VisibilitySwRaster.h"

#include "Logging/LogMacros.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VisibilityMerge.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // visbuffer_sw_raster.comp の SwRasterParams（std140: uvec4 list、ivec4 rect、vec4 viewport、vec4 depthRange）
        struct SwRasterParams
        {
            uint32_t List[4];
            int32_t Rect[4];
            float Viewport[4];
            float DepthRange[4];
        };
        static_assert(sizeof(SwRasterParams) == 64, "SwRasterParams は std140 の 4 つの 16 バイトの組");

        constexpr uint32_t FrameUniformBytes = sizeof(float) * 32; // view + projection
        // MegaGeometryPass のソフトの一覧の頭（パスごとに 4 語。間接 dispatch の引数の x・y・z と、積もうとした数）
        constexpr uint32_t ListHeaderStrideBytes = 4u * sizeof(uint32_t);

        uint32_t ClampBindSize(uint64_t size)
        {
            return size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                               : static_cast<uint32_t>(size);
        }

        // visbuffer_sw_raster.comp の binding 0〜5
        RHI::DescriptorSetDesc MakeSwRasterDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const auto add = [&desc](uint32_t binding, RHI::ResourceBindType type)
            {
                RHI::DescriptorBinding descriptorBinding;
                descriptorBinding.binding = binding;
                descriptorBinding.type = type;
                descriptorBinding.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(descriptorBinding);
            };
            add(0, RHI::ResourceBindType::ConstantBuffer);
            add(1, RHI::ResourceBindType::StructuredBuffer);
            add(2, RHI::ResourceBindType::StructuredBuffer);
            add(3, RHI::ResourceBindType::RWBuffer);
            add(4, RHI::ResourceBindType::StructuredBuffer);
            add(5, RHI::ResourceBindType::ConstantBuffer);
            return desc;
        }
    } // namespace

    VisibilitySwRaster::VisibilitySwRaster() = default;

    VisibilitySwRaster::~VisibilitySwRaster()
    {
        Shutdown();
    }

    bool VisibilitySwRaster::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager || !VisibilityMerge::IsSupported(device->GetCapabilities()))
        {
            return false;
        }

        m_Shader = shaderManager->LoadShader("visbuffer_sw_raster.comp", RHI::ShaderStage::Compute);
        if (!m_Shader)
        {
            NORVES_LOG_WARNING("VisibilitySwRaster", "ソフトウェアラスタのシェーダーの読み込みに失敗。ソフトウェアラスタは行いません");
            Shutdown();
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_Shader;
        pipelineDesc.descriptorSetLayouts.push_back(MakeSwRasterDescriptorSetDesc());
        m_Pipeline = device->CreateComputePipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("VisibilitySwRaster", "ソフトウェアラスタのパイプラインの作成に失敗。ソフトウェアラスタは行いません");
            Shutdown();
            return false;
        }

        m_Device = device;
        return true;
    }

    void VisibilitySwRaster::Shutdown()
    {
        m_Uses.Clear();
        m_Pipeline.reset();
        m_Shader.reset();
        m_Device = nullptr;
        m_DirectFallbackCount = 0;
    }

    void VisibilitySwRaster::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool VisibilitySwRaster::RecordDispatch(RHI::ICommandList* commandList, uint32_t passIndex, const Inputs& inputs)
    {
        if (!IsReady() || !m_Device || !commandList || passIndex > 1u || !inputs.FrameUniform || !inputs.InstanceBuffer ||
            !inputs.RecordTable || !inputs.KeyBuffer || !inputs.List || inputs.ListCapacity == 0 || inputs.KeyWidth == 0 ||
            inputs.KeyHeight == 0)
        {
            return false;
        }

        // 資源は呼び出しの回数ではなくフレームの枠で決める（提出前・GPU が読み終わる前の資源を上書きしない）
        Use& use = m_Uses.Acquire();
        if (!use.Params)
        {
            use.Params = m_Device->CreateBuffer(
                RHI::BufferDesc(sizeof(SwRasterParams), RHI::ResourceUsage::ConstantBuffer, true, "VisBuffer_SwRasterParams"));
        }
        if (!use.DescriptorSet)
        {
            use.DescriptorSet = m_Device->CreateDescriptorSet(MakeSwRasterDescriptorSetDesc());
        }
        if (!use.Params || !use.DescriptorSet)
        {
            NORVES_LOG_WARNING("VisibilitySwRaster", "ソフトウェアラスタの資源の作成に失敗。この dispatch は何もしません");
            return false;
        }

        // 描いてよい画素の矩形: シザーと 64bit のバッファの大きさの共通部分
        SwRasterParams params = {};
        params.List[0] = inputs.ListCapacity;
        params.List[1] = passIndex;
        params.List[2] = inputs.KeyWidth;
        params.List[3] = inputs.KeyHeight;
        params.Rect[0] = std::max(inputs.Scissor.left, 0);
        params.Rect[1] = std::max(inputs.Scissor.top, 0);
        params.Rect[2] = std::min(inputs.Scissor.right, static_cast<int32_t>(inputs.KeyWidth));
        params.Rect[3] = std::min(inputs.Scissor.bottom, static_cast<int32_t>(inputs.KeyHeight));
        params.Viewport[0] = inputs.Viewport.x;
        params.Viewport[1] = inputs.Viewport.y;
        params.Viewport[2] = inputs.Viewport.width;
        params.Viewport[3] = inputs.Viewport.height;
        params.DepthRange[0] = inputs.Viewport.minDepth;
        params.DepthRange[1] = inputs.Viewport.maxDepth;
        use.Params->Update(&params, sizeof(params));

        use.DescriptorSet->BindConstantBuffer(0, inputs.FrameUniform, 0, FrameUniformBytes);
        use.DescriptorSet->BindStorageBuffer(1, inputs.InstanceBuffer, 0, ClampBindSize(inputs.InstanceBufferBytes));
        use.DescriptorSet->BindStorageBuffer(2, inputs.RecordTable, 0, ClampBindSize(inputs.RecordTableBytes));
        use.DescriptorSet->BindStorageBuffer(3, inputs.KeyBuffer, 0, ClampBindSize(inputs.KeyBuffer->GetSize()));
        use.DescriptorSet->BindStorageBuffer(4, inputs.List, 0, ClampBindSize(inputs.List->GetSize()));
        use.DescriptorSet->BindConstantBuffer(5, use.Params, 0, sizeof(SwRasterParams));
        use.DescriptorSet->Update();

        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(use.DescriptorSet, 0);
        if (!commandList->DispatchIndirect(inputs.List, static_cast<uint64_t>(passIndex) * ListHeaderStrideBytes))
        {
            // 間接 dispatch を断るコマンドリスト: 一覧の容量ぶんのワークグループを直接走らせる。
            // 一覧の数を超えるグループは、シェーダーが頭の積もうとした数で捨てる
            const uint32_t groupsX = std::min(inputs.ListCapacity, MaxGroupsX);
            const uint32_t groupsY = (inputs.ListCapacity + MaxGroupsX - 1u) / MaxGroupsX;
            commandList->Dispatch(groupsX, groupsY, 1u);
            if (m_DirectFallbackCount++ == 0)
            {
                NORVES_LOG_WARNING("VisibilitySwRaster",
                                   "SW_RASTER_DIRECT_FALLBACK groups=%u 間接 dispatch を断られたため、直接 dispatch で走らせます",
                                   groupsX * groupsY);
            }
        }
        return true;
    }

} // namespace NorvesLib::Core::Rendering
