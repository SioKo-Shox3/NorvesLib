#include "Rendering/HiZPyramidPass.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // シェーダーの HiZParams（destSize、srcSize）と同じ並び
        constexpr uint32_t ParamsBytes = 16;

        RHI::DescriptorSetDesc MakeDescriptorSetDesc(RHI::ResourceBindType sourceType)
        {
            RHI::DescriptorSetDesc desc;

            RHI::DescriptorBinding source;
            source.binding = 0;
            source.type = sourceType;
            source.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(source);

            RHI::DescriptorBinding dest;
            dest.binding = 1;
            dest.type = RHI::ResourceBindType::RWTexture;
            dest.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(dest);

            RHI::DescriptorBinding params;
            params.binding = 2;
            params.type = RHI::ResourceBindType::ConstantBuffer;
            params.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(params);

            return desc;
        }

        RHI::PipelinePtr CreatePipeline(RHI::IDevice* device,
                                        const RHI::ShaderPtr& shader,
                                        RHI::ResourceBindType sourceType)
        {
            RHI::ComputePipelineDesc desc;
            desc.computeShader = shader;
            desc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc(sourceType));
            return device->CreateComputePipeline(desc);
        }
    } // namespace

    // ========================================
    // HiZPyramid
    // ========================================

    HiZPyramid::HiZPyramid() = default;

    HiZPyramid::~HiZPyramid()
    {
        Shutdown();
    }

    uint32_t HiZPyramid::ComputeBaseSize(uint32_t depthSize)
    {
        return depthSize == 0 ? 0 : (depthSize + 1) / 2;
    }

    uint32_t HiZPyramid::ComputeMipSize(uint32_t baseSize, uint32_t mip)
    {
        const uint32_t size = mip >= 32 ? 0 : (baseSize >> mip);
        return size < 1 ? 1 : size;
    }

    uint32_t HiZPyramid::ComputeMipCount(uint32_t baseWidth, uint32_t baseHeight)
    {
        uint32_t longest = baseWidth > baseHeight ? baseWidth : baseHeight;
        uint32_t count = 0;
        while (longest > 0)
        {
            ++count;
            longest >>= 1;
        }
        return count;
    }

    bool HiZPyramid::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }

        m_Device = device;
        m_ShaderManager = shaderManager;

        m_Mip0Shader = shaderManager->LoadShader("hiz_generate.comp", RHI::ShaderStage::Compute);
        m_DownsampleShader = shaderManager->LoadShader("hiz_downsample.comp", RHI::ShaderStage::Compute);
        if (!m_Mip0Shader || !m_DownsampleShader)
        {
            NORVES_LOG_WARNING("HiZPyramid", "HZBのシェーダーの読み込みに失敗");
            Shutdown();
            return false;
        }

        m_Mip0Pipeline = CreatePipeline(device, m_Mip0Shader, RHI::ResourceBindType::CombinedImageSampler);
        m_DownsamplePipeline = CreatePipeline(device, m_DownsampleShader, RHI::ResourceBindType::RWTexture);

        // 深度を texelFetch で読むだけなので、フィルターとアドレスは使われない（バインドのために必要）
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = device->CreateSampler(samplerDesc);

        if (!m_Mip0Pipeline || !m_DownsamplePipeline || !m_Sampler)
        {
            NORVES_LOG_WARNING("HiZPyramid", "HZBのパイプラインの作成に失敗");
            Shutdown();
            return false;
        }
        // ミップ0 の記述子セットのレイアウト（同じ形と共有する 1 つ）。引けないデバイスでは持ち主の無い形で作る
        m_Mip0DescriptorSetLayout =
            device->GetOrCreateDescriptorSetLayout(MakeDescriptorSetDesc(RHI::ResourceBindType::CombinedImageSampler));
        return true;
    }

    void HiZPyramid::ReleasePyramid()
    {
        // ミップ0 のセットは消えるピラミッドを結んでいるので、次の Build で作り直す（前のセットは手放すまで持つ）
        if (m_UnownedMip0DescriptorSet)
        {
            m_UnownedDescriptorSets.push_back(m_UnownedMip0DescriptorSet);
            m_UnownedMip0DescriptorSet.reset();
        }
        m_BoundDepth.reset();
        m_DescriptorSets.clear();
        m_ParamBuffers.clear();
        m_Pyramid.reset();
        m_DepthWidth = 0;
        m_DepthHeight = 0;
        m_Width = 0;
        m_Height = 0;
        m_MipCount = 0;
    }

    void HiZPyramid::Shutdown()
    {
        ReleasePyramid();
        m_UnownedDescriptorSets.clear();
        m_Mip0DescriptorSetLayout.reset();
        m_Sampler.reset();
        m_DownsamplePipeline.reset();
        m_Mip0Pipeline.reset();
        m_DownsampleShader.reset();
        m_Mip0Shader.reset();
        m_ShaderManager = nullptr;
        m_Device = nullptr;
    }

    bool HiZPyramid::Resize(uint32_t depthWidth, uint32_t depthHeight)
    {
        if (!m_Device || !m_Mip0Pipeline || !m_DownsamplePipeline || !m_Sampler)
        {
            return false;
        }
        if (depthWidth == 0 || depthHeight == 0)
        {
            ReleasePyramid();
            return false;
        }
        if (IsReady() && m_DepthWidth == depthWidth && m_DepthHeight == depthHeight)
        {
            return true;
        }

        ReleasePyramid();

        const uint32_t width = ComputeBaseSize(depthWidth);
        const uint32_t height = ComputeBaseSize(depthHeight);
        const uint32_t mipCount = ComputeMipCount(width, height);

        RHI::TextureDesc textureDesc;
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.MipLevels = mipCount;
        textureDesc.TextureFormat = RHI::Format::R32_FLOAT;
        textureDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
        textureDesc.DebugName = "HiZPyramid";
        RHI::TexturePtr pyramid = m_Device->CreateTexture(textureDesc);
        if (!pyramid)
        {
            NORVES_LOG_ERROR("HiZPyramid", "HZBのテクスチャの作成に失敗 (%ux%u, %u mips)", width, height, mipCount);
            return false;
        }

        Container::VariableArray<RHI::BufferPtr> paramBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> descriptorSets;
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            // 書き込み先はミップ mip の大きさ。読み込み元は、ミップ0 なら深度、それ以外は 1 つ前のミップの大きさ
            const int32_t params[4] = {
                static_cast<int32_t>(ComputeMipSize(width, mip)),
                static_cast<int32_t>(ComputeMipSize(height, mip)),
                static_cast<int32_t>(mip == 0 ? depthWidth : ComputeMipSize(width, mip - 1)),
                static_cast<int32_t>(mip == 0 ? depthHeight : ComputeMipSize(height, mip - 1))};

            RHI::BufferPtr paramBuffer = m_Device->CreateBuffer(
                RHI::BufferDesc(ParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "HiZPyramidParams"));
            // ミップ0 のセットは深度を結ぶので Build ごとに用意する（ここでは作らない）
            RHI::DescriptorSetPtr descriptorSet;
            if (mip != 0)
            {
                descriptorSet = m_Device->CreateDescriptorSet(MakeDescriptorSetDesc(RHI::ResourceBindType::RWTexture));
            }
            if (!paramBuffer || (mip != 0 && !descriptorSet))
            {
                NORVES_LOG_ERROR("HiZPyramid", "HZBのミップ%uの資源の作成に失敗", mip);
                return false;
            }
            paramBuffer->Update(params, ParamsBytes);
            if (descriptorSet)
            {
                descriptorSet->BindStorageTexture(0, pyramid, mip - 1);
                descriptorSet->BindStorageTexture(1, pyramid, mip);
                descriptorSet->BindConstantBuffer(2, paramBuffer, 0, ParamsBytes);
                descriptorSet->Update();
            }
            paramBuffers.push_back(paramBuffer);
            descriptorSets.push_back(descriptorSet);
        }

        m_Pyramid = pyramid;
        m_ParamBuffers = paramBuffers;
        m_DescriptorSets = descriptorSets;
        m_DepthWidth = depthWidth;
        m_DepthHeight = depthHeight;
        m_Width = width;
        m_Height = height;
        m_MipCount = mipCount;
        NORVES_LOG_INFO("HiZPyramid", "HZB作成 (%ux%u, %u mips, 深度 %ux%u)", width, height, mipCount, depthWidth, depthHeight);
        return true;
    }

    bool HiZPyramid::Build(RHI::ICommandList* commandList,
                           const RHI::TexturePtr& depthTexture,
                           FrameResourceLane* frameResources)
    {
        if (!commandList || !depthTexture)
        {
            return false;
        }
        if (!Resize(depthTexture->GetWidth(), depthTexture->GetHeight()))
        {
            return false;
        }

        // ミップ0 の記述子セット（深度・ミップ0・作成時に書いた定数を結ぶ）。持ち主があれば毎回借り、
        // 無ければ深度が替わったときだけ使い回さないセットを作る（記録済みのセットを書き換えない）
        RHI::DescriptorSetPtr mip0DescriptorSet;
        if (frameResources && m_Mip0DescriptorSetLayout)
        {
            mip0DescriptorSet = frameResources->AllocateDescriptorSet(m_Mip0DescriptorSetLayout).Set;
            if (mip0DescriptorSet)
            {
                mip0DescriptorSet->BindTexture(0, depthTexture);
                mip0DescriptorSet->BindSampler(0, m_Sampler);
                mip0DescriptorSet->BindStorageTexture(1, m_Pyramid, 0);
                mip0DescriptorSet->BindConstantBuffer(2, m_ParamBuffers[0], 0, ParamsBytes);
                mip0DescriptorSet->Update();
            }
        }
        else
        {
            if (!m_UnownedMip0DescriptorSet || m_BoundDepth.get() != depthTexture.get())
            {
                RHI::DescriptorSetPtr descriptorSet =
                    m_Device->CreateDescriptorSet(MakeDescriptorSetDesc(RHI::ResourceBindType::CombinedImageSampler));
                if (descriptorSet)
                {
                    descriptorSet->BindTexture(0, depthTexture);
                    descriptorSet->BindSampler(0, m_Sampler);
                    descriptorSet->BindStorageTexture(1, m_Pyramid, 0);
                    descriptorSet->BindConstantBuffer(2, m_ParamBuffers[0], 0, ParamsBytes);
                    descriptorSet->Update();
                }
                if (m_UnownedMip0DescriptorSet)
                {
                    m_UnownedDescriptorSets.push_back(m_UnownedMip0DescriptorSet);
                }
                m_UnownedMip0DescriptorSet = descriptorSet;
                m_BoundDepth = descriptorSet ? depthTexture : RHI::TexturePtr{};
            }
            mip0DescriptorSet = m_UnownedMip0DescriptorSet;
        }
        if (!mip0DescriptorSet)
        {
            NORVES_LOG_ERROR("HiZPyramid", "ミップ0 の記述子セットを用意できないため、このフレームは HZB を作りません");
            return false;
        }

        // 描いたばかりの深度の書き込みを、この Compute の読み取りから見えるようにする。
        // 描画パスの出力依存（EXTERNAL への依存）の宛先は Fragment 段までで Compute 段を含まないので、
        // 宛先に Fragment 段・ShaderRead を含むその依存に、Fragment 段・ShaderRead から Compute 段・ShaderRead への
        // バリアを連ねて、深度書き込み → Compute 読み取りの依存の連鎖を作る（レイアウトは ShaderResource のまま）。
        commandList->TextureBarrier(depthTexture,
                                    RHI::ResourceState::ShaderResource,
                                    RHI::ResourceState::ShaderResource,
                                    0, 0, 0, 0);

        // 全ミップを書き込める状態へ（前のフレームの内容は使わない）
        commandList->TextureBarrier(m_Pyramid,
                                    RHI::ResourceState::Undefined,
                                    RHI::ResourceState::UnorderedAccess,
                                    0, 0, m_MipCount, 0);

        for (uint32_t mip = 0; mip < m_MipCount; ++mip)
        {
            if (mip == 0)
            {
                commandList->SetPipeline(m_Mip0Pipeline);
            }
            else
            {
                // 1 つ前のミップの書き込みが終わってから読む（レイアウトは GENERAL のまま）
                commandList->TextureBarrier(m_Pyramid,
                                            RHI::ResourceState::UnorderedAccess,
                                            RHI::ResourceState::UnorderedAccess,
                                            mip - 1, 0, 1, 0);
                commandList->SetPipeline(m_DownsamplePipeline);
            }
            commandList->SetDescriptorSet(mip == 0 ? mip0DescriptorSet : m_DescriptorSets[mip], 0);
            commandList->Dispatch((ComputeMipSize(m_Width, mip) + 7) / 8,
                                  (ComputeMipSize(m_Height, mip) + 7) / 8,
                                  1);
        }

        commandList->TextureBarrier(m_Pyramid,
                                    RHI::ResourceState::UnorderedAccess,
                                    RHI::ResourceState::ShaderResource,
                                    0, 0, m_MipCount, 0);
        return true;
    }

    // ========================================
    // HiZPyramidPass
    // ========================================

    HiZPyramidPass::HiZPyramidPass() = default;

    HiZPyramidPass::~HiZPyramidPass()
    {
        Shutdown();
    }

    void HiZPyramidPass::Shutdown()
    {
        m_Pyramid.Shutdown();
        m_bInitialized = false;
        m_bBuilt = false;
    }

    void HiZPyramidPass::Declare(RenderGraphBuilder& builder)
    {
        m_DepthHandle = {};
        m_CompleteHandle = {};

        // 深度は読むだけ。GBuffer 深度の Load/Store（MegaGeometry が Load で描く）の宣言には触れない。
        if (!builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth,
                                    m_DepthHandle,
                                    RHI::ResourceState::ShaderResource))
        {
            m_DepthHandle = {};
            return;
        }

        m_CompleteHandle = builder.CreateLogical("HiZPyramidComplete");
        builder.Write(m_CompleteHandle, RHI::ResourceState::Common, RHI::ResourceState::Common);
        builder.PreserveInsertionOrder();
    }

    void HiZPyramidPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_bBuilt = false;
        if (!m_DepthHandle.IsValid() || !context.Device || !context.ShaderMgr || !context.CommandList)
        {
            return;
        }

        RHI::TexturePtr depth = resources.GetTexture(m_DepthHandle);
        if (!depth)
        {
            return;
        }

        if (!m_bInitialized)
        {
            m_bInitialized = m_Pyramid.Initialize(context.Device, context.ShaderMgr);
            if (!m_bInitialized)
            {
                return;
            }
        }

        m_bBuilt = m_Pyramid.Build(context.CommandList, depth, context.FrameResources);
    }

} // namespace NorvesLib::Core::Rendering
