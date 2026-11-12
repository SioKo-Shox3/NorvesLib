#include "TemporalAAReactiveScenario.h"

#include "RenderingValidation/RenderingFloatImage.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderExtension/RenderExtension.h"
#include "Rendering/RenderExtension/RenderExtensionData.h"
#include "Rendering/RenderExtension/RenderExtensionHelpers.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Thread/Mutex.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        using namespace Core::Rendering;
        namespace RHI = NorvesLib::RHI;
        namespace Container = Core::Container;

        constexpr uint32_t Width = 256;
        constexpr uint32_t Height = 256;
        constexpr uint32_t Frames = 68;
        constexpr uint32_t Channels = 4;
        constexpr size_t FrameElements = static_cast<size_t>(Width) * Height * Channels;
        constexpr uint64_t FrameBytes = FrameElements * sizeof(uint16_t);

        enum Phase : uint32_t
        {
            Warmup,
            Reference,
            WithoutReactive,
            WithReactive,
            ZeroWriter,
            Complete
        };

        struct PhaseParams
        {
            uint32_t Value;
        };

        constexpr RenderExtensionDataKey<PhaseParams> PhaseKey{
            Core::Identity::Literal("Test.TemporalAAReactive.Phase", sizeof("Test.TemporalAAReactive.Phase") - 1)};

        struct Evidence
        {
            NorvesLib::Thread::Mutex Mutex;
            Container::VariableArray<uint16_t> Images[4];
            uint32_t Counts[4] = {};
            bool Received[4][Frames] = {};
            const char* Failure = nullptr;
        };

        void SetFailure(const Container::TSharedPtr<Evidence>& evidence, const char* message)
        {
            NorvesLib::Thread::ScopedLock lock(evidence->Mutex);
            if (!evidence->Failure)
            {
                evidence->Failure = message;
            }
        }

        bool Fail(Container::String& reason, const char* message)
        {
            reason = Container::String(message);
            std::cerr << "TAA リアクティブの検証失敗: " << message << '\n';
            return false;
        }

        struct MarkerParams
        {
            float Color[4];
        };

        class DrawingPass final : public IViewPass, public IRenderGraphPass
        {
        public:
            explicit DrawingPass(const Container::TSharedPtr<Evidence>& evidence) : m_Evidence(evidence) {}
            ~DrawingPass() override { Shutdown(); }
            const char* GetName() const override { return "Ext.Test.TemporalAAReactive.Draw"; }

            bool Initialize(ViewRenderContext& context) override
            {
                m_Device = context.Device;
                Container::String root(NORVES_SOURCE_ROOT);
                root += "/Test/Core/Rendering/Shaders";
                if (!m_Device || !m_Shaders.Initialize(m_Device, root))
                {
                    SetFailure(m_Evidence, "試験用シェーダーの初期化に失敗しました");
                    return false;
                }
                m_Reactive.Initialize(m_Device);
                m_Vertex = m_Shaders.LoadShader("ext_stage_marker.vert", RHI::ShaderStage::Vertex);
                m_Checker = m_Shaders.LoadShader("temporal_aa_reactive_checker.frag", RHI::ShaderStage::Pixel);
                m_Marker = m_Shaders.LoadShader("ext_stage_marker.frag", RHI::ShaderStage::Pixel);
                RHI::DescriptorBinding binding;
                binding.binding = 0;
                binding.type = RHI::ResourceBindType::ConstantBuffer;
                binding.stages = RHI::ShaderStage::Pixel;
                m_DescriptorDesc.bindings.push_back(binding);
                m_Layout = m_Device->GetOrCreateDescriptorSetLayout(m_DescriptorDesc);
                if (!m_Vertex || !m_Checker || !m_Marker || !m_Layout)
                {
                    SetFailure(m_Evidence, "試験用シェーダーまたは定数のレイアウトを用意できません");
                    return false;
                }
                return true;
            }

            void Shutdown() override
            {
                m_Reactive.Reset();
                m_ReactivePipeline.reset();
                m_ReactivePass.reset();
                m_Framebuffer.reset();
                m_CheckerPipeline.reset();
                m_MarkerPipeline.reset();
                m_ColorPass.reset();
                m_Layout.reset();
                m_Vertex.reset();
                m_Checker.reset();
                m_Marker.reset();
                m_DescriptorDesc.bindings.clear();
                m_Shaders.Shutdown();
                m_Device = nullptr;
            }

            void Setup(ViewRenderContext&) override {}
            void Execute(ViewRenderContext&) override {}

            void Declare(RenderGraphBuilder& builder) override
            {
                m_Target = {};
                m_bReactiveDeclared = false;
                const ViewRenderContext* context = builder.GetContext();
                const PhaseParams* phase = context ? context->FindExtension(PhaseKey) : nullptr;
                if (!phase || phase->Value >= Complete)
                {
                    return;
                }
                RGTextureHandle existing;
                if (builder.TryGetTexture(RenderGraphResourceNames::SceneReactive, existing))
                {
                    SetFailure(m_Evidence, "試験パスより前にリアクティブの書き手があります");
                    return;
                }
                if (!builder.TryLoadStoreColorAttachment(ResolveHdrChainHead(builder), m_Target,
                        RHI::AttachmentLoadOp::Load, RHI::AttachmentStoreOp::Store,
                        RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource))
                {
                    SetFailure(m_Evidence, "試験の HDR 描画先を宣言できません");
                    return;
                }
                if (phase->Value == WithReactive || phase->Value == ZeroWriter)
                {
                    m_bReactiveDeclared = m_Reactive.Declare(builder, false);
                    if (!m_bReactiveDeclared)
                    {
                        SetFailure(m_Evidence, "リアクティブの描画先を宣言できません");
                    }
                }
                builder.PreserveInsertionOrder();
            }

            void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                const PhaseParams* phase = context.FindExtension(PhaseKey);
                if (!phase || phase->Value >= Complete)
                {
                    return;
                }
                const auto target = m_Target.IsValid() ? resources.GetTexture(m_Target) : RHI::TexturePtr{};
                if (!target || target->GetWidth() != Width || target->GetHeight() != Height ||
                    target->GetFormat() != RHI::Format::R16G16B16A16_FLOAT ||
                    !context.FrameResources || !context.CommandList || !PrepareColor(target))
                {
                    SetFailure(m_Evidence, "試験の HDR 描画先またはパイプラインを用意できません");
                    return;
                }
                const bool bBoard = (phase->Value == WithoutReactive || phase->Value == WithReactive) &&
                    context.bDeterministicCapture && context.TemporalFrameIndex >= 8 &&
                    context.TemporalFrameIndex <= 43;
                const MarkerParams board{{1.25f, 1.25f, 1.25f, 0.75f}};
                const MarkerParams reactive{{1.0f, 0.0f, 0.0f, 0.75f}};
                RHI::DescriptorSetPtr boardSet;
                RHI::DescriptorSetPtr reactiveSet;
                if (bBoard)
                {
                    boardSet = MakeSet(context, board);
                    if (phase->Value == WithReactive)
                    {
                        reactiveSet = MakeSet(context, reactive);
                    }
                    if (!boardSet || (phase->Value == WithReactive && !reactiveSet))
                    {
                        SetFailure(m_Evidence, "板の定数を借りられません");
                        return;
                    }
                }

                RHI::Viewport viewport;
                viewport.width = static_cast<float>(Width);
                viewport.height = static_cast<float>(Height);
                RHI::ScissorRect scissor;
                scissor.left = 0;
                scissor.top = 0;
                scissor.right = Width;
                scissor.bottom = Height;
                auto* commands = context.CommandList;
                commands->BeginRenderPass(m_ColorPass, m_Framebuffer);
                commands->SetViewport(viewport);
                commands->SetScissor(scissor);
                commands->SetPipeline(m_CheckerPipeline);
                commands->Draw(3);
                if (bBoard)
                {
                    scissor.left = 48 + static_cast<int32_t>(context.TemporalFrameIndex - 8) * 4;
                    scissor.right = scissor.left + 24;
                    scissor.top = 96;
                    scissor.bottom = 160;
                    commands->SetScissor(scissor);
                    commands->SetPipeline(m_MarkerPipeline);
                    commands->SetDescriptorSet(boardSet);
                    commands->Draw(3);
                }
                commands->EndRenderPass();

                if (m_bReactiveDeclared)
                {
                    if (!m_Reactive.Prepare(resources))
                    {
                        SetFailure(m_Evidence, "リアクティブの描画先を用意できません");
                        return;
                    }
                    if (m_ReactivePass != m_Reactive.GetRenderPass())
                    {
                        m_ReactivePass = m_Reactive.GetRenderPass();
                        m_ReactivePipeline = CreatePipeline(m_ReactivePass, m_Marker, true);
                    }
                    if (!m_ReactivePipeline)
                    {
                        SetFailure(m_Evidence, "リアクティブのパイプラインを作れません");
                        return;
                    }
                    // 描画しない場合も最初の書き手としてゼロにクリアする。
                    commands->BeginRenderPass(m_ReactivePass, m_Reactive.GetFramebuffer());
                    if (bBoard && phase->Value == WithReactive)
                    {
                        commands->SetViewport(viewport);
                        commands->SetScissor(scissor);
                        commands->SetPipeline(m_ReactivePipeline);
                        commands->SetDescriptorSet(reactiveSet);
                        commands->Draw(3);
                    }
                    commands->EndRenderPass();
                }
            }

        private:
            RHI::PipelinePtr CreatePipeline(const RHI::RenderPassPtr& pass,
                                            const RHI::ShaderPtr& fragment, bool bBlend)
            {
                RHI::GraphicsPipelineDesc pipeline;
                pipeline.vertexShader = m_Vertex;
                pipeline.pixelShader = fragment;
                pipeline.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
                pipeline.rasterState.cullMode = RHI::CullMode::None;
                pipeline.depthStencilState.depthTestEnable = false;
                pipeline.depthStencilState.depthWriteEnable = false;
                RHI::BlendAttachmentDesc blend;
                blend.blendEnable = bBlend;
                blend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
                blend.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
                blend.colorBlendOp = RHI::BlendOp::Add;
                blend.srcAlphaBlendFactor = RHI::BlendFactor::One;
                blend.dstAlphaBlendFactor = RHI::BlendFactor::InvSrcAlpha;
                blend.alphaBlendOp = RHI::BlendOp::Add;
                blend.colorWriteMask = RHI::ColorWriteMask::All;
                pipeline.blendState.attachments.push_back(blend);
                pipeline.renderPass = pass;
                pipeline.descriptorSetLayouts.push_back(m_DescriptorDesc);
                return pass ? m_Device->CreateGraphicsPipeline(pipeline) : nullptr;
            }

            bool PrepareColor(const RHI::TexturePtr& target)
            {
                if (!m_ColorPass || m_Format != target->GetFormat())
                {
                    m_Format = target->GetFormat();
                    RHI::RenderPassDesc desc;
                    RHI::AttachmentDesc attachment;
                    attachment.format = m_Format;
                    attachment.clear = false;
                    attachment.loadOp = RHI::AttachmentLoadOp::Load;
                    attachment.storeOp = RHI::AttachmentStoreOp::Store;
                    attachment.initialState = RHI::ResourceState::RenderTarget;
                    attachment.finalState = RHI::ResourceState::ShaderResource;
                    desc.colorAttachments.push_back(attachment);
                    m_ColorPass = m_Device->CreateRenderPass(desc);
                    m_CheckerPipeline = CreatePipeline(m_ColorPass, m_Checker, false);
                    m_MarkerPipeline = CreatePipeline(m_ColorPass, m_Marker, true);
                }
                RHI::FramebufferDesc desc;
                desc.renderPass = m_ColorPass;
                desc.colorTargets.push_back(target);
                desc.width = Width;
                desc.height = Height;
                m_Framebuffer = m_ColorPass ? m_Device->CreateFramebuffer(desc) : nullptr;
                return m_Framebuffer && m_CheckerPipeline && m_MarkerPipeline;
            }

            RHI::DescriptorSetPtr MakeSet(ViewRenderContext& context, const MarkerParams& params)
            {
                const auto allocation = context.FrameResources->AllocateConstants(params);
                const auto set = context.FrameResources->AllocateDescriptorSet(m_Layout).Set;
                if (!allocation.IsValid() || !set)
                {
                    return {};
                }
                set->BindConstantBuffer(0, allocation.Buffer, allocation.GetOffset32(), sizeof(params));
                set->Update();
                return set;
            }

            Container::TSharedPtr<Evidence> m_Evidence;
            RHI::IDevice* m_Device = nullptr;
            ShaderManager m_Shaders;
            RHI::ShaderPtr m_Vertex, m_Checker, m_Marker;
            RHI::DescriptorSetDesc m_DescriptorDesc;
            RHI::DescriptorSetLayoutPtr m_Layout;
            RHI::RenderPassPtr m_ColorPass, m_ReactivePass;
            RHI::FramebufferPtr m_Framebuffer;
            RHI::PipelinePtr m_CheckerPipeline, m_MarkerPipeline, m_ReactivePipeline;
            RHI::Format m_Format = RHI::Format::UNKNOWN;
            RGTextureHandle m_Target;
            ReactiveTargetBinder m_Reactive;
            bool m_bReactiveDeclared = false;
        };

        class ReadbackPass final : public IViewPass, public IRenderGraphPass, public IFrameReadbackReceiver
        {
        public:
            explicit ReadbackPass(const Container::TSharedPtr<Evidence>& evidence)
                : m_Evidence(evidence), m_Receiver(this) {}
            ~ReadbackPass() override { Shutdown(); }
            const char* GetName() const override { return "Ext.Test.TemporalAAReactive.Readback"; }
            bool Initialize(ViewRenderContext&) override { return true; }
            void Shutdown() override { m_Receiver.CancelPending(); m_Pending.clear(); }
            void Setup(ViewRenderContext&) override {}
            void Execute(ViewRenderContext&) override {}

            void Declare(RenderGraphBuilder& builder) override
            {
                m_Target = {};
                const ViewRenderContext* context = builder.GetContext();
                const PhaseParams* phase = context ? context->FindExtension(PhaseKey) : nullptr;
                if (phase && phase->Value > Warmup && phase->Value < Complete &&
                    !builder.TryReadTexture(ResolveHdrChainHead(builder), m_Target, RHI::ResourceState::ShaderResource))
                {
                    SetFailure(m_Evidence, "TAA 後の HDR を宣言できません");
                }
                builder.PreserveInsertionOrder();
            }

            void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                const PhaseParams* phase = context.FindExtension(PhaseKey);
                if (!phase || phase->Value <= Warmup || phase->Value >= Complete || !context.bDeterministicCapture)
                {
                    return;
                }
                if (context.bTemporalEpochStart && context.TemporalFrameIndex == 0)
                {
                    m_ActivePhase = phase->Value;
                    m_NextFrame = 0;
                }
                if (m_ActivePhase != phase->Value || m_NextFrame >= Frames)
                {
                    return;
                }
                if (context.TemporalFrameIndex != m_NextFrame)
                {
                    SetFailure(m_Evidence, "決定的エポックの連続フレームが飛びました");
                    m_ActivePhase = Complete;
                    return;
                }
                const auto target = m_Target.IsValid() ? resources.GetTexture(m_Target) : RHI::TexturePtr{};
                if (!target || target->GetWidth() != Width || target->GetHeight() != Height ||
                    target->GetFormat() != RHI::Format::R16G16B16A16_FLOAT ||
                    !context.FrameResources || !context.CommandList)
                {
                    SetFailure(m_Evidence, "TAA 後の読み戻し元の形式または寸法が不正です");
                    return;
                }
                FrameReadbackRequest request;
                request.Receiver = &m_Receiver;
                request.Size = FrameBytes;
                const auto readback = context.FrameResources->RequestReadback(request);
                if (!readback.IsValid())
                {
                    SetFailure(m_Evidence, "連続フレームの読み戻し先を借りられません");
                    return;
                }
                m_Pending.push_back({readback.RequestId, phase->Value - Reference, m_NextFrame});
                ++m_NextFrame;
                auto* commands = context.CommandList;
                commands->TextureBarrier(target, RHI::ResourceState::ShaderResource, RHI::ResourceState::CopySource);
                commands->BufferBarrier(readback.Buffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
                commands->CopyTextureToBuffer(target, readback.Buffer, Width, Height, readback.Offset);
                commands->BufferBarrier(readback.Buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::HostRead);
                commands->TextureBarrier(target, RHI::ResourceState::CopySource, RHI::ResourceState::ShaderResource);
            }

            void OnFrameReadback(const FrameReadbackResult& result) override
            {
                for (size_t i = 0; i < m_Pending.size(); ++i)
                {
                    if (m_Pending[i].RequestId != result.RequestId)
                    {
                        continue;
                    }
                    const Pending pending = m_Pending[i];
                    m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
                    NorvesLib::Thread::ScopedLock lock(m_Evidence->Mutex);
                    if (result.Status != FrameReadbackStatus::Delivered || !result.Data || result.Size != FrameBytes ||
                        m_Evidence->Received[pending.Phase][pending.Frame])
                    {
                        m_Evidence->Failure = "連続フレームの読み戻しが失敗または重複しました";
                        return;
                    }
                    std::memcpy(m_Evidence->Images[pending.Phase].data() + pending.Frame * FrameElements,
                                result.Data, static_cast<size_t>(FrameBytes));
                    m_Evidence->Received[pending.Phase][pending.Frame] = true;
                    ++m_Evidence->Counts[pending.Phase];
                    return;
                }
                SetFailure(m_Evidence, "対応する読み戻し要求がありません");
            }

        private:
            struct Pending { uint64_t RequestId; uint32_t Phase; uint32_t Frame; };
            Container::TSharedPtr<Evidence> m_Evidence;
            FrameReadbackReceiver m_Receiver;
            Container::VariableArray<Pending> m_Pending;
            RGTextureHandle m_Target;
            uint32_t m_ActivePhase = Warmup;
            uint32_t m_NextFrame = 0;
        };

        Container::TUniquePtr<IViewPass> CreateDrawingPass(void* context)
        {
            return Container::MakeUnique<DrawingPass>(*static_cast<Container::TSharedPtr<Evidence>*>(context));
        }

        Container::TUniquePtr<IViewPass> CreateReadbackPass(void* context)
        {
            return Container::MakeUnique<ReadbackPass>(*static_cast<Container::TSharedPtr<Evidence>*>(context));
        }

        bool EvaluateImages(const Evidence& evidence, Container::String& reason)
        {
            float differences[2][Frames] = {};
            float peaks[2] = {};
            uint64_t differentPixels = 0;
            float maximumDifference = 0.0f;
            for (uint32_t frame = 0; frame < Frames; ++frame)
            {
                const size_t frameOffset = frame * FrameElements;
                for (uint32_t y = 0; y < Height; ++y)
                {
                    for (uint32_t x = 0; x < Width; ++x)
                    {
                        const size_t offset = frameOffset + (static_cast<size_t>(y) * Width + x) * Channels;
                        bool bDifferent = false;
                        for (uint32_t channel = 0; channel < 3; ++channel)
                        {
                            const uint16_t referenceBits = evidence.Images[0][offset + channel];
                            const uint16_t zeroBits = evidence.Images[3][offset + channel];
                            const float reference = DecodeIeee754Binary16(referenceBits);
                            const float zero = DecodeIeee754Binary16(zeroBits);
                            if (!std::isfinite(reference) || !std::isfinite(zero))
                            {
                                return Fail(reason, "基準またはゼロの書き手に非有限の値があります");
                            }
                            bDifferent |= referenceBits != zeroBits;
                            maximumDifference = std::max(maximumDifference, std::abs(reference - zero));
                            if (x >= 64 && x < 192 && y >= 96 && y < 160)
                            {
                                for (uint32_t variant = 0; variant < 2; ++variant)
                                {
                                    const float value = DecodeIeee754Binary16(evidence.Images[variant + 1][offset + channel]);
                                    if (!std::isfinite(value))
                                    {
                                        return Fail(reason, "板を描いた系列に非有限の値があります");
                                    }
                                    differences[variant][frame] = std::max(differences[variant][frame],
                                                                            std::abs(value - reference));
                                }
                            }
                        }
                        differentPixels += bDifferent ? 1u : 0u;
                    }
                }
            }
            uint32_t trailingFrames[2] = {};
            for (uint32_t variant = 0; variant < 2; ++variant)
            {
                for (uint32_t frame = 8; frame <= 43; ++frame)
                {
                    peaks[variant] = std::max(peaks[variant], differences[variant][frame]);
                }
                for (uint32_t frame = 44; frame < Frames; ++frame)
                {
                    trailingFrames[variant] += differences[variant][frame] > peaks[variant] * 0.05f ? 1u : 0u;
                }
            }
            std::cout << "リアクティブ無し: ピーク=" << peaks[0] << " 残像フレーム=" << trailingFrames[0] << '\n';
            std::cout << "リアクティブ有り: ピーク=" << peaks[1] << " 残像フレーム=" << trailingFrames[1] << '\n';
            std::cout << "ゼロの書き手: 違う画素=" << differentPixels << " 最大差=" << maximumDifference << '\n';
            if (!(peaks[0] > 0.0f && peaks[1] > 0.0f) || trailingFrames[0] < 8 || trailingFrames[1] > 2 ||
                differentPixels != 0)
            {
                return Fail(reason, "残像フレーム数またはゼロの書き手のビット一致が条件を満たしません");
            }
            return true;
        }
    } // namespace

    struct TemporalAAReactiveScenario::State
    {
        Container::TSharedPtr<Evidence> Shared = Container::MakeShared<Evidence>();
    };

    TemporalAAReactiveScenario::TemporalAAReactiveScenario() : m_State(Container::MakeUnique<State>()) {}
    TemporalAAReactiveScenario::~TemporalAAReactiveScenario() = default;

    bool TemporalAAReactiveScenario::ParseArgument(const Container::String& argument, Container::String& reason)
    {
        if (m_bEnabled || argument != TEXT("--temporal-aa-reactive-scenario=ab"))
        {
            return Fail(reason, "シナリオの値が不正か重複しています");
        }
        m_bEnabled = true;
        return true;
    }

    bool TemporalAAReactiveScenario::IsEnabled() const { return m_bEnabled; }

    bool TemporalAAReactiveScenario::Initialize(RenderWorld& world, bool bOutdoor, FrameCaptureSourceKind source)
    {
        if (!bOutdoor || source != FrameCaptureSourceKind::SceneColor)
        {
            std::cerr << "TAA リアクティブの検証には outdoor と scene-color が必要です\n";
            return false;
        }
        for (auto& image : m_State->Shared->Images)
        {
            image.resize(static_cast<size_t>(Frames) * FrameElements);
        }
        world.SetDeterministicCapture(true);
        RenderExtensionDesc draw;
        draw.Name = "Ext.Test.TemporalAAReactive.Draw";
        draw.Stage = RenderExtensionStage::Transparent;
        RenderExtensionDesc readback;
        readback.Name = "Ext.Test.TemporalAAReactive.Readback";
        readback.Stage = RenderExtensionStage::PostTemporalAA;
        if (!world.RegisterRenderExtension(draw, {CreateDrawingPass, &m_State->Shared}) ||
            !world.RegisterRenderExtension(readback, {CreateReadbackPass, &m_State->Shared}))
        {
            std::cerr << "TAA リアクティブの検証パスを登録できません\n";
            return false;
        }
        return true;
    }

    void TemporalAAReactiveScenario::ApplyState(RenderWorld& world, const CameraProxy& baseCamera)
    {
        CameraProxy camera = baseCamera;
        camera.AntiAliasing = CameraAntiAliasingMode::TemporalAA;
        camera.ExposureMode = CameraExposureMode::Manual;
        world.SetMainCamera(camera);
        world.SetExtensionData(PhaseKey, PhaseParams{m_Phase});
        if (m_Phase > Warmup && m_Phase < Complete && m_StartedPhase != m_Phase)
        {
            world.BeginDeterministicEpoch();
            m_StartedPhase = m_Phase;
        }
    }

    bool TemporalAAReactiveScenario::Evaluate(const CapturedFrame& frame, Container::String& reason)
    {
        if (!frame.IsSuccess() || m_Phase >= Complete)
        {
            return Fail(reason, "取得の段階または結果が不正です");
        }
        NorvesLib::Thread::ScopedLock lock(m_State->Shared->Mutex);
        if (m_State->Shared->Failure)
        {
            return Fail(reason, m_State->Shared->Failure);
        }
        m_bAdvance = m_Phase == Warmup || m_State->Shared->Counts[m_Phase - Reference] == Frames;
        if (m_Phase == ZeroWriter && m_bAdvance)
        {
            return EvaluateImages(*m_State->Shared, reason);
        }
        return true;
    }

    void TemporalAAReactiveScenario::Advance()
    {
        if (m_bAdvance)
        {
            ++m_Phase;
            m_bAdvance = false;
        }
    }

    bool TemporalAAReactiveScenario::RequestFollowup(FrameCaptureRequest& request) const
    {
        if (m_Phase >= Complete)
        {
            return false;
        }
        request.SourceKind = FrameCaptureSourceKind::SceneColor;
        return true;
    }
} // namespace NorvesLib::Test::RenderingValidation
