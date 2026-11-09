#include "Rendering/WorldFieldPass.h"
#include "Logging/LogMacros.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include <cmath>
#include <exception>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        RHI::DescriptorSetDesc MakeWorldFieldDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::RWTexture, RHI::ResourceBindType::RWTexture,
                RHI::ResourceBindType::StructuredBuffer, RHI::ResourceBindType::ConstantBuffer};
            for (uint32_t binding = 0u; binding < 4u; ++binding)
            {
                RHI::DescriptorBinding entry;
                entry.binding = binding;
                entry.type = types[binding];
                entry.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(entry);
            }
            return desc;
        }

        RHI::ResourceState GetFieldTextureState(const ViewRenderContext& context,
                                                const RHI::TexturePtr& texture)
        {
            // 後続の消費側も layout を変えられるので、前回のパスの最終状態だけでは判断しない。
            switch (context.CommandList->GetTextureLayout(texture.get()))
            {
            case RHI::TextureLayout::ShaderReadOnly:
                return RHI::ResourceState::ShaderResource;
            case RHI::TextureLayout::General:
                return RHI::ResourceState::UnorderedAccess;
            case RHI::TextureLayout::TransferSource:
                return RHI::ResourceState::CopySource;
            case RHI::TextureLayout::TransferDest:
                return RHI::ResourceState::CopyDest;
            default:
                return RHI::ResourceState::Undefined;
            }
        }

        void BindWorldFieldSet(const RHI::DescriptorSetPtr& set,
                               const RHI::TexturePtr& previous, const RHI::TexturePtr& current,
                               const FrameUploadAllocation& interactors, const FrameUploadAllocation& constants)
        {
            set->BindStorageTexture(0u, previous);
            set->BindStorageTexture(1u, current);
            set->BindStorageBuffer(2u, interactors.Buffer, interactors.GetOffset32(), interactors.GetSize32());
            set->BindConstantBuffer(3u, constants.Buffer, constants.GetOffset32(), constants.GetSize32());
            set->Update();
        }
    } // 無名名前空間

    WorldFieldPass::~WorldFieldPass()
    {
        Shutdown();
    }

    bool WorldFieldPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        bool bReady = false;
        try
        {
            m_Device = context.Device;
            if (m_Device && context.ShaderMgr)
            {
                const RHI::DescriptorSetDesc layoutDesc = MakeWorldFieldDescriptorSetDesc();
                m_UpdateShader = context.ShaderMgr->LoadShader("WorldField/WorldFieldUpdate.comp", RHI::ShaderStage::Compute);
                m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(layoutDesc);
                if (m_UpdateShader && m_DescriptorSetLayout)
                {
                    RHI::ComputePipelineDesc pipelineDesc;
                    pipelineDesc.computeShader = m_UpdateShader;
                    pipelineDesc.descriptorSetLayouts.push_back(layoutDesc);
                    m_UpdatePipeline = m_Device->CreateComputePipeline(pipelineDesc);
                    bReady = m_UpdatePipeline && EnsureTextures();
                }
            }
        }
        catch (const std::exception&)
        {
            bReady = false;
        }
        if (!bReady)
        {
            Shutdown();
            m_bEnabled = false;
            NORVES_LOG_WARNING("WorldFieldPass", "世界フィールドの資源を用意できないため、パスを無効にします");
        }
        m_bInitialized = true;
        return true;
    }

    void WorldFieldPass::Shutdown()
    {
        m_UpdateSet.reset();
        m_ResetSet.reset();
        m_HistoryTextures[0].reset();
        m_HistoryTextures[1].reset();
        m_UpdatePipeline.reset();
        m_UpdateShader.reset();
        m_DescriptorSetLayout.reset();
        m_Device = nullptr;
        m_HistoryWriteIndex = 0u;
        m_FieldState = {};
        m_Origin = {};
        m_LastUpdateTime = 0.0;
        m_bPrepared = false;
        m_bInitialized = false;
    }

    void WorldFieldPass::SetSettings(const WorldFieldSettings& settings)
    {
        m_Settings = SanitizeWorldFieldSettings(settings);
    }

    WorldFieldState WorldFieldPass::GetFieldState() const
    {
        WorldFieldState state = m_FieldState;
        state.bValid = state.bValid && m_bEnabled && m_Settings.bEnabled;
        return state;
    }

    bool WorldFieldPass::EnsureTextures()
    {
        if (m_HistoryTextures[0] && m_HistoryTextures[1])
        {
            return true;
        }
        if (!m_Device)
        {
            return false;
        }
        RHI::TextureDesc desc;
        desc.Width = WorldFieldResolution;
        desc.Height = WorldFieldResolution;
        desc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
        desc.DebugName = "WorldField.History0";
        RHI::TexturePtr first = m_Device->CreateTexture(desc);
        desc.DebugName = "WorldField.History1";
        RHI::TexturePtr second = m_Device->CreateTexture(desc);
        if (!first || !second)
        {
            return false;
        }
        m_HistoryTextures[0] = first;
        m_HistoryTextures[1] = second;
        m_HistoryWriteIndex = 0u;
        m_FieldState = {};
        m_Origin = {};
        return true;
    }

    bool WorldFieldPass::PrepareUpdate(const ViewRenderContext& context)
    {
        m_bPrepared = false;
        m_UpdateSet.reset();
        m_ResetSet.reset();
        const CameraProxy* camera = context.GetActiveCamera();
        if (!m_bEnabled || !m_Settings.bEnabled || !m_UpdatePipeline || !context.CommandList ||
            !context.FrameResources || !camera || !std::isfinite(context.TotalTime) ||
            (context.MainCamera && camera->CameraId != context.MainCamera->CameraId) ||
            (m_FieldState.bValid && context.TotalTime == m_LastUpdateTime))
        {
            return false;
        }
        if (!EnsureTextures())
        {
            return false;
        }

        double centerX = camera->PositionX;
        double centerZ = camera->PositionZ;
        if (m_Settings.CenterMode == WorldFieldCenterMode::CameraForward)
        {
            const double directionX = camera->ForwardX;
            const double directionZ = camera->ForwardZ;
            const double length = std::sqrt(directionX * directionX + directionZ * directionZ);
            if (std::isfinite(length) && length > 0.0)
            {
                centerX += directionX / length * m_Settings.ForwardOffsetMeters;
                centerZ += directionZ / length * m_Settings.ForwardOffsetMeters;
            }
        }
        const float texelSize = GetWorldFieldTexelSizeMeters(m_Settings.Range);
        m_PreparedScroll = CalculateWorldFieldScroll(m_Origin, centerX, centerZ, texelSize);
        if (!m_PreparedScroll.Origin.bValid)
        {
            return false;
        }

        // GR02 の sim 時間が来たら差し替える。
        const double dt = context.TotalTime - m_LastUpdateTime;
        const bool bReset = !m_FieldState.bValid || dt < 0.0 || m_LastRange != m_Settings.Range ||
                            m_PreparedScroll.bReset;
        WorldFieldUpdateConstants constants;
        // reset では shift を使わない。巨大な移動の int64_t を int32_t へ狭めない。
        constants.Shift[0] = bReset ? 0 : static_cast<int32_t>(m_PreparedScroll.ShiftX);
        constants.Shift[1] = bReset ? 0 : static_cast<int32_t>(m_PreparedScroll.ShiftZ);
        constants.OriginXZ[0] = static_cast<float>(m_PreparedScroll.OriginXMeters);
        constants.OriginXZ[1] = static_cast<float>(m_PreparedScroll.OriginZMeters);
        constants.TexelSize = texelSize;
        constants.Decay = static_cast<float>(std::exp(-(dt > 0.0 ? dt : 0.0) / m_Settings.DecayTimeConstantSeconds));
        constants.bResetHistory = bReset ? 1u : 0u;

        Container::VariableArray<WorldFieldInteractor> interactors;
        const auto* snapshot = context.SnapshotWorldFieldInteractors;
        if (snapshot)
        {
            constants.InteractorCount = SanitizeWorldFieldInteractors(
                Container::Span<const WorldFieldInteractor>(snapshot->data(), snapshot->size()), interactors);
        }
        // 入力が 0 個でも有効な storage 範囲を束縛し、減衰とスクロールを記録する。
        const WorldFieldInteractor emptyInteractor{};
        FrameResourceLane* frameResources = context.FrameResources;
        const FrameUploadAllocation interactorAllocation = frameResources->AllocateUploadData(
            interactors.empty() ? &emptyInteractor : interactors.data(),
            (interactors.empty() ? 1u : interactors.size()) * sizeof(WorldFieldInteractor), FrameUploadUsage::Storage);
        const FrameUploadAllocation constantAllocation = frameResources->AllocateConstants(constants);
        const FrameDescriptorAllocation updateAllocation = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout);
        if (!interactorAllocation.IsValid() || !constantAllocation.IsValid() || !updateAllocation.IsValid())
        {
            return false;
        }
        const uint32_t previousIndex = 1u - m_HistoryWriteIndex;
        BindWorldFieldSet(updateAllocation.Set, m_HistoryTextures[previousIndex],
                          m_HistoryTextures[m_HistoryWriteIndex], interactorAllocation, constantAllocation);
        if (bReset)
        {
            // 「前」もゼロから公開する。初回の未初期化値や範囲変更前の痕跡を消費側へ渡さない。
            WorldFieldUpdateConstants resetConstants = constants;
            resetConstants.InteractorCount = 0u;
            const FrameUploadAllocation resetConstantAllocation = frameResources->AllocateConstants(resetConstants);
            const FrameDescriptorAllocation resetAllocation = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout);
            if (!resetConstantAllocation.IsValid() || !resetAllocation.IsValid())
            {
                return false;
            }
            BindWorldFieldSet(resetAllocation.Set, m_HistoryTextures[m_HistoryWriteIndex],
                              m_HistoryTextures[previousIndex], interactorAllocation, resetConstantAllocation);
            m_ResetSet = resetAllocation.Set;
        }
        m_UpdateSet = updateAllocation.Set;
        m_PreparedConstants = constants;
        m_PreparedRange = m_Settings.Range;
        m_PreparedTime = context.TotalTime;
        m_bPrepared = true;
        return true;
    }

    void WorldFieldPass::Setup(ViewRenderContext& context)
    {
        PrepareUpdate(context);
    }

    void WorldFieldPass::Declare(RenderGraphBuilder& builder)
    {
        const ViewRenderContext* context = builder.GetContext();
        m_bPrepared = false;
        if (!m_bEnabled || !m_Settings.bEnabled || !context || !context->CommandList)
        {
            return;
        }
        try
        {
            PrepareUpdate(*context);
        }
        catch (const std::exception&)
        {
            m_bPrepared = false;
            NORVES_LOG_WARNING("WorldFieldPass", "世界フィールドの更新資源を借りられないため、前の状態を使います");
        }
        if (!m_bPrepared && !m_FieldState.bValid)
        {
            return;
        }
        const uint32_t currentIndex = m_bPrepared ? m_HistoryWriteIndex : 1u - m_HistoryWriteIndex;
        const uint32_t previousIndex = 1u - currentIndex;
        const RGResourceHandle current = builder.ImportTexture(m_HistoryTextures[currentIndex],
            GetFieldTextureState(*context, m_HistoryTextures[currentIndex]), "WorldField.Current");
        const RGResourceHandle previous = builder.ImportTexture(m_HistoryTextures[previousIndex],
            GetFieldTextureState(*context, m_HistoryTextures[previousIndex]), "WorldField.Previous");
        builder.PublishTexture(RenderGraphResourceNames::WorldFieldCurrent, current);
        builder.PublishTexture(RenderGraphResourceNames::WorldFieldPrevious, previous);
        if (m_bPrepared)
        {
            builder.Write(current, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
            if (m_ResetSet)
            {
                builder.Write(previous, RHI::ResourceState::UnorderedAccess);
            }
            else
            {
                // readonly image2D もストレージ画像なので、読み取り時の layout は General。
                builder.Read(previous, RHI::ResourceState::UnorderedAccess);
            }
        }
        else
        {
            builder.Read(current, RHI::ResourceState::ShaderResource);
            builder.Read(previous, RHI::ResourceState::ShaderResource);
        }
        builder.PreserveInsertionOrder();
    }

    void WorldFieldPass::RecordUpdate(ViewRenderContext& context)
    {
        if (!m_bPrepared || !context.CommandList || !m_UpdateSet || m_UpdateSet->IsExpired() ||
            (m_ResetSet && m_ResetSet->IsExpired()))
        {
            return;
        }
        RHI::ICommandList* commandList = context.CommandList;
        // General のまま次のフレームに渡った画像にも、前の読み書きとの実行依存を張る。
        for (const RHI::TexturePtr& texture : m_HistoryTextures)
        {
            commandList->TextureBarrier(texture, RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::UnorderedAccess);
        }
        commandList->SetPipeline(m_UpdatePipeline);
        if (m_ResetSet)
        {
            commandList->SetDescriptorSet(m_ResetSet, 0u);
            commandList->Dispatch(WorldFieldUpdateGroupCount, WorldFieldUpdateGroupCount, 1u);
            commandList->TextureBarrier(m_HistoryTextures[1u - m_HistoryWriteIndex],
                RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        }
        commandList->SetDescriptorSet(m_UpdateSet, 0u);
        commandList->Dispatch(WorldFieldUpdateGroupCount, WorldFieldUpdateGroupCount, 1u);
        commandList->TextureBarrier(m_HistoryTextures[m_HistoryWriteIndex],
            RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);

        // 宣言だけのフレームは確定しない。記録した画像を「今」にしてから次の書き込み側へ進める。
        m_HistoryWriteIndex = 1u - m_HistoryWriteIndex;
        m_Origin = m_PreparedScroll.Origin;
        m_LastRange = m_PreparedRange;
        m_LastUpdateTime = m_PreparedTime;
        m_FieldState.OriginXZ[0] = m_PreparedScroll.OriginXMeters;
        m_FieldState.OriginXZ[1] = m_PreparedScroll.OriginZMeters;
        m_FieldState.ExtentMeters = GetWorldFieldExtentMeters(m_PreparedRange);
        m_FieldState.TexelSizeMeters = m_PreparedConstants.TexelSize;
        m_FieldState.bValid = true;
        m_bPrepared = false;
    }

    void WorldFieldPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        (void)resources;
        RecordUpdate(context);
    }

    void WorldFieldPass::Execute(ViewRenderContext& context)
    {
        if (!m_bPrepared || !context.CommandList)
        {
            return;
        }
        // グラフを介さない呼び出しでは、グラフが張る開始時のバリアをここで補う。
        for (const RHI::TexturePtr& texture : m_HistoryTextures)
        {
            context.CommandList->TextureBarrier(texture, GetFieldTextureState(context, texture),
                                                RHI::ResourceState::UnorderedAccess);
        }
        RecordUpdate(context);
    }
} // 名前空間 NorvesLib::Core::Rendering
