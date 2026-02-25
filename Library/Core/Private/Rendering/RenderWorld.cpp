#include "Rendering/RenderWorld.h"
#include "RHI/IDevice.h"
#include "RHI/ISwapChain.h"
#include "RHI/ICommandList.h"
#include "RHI/IRenderPass.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IShader.h"
#include "Logging/LogMacros.h"
#include "Rendering/Shaders/TriangleShaders.h"

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{

    bool RenderWorld::Initialize(const RenderWorldSettings &settings)
    {
        if (m_bInitialized)
        {
            LOG_WARNING("RenderWorld is already initialized");
            return true;
        }

        LOG_INFO("RenderWorld::Initialize() - Starting RHI initialization");

        m_Width = settings.Width;
        m_Height = settings.Height;
        m_bVSyncEnabled = settings.bVSync;
        m_bFullscreen = settings.bFullscreen;
        m_bMultiThreadedRendering = settings.bEnableMultiThreadedRendering;

        // ========================================
        // 1. RHI Device (passed from Engine layer)
        // ========================================
        m_Device = settings.Device;
        if (!m_Device)
        {
            NORVES_LOG_ERROR("Rendering", "RHI Device is null - Engine must initialize RHI before RenderWorld");
            return false;
        }

        LOG_INFO("RHI Device received (API: %d)", static_cast<int>(m_Device->GetAPI()));

        // ========================================
        // 2. SwapChain creation
        // ========================================
        RHI::SwapChainDesc swapChainDesc;
        swapChainDesc.windowHandle = settings.WindowHandle;
        swapChainDesc.width = m_Width;
        swapChainDesc.height = m_Height;
        swapChainDesc.format = RHI::Format::B8G8R8A8_UNORM;
        swapChainDesc.bufferCount = settings.BackBufferCount;
        swapChainDesc.vsync = m_bVSyncEnabled;

        m_SwapChain = m_Device->CreateSwapChain(swapChainDesc);
        if (!m_SwapChain)
        {
            NORVES_LOG_ERROR("Rendering", "Failed to create swap chain");
            return false;
        }

        LOG_INFO("SwapChain created (%ux%u, %u buffers)",
                 m_SwapChain->GetWidth(), m_SwapChain->GetHeight(), m_SwapChain->GetBufferCount());

        // ========================================
        // 3. CommandList creation
        // ========================================
        m_CommandList = m_Device->CreateCommandList();
        if (!m_CommandList)
        {
            NORVES_LOG_ERROR("Rendering", "Failed to create command list");
            return false;
        }

        // ========================================
        // 4. RenderPass for triangle rendering
        // ========================================
        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = m_SwapChain->GetFormat();
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = true;
        // Cornflower blue clear color
        colorAttachment.clearColor[0] = 0.392f;
        colorAttachment.clearColor[1] = 0.584f;
        colorAttachment.clearColor[2] = 0.929f;
        colorAttachment.clearColor[3] = 1.0f;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::Undefined;
        colorAttachment.finalState = RHI::ResourceState::Present;

        RHI::RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;

        m_TriangleRenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_TriangleRenderPass)
        {
            NORVES_LOG_ERROR("Rendering", "Failed to create render pass");
            return false;
        }

        // ========================================
        // 5. Framebuffers (one per swapchain image)
        // ========================================
        if (!CreateSwapChainFramebuffers())
        {
            NORVES_LOG_ERROR("Rendering", "Failed to create framebuffers");
            return false;
        }

        // ========================================
        // 6. Shaders
        // ========================================
        RHI::ShaderDesc vertexShaderDesc;
        vertexShaderDesc.stage = RHI::ShaderStage::Vertex;
        vertexShaderDesc.entryPoint = "main";
        vertexShaderDesc.byteCode.assign(
            TriangleVertexShaderSpirV,
            TriangleVertexShaderSpirV + sizeof(TriangleVertexShaderSpirV));

        m_TriangleVertexShader = m_Device->CreateShader(vertexShaderDesc);
        if (!m_TriangleVertexShader)
        {
            NORVES_LOG_ERROR("Rendering", "Failed to create triangle vertex shader");
            return false;
        }

        RHI::ShaderDesc fragmentShaderDesc;
        fragmentShaderDesc.stage = RHI::ShaderStage::Pixel;
        fragmentShaderDesc.entryPoint = "main";
        fragmentShaderDesc.byteCode.assign(
            TriangleFragmentShaderSpirV,
            TriangleFragmentShaderSpirV + sizeof(TriangleFragmentShaderSpirV));

        m_TriangleFragmentShader = m_Device->CreateShader(fragmentShaderDesc);
        if (!m_TriangleFragmentShader)
        {
            NORVES_LOG_ERROR("Rendering", "Failed to create triangle fragment shader");
            return false;
        }

        // ========================================
        // 7. Graphics Pipeline
        // ========================================
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_TriangleVertexShader;
        pipelineDesc.pixelShader = m_TriangleFragmentShader;

        // No vertex input (positions/colors hardcoded in shader)
        // vertexBindings and vertexAttributes remain empty

        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // Rasterizer: fill, no culling (triangle is single-sided, ensure visibility)
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // Depth/Stencil: disabled
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        // Blend: no blending, write all color channels
        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_TriangleRenderPass;

        m_TrianglePipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_TrianglePipeline)
        {
            NORVES_LOG_ERROR("Rendering", "Failed to create triangle graphics pipeline");
            return false;
        }

        m_bInitialized = true;
        LOG_INFO("RenderWorld::Initialize() - Initialization completed successfully");
        return true;
    }

    void RenderWorld::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        LOG_INFO("RenderWorld::Shutdown() - Starting shutdown");

        // Wait for all GPU work to complete
        if (m_Device)
        {
            m_Device->WaitIdle();
        }

        // Release resources in reverse order
        m_TrianglePipeline.reset();
        m_TriangleFragmentShader.reset();
        m_TriangleVertexShader.reset();
        m_SwapChainFramebuffers.clear();
        m_TriangleRenderPass.reset();
        m_CommandList.reset();
        m_SwapChain.reset();

        // Release device reference (Engine layer handles RHI shutdown)
        m_Device.reset();

        m_bInitialized = false;
        LOG_INFO("RenderWorld::Shutdown() - Shutdown completed");
    }

    void RenderWorld::BeginFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // Acquire next swapchain image (fence wait + image acquire)
        if (!m_SwapChain->BeginFrame())
        {
            // Swapchain might need recreation (e.g., window resize)
            return;
        }
    }

    void RenderWorld::RenderTriangle()
    {
        if (!m_bInitialized)
        {
            return;
        }

        uint32_t imageIndex = m_SwapChain->GetCurrentBackBufferIndex();

        // フレーム別コマンドバッファを選択（ダブルバッファリングでの同期問題を回避）
        m_CommandList->SetFrameIndex(m_SwapChain->GetCurrentFrameIndex());

        // Begin command recording (no fence management - SwapChain handles sync)
        m_CommandList->BeginRecording();

        // Begin render pass with current swapchain framebuffer
        m_CommandList->BeginRenderPass(m_TriangleRenderPass, m_SwapChainFramebuffers[imageIndex]);

        // Set viewport
        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_SwapChain->GetWidth());
        viewport.height = static_cast<float>(m_SwapChain->GetHeight());
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        m_CommandList->SetViewport(viewport);

        // Set scissor
        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_SwapChain->GetWidth());
        scissor.bottom = static_cast<int32_t>(m_SwapChain->GetHeight());
        m_CommandList->SetScissor(scissor);

        // Bind pipeline
        m_CommandList->SetPipeline(m_TrianglePipeline);

        // Draw triangle (3 vertices, no vertex buffer - positions in shader)
        m_CommandList->Draw(3, 0);

        // End render pass
        m_CommandList->EndRenderPass();

        // End command recording
        m_CommandList->End();
    }

    void RenderWorld::CollectScene()
    {
        // TODO: Collect scene proxies for rendering
    }

    void RenderWorld::SetMainCamera(const CameraProxy &camera)
    {
        // TODO: Store camera for view/projection matrix calculation
        (void)camera;
    }

    void RenderWorld::EndFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // Submit commands and present (semaphore-synchronized)
        m_SwapChain->EndFrame(m_CommandList);

        // Update stats
        m_Stats.FrameNumber++;
    }

    void RenderWorld::WaitForRender()
    {
        if (m_Device)
        {
            m_Device->WaitIdle();
        }
    }

    void RenderWorld::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }

        if (!m_bInitialized)
        {
            return;
        }

        LOG_INFO("RenderWorld::Resize(%u, %u)", width, height);

        // Wait for GPU to finish
        m_Device->WaitIdle();

        m_Width = width;
        m_Height = height;

        // Resize swapchain
        m_SwapChain->Resize(width, height);

        // Recreate framebuffers for new swapchain images
        m_SwapChainFramebuffers.clear();
        CreateSwapChainFramebuffers();
    }

    void RenderWorld::SetVSync(bool bEnabled)
    {
        m_bVSyncEnabled = bEnabled;
        // TODO: Recreate swapchain with new vsync setting if needed
    }

    void RenderWorld::SetFullscreen(bool bEnabled)
    {
        m_bFullscreen = bEnabled;
        // TODO: Toggle fullscreen mode
    }

    bool RenderWorld::CreateSwapChainFramebuffers()
    {
        uint32_t imageCount = m_SwapChain->GetBufferCount();
        m_SwapChainFramebuffers.reserve(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            RHI::FramebufferDesc fbDesc;
            fbDesc.colorTargets.push_back(m_SwapChain->GetBackBuffer(i));
            fbDesc.renderPass = m_TriangleRenderPass;
            fbDesc.width = m_SwapChain->GetWidth();
            fbDesc.height = m_SwapChain->GetHeight();

            auto framebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!framebuffer)
            {
                NORVES_LOG_ERROR("Rendering", "Failed to create framebuffer for swapchain image %u", i);
                return false;
            }

            m_SwapChainFramebuffers.push_back(framebuffer);
        }

        LOG_INFO("Created %u swapchain framebuffers", imageCount);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
