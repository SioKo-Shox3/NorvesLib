#include "Rendering/NeuralMaterialDecoder.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "RHI/IShader.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    NeuralMaterialDecoder::~NeuralMaterialDecoder()
    {
        Shutdown();
    }

    bool NeuralMaterialDecoder::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!device || !shaderManager)
        {
            NORVES_LOG_ERROR("NeuralMaterialDecoder", "Device or ShaderManager is null");
            return false;
        }

        m_Device = device;
        m_ShaderManager = shaderManager;

        // ========================================
        // コンピュートシェーダーをロード
        // ========================================
        auto computeShader = shaderManager->LoadShader(
            "neural_material_decode.slang",
            RHI::ShaderStage::Compute,
            "main");

        if (!computeShader)
        {
            NORVES_LOG_WARNING("NeuralMaterialDecoder",
                               "Failed to load neural_material_decode.slang - decoder will be disabled");
            // シェーダーが存在しなくても初期化は成功扱い
            // （Slang/Cooperative Vectorが利用不可な環境での猶予）
            m_bInitialized = true;
            return true;
        }

        // ========================================
        // コンピュートパイプライン作成
        // ========================================
        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = computeShader;

        m_ComputePipeline = device->CreateComputePipeline(pipelineDesc);
        if (!m_ComputePipeline)
        {
            NORVES_LOG_ERROR("NeuralMaterialDecoder", "Failed to create compute pipeline");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("NeuralMaterialDecoder", "Initialized with compute pipeline");
        return true;
    }

    void NeuralMaterialDecoder::Shutdown()
    {
        m_Resources.clear();
        m_ComputePipeline.reset();
        m_ShaderManager = nullptr;
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void NeuralMaterialDecoder::RegisterResource(NeuralMaterialResource* resource)
    {
        if (resource && resource->IsInitialized())
        {
            m_Resources.push_back(resource);
        }
    }

    void NeuralMaterialDecoder::ClearResources()
    {
        m_Resources.clear();
    }

    void NeuralMaterialDecoder::GenerateDecodeCommands(Container::VariableArray<DrawCommand>& outCommands)
    {
        if (!m_bInitialized || !m_ComputePipeline || m_Resources.empty())
        {
            return;
        }

        for (auto* resource : m_Resources)
        {
            if (!resource || !resource->IsInitialized())
            {
                continue;
            }

            const auto& desc = resource->GetDesc();

            // スレッドグループ数を計算
            uint32_t groupX = (desc.OutputWidth + ThreadGroupSizeX - 1) / ThreadGroupSizeX;
            uint32_t groupY = (desc.OutputHeight + ThreadGroupSizeY - 1) / ThreadGroupSizeY;

            // DrawCommand::CreateDispatch() でコマンドを生成
            // 1マテリアルにつき1 Dispatch（全スロットを同時デコード）
            DrawCommand cmd = DrawCommand::CreateDispatch(groupX, groupY, 1);
            cmd.Pipeline = m_ComputePipeline;

            // DescriptorSet作成・バインド
            auto descriptorSet = CreateDescriptorSetForResource(resource);
            if (descriptorSet)
            {
                cmd.DescriptorSet = descriptorSet;
                cmd.DescriptorSetSlot = 0;
            }

            outCommands.push_back(cmd);
        }
    }

    RHI::DescriptorSetPtr NeuralMaterialDecoder::CreateDescriptorSetForResource(NeuralMaterialResource* resource)
    {
        if (!m_Device || !resource)
        {
            return nullptr;
        }

        const auto& desc = resource->GetDesc();
        uint32_t slotCount = static_cast<uint32_t>(desc.OutputSlots.size());

        if (slotCount > MaxOutputSlots)
        {
            NORVES_LOG_ERROR("NeuralMaterialDecoder",
                             "Output slot count (%u) exceeds max (%u)", slotCount, MaxOutputSlots);
            return nullptr;
        }

        // DescriptorSetレイアウト:
        //   binding 0:   StorageBuffer  (MLP重みデータ) - readonly
        //   binding 1～N: StorageTexture (出力テクスチャ) - writeonly（スロット別）
        RHI::DescriptorSetDesc dsDesc;
        dsDesc.bindings.resize(1 + slotCount);

        // binding 0: 重みバッファ
        dsDesc.bindings[0].binding = 0;
        dsDesc.bindings[0].type = RHI::ResourceBindType::RWBuffer;
        dsDesc.bindings[0].stages = RHI::ShaderStage::Compute;

        // binding 1～N: 出力テクスチャ（スロット別）
        for (uint32_t i = 0; i < slotCount; ++i)
        {
            dsDesc.bindings[1 + i].binding = 1 + i;
            dsDesc.bindings[1 + i].type = RHI::ResourceBindType::RWTexture;
            dsDesc.bindings[1 + i].stages = RHI::ShaderStage::Compute;
        }

        auto descriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!descriptorSet)
        {
            NORVES_LOG_ERROR("NeuralMaterialDecoder", "Failed to create descriptor set");
            return nullptr;
        }

        // 重みバッファをバインド
        auto weightBuffer = resource->GetWeightBuffer();
        if (weightBuffer)
        {
            size_t bufferSize = resource->CalculateWeightBufferSize();
            descriptorSet->BindStorageBuffer(0, weightBuffer, 0, static_cast<uint32_t>(bufferSize));
        }

        // 出力テクスチャをスロット別にバインド
        for (uint32_t i = 0; i < slotCount; ++i)
        {
            auto outputTexture = resource->GetOutputTexture(i);
            if (outputTexture)
            {
                descriptorSet->BindStorageTexture(1 + i, outputTexture);
            }
        }

        descriptorSet->Update();
        return descriptorSet;
    }

} // namespace NorvesLib::Core::Rendering
