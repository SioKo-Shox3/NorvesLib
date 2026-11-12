// test は GBuffer 段の深度の試しで深度の有無を直接数える。早期の深度試験を持つ FS では discard が深度を止めず、色だけでは確かめられない。
// skinned と Hash は背景と区別できる固定の発光値の書き込みで被覆を数える。
#include "AlphaClipScenario.h"

#include "FileStream/FileStream.h"
#include "Math/MatrixUtils.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderExtension/RenderExtension.h"
#include "Rendering/RenderExtension/RenderExtensionData.h"
#include "Rendering/RenderExtension/RenderExtensionHelpers.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Thread/Mutex.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        struct PixelRect
        {
            int32_t Left;
            int32_t Top;
            int32_t Right;
            int32_t Bottom;
        };

        struct PlateProjection
        {
            double CenterX;
            double CenterY;
            double HalfExtent;
        };

        PlateProjection ProjectPlate(const AlphaClipPlateSpec& plate, const Core::Rendering::CameraProxy& camera)
        {
            const double focal = ValidationHeight * 0.5 / std::tan(camera.FieldOfView * 3.141592653589793 / 360.0);
            const double scale = focal / camera.PositionZ;
            return {ValidationWidth * 0.5 + scale * plate.X,
                    ValidationHeight * 0.5 - scale * plate.Y, scale * plate.HalfExtent};
        }

        bool MakeRect(const PlateProjection& plate, double extentFraction, PixelRect& rect)
        {
            const double extent = plate.HalfExtent * extentFraction;
            // ピクセル中心が領域内に入る範囲を、右端・下端を含まない整数区間にする。
            rect = {static_cast<int32_t>(std::ceil(plate.CenterX - extent - 0.5)),
                    static_cast<int32_t>(std::ceil(plate.CenterY - extent - 0.5)),
                    static_cast<int32_t>(std::ceil(plate.CenterX + extent - 0.5)),
                    static_cast<int32_t>(std::ceil(plate.CenterY + extent - 0.5))};
            return rect.Left >= 0 && rect.Top >= 0 && rect.Right <= static_cast<int32_t>(ValidationWidth) &&
                   rect.Bottom <= static_cast<int32_t>(ValidationHeight) &&
                   rect.Left < rect.Right && rect.Top < rect.Bottom;
        }
        using namespace Core::Rendering;
        namespace Container = Core::Container;
        namespace RHI = NorvesLib::RHI;
        constexpr size_t HashFrameElements = static_cast<size_t>(ValidationWidth) * ValidationHeight * 4u;
        constexpr uint64_t HashFrameBytes = HashFrameElements * sizeof(uint16_t);
        struct HashPhase { uint32_t Value; };
        constexpr RenderExtensionDataKey<HashPhase> HashPhaseKey{
            Core::Identity::Literal("Test.AlphaClip.Phase", sizeof("Test.AlphaClip.Phase") - 1)};

        struct DepthProbeParams
        {
            float Emissive[4];
            float Depth[4];
        };
        constexpr RenderExtensionDataKey<DepthProbeParams> DepthProbeKey{
            Core::Identity::Literal("Test.AlphaClip.DepthProbe", sizeof("Test.AlphaClip.DepthProbe") - 1)};

        bool BuildDepthProbe(const CameraProxy& camera, DepthProbeParams& params, float& plateDepth)
        {
            const float aspect = static_cast<float>(ValidationWidth) / static_cast<float>(ValidationHeight);
            // 列ベクトル規約で板と 0.25 m 奥の点を写す。ビューの -Z を投影の +Z へ直す補正は
            // CameraViewConstants::BuildCullingViewProjectionMatrix と Vulkan の描画時と同じ。Y の反転は深度に影響しない。
            const auto clipSpaceCorrection = Math::MatrixUtils::CreateScale(1.0f, 1.0f, -1.0f);
            const auto viewProjection = CameraViewConstants::BuildProjectionMatrix(camera, aspect) *
                                        clipSpaceCorrection *
                                        CameraViewConstants::BuildViewMatrix(camera);
            const auto plateClip = viewProjection * Math::Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            const auto probeClip = viewProjection * Math::Vector4(0.0f, 0.0f, -0.25f, 1.0f);
            plateDepth = plateClip.z / plateClip.w;
            params = {{0.0f, AlphaClipEmissiveNits * camera.PreExposure, 0.0f, 1.0f},
                      {probeClip.z / probeClip.w, 0.0f, 0.0f, 0.0f}};
            return std::isfinite(plateDepth) && std::isfinite(params.Depth[0]) &&
                   0.0f < plateDepth && plateDepth < params.Depth[0] && params.Depth[0] < 1.0f;
        }

        // 板より奥の深度で試しの色を描き、GBuffer の深度が空の画素だけを識別する。
        class DepthProbePass final : public IViewPass, public IRenderGraphPass
        {
        public:
            ~DepthProbePass() override
            {
                Shutdown();
            }

            const char* GetName() const override
            {
                return "Ext.Test.AlphaClip.DepthProbe";
            }

            bool Initialize(ViewRenderContext& context) override
            {
                m_Device = context.Device;
                Container::String root(NORVES_SOURCE_ROOT "/Test/Core/Rendering/Shaders");
                if (!m_Device || !m_Shaders.Initialize(m_Device, root))
                {
                    return false;
                }
                m_GBuffer.Initialize(m_Device);
                m_Vertex = m_Shaders.LoadShader("alpha_clip_depth_probe.vert", RHI::ShaderStage::Vertex);
                m_Fragment = m_Shaders.LoadShader("alpha_clip_depth_probe.frag", RHI::ShaderStage::Pixel);
                RHI::DescriptorBinding binding;
                binding.binding = 0;
                binding.type = RHI::ResourceBindType::ConstantBuffer;
                binding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
                m_DescriptorDesc.bindings.push_back(binding);
                m_Layout = m_Device->GetOrCreateDescriptorSetLayout(m_DescriptorDesc);
                m_bInitialized = m_Vertex && m_Fragment && m_Layout;
                return m_bInitialized;
            }

            void Shutdown() override
            {
                m_GBuffer.Reset();
                m_Pipeline.reset();
                m_RenderPass.reset();
                m_Layout.reset();
                m_Vertex.reset();
                m_Fragment.reset();
                m_DescriptorDesc.bindings.clear();
                m_Shaders.Shutdown();
                m_Device = nullptr;
                m_bInitialized = false;
            }

            void Setup(ViewRenderContext&) override {}
            void Execute(ViewRenderContext&) override {}

            void Declare(RenderGraphBuilder& builder) override
            {
                m_Target = {};
                if (m_GBuffer.Declare(builder))
                {
                    builder.TryGetTexture(RenderGraphResourceNames::GBufferEmissive, m_Target);
                }
                builder.PreserveInsertionOrder();
            }

            void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                const auto* params = context.FindExtension(DepthProbeKey);
                if (!params || !m_bInitialized || !context.FrameResources || !context.CommandList || !m_Target.IsValid())
                {
                    // データや描画先が欠けたフレームは描かず、試しの色の欠落を判定に残す。
                    return;
                }
                const auto target = resources.GetTexture(m_Target);
                if (!target || !PrepareGBuffer(resources))
                {
                    return;
                }
                const auto allocation = context.FrameResources->AllocateConstants(*params);
                const auto set = context.FrameResources->AllocateDescriptorSet(m_Layout).Set;
                if (!allocation.IsValid() || !set)
                {
                    return;
                }
                set->BindConstantBuffer(0, allocation.Buffer, allocation.GetOffset32(), sizeof(*params));
                set->Update();
                RHI::Viewport viewport;
                viewport.width = static_cast<float>(target->GetWidth());
                viewport.height = static_cast<float>(target->GetHeight());
                RHI::ScissorRect scissor;
                scissor.right = target->GetWidth();
                scissor.bottom = target->GetHeight();
                auto* commands = context.CommandList;
                commands->BeginRenderPass(m_RenderPass, m_GBuffer.GetFramebuffer());
                commands->SetViewport(viewport);
                commands->SetScissor(scissor);
                commands->SetPipeline(m_Pipeline);
                commands->SetDescriptorSet(set);
                commands->Draw(3);
                commands->EndRenderPass();
            }

        private:
            RHI::PipelinePtr CreatePipeline()
            {
                RHI::GraphicsPipelineDesc pipeline;
                pipeline.vertexShader = m_Vertex;
                pipeline.pixelShader = m_Fragment;
                pipeline.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
                pipeline.rasterState.cullMode = RHI::CullMode::None;
                pipeline.depthStencilState.depthTestEnable = true;
                pipeline.depthStencilState.depthWriteEnable = false;
                pipeline.depthStencilState.depthCompareOp = RHI::CompareOp::Less;
                for (uint32_t index = 0u; index < 5u; ++index)
                {
                    RHI::BlendAttachmentDesc blend;
                    blend.blendEnable = false;
                    blend.colorWriteMask = index == 3u ? RHI::ColorWriteMask::All : RHI::ColorWriteMask::None;
                    pipeline.blendState.attachments.push_back(blend);
                }
                pipeline.renderPass = m_RenderPass;
                pipeline.descriptorSetLayouts.push_back(m_DescriptorDesc);
                return m_Device->CreateGraphicsPipeline(pipeline);
            }

            bool PrepareGBuffer(RenderGraphResources& resources)
            {
                if (!m_GBuffer.Prepare(resources))
                {
                    return false;
                }
                if (m_RenderPass != m_GBuffer.GetRenderPass())
                {
                    m_RenderPass = m_GBuffer.GetRenderPass();
                    m_Pipeline = CreatePipeline();
                }
                return static_cast<bool>(m_Pipeline);
            }

            RHI::IDevice* m_Device = nullptr;
            ShaderManager m_Shaders;
            GBufferTargetBinder m_GBuffer;
            RHI::ShaderPtr m_Vertex;
            RHI::ShaderPtr m_Fragment;
            RHI::DescriptorSetDesc m_DescriptorDesc;
            RHI::DescriptorSetLayoutPtr m_Layout;
            RHI::RenderPassPtr m_RenderPass;
            RHI::PipelinePtr m_Pipeline;
            RGTextureHandle m_Target;
            bool m_bInitialized = false;
        };

        Container::TUniquePtr<IViewPass> CreateDepthProbePass(void*)
        {
            return Container::MakeUnique<DepthProbePass>();
        }

        struct HashEvidence
        {
            Thread::Mutex Mutex;
            Container::VariableArray<uint16_t> Images;
            Container::VariableArray<uint8_t> Received;
            uint32_t FrameCount = 0u;
            uint32_t Count = 0u;
            uint32_t TaaAppliedCount = 0u;
            uint64_t FirstFrame = 0u;
            uint64_t LastFrame = 0u;
            bool bExpectTaa = false;
            const char* Failure = nullptr;
        };

        void SetHashFailure(const Container::TSharedPtr<HashEvidence>& evidence, const char* reason)
        {
            Thread::ScopedLock lock(evidence->Mutex);
            if (!evidence->Failure)
            {
                evidence->Failure = reason;
            }
        }

        // GBuffer の生成後に生の発光をフレームの持ち主から借りた領域へ連続して写す。
        class HashReadbackPass final : public IViewPass, public IRenderGraphPass, public IFrameReadbackReceiver
        {
        public:
            explicit HashReadbackPass(const Container::TSharedPtr<HashEvidence>& evidence)
                : m_Evidence(evidence), m_Receiver(this) {}
            ~HashReadbackPass() override { Shutdown(); }
            const char* GetName() const override { return "Ext.Test.AlphaClip.Readback"; }
            bool Initialize(ViewRenderContext&) override { return true; }
            void Shutdown() override { m_Receiver.CancelPending(); m_Pending.clear(); }
            void Setup(ViewRenderContext&) override {}
            void Execute(ViewRenderContext&) override {}

            void Declare(RenderGraphBuilder& builder) override
            {
                m_Target = {};
                const auto* context = builder.GetContext();
                const auto* phase = context ? context->FindExtension(HashPhaseKey) : nullptr;
                if (phase && phase->Value == 1u &&
                    !builder.TryReadTexture(RenderGraphResourceNames::GBufferEmissive, m_Target,
                                            RHI::ResourceState::ShaderResource))
                {
                    SetHashFailure(m_Evidence, "GBuffer の発光を読み戻し元に指定できません");
                }
                builder.PreserveInsertionOrder();
            }

            void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                const auto* phase = context.FindExtension(HashPhaseKey);
                if (!phase || phase->Value != 1u || !context.bDeterministicCapture)
                {
                    return;
                }
                if (context.bTemporalEpochStart && context.TemporalFrameIndex == 0u)
                {
                    m_bStarted = true;
                    m_NextFrame = 0u;
                }
                if (!m_bStarted || m_NextFrame >= m_Evidence->FrameCount)
                {
                    return;
                }
                if (context.TemporalFrameIndex != m_NextFrame ||
                    (m_NextFrame > 0u && context.FrameNumber != m_PreviousFrame + 1u))
                {
                    SetHashFailure(m_Evidence, "連続フレームの番号が飛びました");
                    m_bStarted = false;
                    return;
                }
                if (context.bTemporalAAApplied != m_Evidence->bExpectTaa)
                {
                    SetHashFailure(m_Evidence, "TAA の適用状態が指定と一致しません");
                    m_bStarted = false;
                    return;
                }
                const auto target = m_Target.IsValid() ? resources.GetTexture(m_Target) : RHI::TexturePtr{};
                if (!target || target->GetWidth() != ValidationWidth || target->GetHeight() != ValidationHeight ||
                    target->GetFormat() != RHI::Format::R16G16B16A16_FLOAT ||
                    !context.FrameResources || !context.CommandList)
                {
                    SetHashFailure(m_Evidence, "GBuffer の発光または読み戻し元の寸法が不正です");
                    m_bStarted = false;
                    return;
                }
                FrameReadbackRequest request;
                request.Receiver = &m_Receiver;
                request.Size = HashFrameBytes;
                const auto readback = context.FrameResources->RequestReadback(request);
                if (!readback.IsValid())
                {
                    SetHashFailure(m_Evidence, "連続フレームの読み戻し先を借りられません");
                    m_bStarted = false;
                    return;
                }
                {
                    Thread::ScopedLock lock(m_Evidence->Mutex);
                    if (m_NextFrame == 0u)
                    {
                        m_Evidence->FirstFrame = context.FrameNumber;
                    }
                    m_Evidence->LastFrame = context.FrameNumber;
                    m_Evidence->TaaAppliedCount += context.bTemporalAAApplied ? 1u : 0u;
                }
                m_PreviousFrame = context.FrameNumber;
                m_Pending.push_back({readback.RequestId, m_NextFrame++});
                auto* commands = context.CommandList;
                commands->TextureBarrier(target, RHI::ResourceState::ShaderResource, RHI::ResourceState::CopySource);
                commands->BufferBarrier(readback.Buffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
                commands->CopyTextureToBuffer(target, readback.Buffer, ValidationWidth, ValidationHeight, readback.Offset);
                commands->BufferBarrier(readback.Buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::HostRead);
                commands->TextureBarrier(target, RHI::ResourceState::CopySource, RHI::ResourceState::ShaderResource);
            }

            void OnFrameReadback(const FrameReadbackResult& result) override
            {
                for (size_t index = 0u; index < m_Pending.size(); ++index)
                {
                    if (m_Pending[index].RequestId != result.RequestId)
                    {
                        continue;
                    }
                    const uint32_t frame = m_Pending[index].Frame;
                    m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(index));
                    Thread::ScopedLock lock(m_Evidence->Mutex);
                    if (result.Status != FrameReadbackStatus::Delivered || !result.Data ||
                        result.Size != HashFrameBytes || m_Evidence->Received[frame])
                    {
                        m_Evidence->Failure = "連続フレームの読み戻しが失敗または重複しました";
                        return;
                    }
                    std::memcpy(m_Evidence->Images.data() + frame * HashFrameElements,
                                result.Data, static_cast<size_t>(HashFrameBytes));
                    m_Evidence->Received[frame] = 1u;
                    ++m_Evidence->Count;
                    return;
                }
                SetHashFailure(m_Evidence, "対応する読み戻し要求がありません");
            }

        private:
            struct Pending { uint64_t RequestId; uint32_t Frame; };
            Container::TSharedPtr<HashEvidence> m_Evidence;
            FrameReadbackReceiver m_Receiver;
            Container::VariableArray<Pending> m_Pending;
            RGTextureHandle m_Target;
            uint64_t m_PreviousFrame = 0u;
            uint32_t m_NextFrame = 0u;
            bool m_bStarted = false;
        };

        Container::TUniquePtr<IViewPass> CreateHashReadbackPass(void* context)
        {
            return Container::MakeUnique<HashReadbackPass>(*static_cast<Container::TSharedPtr<HashEvidence>*>(context));
        }
    } // namespace

    struct AlphaClipScenario::State
    {
        Container::TSharedPtr<HashEvidence> Evidence = Container::MakeShared<HashEvidence>();
        DepthProbeParams Probe{};
        float PlateDepth = 0.0f;
        bool bDepthValid = false;
    };

    AlphaClipScenario::AlphaClipScenario() : m_State(Container::MakeUnique<State>()) {}
    AlphaClipScenario::~AlphaClipScenario() = default;

    bool AlphaClipScenario::IsHash() const
    {
        return m_Kind == AlphaClipFixtureKind::HashTaa || m_Kind == AlphaClipFixtureKind::HashNoTaa;
    }

    bool AlphaClipScenario::ParseArgument(const Core::Container::String& argument, Core::Container::String& reason)
    {
        if (m_bEnabled)
        {
            reason = TEXT("切り抜きのシナリオが重複しています");
            return false;
        }
        if (argument == TEXT("--alpha-clip-scenario=test"))
        {
            m_Kind = AlphaClipFixtureKind::Test;
        }
        else if (argument == TEXT("--alpha-clip-scenario=two-sided"))
        {
            m_Kind = AlphaClipFixtureKind::TwoSided;
        }
        else if (argument == TEXT("--alpha-clip-scenario=opaque"))
        {
            m_Kind = AlphaClipFixtureKind::Opaque;
        }
        else if (argument == TEXT("--alpha-clip-scenario=skinned"))
        {
            m_Kind = AlphaClipFixtureKind::Skinned;
        }
        else if (argument == TEXT("--alpha-clip-scenario=hash-taa"))
        {
            m_Kind = AlphaClipFixtureKind::HashTaa;
        }
        else if (argument == TEXT("--alpha-clip-scenario=hash-no-taa"))
        {
            m_Kind = AlphaClipFixtureKind::HashNoTaa;
        }
        else
        {
            reason = TEXT("切り抜きのシナリオ名が不正です");
            return false;
        }
        m_bEnabled = true;
        return true;
    }

    bool AlphaClipScenario::IsEnabled() const
    {
        return m_bEnabled;
    }

    Core::Rendering::FrameCaptureSourceKind AlphaClipScenario::GetCaptureSource() const
    {
        return m_Kind == AlphaClipFixtureKind::TwoSided ? Core::Rendering::FrameCaptureSourceKind::BackBuffer :
                                                       Core::Rendering::FrameCaptureSourceKind::GBufferEmissive;
    }

    bool AlphaClipScenario::LoadThresholds()
    {
        using namespace Core::Container;
        String path(NORVES_SOURCE_ROOT "/Test/Core/Rendering/Thresholds/RenderingValidation/AlphaClip.tsv");
        auto stream = FileStream::FileStream::CreateUnique(path, FileStream::FileMode::Read, FileStream::FileAccess::Read);
        if (!stream || stream->GetSize() < 4 || stream->GetSize() > 4096)
        {
            return false;
        }
        const size_t size = static_cast<size_t>(stream->GetSize());
        VariableArray<char> text(size + 1u, '\0');
        if (stream->Read(text.data(), size) != size)
        {
            return false;
        }
        char* cursor = text.data();
        if (std::memcmp(cursor, "\xEF\xBB\xBF", 3u) == 0)
        {
            cursor += 3;
        }
        auto takeLine = [&cursor]() -> char*
        {
            if (*cursor == '\0')
            {
                return nullptr;
            }
            char* line = cursor;
            while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n')
            {
                ++cursor;
            }
            if (*cursor == '\r')
            {
                *cursor++ = '\0';
            }
            if (*cursor == '\n')
            {
                *cursor++ = '\0';
            }
            return line;
        };
        char* header = takeLine();
        if (header == nullptr || std::strcmp(header, reinterpret_cast<const char*>(u8"名前\t値\t根拠")) != 0)
        {
            return false;
        }
        struct Entry
        {
            const char* Name;
            double* Value;
            bool bRead;
        };
        Entry entries[] = {
            {"coverage_target", &m_CoverageTarget, false},
            {"coverage_tolerance", &m_CoverageTolerance, false},
            {"emissive_tolerance", &m_EmissiveTolerance, false},
            {"maximum_luma_delta", &m_MaximumLumaDelta, false},
            {"minimum_background_luma_gain", &m_MinimumBackgroundLumaGain, false},
            {"minimum_texel_pixels", &m_MinimumTexelPixels, false},
            {"hash_coverage_tolerance", &m_HashCoverageTolerance, false},
            {"hash_minimum_changed_fraction", &m_HashMinimumChangedFraction, false},
            {"hash_frame_count", &m_HashFrameCount, false}};
        while (char* line = takeLine())
        {
            char* value = std::strchr(line, '\t');
            if (value == nullptr)
            {
                return false;
            }
            *value++ = '\0';
            char* rationale = std::strchr(value, '\t');
            if (rationale == nullptr || rationale[1] == '\0')
            {
                return false;
            }
            *rationale = '\0';
            char* end = nullptr;
            const double number = std::strtod(value, &end);
            if (end == value || *end != '\0' || !std::isfinite(number) || number <= 0.0)
            {
                return false;
            }
            bool bFound = false;
            for (auto& entry : entries)
            {
                if (std::strcmp(entry.Name, line) == 0)
                {
                    if (entry.bRead)
                    {
                        return false;
                    }
                    *entry.Value = number;
                    entry.bRead = bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                return false;
            }
        }
        for (const auto& entry : entries)
        {
            if (!entry.bRead)
            {
                return false;
            }
        }
        return m_CoverageTarget < 1.0 && m_CoverageTolerance < m_CoverageTarget &&
               m_EmissiveTolerance < 1.0 && m_MaximumLumaDelta < 1.0 && m_MinimumBackgroundLumaGain < 1.0 &&
               m_HashCoverageTolerance < 1.0 && m_HashMinimumChangedFraction < 1.0 && m_HashFrameCount == 16.0;
    }

    bool AlphaClipScenario::Initialize(Core::Rendering::RenderWorld& world, const RenderingValidationSceneFixture& fixture)
    {
        if (!LoadThresholds() || !fixture.ApplyAlphaClipFixture(m_Kind))
        {
            LOG_ERROR("切り抜きのしきい値または試験用シーンを準備できませんでした");
            return false;
        }
        m_Fixture = &fixture;
        if (m_Kind == AlphaClipFixtureKind::Test)
        {
            RenderExtensionDesc probe;
            probe.Name = "Ext.Test.AlphaClip.DepthProbe";
            probe.Stage = RenderExtensionStage::GBuffer;
            if (!world.RegisterRenderExtension(probe, {CreateDepthProbePass, nullptr}))
            {
                LOG_ERROR("切り抜きの深度を調べるパスを登録できませんでした");
                return false;
            }
        }
        if (IsHash())
        {
            auto& evidence = *m_State->Evidence;
            evidence.FrameCount = static_cast<uint32_t>(m_HashFrameCount);
            evidence.bExpectTaa = m_Kind == AlphaClipFixtureKind::HashTaa;
            evidence.Images.resize(static_cast<size_t>(evidence.FrameCount) * HashFrameElements);
            evidence.Received.resize(evidence.FrameCount, 0u);
            world.SetDeterministicCapture(true);
            RenderExtensionDesc readback;
            readback.Name = "Ext.Test.AlphaClip.Readback";
            readback.Stage = RenderExtensionStage::AfterLighting;
            if (!world.RegisterRenderExtension(readback, {CreateHashReadbackPass, &m_State->Evidence}))
            {
                LOG_ERROR("切り抜きの連続読み戻しパスを登録できませんでした");
                return false;
            }
        }
        return true;
    }

    void AlphaClipScenario::ApplyState(Core::Rendering::RenderWorld& world)
    {
        auto camera = m_Kind == AlphaClipFixtureKind::Opaque ? m_Fixture->GetCamera() :
                                                              m_Fixture->GetShadingModelGBufferCamera();
        if (m_Kind == AlphaClipFixtureKind::HashTaa)
        {
            camera.AntiAliasing = CameraAntiAliasingMode::TemporalAA;
            camera.ExposureMode = CameraExposureMode::Manual;
        }
        world.SetMainCamera(camera);
        if (m_Kind == AlphaClipFixtureKind::Test)
        {
            m_State->bDepthValid = BuildDepthProbe(camera, m_State->Probe, m_State->PlateDepth);
            if (m_State->bDepthValid)
            {
                world.SetExtensionData(DepthProbeKey, m_State->Probe);
            }
        }
        if (IsHash())
        {
            world.SetExtensionData(HashPhaseKey, HashPhase{m_HashPhase});
            if (m_HashPhase == 1u && !m_bHashEpochStarted)
            {
                world.BeginDeterministicEpoch();
                m_bHashEpochStarted = true;
            }
        }
        auto sky = Core::Rendering::MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = false;
        world.SetSkyAtmosphere(sky);
        auto fog = Core::Rendering::MakeDefaultVolumetricFogParameters();
        fog.bEnabled = false;
        world.SetVolumetricFogParameters(fog);
    }

    void AlphaClipScenario::Advance()
    {
        if (IsHash() && m_HashPhase == 0u)
        {
            m_HashPhase = 1u;
        }
        else if (IsHash() && m_bHashComplete)
        {
            m_HashPhase = 2u;
        }
    }

    bool AlphaClipScenario::RequestFollowup(Core::Rendering::FrameCaptureRequest& request) const
    {
        if (!IsHash() || m_HashPhase >= 2u)
        {
            return false;
        }
        request.SourceKind = FrameCaptureSourceKind::GBufferEmissive;
        return true;
    }

    bool AlphaClipScenario::EvaluateHash(Core::Container::String& reason)
    {
        auto& evidence = *m_State->Evidence;
        Thread::ScopedLock lock(evidence.Mutex);
        if (evidence.Failure)
        {
            reason = Container::String(evidence.Failure);
            return false;
        }
        if (evidence.Count < evidence.FrameCount)
        {
            return true;
        }
        const auto camera = m_Fixture->GetShadingModelGBufferCamera();
        const auto plate = ProjectPlate(AlphaClipHashPlate, camera);
        PixelRect rect;
        if (!MakeRect(plate, 7.0 / 8.0, rect))
        {
            reason = TEXT("Hash の検査領域が画像の範囲外です");
            return false;
        }
        const double expectedEmissive = AlphaClipEmissiveNits * camera.PreExposure;
        uint64_t coveredTotal = 0u;
        uint64_t changedPixels = 0u;
        uint64_t invalidPixels = 0u;
        uint32_t maxCovered = 0u;
        Container::VariableArray<uint32_t> frameCovered(evidence.FrameCount, 0u);
        for (int32_t y = rect.Top; y < rect.Bottom; ++y)
        {
            for (int32_t x = rect.Left; x < rect.Right; ++x)
            {
                uint32_t pixelCovered = 0u;
                for (uint32_t frame = 0u; frame < evidence.FrameCount; ++frame)
                {
                    const size_t offset = static_cast<size_t>(frame) * HashFrameElements +
                        (static_cast<size_t>(y) * ValidationWidth + x) * 4u;
                    bool bCovered = true;
                    bool bClear = true;
                    bool bFinite = true;
                    for (uint32_t channel = 0u; channel < 4u; ++channel)
                    {
                        const double value = DecodeIeee754Binary16(evidence.Images[offset + channel]);
                        bFinite = bFinite && std::isfinite(value);
                        if (channel < 3u)
                        {
                            bCovered = bCovered && std::abs(value - expectedEmissive) <= m_EmissiveTolerance;
                            bClear = bClear && std::abs(value) <= m_EmissiveTolerance;
                        }
                    }
                    invalidPixels += (!bFinite || (!bCovered && !bClear)) ? 1u : 0u;
                    pixelCovered += bCovered ? 1u : 0u;
                    frameCovered[frame] += bCovered ? 1u : 0u;
                }
                coveredTotal += pixelCovered;
                changedPixels += pixelCovered > 0u && pixelCovered < evidence.FrameCount ? 1u : 0u;
            }
        }
        for (uint32_t count : frameCovered)
        {
            maxCovered = count > maxCovered ? count : maxCovered;
        }
        const uint32_t pixels = static_cast<uint32_t>((rect.Right - rect.Left) * (rect.Bottom - rect.Top));
        const double coverage = static_cast<double>(coveredTotal) / (pixels * evidence.FrameCount);
        const double changed = static_cast<double>(changedPixels) / pixels;
        const double maxFrameCoverage = static_cast<double>(maxCovered) / pixels;
        const double alpha = static_cast<double>(AlphaClipHashAlpha) / 255.0;
        const bool bTaa = m_Kind == AlphaClipFixtureKind::HashTaa;
        const bool bPassed = invalidPixels == 0u &&
            (bTaa ? std::abs(coverage - alpha) <= m_HashCoverageTolerance &&
                    changed >= m_HashMinimumChangedFraction && evidence.TaaAppliedCount == evidence.FrameCount :
                    maxCovered == 0u && evidence.TaaAppliedCount == 0u);
        LOG_INFO("Hash の判定 ALPHA_CLIP_RESULT scenario=%s frames=%u first_frame=%llu last_frame=%llu taa_applied_frames=%u coverage=%g target=%g changed_fraction=%g max_frame_coverage=%g invalid_pixels=%llu result=%s",
                 bTaa ? "hash-taa" : "hash-no-taa", evidence.FrameCount,
                 static_cast<unsigned long long>(evidence.FirstFrame), static_cast<unsigned long long>(evidence.LastFrame),
                 evidence.TaaAppliedCount, coverage, alpha, changed, maxFrameCoverage,
                 static_cast<unsigned long long>(invalidPixels), bPassed ? "PASS" : "FAIL");
        m_bHashComplete = bPassed;
        if (!bPassed)
        {
            reason = TEXT("Hash の平均被覆率、位相の変化または TAA 無しの被覆が基準を満たしません");
        }
        return bPassed;
    }

    bool AlphaClipScenario::Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Container::String& reason)
    {
        if (!m_Fixture || !frame.IsSuccess() || frame.Width != ValidationWidth || frame.Height != ValidationHeight)
        {
            reason = TEXT("切り抜きの検証画像を取得できませんでした");
            return false;
        }
        if (m_Kind == AlphaClipFixtureKind::Opaque)
        {
            LOG_INFO("切り抜きの判定 ALPHA_CLIP_RESULT scenario=opaque captured=1 result=PASS");
            return true;
        }
        if (IsHash())
        {
            return m_HashPhase == 0u || EvaluateHash(reason);
        }
        const auto camera = m_Fixture->GetShadingModelGBufferCamera();
        if (m_Kind == AlphaClipFixtureKind::TwoSided)
        {
            const bool bBgra = frame.Format == RHI::Format::B8G8R8A8_UNORM || frame.Format == RHI::Format::B8G8R8A8_SRGB;
            const bool bRgba = frame.Format == RHI::Format::R8G8B8A8_UNORM || frame.Format == RHI::Format::R8G8B8A8_SRGB;
            const bool bHardware = RHI::IsPresentationSrgbFormat(frame.Format);
            if ((!bBgra && !bRgba) || frame.BytesPerPixel != 4u || frame.RowPitchBytes < frame.Width * 4u ||
                frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height ||
                frame.ColorSpace != RHI::PresentationColorSpace::Rec709D65 ||
                frame.Transfer != RHI::PresentationTransfer::SRGB ||
                frame.bHardwareSrgbEncode != bHardware || frame.bShaderSrgbEncode == bHardware)
            {
                reason = TEXT("両面の輝度を調べる sRGB 画像の形式が不正です");
                return false;
            }
            auto sampleLuma = [&frame, bBgra](const PixelRect& rect)
            {
                double sum = 0.0;
                for (int32_t y = rect.Top; y < rect.Bottom; ++y)
                {
                    for (int32_t x = rect.Left; x < rect.Right; ++x)
                    {
                        const size_t offset = static_cast<size_t>(y) * frame.RowPitchBytes + static_cast<size_t>(x) * 4u;
                        sum += (0.2126 * frame.Pixels[offset + (bBgra ? 2u : 0u)] +
                                0.7152 * frame.Pixels[offset + 1u] +
                                0.0722 * frame.Pixels[offset + (bBgra ? 0u : 2u)]) / 255.0;
                    }
                }
                return sum / ((rect.Right - rect.Left) * (rect.Bottom - rect.Top));
            };
            double luma[2]{};
            double backgroundLuma[2]{};
            double lumaGain[2]{};
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                const auto plate = ProjectPlate(AlphaClipLightingPlates[index], camera);
                PixelRect center;
                PixelRect background;
                // 板の中央半分と、その真上の、板を含まない同じ大きさの背景を比べる。
                auto above = plate;
                above.CenterY -= 2.0 * plate.HalfExtent;
                if (!MakeRect(plate, 0.5, center) || !MakeRect(above, 0.5, background))
                {
                    reason = TEXT("両面の検査領域が画像の範囲外です");
                    return false;
                }
                luma[index] = sampleLuma(center);
                backgroundLuma[index] = sampleLuma(background);
                lumaGain[index] = luma[index] - backgroundLuma[index];
            }
            const double delta = std::abs(luma[0] - luma[1]);
            // 板は Z だけを向き、光の送り先は -Z。板から光へ向かう L は -direction = +Z。
            // 表は N・L=+1。裏は反転前なら -1 で直接光が 0、反転後なら +1 で表と同じ明るさになる。
            // 下の内積と実測の輝度・背景からの増分を同じ行に残し、暗い板同士の一致を不合格にする。
            const double frontNdotL = AlphaClipPlateNormalZ[0] * -AlphaClipLightingDirection[2];
            const double backUnflippedNdotL = AlphaClipPlateNormalZ[1] * -AlphaClipLightingDirection[2];
            const double backNdotL = -backUnflippedNdotL;
            const bool bPassed = delta <= m_MaximumLumaDelta && lumaGain[0] >= m_MinimumBackgroundLumaGain &&
                                 lumaGain[1] >= m_MinimumBackgroundLumaGain &&
                                 frontNdotL > 0.0 && backNdotL > 0.0 && backUnflippedNdotL < 0.0;
            LOG_INFO("両面の判定 ALPHA_CLIP_RESULT scenario=two-sided front_luma=%g back_luma=%g luma_delta=%g front_background_luma=%g back_background_luma=%g front_luma_gain=%g back_luma_gain=%g front_ndotl=%g back_ndotl=%g back_unflipped_ndotl=%g direct_lux=%g result=%s",
                     luma[0], luma[1], delta, backgroundLuma[0], backgroundLuma[1], lumaGain[0], lumaGain[1],
                     frontNdotL, backNdotL, backUnflippedNdotL, AlphaClipLightingLux, bPassed ? "PASS" : "FAIL");
            if (!bPassed)
            {
                reason = TEXT("両面の表裏の輝度差・背景からの明るさの増分または光の向きが基準を満たしません");
            }
            return bPassed;
        }

        RgbaFloatImage image;
        if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
        {
            reason = TEXT("切り抜きの発光画像の形式が不正です");
            return false;
        }
        const double expectedEmissive = AlphaClipEmissiveNits * camera.PreExposure;
        bool bFinite = true;
        auto covered = [&](int32_t x, int32_t y)
        {
            const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
            bool bCovered = true;
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                const double value = image.Values[offset + channel];
                bFinite = bFinite && std::isfinite(value);
                if (channel < 3u)
                {
                    bCovered = bCovered && std::isfinite(value) && std::abs(value - expectedEmissive) <= m_EmissiveTolerance;
                }
            }
            return bCovered;
        };
        if (m_Kind == AlphaClipFixtureKind::Skinned)
        {
            double plateCoverage[2]{};
            uint32_t matches[2]{};
            double texelPixels = 0.0;
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                const auto plate = ProjectPlate(AlphaClipSkinnedPlates[index], camera);
                texelPixels = plate.HalfExtent * 2.0 / 8.0;
                PixelRect rect;
                if (!MakeRect(plate, 7.0 / 8.0, rect))
                {
                    reason = TEXT("スキンの市松の検査領域が画像の範囲外です");
                    return false;
                }
                uint32_t count = 0u;
                uint32_t total = 0u;
                for (int32_t y = rect.Top; y < rect.Bottom; ++y)
                {
                    for (int32_t x = rect.Left; x < rect.Right; ++x)
                    {
                        count += covered(x, y) ? 1u : 0u;
                        ++total;
                    }
                }
                plateCoverage[index] = static_cast<double>(count) / total;
                for (uint32_t y = 0u; y < 8u; ++y)
                {
                    for (uint32_t x = 0u; x < 8u; ++x)
                    {
                        const int32_t px = static_cast<int32_t>(std::floor(plate.CenterX + plate.HalfExtent * ((x + 0.5) / 4.0 - 1.0)));
                        const int32_t py = static_cast<int32_t>(std::floor(plate.CenterY - plate.HalfExtent * ((y + 0.5) / 4.0 - 1.0)));
                        if (px < 0 || py < 0 || px >= static_cast<int32_t>(image.Width) || py >= static_cast<int32_t>(image.Height))
                        {
                            reason = TEXT("スキンのテクセル中心が画像の範囲外です");
                            return false;
                        }
                        matches[index] += covered(px, py) == ((x + y) % 2u == 0u) ? 1u : 0u;
                    }
                }
            }
            const double delta = std::abs(plateCoverage[0] - plateCoverage[1]);
            const bool bPassed = bFinite && texelPixels >= m_MinimumTexelPixels &&
                std::abs(plateCoverage[0] - m_CoverageTarget) <= m_CoverageTolerance &&
                std::abs(plateCoverage[1] - m_CoverageTarget) <= m_CoverageTolerance &&
                delta <= m_CoverageTolerance && matches[0] == 64u && matches[1] == 64u;
            LOG_INFO("スキンの判定 ALPHA_CLIP_RESULT scenario=skinned static_coverage=%g skinned_coverage=%g coverage_delta=%g static_texel_match=%u/64 skinned_texel_match=%u/64 texel_pixels=%g finite=%d result=%s",
                     plateCoverage[0], plateCoverage[1], delta, matches[0], matches[1], texelPixels,
                     bFinite ? 1 : 0, bPassed ? "PASS" : "FAIL");
            if (!bPassed)
            {
                reason = TEXT("スキンと静的な市松の被覆またはテクセル中心が一致しません");
            }
            return bPassed;
        }
        enum class DepthPixelKind { Plate, Probe, Invalid };
        auto classifyDepth = [&](int32_t x, int32_t y)
        {
            const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
            bool bPlate = true;
            bool bProbe = true;
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                const double value = image.Values[offset + channel];
                if (!std::isfinite(value))
                {
                    bFinite = false;
                    return DepthPixelKind::Invalid;
                }
                if (channel < 3u)
                {
                    bPlate = bPlate && std::abs(value - expectedEmissive) <= m_EmissiveTolerance;
                    bProbe = bProbe && std::abs(value - m_State->Probe.Emissive[channel]) <= m_EmissiveTolerance;
                }
            }
            return bPlate ? DepthPixelKind::Plate : bProbe ? DepthPixelKind::Probe : DepthPixelKind::Invalid;
        };
        double coverage[3]{};
        uint32_t counts[3]{};
        uint32_t totals[3]{};
        uint32_t probeCounts[3]{};
        uint32_t invalidPixels = 0u;
        const auto checker = ProjectPlate(AlphaClipCoveragePlates[0], camera);
        const double texelPixels = checker.HalfExtent * 2.0 / 8.0;
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            PixelRect rect;
            // 市松は外周から半テクセル内側、裏向きの板は外周のラスタ境界を除いた内側全体。
            const auto plate = ProjectPlate(AlphaClipCoveragePlates[index], camera);
            const double innerFraction = index == 0u ? 7.0 / 8.0 : 1.0 - 1.0 / plate.HalfExtent;
            if (!MakeRect(plate, innerFraction, rect))
            {
                reason = TEXT("切り抜きの深度の検査領域が画像の範囲外です");
                return false;
            }
            for (int32_t y = rect.Top; y < rect.Bottom; ++y)
            {
                for (int32_t x = rect.Left; x < rect.Right; ++x)
                {
                    const auto kind = classifyDepth(x, y);
                    counts[index] += kind == DepthPixelKind::Plate ? 1u : 0u;
                    probeCounts[index] += kind == DepthPixelKind::Probe ? 1u : 0u;
                    invalidPixels += kind == DepthPixelKind::Invalid ? 1u : 0u;
                    ++totals[index];
                }
            }
            coverage[index] = static_cast<double>(counts[index]) / totals[index];
        }
        uint32_t matched = 0u;
        for (uint32_t y = 0u; y < 8u; ++y)
        {
            for (uint32_t x = 0u; x < 8u; ++x)
            {
                const int32_t pixelX = static_cast<int32_t>(std::floor(checker.CenterX + checker.HalfExtent * ((x + 0.5) / 4.0 - 1.0)));
                const int32_t pixelY = static_cast<int32_t>(std::floor(checker.CenterY - checker.HalfExtent * ((y + 0.5) / 4.0 - 1.0)));
                if (pixelX < 0 || pixelY < 0 || pixelX >= static_cast<int32_t>(image.Width) || pixelY >= static_cast<int32_t>(image.Height))
                {
                    reason = TEXT("深度を調べるテクセル中心が画像の範囲外です");
                    return false;
                }
                const auto expectedKind = (x + y) % 2u == 0u ? DepthPixelKind::Plate : DepthPixelKind::Probe;
                matched += classifyDepth(pixelX, pixelY) == expectedKind ? 1u : 0u;
            }
        }
        const bool bPassed = m_State->bDepthValid && bFinite && invalidPixels == 0u && texelPixels >= m_MinimumTexelPixels &&
            std::abs(coverage[0] - m_CoverageTarget) <= m_CoverageTolerance && matched == 64u &&
            counts[1] == 0u && probeCounts[1] == totals[1] && counts[2] == totals[2];
        LOG_INFO("切り抜きの深度の判定 ALPHA_CLIP_RESULT scenario=test coverage=%g texel_match=%u/64 single_back_coverage=%g two_back_coverage=%g single_back_pixels=%u/%u two_back_pixels=%u/%u texel_pixels=%g finite=%d result=%s depth_filled=%g probe_pixels=%u invalid_pixels=%u plate_depth=%.9g probe_depth=%.9g",
                 coverage[0], matched, coverage[1], coverage[2], counts[1], totals[1], counts[2], totals[2], texelPixels,
                 bFinite ? 1 : 0, bPassed ? "PASS" : "FAIL", coverage[0],
                 probeCounts[0] + probeCounts[1] + probeCounts[2], invalidPixels,
                 m_State->PlateDepth, m_State->Probe.Depth[0]);
        if (!bPassed)
        {
            reason = !m_State->bDepthValid
                ? TEXT("板と試しの深度が 0 < 板の深度 < 試しの深度 < 1 を満たしません")
                : TEXT("市松の深度が埋まった率・テクセル中心・裏向きの深度または試しの色が基準を満たしません");
        }
        return bPassed;
    }
} // namespace NorvesLib::Test::RenderingValidation
