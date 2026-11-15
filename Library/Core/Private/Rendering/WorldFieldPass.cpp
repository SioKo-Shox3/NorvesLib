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

        RHI::DescriptorSetDesc MakeWorldFieldDrynessDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::RWTexture, RHI::ResourceBindType::RWTexture,
                RHI::ResourceBindType::ConstantBuffer, RHI::ResourceBindType::ConstantBuffer};
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
        else
        {
            try
            {
                const RHI::DescriptorSetDesc layoutDesc = MakeWorldFieldDrynessDescriptorSetDesc();
                m_DrynessShader = context.ShaderMgr->LoadShader("WorldField/WorldFieldDryness.comp", RHI::ShaderStage::Compute);
                m_DrynessDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(layoutDesc);
                if (m_DrynessShader && m_DrynessDescriptorSetLayout)
                {
                    RHI::ComputePipelineDesc pipelineDesc;
                    pipelineDesc.computeShader = m_DrynessShader;
                    pipelineDesc.descriptorSetLayouts.push_back(layoutDesc);
                    m_DrynessPipeline = m_Device->CreateComputePipeline(pipelineDesc);
                    m_bDrynessReady = m_DrynessPipeline && EnsureDrynessTextures();
                }
            }
            catch (const std::exception&)
            {
                m_bDrynessReady = false;
            }
            if (!m_bDrynessReady)
            {
                m_DrynessTextures[0].reset();
                m_DrynessTextures[1].reset();
                m_DrynessPipeline.reset();
                m_DrynessShader.reset();
                m_DrynessDescriptorSetLayout.reset();
                NORVES_LOG_WARNING("WorldFieldPass", "枯れ度フィールドの資源を用意できないため、枯れ度の更新を無効にします");
            }
        }
        m_bInitialized = true;
        return true;
    }

    void WorldFieldPass::Shutdown()
    {
        m_UpdateSet.reset();
        m_ResetSet.reset();
        m_DrynessSet.reset();
        m_DrynessTextures[0].reset();
        m_DrynessTextures[1].reset();
        m_DrynessPipeline.reset();
        m_DrynessShader.reset();
        m_DrynessDescriptorSetLayout.reset();
        m_HistoryTextures[0].reset();
        m_HistoryTextures[1].reset();
        m_UpdatePipeline.reset();
        m_UpdateShader.reset();
        m_DescriptorSetLayout.reset();
        m_Device = nullptr;
        m_HistoryWriteIndex = 0u;
        m_HistoryStates[0] = {};
        m_HistoryStates[1] = {};
        m_PreparedDryness[0] = {};
        m_PreparedDryness[1] = {};
        m_DrynessStates[0] = {};
        m_DrynessStates[1] = {};
        m_Origin = {};
        m_LastUpdateTime = 0.0;
        m_bPrepared = false;
        m_bDrynessReady = false;
        m_bDrynessPrepared = false;
        m_bInitialized = false;
    }

    void WorldFieldPass::SetSettings(const WorldFieldSettings& settings)
    {
        m_Settings = SanitizeWorldFieldSettings(settings);
    }

    WorldFieldState WorldFieldPass::GetFieldState() const
    {
        WorldFieldState state = m_HistoryStates[1u - m_HistoryWriteIndex];
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
        m_HistoryStates[0] = {};
        m_HistoryStates[1] = {};
        m_Origin = {};
        return true;
    }

    bool WorldFieldPass::EnsureDrynessTextures()
    {
        if (m_DrynessTextures[0] && m_DrynessTextures[1])
        {
            return true;
        }
        if (!m_Device)
        {
            return false;
        }
        RHI::TextureDesc desc;
        desc.Width = WorldFieldDrynessResolution;
        desc.Height = WorldFieldDrynessResolution;
        desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
        desc.DebugName = "WorldField.DrynessNear";
        RHI::TexturePtr nearTexture = m_Device->CreateTexture(desc);
        desc.DebugName = "WorldField.DrynessFar";
        RHI::TexturePtr farTexture = m_Device->CreateTexture(desc);
        if (!nearTexture || !farTexture)
        {
            return false;
        }
        m_DrynessTextures[0] = nearTexture;
        m_DrynessTextures[1] = farTexture;
        m_DrynessStates[0] = {};
        m_DrynessStates[1] = {};
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
            (m_HistoryStates[1u - m_HistoryWriteIndex].bValid && context.TotalTime == m_LastUpdateTime))
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
        const bool bReset = !m_HistoryStates[1u - m_HistoryWriteIndex].bValid || dt < 0.0 || m_LastRange != m_Settings.Range ||
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

    WorldFieldState WorldFieldPass::MakeDrynessState(const WorldFieldScrollResult& scroll, float texelSize)
    {
        WorldFieldState state;
        state.OriginXZ[0] = scroll.OriginXMeters;
        state.OriginXZ[1] = scroll.OriginZMeters;
        state.ExtentMeters = texelSize * static_cast<float>(WorldFieldDrynessResolution);
        state.TexelSizeMeters = texelSize;
        state.bValid = true;
        return state;
    }

    bool WorldFieldPass::PrepareDryness(const ViewRenderContext& context)
    {
        m_bDrynessPrepared = false;
        m_DrynessSet.reset();
        const CameraProxy* camera = context.GetActiveCamera();
        if (!m_bEnabled || !m_bDrynessReady || !m_Settings.bDrynessEnabled || !context.CommandList ||
            !context.FrameResources || !camera ||
            (context.MainCamera && camera->CameraId != context.MainCamera->CameraId))
        {
            return false;
        }

        // 履歴がないので、同じ時刻でも源とカメラを毎回評価してよい。
        const WorldFieldScrollResult nearScroll = CalculateWorldFieldScroll(WorldFieldOrigin{},
            camera->PositionX, camera->PositionZ, m_Settings.DrynessNearTexelMeters, WorldFieldDrynessResolution);
        const WorldFieldScrollResult farScroll = CalculateWorldFieldScroll(WorldFieldOrigin{},
            camera->PositionX, camera->PositionZ, m_Settings.DrynessFarTexelMeters, WorldFieldDrynessResolution);
        if (!nearScroll.Origin.bValid || !farScroll.Origin.bValid)
        {
            return false;
        }

        WorldFieldDrynessParams params;
        params.NearOriginXZ[0] = static_cast<float>(nearScroll.OriginXMeters);
        params.NearOriginXZ[1] = static_cast<float>(nearScroll.OriginZMeters);
        params.NearTexelSize = m_Settings.DrynessNearTexelMeters;
        params.FarOriginXZ[0] = static_cast<float>(farScroll.OriginXMeters);
        params.FarOriginXZ[1] = static_cast<float>(farScroll.OriginZMeters);
        params.FarTexelSize = m_Settings.DrynessFarTexelMeters;

        WorldFieldDrynessSourceBlock sourceBlock{};
        // 源の既定値 CosYaw は 1 なので、未使用部分も含めて明示的に 0 にする。
        for (WorldFieldDrynessSource& source : sourceBlock.Sources)
        {
            source.CosYaw = 0.0f;
        }
        Container::VariableArray<WorldFieldDrynessSource> sources;
        const auto* snapshot = context.SnapshotWorldFieldDrynessSources;
        if (snapshot)
        {
            params.SourceCount = SanitizeWorldFieldDrynessSources(
                Container::Span<const WorldFieldDrynessSource>(snapshot->data(), snapshot->size()), sources);
            for (uint32_t index = 0u; index < params.SourceCount; ++index)
            {
                sourceBlock.Sources[index] = sources[index];
            }
        }

        FrameResourceLane* frameResources = context.FrameResources;
        const FrameUploadAllocation paramsAllocation = frameResources->AllocateConstants(params);
        const FrameUploadAllocation sourcesAllocation = frameResources->AllocateConstants(sourceBlock);
        const FrameDescriptorAllocation setAllocation = frameResources->AllocateDescriptorSet(m_DrynessDescriptorSetLayout);
        if (!paramsAllocation.IsValid() || !sourcesAllocation.IsValid() || !setAllocation.IsValid())
        {
            return false;
        }
        setAllocation.Set->BindStorageTexture(0u, m_DrynessTextures[0]);
        setAllocation.Set->BindStorageTexture(1u, m_DrynessTextures[1]);
        setAllocation.Set->BindConstantBuffer(2u, paramsAllocation.Buffer,
            paramsAllocation.GetOffset32(), paramsAllocation.GetSize32());
        setAllocation.Set->BindConstantBuffer(3u, sourcesAllocation.Buffer,
            sourcesAllocation.GetOffset32(), sourcesAllocation.GetSize32());
        setAllocation.Set->Update();

        m_PreparedDryness[0] = MakeDrynessState(nearScroll, m_Settings.DrynessNearTexelMeters);
        m_PreparedDryness[1] = MakeDrynessState(farScroll, m_Settings.DrynessFarTexelMeters);
        m_DrynessSet = setAllocation.Set;
        m_bDrynessPrepared = true;
        return true;
    }

    WorldFieldFrameState WorldFieldPass::GetFrameState(uint32_t& currentIndex, uint32_t& previousIndex) const
    {
        currentIndex = m_bPrepared ? m_HistoryWriteIndex : 1u - m_HistoryWriteIndex;
        previousIndex = 1u - currentIndex;
        WorldFieldFrameState state;
        if (m_bEnabled && m_Settings.bEnabled)
        {
            state.Current = m_HistoryStates[currentIndex];
            state.Previous = m_HistoryStates[previousIndex];
            if (m_bPrepared)
            {
                state.Current.OriginXZ[0] = m_PreparedScroll.OriginXMeters;
                state.Current.OriginXZ[1] = m_PreparedScroll.OriginZMeters;
                state.Current.ExtentMeters = GetWorldFieldExtentMeters(m_PreparedRange);
                state.Current.TexelSizeMeters = m_PreparedConstants.TexelSize;
                state.Current.bValid = true;
                if (m_ResetSet)
                {
                    // この原点でゼロに消す前の画像にも、同じ有効な座標を付ける。
                    state.Previous = state.Current;
                }
            }
            if (!state.Previous.bValid)
            {
                // 原点不明の画像は公開せず、画像そのものも「今」を使う。
                previousIndex = currentIndex;
                state.Previous = state.Current;
            }
        }
        if (m_bEnabled && m_bDrynessReady && m_Settings.bDrynessEnabled)
        {
            // 準備した画像と、最後に記録した画像の座標を混ぜない。
            const WorldFieldState* dryness = m_bDrynessPrepared ? m_PreparedDryness : m_DrynessStates;
            state.DrynessNear = dryness[0];
            state.DrynessFar = dryness[1];
            state.DrynessNearBlendMeters = m_Settings.DrynessNearBlendMeters;
        }
        return state;
    }

    void WorldFieldPass::Setup(ViewRenderContext& context)
    {
        PrepareUpdate(context);
        try
        {
            PrepareDryness(context);
        }
        catch (const std::exception&)
        {
            m_bDrynessPrepared = false;
            NORVES_LOG_WARNING("WorldFieldPass", "枯れ度フィールドの更新資源を借りられないため、前の状態を使います");
        }
        uint32_t currentIndex;
        uint32_t previousIndex;
        context.PhysicalLighting.PublishWorldField(GetFrameState(currentIndex, previousIndex));
    }

    void WorldFieldPass::Declare(RenderGraphBuilder& builder)
    {
        const ViewRenderContext* context = builder.GetContext();
        m_bPrepared = false;
        m_bDrynessPrepared = false;
        if (!context)
        {
            return;
        }

        bool bDeclared = false;
        if (m_bEnabled && m_Settings.bEnabled && context->CommandList)
        {
            try
            {
                PrepareUpdate(*context);
            }
            catch (const std::exception&)
            {
                m_bPrepared = false;
                NORVES_LOG_WARNING("WorldFieldPass", "世界フィールドの更新資源を借りられないため、前の状態を使います");
            }
            if (m_bPrepared || m_HistoryStates[1u - m_HistoryWriteIndex].bValid)
            {
                uint32_t currentIndex;
                uint32_t previousIndex;
                GetFrameState(currentIndex, previousIndex);
                const RGResourceHandle current = builder.ImportTexture(m_HistoryTextures[currentIndex],
                    GetFieldTextureState(*context, m_HistoryTextures[currentIndex]), "WorldField.Current");
                const RGResourceHandle previous = previousIndex == currentIndex ? current :
                    builder.ImportTexture(m_HistoryTextures[previousIndex],
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
                bDeclared = true;
            }
        }

        // 曲げの設定や準備の成否に関係なく、枯れ度の層を宣言する。
        if (m_bEnabled && m_bDrynessReady && m_Settings.bDrynessEnabled && context->CommandList)
        {
            try
            {
                PrepareDryness(*context);
            }
            catch (const std::exception&)
            {
                m_bDrynessPrepared = false;
                NORVES_LOG_WARNING("WorldFieldPass", "枯れ度フィールドの更新資源を借りられないため、前の状態を使います");
            }
            if (m_bDrynessPrepared || (m_DrynessStates[0].bValid && m_DrynessStates[1].bValid))
            {
                const RGResourceHandle nearLayer = builder.ImportTexture(m_DrynessTextures[0],
                    GetFieldTextureState(*context, m_DrynessTextures[0]), "WorldField.DrynessNear");
                const RGResourceHandle farLayer = builder.ImportTexture(m_DrynessTextures[1],
                    GetFieldTextureState(*context, m_DrynessTextures[1]), "WorldField.DrynessFar");
                builder.PublishTexture(RenderGraphResourceNames::WorldFieldDrynessNear, nearLayer);
                builder.PublishTexture(RenderGraphResourceNames::WorldFieldDrynessFar, farLayer);
                if (m_bDrynessPrepared)
                {
                    builder.Write(nearLayer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
                    builder.Write(farLayer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
                }
                else
                {
                    builder.Read(nearLayer, RHI::ResourceState::ShaderResource);
                    builder.Read(farLayer, RHI::ResourceState::ShaderResource);
                }
                bDeclared = true;
            }
        }

        // Declare の登録順で、後続の消費側へ画像と座標を組にして渡す。
        uint32_t currentIndex;
        uint32_t previousIndex;
        const WorldFieldFrameState state = context->CommandList ? GetFrameState(currentIndex, previousIndex) :
            WorldFieldFrameState{};
        context->PhysicalLighting.PublishWorldField(state);
        if (bDeclared)
        {
            builder.PreserveInsertionOrder();
        }
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

        // 宣言だけのフレームは確定しない。実際に記録した画像と座標を組にして保存する。
        uint32_t currentIndex;
        uint32_t previousIndex;
        const WorldFieldFrameState state = GetFrameState(currentIndex, previousIndex);
        m_HistoryStates[currentIndex] = state.Current;
        if (m_ResetSet)
        {
            m_HistoryStates[previousIndex] = state.Previous;
        }
        m_HistoryWriteIndex = 1u - m_HistoryWriteIndex;
        m_Origin = m_PreparedScroll.Origin;
        m_LastRange = m_PreparedRange;
        m_LastUpdateTime = m_PreparedTime;
        m_bPrepared = false;
    }

    void WorldFieldPass::RecordDryness(ViewRenderContext& context)
    {
        if (!m_bDrynessPrepared || !context.CommandList || !m_DrynessSet || m_DrynessSet->IsExpired())
        {
            return;
        }
        RHI::ICommandList* commandList = context.CommandList;
        for (const RHI::TexturePtr& texture : m_DrynessTextures)
        {
            commandList->TextureBarrier(texture, RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::UnorderedAccess);
        }
        commandList->SetPipeline(m_DrynessPipeline);
        commandList->SetDescriptorSet(m_DrynessSet, 0u);
        commandList->Dispatch(WorldFieldDrynessGroupCount, WorldFieldDrynessGroupCount, 2u);
        for (const RHI::TexturePtr& texture : m_DrynessTextures)
        {
            commandList->TextureBarrier(texture, RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::ShaderResource);
        }

        // 記録できた画像と座標を組にして保存する。
        m_DrynessStates[0] = m_PreparedDryness[0];
        m_DrynessStates[1] = m_PreparedDryness[1];
        m_bDrynessPrepared = false;
    }

    void WorldFieldPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        (void)resources;
        RecordUpdate(context);
        RecordDryness(context);
    }

    void WorldFieldPass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList)
        {
            return;
        }
        // グラフを介さない呼び出しでは、グラフが張る開始時のバリアをここで補う。
        if (m_bPrepared)
        {
            for (const RHI::TexturePtr& texture : m_HistoryTextures)
            {
                context.CommandList->TextureBarrier(texture, GetFieldTextureState(context, texture),
                                                    RHI::ResourceState::UnorderedAccess);
            }
            RecordUpdate(context);
        }
        if (m_bDrynessPrepared)
        {
            for (const RHI::TexturePtr& texture : m_DrynessTextures)
            {
                context.CommandList->TextureBarrier(texture, GetFieldTextureState(context, texture),
                                                    RHI::ResourceState::UnorderedAccess);
            }
            RecordDryness(context);
        }
    }
} // 名前空間 NorvesLib::Core::Rendering
