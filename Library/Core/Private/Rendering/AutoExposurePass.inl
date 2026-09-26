// 自動露出の測定パス（AutoExposurePass）の実装。Coreの登録済みのSceneView.cppから取り込む。
#include "Rendering/AutoExposurePass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

namespace NorvesLib::Core::Rendering
{
    namespace AutoExposurePassDetail
    {
        struct alignas(16) GPUAutoExposureParams
        {
            // x: ヒストグラムの下端（log2 cd/m²）、y: log2 輝度1あたりの区間の数、z: プリエクスポージャの逆数
            float RangeAndScale[4];
            // xy: 画像の寸法
            uint32_t ImageSize[4];
        };
        static_assert(sizeof(GPUAutoExposureParams) == 32u);

        constexpr uint32_t WorkgroupSize = 16u;
        constexpr uint64_t HistogramByteSize =
            static_cast<uint64_t>(AutoExposureHistogramBinCount) * sizeof(uint32_t);
        // 最初の測定と、その後この数ごとにログへ出す
        constexpr uint64_t LogIntervalMeasurements = 30u;

        RHI::DescriptorSetDesc CreateHistogramDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::CombinedImageSampler,
                RHI::ResourceBindType::RWBuffer,
                RHI::ResourceBindType::ConstantBuffer};
            for (uint32_t bindingIndex = 0u; bindingIndex < 3u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        // SceneColor にプリエクスポージャが掛かっている描画モードだけ、その逆数で絶対輝度へ戻す
        // （ForwardPass の bApplySceneColorPreExposure と同じ条件）。
        float ResolveInversePreExposure(const ViewRenderContext& context)
        {
            const CameraProxy* camera = context.GetActiveCamera();
            if (camera == nullptr)
            {
                return 1.0f;
            }
            const DebugViewMode debugMode = context.GetActiveDebugMode();
            const uint32_t debugModeValue = static_cast<uint32_t>(debugMode);
            const bool bPreExposed = debugMode == DebugViewMode::Normal ||
                                     debugModeValue == 252u ||
                                     debugModeValue == 253u ||
                                     debugModeValue == 254u;
            if (!bPreExposed)
            {
                return 1.0f;
            }
            const float preExposure = camera->PreExposure;
            if (!std::isfinite(preExposure) || preExposure <= 0.0f)
            {
                return 1.0f;
            }
            return 1.0f / std::clamp(preExposure, 1.0e-6f, 1.0e6f);
        }
    } // namespace AutoExposurePassDetail

    AutoExposurePass::AutoExposurePass(const AutoExposureSettings& settings)
        : m_Settings(settings)
    {
    }

    AutoExposurePass::~AutoExposurePass()
    {
        Shutdown();
    }

