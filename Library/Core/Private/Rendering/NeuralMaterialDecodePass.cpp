#include "Rendering/NeuralMaterialDecodePass.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/DeviceCapabilities.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    NeuralMaterialDecodePass::~NeuralMaterialDecodePass()
    {
        Shutdown();
    }

    bool NeuralMaterialDecodePass::Initialize(ViewRenderContext &context)
    {
        if (!context.Device)
        {
            NORVES_LOG_ERROR("NeuralMaterialDecodePass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // Cooperative Vectorサポート確認
        // ========================================
        if (context.Capabilities)
        {
            m_bCooperativeVectorSupported = context.Capabilities->NeuralShaders.bSupported;
        }

        if (!m_bCooperativeVectorSupported)
        {
            NORVES_LOG_INFO("NeuralMaterialDecodePass",
                            "Cooperative Vector not supported - pass will be skipped");
            m_bInitialized = true;
            return true;
        }

        // ========================================
        // デコーダー初期化
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("NeuralMaterialDecodePass", "ShaderManager is null");
            return false;
        }

        if (!m_Decoder.Initialize(m_Device, context.ShaderMgr))
        {
            NORVES_LOG_WARNING("NeuralMaterialDecodePass",
                               "Decoder initialization failed - pass will be disabled");
            m_bCooperativeVectorSupported = false;
            m_bInitialized = true;
            return true;
        }

        NORVES_LOG_INFO("NeuralMaterialDecodePass", "Initialized (CooperativeVector=%s)",
                        m_bCooperativeVectorSupported ? "enabled" : "disabled");
        m_bInitialized = true;
        return true;
    }

    void NeuralMaterialDecodePass::Shutdown()
    {
        m_Decoder.Shutdown();
        m_FrameDecodeTargets.clear();

        m_Device = nullptr;
        m_SceneRenderer = nullptr;
        m_SceneView = nullptr;
    }

    void NeuralMaterialDecodePass::Setup(ViewRenderContext &context)
    {
        if (!m_bCooperativeVectorSupported || !m_Decoder.IsInitialized())
        {
            return;
        }

        // ViewRenderContext::Resources.Materialsからプルモデルでデコード対象を取得
        m_FrameDecodeTargets.clear();
        if (context.Resources.Materials)
        {
            m_FrameDecodeTargets = context.Resources.Materials->GetNeuralResources();
        }

        m_Decoder.ClearResources();
        for (auto *resource : m_FrameDecodeTargets)
        {
            m_Decoder.RegisterResource(resource);
        }
    }

    void NeuralMaterialDecodePass::Execute(ViewRenderContext &context)
    {
        if (!m_bCooperativeVectorSupported || !m_Decoder.IsInitialized())
        {
            return;
        }

        if (!context.CommandList || !m_SceneRenderer)
        {
            return;
        }

        // ========================================
        // Dispatchコマンド生成
        // ========================================
        Container::VariableArray<DrawCommand> decodeCommands;
        m_Decoder.GenerateDecodeCommands(decodeCommands);

        if (decodeCommands.empty())
        {
            return;
        }

        // ========================================
        // DrawCommand機構を通じてDispatch実行
        // ========================================
        m_SceneRenderer->ExecuteDrawCommands(decodeCommands, context.CommandList);

        // ========================================
        // 出力テクスチャのバリア
        // （UnorderedAccess → ShaderResource へ遷移）
        // ========================================
        for (auto *resource : m_FrameDecodeTargets)
        {
            if (!resource || !resource->IsInitialized())
            {
                continue;
            }

            const auto &desc = resource->GetDesc();
            for (uint32_t i = 0; i < static_cast<uint32_t>(desc.OutputSlots.size()); ++i)
            {
                auto outputTexture = resource->GetOutputTexture(i);
                if (outputTexture)
                {
                    context.EnqueueTextureBarrier(outputTexture,
                                                 RHI::ResourceState::UnorderedAccess,
                                                 RHI::ResourceState::ShaderResource);
                }
            }
        }
    }

} // namespace NorvesLib::Core::Rendering
