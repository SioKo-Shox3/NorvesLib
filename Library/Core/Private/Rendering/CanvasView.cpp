#include "Rendering/CanvasView.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Viewport.h"
#include "Rendering/ViewRenderContext.h"
#include "Math/MatrixUtils.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/IShader.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"
#include <algorithm>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t MaxRetainedBoardFrameResources = 8;

        struct SortedBoardEntry
        {
            const BoardProxy* Proxy = nullptr;
            uint64_t CanvasInsertionSequence = 0;
        };

        void FillBoardInstanceData(const BoardProxy &proxy,
                                   const ViewportRenderPlan &viewportPlan,
                                   GPUSceneInstanceData &outData)
        {
            Math::MatrixUtils::CopyToShaderData(proxy.WorldTransform, outData.World);

            for (float &value : outData.NormalMatrix)
            {
                value = 0.0f;
            }

            outData.NormalMatrix[0] = 1.0f;
            outData.NormalMatrix[5] = 1.0f;
            outData.NormalMatrix[10] = 1.0f;
            outData.ObjectColor[0] = 1.0f;
            outData.ObjectColor[1] = 1.0f;
            outData.ObjectColor[2] = 1.0f;
            outData.ObjectColor[3] = 0.75f;
            outData.CustomData[0] = viewportPlan.PixelRect.Width > 0.0f
                                        ? viewportPlan.PixelRect.Width
                                        : static_cast<float>(viewportPlan.RenderWidth);
            outData.CustomData[1] = viewportPlan.PixelRect.Height > 0.0f
                                        ? viewportPlan.PixelRect.Height
                                        : static_cast<float>(viewportPlan.RenderHeight);
            outData.CustomData[2] = 0.0f;
            outData.CustomData[3] = 0.0f;
        }
    } // namespace

    class CanvasBoardPass final : public IRenderGraphPass
    {
    public:
        explicit CanvasBoardPass(CanvasView *owner)
            : m_Owner(owner)
        {
        }

        const char* GetName() const override
        {
            return "CanvasBoardPass";
        }

        bool WasCleared() const
        {
            return m_bCleared;
        }

        RHI::TexturePtr GetOutputTexture() const
        {
            return m_OutputTexture;
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            const ViewRenderContext* context = builder.GetContext();
            const uint32_t width = std::max(1u, context ? context->GetActiveRenderWidth() : 1u);
            const uint32_t height = std::max(1u, context ? context->GetActiveRenderHeight() : 1u);

            m_OutputHandle = builder.WriteTextureAttachment(
                RenderGraphResourceNames::CanvasColor,
                RGTextureDesc::RenderTarget(width,
                                            height,
                                            RHI::Format::R8G8B8A8_UNORM,
                                            "Canvas.Color"),
                RGAttachmentKind::Color,
                RHI::AttachmentLoadOp::Clear,
                RHI::AttachmentStoreOp::Store,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            builder.ExportTexture(RenderGraphResourceNames::CanvasColor, m_OutputHandle);
            builder.PreserveInsertionOrder();
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            m_bCleared = false;
            m_OutputTexture.reset();

            RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
            if (!outputTexture || !context.Device || !context.CommandList)
            {
                return;
            }

            RHI::RenderPassDesc renderPassDesc;
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = outputTexture->GetFormat();
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = true;
            colorAttachment.clearColor[0] = 0.0f;
            colorAttachment.clearColor[1] = 0.0f;
            colorAttachment.clearColor[2] = 0.0f;
            colorAttachment.clearColor[3] = 0.0f;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(colorAttachment);
            renderPassDesc.hasDepthStencil = false;

            RHI::RenderPassPtr renderPass = context.Device->CreateRenderPass(renderPassDesc);
            if (!renderPass)
            {
                NORVES_LOG_ERROR("CanvasView", "Failed to create canvas clear render pass");
                return;
            }

            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = renderPass;
            framebufferDesc.colorTargets.push_back(outputTexture);
            framebufferDesc.width = outputTexture->GetWidth();
            framebufferDesc.height = outputTexture->GetHeight();

            RHI::FramebufferPtr framebuffer = context.Device->CreateFramebuffer(framebufferDesc);
            if (!framebuffer)
            {
                NORVES_LOG_ERROR("CanvasView", "Failed to create canvas clear framebuffer");
                return;
            }

            CanvasView::RetainedBoardFrameResources retainedResources;
            retainedResources.OutputTexture = outputTexture;
            retainedResources.RenderPass = renderPass;
            retainedResources.Framebuffer = framebuffer;

            context.CommandList->BeginRenderPass(renderPass, framebuffer);
            context.CommandList->SetViewport(context.GetActiveLocalViewport());
            context.CommandList->SetScissor(context.GetActiveLocalScissor());
            RecordBoardDrawCommands(renderPass, context, retainedResources);
            context.CommandList->EndRenderPass();

            m_OutputTexture = outputTexture;
            m_bCleared = true;
            if (m_Owner)
            {
                m_Owner->RetainBoardFrameResources(retainedResources);
            }
        }

    private:
        RHI::DescriptorSetPtr CreateBoardDescriptorSet(ViewRenderContext& context)
        {
            if (!context.Device || !context.InstanceDataBuffer)
            {
                return nullptr;
            }

            RHI::DescriptorSetDesc descriptorSetDesc;
            RHI::DescriptorBinding instanceBinding;
            instanceBinding.binding = 7;
            instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
            instanceBinding.stages = RHI::ShaderStage::Vertex;
            descriptorSetDesc.bindings.push_back(instanceBinding);

            RHI::DescriptorSetPtr descriptorSet = context.Device->CreateDescriptorSet(descriptorSetDesc);
            if (!descriptorSet)
            {
                return nullptr;
            }

            descriptorSet->BindStorageBuffer(7,
                                             context.InstanceDataBuffer,
                                             0,
                                             static_cast<uint32_t>(context.InstanceDataBuffer->GetSize()));
            descriptorSet->Update();
            return descriptorSet;
        }

        RHI::PipelinePtr CreateBoardPipeline(RHI::RenderPassPtr renderPass, ViewRenderContext& context)
        {
            if (!renderPass || !context.Device || !context.ShaderMgr)
            {
                return nullptr;
            }

            RHI::ShaderPtr vertexShader = context.ShaderMgr->LoadShader("board2d.vert", RHI::ShaderStage::Vertex);
            RHI::ShaderPtr pixelShader = context.ShaderMgr->LoadShader("board2d.frag", RHI::ShaderStage::Pixel);
            if (!vertexShader || !pixelShader)
            {
                return nullptr;
            }

            RHI::DescriptorSetDesc descriptorSetDesc;
            RHI::DescriptorBinding instanceBinding;
            instanceBinding.binding = 7;
            instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
            instanceBinding.stages = RHI::ShaderStage::Vertex;
            descriptorSetDesc.bindings.push_back(instanceBinding);

            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = vertexShader;
            pipelineDesc.pixelShader = pixelShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;
            pipelineDesc.depthStencilState.depthTestEnable = false;
            pipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc blend;
            blend.blendEnable = false;
            blend.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blend);
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.descriptorSetLayouts.push_back(descriptorSetDesc);
            return context.Device->CreateGraphicsPipeline(pipelineDesc);
        }

        void RecordBoardDrawCommands(RHI::RenderPassPtr renderPass,
                                     ViewRenderContext& context,
                                     CanvasView::RetainedBoardFrameResources &retainedResources)
        {
            const DrawCommandView boardCommands = context.GetActiveDrawCommands();
            if (boardCommands.empty() || !context.Renderer)
            {
                return;
            }

            RHI::DescriptorSetPtr descriptorSet = CreateBoardDescriptorSet(context);
            RHI::PipelinePtr pipeline = CreateBoardPipeline(renderPass, context);
            if (!descriptorSet || !pipeline)
            {
                return;
            }

            retainedResources.DescriptorSet = descriptorSet;
            retainedResources.Pipeline = pipeline;

            Container::VariableArray<DrawCommand> executableCommands;
            executableCommands.reserve(boardCommands.size());
            for (const DrawCommand &command : boardCommands)
            {
                DrawCommand executableCommand = command;
                executableCommand.Pipeline = pipeline;
                executableCommand.DescriptorSet = descriptorSet;
                executableCommand.DescriptorSetSlot = 0;
                executableCommands.push_back(executableCommand);
            }

            context.Renderer->ExecuteDrawCommands(executableCommands, context.CommandList);
        }

        CanvasView *m_Owner = nullptr;
        RGTextureHandle m_OutputHandle;
        RHI::TexturePtr m_OutputTexture;
        bool m_bCleared = false;
    };

    CanvasView::CanvasView() = default;

    CanvasView::~CanvasView()
    {
        ReleaseRetainedBoardFrameResources();
    }

    bool CanvasView::Initialize(const ViewSettings& settings)
    {
        ViewSettings canvasSettings = settings;
        canvasSettings.Type = ViewType::UI;
        canvasSettings.bClearColor = true;
        canvasSettings.ClearColor[0] = 0.0f;
        canvasSettings.ClearColor[1] = 0.0f;
        canvasSettings.ClearColor[2] = 0.0f;
        canvasSettings.ClearColor[3] = 0.0f;
        canvasSettings.bClearDepth = false;
        if (!View::Initialize(canvasSettings))
        {
            return false;
        }

        auto viewport = Container::MakeShared<Viewport>();
        ViewportSettings viewportSettings;
        viewportSettings.X = 0.0f;
        viewportSettings.Y = 0.0f;
        viewportSettings.Width = 1.0f;
        viewportSettings.Height = 1.0f;
        viewportSettings.MinDepth = 0.0f;
        viewportSettings.MaxDepth = 1.0f;
        if (!viewport->Initialize(viewportSettings))
        {
            View::Shutdown();
            return false;
        }

        viewport->SetEnabled(true);
        AddViewport(viewport);
        return true;
    }

    void CanvasView::Shutdown()
    {
        ReleaseRetainedBoardFrameResources();
        ClearBoardDrawCommands();
        m_BoardProxies.clear();
        m_BoardProxyIndex.clear();
        m_BoardInsertionSequenceByComponentId.clear();
        m_NextBoardInsertionSequence = 0;
        View::Shutdown();
    }

    void CanvasView::UpdateBoardProxy(uint64_t componentId, const BoardProxy &proxy)
    {
        if (componentId == 0 || !proxy.IsValid())
        {
            RemoveBoardProxy(componentId);
            return;
        }

        auto indexIt = m_BoardProxyIndex.find(componentId);
        if (indexIt != m_BoardProxyIndex.end())
        {
            m_BoardProxies[indexIt->second] = proxy;
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_BoardProxies.size());
        auto sequenceIt = m_BoardInsertionSequenceByComponentId.find(componentId);
        if (sequenceIt == m_BoardInsertionSequenceByComponentId.end())
        {
            m_BoardInsertionSequenceByComponentId[componentId] = m_NextBoardInsertionSequence++;
        }
        m_BoardProxies.push_back(proxy);
        m_BoardProxyIndex[componentId] = index;
    }

    void CanvasView::RemoveBoardProxy(uint64_t componentId)
    {
        auto indexIt = m_BoardProxyIndex.find(componentId);
        if (indexIt == m_BoardProxyIndex.end())
        {
            return;
        }

        const uint32_t removeIndex = indexIt->second;
        const uint32_t lastIndex = static_cast<uint32_t>(m_BoardProxies.size() - 1);
        m_BoardProxyIndex.erase(indexIt);
        m_BoardInsertionSequenceByComponentId.erase(componentId);

        if (removeIndex != lastIndex)
        {
            m_BoardProxies[removeIndex] = m_BoardProxies[lastIndex];
            m_BoardProxyIndex[m_BoardProxies[removeIndex].ComponentId] = removeIndex;
        }

        m_BoardProxies.pop_back();
    }

    void CanvasView::RemoveStaleBoardProxies(const Container::UnorderedSet<uint64_t> &liveComponentIds)
    {
        uint32_t index = 0;
        while (index < m_BoardProxies.size())
        {
            const uint64_t componentId = m_BoardProxies[index].ComponentId;
            if (liveComponentIds.find(componentId) != liveComponentIds.end())
            {
                ++index;
                continue;
            }

            const uint32_t lastIndex = static_cast<uint32_t>(m_BoardProxies.size() - 1);
            m_BoardProxyIndex.erase(componentId);
            m_BoardInsertionSequenceByComponentId.erase(componentId);
            if (index != lastIndex)
            {
                m_BoardProxies[index] = m_BoardProxies[lastIndex];
                m_BoardProxyIndex[m_BoardProxies[index].ComponentId] = index;
            }
            m_BoardProxies.pop_back();
        }
    }

    void CanvasView::PrepareBoardDrawCommands(const ViewportRenderPlan &viewportPlan)
    {
        ClearBoardDrawCommands();

        if (!m_bEnabled || !m_bInitialized || !viewportPlan.HasDrawableExtent())
        {
            return;
        }

        Container::VariableArray<SortedBoardEntry> sortedBoards;
        sortedBoards.reserve(m_BoardProxies.size());
        for (const BoardProxy &proxy : m_BoardProxies)
        {
            if (!proxy.IsValid() ||
                proxy.Space != BoardSpace::ScreenSpace ||
                !HasFlag(viewportPlan.Camera.CullingMask, proxy.LayerMask))
            {
                continue;
            }

            auto sequenceIt = m_BoardInsertionSequenceByComponentId.find(proxy.ComponentId);
            if (sequenceIt == m_BoardInsertionSequenceByComponentId.end())
            {
                continue;
            }

            SortedBoardEntry entry;
            entry.Proxy = &proxy;
            entry.CanvasInsertionSequence = sequenceIt->second;
            sortedBoards.push_back(entry);
        }

        std::stable_sort(sortedBoards.begin(),
                         sortedBoards.end(),
                         [](const SortedBoardEntry &lhs, const SortedBoardEntry &rhs)
                         {
                             if (lhs.Proxy->SortKey != rhs.Proxy->SortKey)
                             {
                                 return lhs.Proxy->SortKey < rhs.Proxy->SortKey;
                             }

                             if (lhs.CanvasInsertionSequence != rhs.CanvasInsertionSequence)
                             {
                                 return lhs.CanvasInsertionSequence < rhs.CanvasInsertionSequence;
                             }

                             return lhs.Proxy->ComponentId < rhs.Proxy->ComponentId;
                         });

        for (const SortedBoardEntry &entry : sortedBoards)
        {
            const BoardProxy &proxy = *entry.Proxy;
            GPUSceneInstanceData instanceData;
            FillBoardInstanceData(proxy, viewportPlan, instanceData);
            const uint32_t instanceIndex = static_cast<uint32_t>(m_BoardInstanceData.size());
            m_BoardInstanceData.push_back(instanceData);

            DrawCommand command = DrawCommand::CreateDraw();
            command.Type = DrawCommandType::DrawInstanced;
            command.Draw.VertexOffset = 6;
            command.Draw.InstanceCount = 1;
            command.Draw.FirstInstance = instanceIndex;
            command.Draw.InstanceDataOffset = instanceIndex;
            command.Draw.bInstanced = true;
            command.Draw.MaterialBlendMode = BlendMode::Translucent;
            command.Draw.ObjectId = proxy.ObjectId;
            command.SortKey = proxy.SortKey;
            m_BoardDrawCommands.push_back(command);
        }
    }

    void CanvasView::ClearBoardDrawCommands()
    {
        m_BoardDrawCommands.clear();
        m_BoardInstanceData.clear();
    }

    void CanvasView::RetainBoardFrameResources(const RetainedBoardFrameResources &resources)
    {
        if (!resources.OutputTexture || !resources.RenderPass || !resources.Framebuffer)
        {
            return;
        }

        m_RetainedBoardFrameResources.push_back(resources);
        while (m_RetainedBoardFrameResources.size() > MaxRetainedBoardFrameResources)
        {
            m_RetainedBoardFrameResources.erase(m_RetainedBoardFrameResources.begin());
        }
    }

    void CanvasView::ReleaseRetainedBoardFrameResources()
    {
        m_RetainedBoardFrameResources.clear();
    }

    void CanvasView::Render(ViewRenderContext& context)
    {
        ResetFrameOutput();
        context.CurrentGraphExecutionResult = nullptr;
        context.bPresentationGraphPassHandled = false;

        if (!m_bEnabled || !m_bInitialized)
        {
            return;
        }

        if (!context.Graph)
        {
            NORVES_LOG_ERROR("CanvasView", "RenderGraph context is required");
            return;
        }

        context.Graph->Reset();

        CanvasBoardPass clearPass(this);
        context.Graph->AddPass(&clearPass);

        if (!context.Graph->Compile(context))
        {
            NORVES_LOG_ERROR("CanvasView", "RenderGraph compile failed");
            ResetFrameOutput();
            return;
        }

        RenderGraphExecutionResult executionResult = context.Graph->ExecuteWithResult(context);
        if (!executionResult.bSuccess)
        {
            NORVES_LOG_ERROR("CanvasView",
                             "RenderGraph execution failed after %u pass(es)",
                             context.Graph->GetLastExecutedPassCount());
            ResetFrameOutput();
            return;
        }

        const RenderGraphExecutionResult& lastResult = context.Graph->GetLastExecutionResult();
        if (!clearPass.WasCleared())
        {
            ResetFrameOutput();
            return;
        }

        SetFrameOutputTexture(clearPass.GetOutputTexture());
        context.CurrentGraphExecutionResult = &lastResult;
    }

} // namespace NorvesLib::Core::Rendering