    bool AutoExposurePass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        m_Device = context.Device;
        // パイプラインは最初の Execute で作る。作れなかったときは測定だけを止め、描画は続ける。
        m_bInitialized = true;
        return true;
    }

    void AutoExposurePass::Shutdown()
    {
        m_FrameSlots.clear();
        m_HistogramBuffer.reset();
        m_HistogramState = RHI::ResourceState::Undefined;
        m_PointSampler.reset();
        m_Pipeline.reset();
        m_ComputeShader.reset();
        m_Device = nullptr;
        m_Adaptation = AutoExposureAdaptationState{};
        m_LatestMeasurement = AutoExposureMeasurement{};
        m_LastAdaptationTime = 0.0;
        m_ConsumedMeasurementCount = 0u;
        m_bUnavailable = false;
        m_bInitialized = false;
    }

    void AutoExposurePass::Setup(ViewRenderContext& context)
    {
        (void)context;
    }

    void AutoExposurePass::Execute(ViewRenderContext& context)
    {
        // RenderGraph を通らない旧経路では測らない
        (void)context;
    }

    void AutoExposurePass::Declare(RenderGraphBuilder& builder)
    {
        m_InputSceneColorHandle = {};
        RGTextureHandle sceneColorHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::SSRSceneColor,
                                   sceneColorHandle,
                                   RHI::ResourceState::ShaderResource) ||
            builder.TryReadTexture(RenderGraphResourceNames::SceneColor,
                                   sceneColorHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputSceneColorHandle = sceneColorHandle.ToResourceHandle();
        }
    }

    void AutoExposurePass::DisableAfterFailure(const char* reason)
    {
        if (!m_bUnavailable)
        {
            NORVES_LOG_ERROR("AutoExposurePass", "自動露出の測定を止めます: %s", reason);
        }
        m_bUnavailable = true;
        m_FrameSlots.clear();
        m_HistogramBuffer.reset();
        m_HistogramState = RHI::ResourceState::Undefined;
        m_Pipeline.reset();
    }

    bool AutoExposurePass::EnsurePipeline(ViewRenderContext& context)
    {
        if (m_bUnavailable)
        {
            return false;
        }
        if (m_Pipeline && m_PointSampler && m_HistogramBuffer)
        {
            return true;
        }
        if (context.Device == nullptr || context.ShaderMgr == nullptr)
        {
            return false;
        }
        m_Device = context.Device;

        try
        {
            if (!m_ComputeShader)
            {
                m_ComputeShader = context.ShaderMgr->LoadShader("auto_exposure_histogram.comp",
                                                                RHI::ShaderStage::Compute);
                if (!m_ComputeShader)
                {
                    DisableAfterFailure("ヒストグラムのシェーダーを読み込めません");
                    return false;
                }
            }

            if (!m_Pipeline)
            {
                RHI::ComputePipelineDesc pipelineDesc;
                pipelineDesc.computeShader = m_ComputeShader;
                pipelineDesc.descriptorSetLayouts.push_back(
                    AutoExposurePassDetail::CreateHistogramDescriptorSetDesc());
                m_Pipeline = context.Device->CreateComputePipeline(pipelineDesc);
                if (!m_Pipeline)
                {
                    DisableAfterFailure("ヒストグラムのパイプラインを作れません");
                    return false;
                }
            }

            if (!m_PointSampler)
            {
                RHI::SamplerDesc samplerDesc;
                samplerDesc.filterMin = RHI::FilterMode::Point;
                samplerDesc.filterMag = RHI::FilterMode::Point;
                samplerDesc.filterMip = RHI::FilterMode::Point;
                samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
                samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
                samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
                m_PointSampler = context.Device->CreateSampler(samplerDesc);
                if (!m_PointSampler)
                {
                    DisableAfterFailure("SceneColor のサンプラーを作れません");
                    return false;
                }
            }

            if (!m_HistogramBuffer)
            {
                RHI::BufferDesc histogramDesc;
                histogramDesc.Size = AutoExposurePassDetail::HistogramByteSize;
                histogramDesc.Usage = RHI::ResourceUsage::StorageBuffer |
                                      RHI::ResourceUsage::TransferSrc |
                                      RHI::ResourceUsage::TransferDst;
                histogramDesc.DebugName = "AutoExposure.Histogram";
                m_HistogramBuffer = context.Device->CreateBuffer(histogramDesc);
                if (!m_HistogramBuffer)
                {
                    DisableAfterFailure("ヒストグラムのバッファを作れません");
                    return false;
                }
                m_HistogramState = RHI::ResourceState::Undefined;
            }
        }
        catch (const std::exception& exception)
        {
            DisableAfterFailure(exception.what());
            return false;
        }
        catch (...)
        {
            DisableAfterFailure("不明な例外");
            return false;
        }
        return true;
    }

    AutoExposurePass::FrameSlot* AutoExposurePass::EnsureFrameSlot(uint32_t frameIndex)
    {
        if (frameIndex >= m_FrameSlots.size())
        {
            m_FrameSlots.resize(static_cast<size_t>(frameIndex) + 1u);
        }
        FrameSlot& slot = m_FrameSlots[frameIndex];
        try
        {
            if (!slot.ReadbackBuffer)
            {
                RHI::BufferDesc readbackDesc(AutoExposurePassDetail::HistogramByteSize,
                                             RHI::ResourceUsage::TransferDst,
                                             true,
                                             "AutoExposure.HistogramReadback");
                slot.ReadbackBuffer = m_Device->CreateBuffer(readbackDesc);
            }
            if (!slot.ParamsBuffer)
            {
                RHI::BufferDesc paramsDesc(sizeof(AutoExposurePassDetail::GPUAutoExposureParams),
                                           RHI::ResourceUsage::ConstantBuffer,
                                           true,
                                           "AutoExposure.Params");
                slot.ParamsBuffer = m_Device->CreateBuffer(paramsDesc);
            }
            if (!slot.DescriptorSet)
            {
                slot.DescriptorSet = m_Device->CreateDescriptorSet(
                    AutoExposurePassDetail::CreateHistogramDescriptorSetDesc());
            }
        }
        catch (const std::exception& exception)
        {
            DisableAfterFailure(exception.what());
            return nullptr;
        }
        catch (...)
        {
            DisableAfterFailure("不明な例外");
            return nullptr;
        }
        if (!slot.ReadbackBuffer || !slot.ParamsBuffer || !slot.DescriptorSet)
        {
            DisableAfterFailure("フレームスロットの資源を作れません");
            return nullptr;
        }
        return &slot;
    }

    void AutoExposurePass::ConsumeCompletedSlot(FrameSlot& slot)
    {
        if (!slot.bPending)
        {
            return;
        }
        slot.bPending = false;

        uint32_t bins[AutoExposureHistogramBinCount] = {};
        void* mapped = nullptr;
        try
        {
            mapped = slot.ReadbackBuffer->Map(0u, AutoExposurePassDetail::HistogramByteSize);
        }
        catch (...)
        {
            mapped = nullptr;
        }
        if (mapped == nullptr)
        {
            NORVES_LOG_WARNING("AutoExposurePass", "ヒストグラムの読み戻しを写像できません（frame=%llu）",
                            static_cast<unsigned long long>(slot.FrameNumber));
            return;
        }
        std::memcpy(bins, mapped, sizeof(bins));
        slot.ReadbackBuffer->Unmap();

        const AutoExposureHistogramResult result =
            ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, m_Settings);
        // 数えた画素の総数が記録した画像の画素数と違う中身（記録後に提出されなかった間の古い中身など）は使わない
        if (!result.bValid || result.TotalCount != slot.PixelCount)
        {
            NORVES_LOG_WARNING("AutoExposurePass",
                            "ヒストグラムの画素数が合わないので捨てます（frame=%llu 数えた数=%llu 画素数=%llu）",
                            static_cast<unsigned long long>(slot.FrameNumber),
                            static_cast<unsigned long long>(result.TotalCount),
                            static_cast<unsigned long long>(slot.PixelCount));
            return;
        }

        const float deltaSeconds = m_Adaptation.bValid
                                       ? static_cast<float>(slot.TotalTime - m_LastAdaptationTime)
                                       : 0.0f;
        UpdateAutoExposureAdaptation(m_Adaptation, result, deltaSeconds, m_Settings);
        m_LastAdaptationTime = slot.TotalTime;

        m_LatestMeasurement.FrameNumber = slot.FrameNumber;
        m_LatestMeasurement.PixelCount = result.TotalCount;
        m_LatestMeasurement.AverageLog2Luminance = result.AverageLog2Luminance;
        m_LatestMeasurement.TargetEV100 = result.TargetEV100;
        m_LatestMeasurement.AdaptedEV100 = m_Adaptation.EV100;
        m_LatestMeasurement.bValid = true;

        if (m_ConsumedMeasurementCount % AutoExposurePassDetail::LogIntervalMeasurements == 0u)
        {
            NORVES_LOG_INFO("AutoExposurePass",
                            "自動露出の測定 frame=%llu pixels=%llu avg_log2_luminance=%.3f target_ev100=%.3f adapted_ev100=%.3f",
                            static_cast<unsigned long long>(slot.FrameNumber),
                            static_cast<unsigned long long>(result.TotalCount),
                            result.AverageLog2Luminance,
                            result.TargetEV100,
                            m_Adaptation.EV100);
        }
        ++m_ConsumedMeasurementCount;
    }

    void AutoExposurePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        if (!context.CommandList || !m_InputSceneColorHandle.IsValid() || !EnsurePipeline(context))
        {
            return;
        }

        FrameSlot* slot = EnsureFrameSlot(context.FrameIndex);
        if (slot == nullptr)
        {
            return;
        }
        // このスロットを前に使った提出は、スワップチェーンの待機で完了している
        ConsumeCompletedSlot(*slot);

        const RHI::TexturePtr sceneColor = resources.GetTexture(m_InputSceneColorHandle);
        if (!sceneColor)
        {
            return;
        }
        const uint32_t width = sceneColor->GetWidth();
        const uint32_t height = sceneColor->GetHeight();
        if (width == 0u || height == 0u)
        {
            return;
        }

        AutoExposurePassDetail::GPUAutoExposureParams params = {};
        params.RangeAndScale[0] = AutoExposureHistogramMinLog2Luminance;
        params.RangeAndScale[1] = AutoExposureHistogramBinsPerLog2;
        params.RangeAndScale[2] = AutoExposurePassDetail::ResolveInversePreExposure(context);
        params.ImageSize[0] = width;
        params.ImageSize[1] = height;

        try
        {
            slot->ParamsBuffer->Update(&params, sizeof(params));
            slot->DescriptorSet->BindTexture(0u, sceneColor);
            slot->DescriptorSet->BindSampler(0u, m_PointSampler);
            slot->DescriptorSet->BindStorageBuffer(
                1u, m_HistogramBuffer, 0u, static_cast<uint32_t>(AutoExposurePassDetail::HistogramByteSize));
            slot->DescriptorSet->BindConstantBuffer(
                2u, slot->ParamsBuffer, 0u, static_cast<uint32_t>(sizeof(params)));
            slot->DescriptorSet->Update();
        }
        catch (const std::exception& exception)
        {
            DisableAfterFailure(exception.what());
            return;
        }
        catch (...)
        {
            DisableAfterFailure("不明な例外");
            return;
        }

        RHI::ICommandList* commandList = context.CommandList;
        commandList->BufferBarrier(m_HistogramBuffer, m_HistogramState, RHI::ResourceState::CopyDest,
                                   0u, AutoExposurePassDetail::HistogramByteSize);
        commandList->FillBuffer(m_HistogramBuffer, 0u, AutoExposurePassDetail::HistogramByteSize, 0u);
        commandList->BufferBarrier(m_HistogramBuffer, RHI::ResourceState::CopyDest,
                                   RHI::ResourceState::UnorderedAccess,
                                   0u, AutoExposurePassDetail::HistogramByteSize);
        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(slot->DescriptorSet);
        commandList->Dispatch((width + AutoExposurePassDetail::WorkgroupSize - 1u) / AutoExposurePassDetail::WorkgroupSize,
                              (height + AutoExposurePassDetail::WorkgroupSize - 1u) / AutoExposurePassDetail::WorkgroupSize,
                              1u);
        commandList->BufferBarrier(m_HistogramBuffer, RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::CopySource,
                                   0u, AutoExposurePassDetail::HistogramByteSize);
        commandList->CopyBuffer(m_HistogramBuffer, slot->ReadbackBuffer, AutoExposurePassDetail::HistogramByteSize);
        m_HistogramState = RHI::ResourceState::CopySource;

        slot->FrameNumber = context.FrameNumber;
        slot->TotalTime = context.TotalTime;
        slot->PixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        slot->bPending = true;
    }

} // namespace NorvesLib::Core::Rendering
