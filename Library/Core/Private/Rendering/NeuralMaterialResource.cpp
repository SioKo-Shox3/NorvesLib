#include "Rendering/NeuralMaterialResource.h"
#include "Rendering/RenderResourceManager.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    NeuralMaterialResource::~NeuralMaterialResource()
    {
        Shutdown();
    }

    bool NeuralMaterialResource::Initialize(RHI::IDevice* device, const NeuralMaterialDesc& desc)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!device)
        {
            NORVES_LOG_ERROR("NeuralMaterialResource", "Device is null");
            return false;
        }

        if (desc.OutputWidth == 0 || desc.OutputHeight == 0)
        {
            NORVES_LOG_ERROR("NeuralMaterialResource", "Output dimensions must be > 0");
            return false;
        }

        if (desc.OutputSlots.empty())
        {
            NORVES_LOG_ERROR("NeuralMaterialResource", "No output slots defined");
            return false;
        }

        m_Device = device;
        m_Desc = desc;

        // ========================================
        // 重みバッファ作成（StorageBuffer）
        // ========================================
        size_t weightSize = CalculateWeightBufferSize();
        if (weightSize == 0)
        {
            NORVES_LOG_ERROR("NeuralMaterialResource", "Weight buffer size is 0");
            return false;
        }

        Container::String bufferName = desc.DebugName.empty()
            ? Container::String("NeuralMat_Weights")
            : desc.DebugName + "_Weights";

        RHI::BufferDesc bufferDesc(
            static_cast<uint64_t>(weightSize),
            RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst,
            true, // HostVisible（CPU→GPUアップロード用）
            bufferName.c_str());

        m_WeightBuffer = device->CreateBuffer(bufferDesc);
        if (!m_WeightBuffer)
        {
            NORVES_LOG_ERROR("NeuralMaterialResource", "Failed to create weight buffer (%zu bytes)", weightSize);
            return false;
        }

        // ========================================
        // 出力テクスチャ群作成（スロット別StorageImage）
        // ========================================
        m_OutputTextures.resize(desc.OutputSlots.size());
        m_OutputHandles.resize(desc.OutputSlots.size());

        for (size_t i = 0; i < desc.OutputSlots.size(); ++i)
        {
            const auto& slot = desc.OutputSlots[i];

            Container::String textureName = desc.DebugName.empty()
                ? Container::String("NeuralMat_") + slot.Name
                : desc.DebugName + "_" + slot.Name;

            RHI::TextureDesc texDesc;
            texDesc.Width = desc.OutputWidth;
            texDesc.Height = desc.OutputHeight;
            texDesc.TextureFormat = slot.TextureFormat;
            texDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
            texDesc.DebugName = textureName.c_str();

            m_OutputTextures[i] = device->CreateTexture(texDesc);
            if (!m_OutputTextures[i])
            {
                NORVES_LOG_ERROR("NeuralMaterialResource", "Failed to create output texture '%s' (%ux%u)",
                                 slot.Name.c_str(), desc.OutputWidth, desc.OutputHeight);
                Shutdown();
                return false;
            }
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("NeuralMaterialResource", "Initialized: %s (%ux%u, %u slots, %zu weight bytes)",
                        desc.DebugName.c_str(), desc.OutputWidth, desc.OutputHeight,
                        static_cast<uint32_t>(desc.OutputSlots.size()), weightSize);
        return true;
    }

    bool NeuralMaterialResource::RegisterOutputTextures(RenderResourceManager& resourceManager)
    {
        if (!m_bInitialized)
        {
            NORVES_LOG_ERROR("NeuralMaterialResource", "Not initialized");
            return false;
        }

        for (size_t i = 0; i < m_OutputTextures.size(); ++i)
        {
            if (!m_OutputTextures[i])
            {
                continue;
            }

            m_OutputHandles[i] = resourceManager.RegisterExternalTexture(
                m_OutputTextures[i],
                m_Desc.OutputSlots[i].Name);
        }

        return true;
    }

    bool NeuralMaterialResource::UploadWeights(const void* weightData, size_t dataSize)
    {
        if (!m_bInitialized || !m_WeightBuffer)
        {
            NORVES_LOG_ERROR("NeuralMaterialResource", "Not initialized");
            return false;
        }

        size_t expectedSize = CalculateWeightBufferSize();
        if (dataSize != expectedSize)
        {
            NORVES_LOG_WARNING("NeuralMaterialResource",
                               "Weight data size mismatch: expected %zu, got %zu", expectedSize, dataSize);
        }

        size_t uploadSize = (dataSize < expectedSize) ? dataSize : expectedSize;
        m_WeightBuffer->Update(weightData, uploadSize);
        return true;
    }

    void NeuralMaterialResource::Shutdown()
    {
        m_WeightBuffer.reset();
        m_OutputTextures.clear();
        m_OutputHandles.clear();
        m_Device = nullptr;
        m_bInitialized = false;
    }

    RHI::TexturePtr NeuralMaterialResource::GetOutputTexture(uint32_t slotIndex) const
    {
        if (slotIndex < m_OutputTextures.size())
        {
            return m_OutputTextures[slotIndex];
        }
        return nullptr;
    }

    TextureHandle NeuralMaterialResource::GetOutputTextureHandle(uint32_t slotIndex) const
    {
        if (slotIndex < m_OutputHandles.size())
        {
            return m_OutputHandles[slotIndex];
        }
        return TextureHandle{};
    }

    size_t NeuralMaterialResource::CalculateWeightBufferSize() const
    {
        // MLP重みサイズ = Σ (layer_in * layer_out + layer_out) * sizeof(float16)
        // 構成: Input → Hidden[0] → Hidden[1] → ... → Hidden[N-1] → Output(全スロット結合)

        uint32_t totalOutputChannels = m_Desc.GetTotalOutputChannels();
        if (totalOutputChannels == 0)
        {
            return 0;
        }

        size_t totalParams = 0;

        // 入力層 → 最初の隠れ層
        totalParams += static_cast<size_t>(m_Desc.InputChannels) * m_Desc.HiddenWidth + m_Desc.HiddenWidth;

        // 隠れ層間
        for (uint32_t i = 1; i < m_Desc.HiddenLayers; ++i)
        {
            totalParams += static_cast<size_t>(m_Desc.HiddenWidth) * m_Desc.HiddenWidth + m_Desc.HiddenWidth;
        }

        // 最後の隠れ層 → 出力層（全スロットの合計チャンネル数）
        totalParams += static_cast<size_t>(m_Desc.HiddenWidth) * totalOutputChannels + totalOutputChannels;

        // float16 (2 bytes per parameter)
        return totalParams * 2;
    }

} // namespace NorvesLib::Core::Rendering
