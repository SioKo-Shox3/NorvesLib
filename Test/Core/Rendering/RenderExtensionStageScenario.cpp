#include "RenderExtensionStageScenario.h"

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
#include <cmath>
#include <iostream>
#include <cstring>

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

        struct ExtStageMarkerParams
        {
            float Color[4];
        };

        constexpr RenderExtensionDataKey<ExtStageMarkerParams> TemporalKey{
            Core::Identity::Literal("Test.Extension.Temporal", sizeof("Test.Extension.Temporal") - 1)};
        constexpr RenderExtensionDataKey<ExtStageMarkerParams> DisplayKey{
            Core::Identity::Literal("Test.Extension.Display", sizeof("Test.Extension.Display") - 1)};
        constexpr ExtStageMarkerParams TemporalColors[] = {
            {{0.125f, 0.25f, 0.5f, 1.0f}}, {{0.5f, 0.125f, 0.25f, 1.0f}}};
        constexpr ExtStageMarkerParams DisplayColor{{0.25f, 0.5f, 0.125f, 1.0f}};
        constexpr uint32_t TemporalCaptureCount = 3;
        constexpr uint32_t DisplayStage = 2 * TemporalCaptureCount;
        constexpr uint32_t CompleteStage = DisplayStage + 2;

        struct MarkerSpec
        {
            const char* Name;
            RenderExtensionStage Stage;
            RenderExtensionDataKey<ExtStageMarkerParams> Key;
            int32_t Left;
            int32_t Top;
            int32_t Size;
        };

        // 段を足すときは登録表に目印を追加し、Declare の対象資源と取得の段階を追加する。
        MarkerSpec Markers[] = {
            {"Ext.Test.PostTemporalAA", RenderExtensionStage::PostTemporalAA, TemporalKey, 32, 32, 32},
            {"Ext.Test.PostTonemap", RenderExtensionStage::PostTonemap, DisplayKey, 96, 32, 32}};

        struct TemporalEvidence
        {
            NorvesLib::Thread::Mutex Mutex;
            uint32_t Count[2] = {};
            uint64_t PreviousFrame[2] = {};
            bool bFailed = false;
        };

        struct MarkerRegistration
        {
            const MarkerSpec* Spec;
            Container::TSharedPtr<TemporalEvidence> Evidence;
        };

        class MarkerPass final : public IViewPass, public IRenderGraphPass, public IFrameReadbackReceiver
        {
        public:
            explicit MarkerPass(const MarkerRegistration& registration)
                : m_Spec(*registration.Spec), m_Evidence(registration.Evidence), m_Receiver(this)
            {
            }
            ~MarkerPass() override
            {
                Shutdown();
            }
            const char* GetName() const override
            {
                return m_Spec.Name;
            }

            bool Initialize(ViewRenderContext& context) override
            {
                m_Device = context.Device;
                Container::String root(NORVES_SOURCE_ROOT);
                root += "/Test/Core/Rendering/Shaders";
                if (!m_Device || !m_Shaders.Initialize(m_Device, root))
                {
                    return false;
                }
                m_Vertex = m_Shaders.LoadShader("ext_stage_marker.vert", RHI::ShaderStage::Vertex);
                m_Fragment = m_Shaders.LoadShader("ext_stage_marker.frag", RHI::ShaderStage::Pixel);
                RHI::DescriptorBinding binding;
                binding.binding = 0;
                binding.type = RHI::ResourceBindType::ConstantBuffer;
                binding.stages = RHI::ShaderStage::Pixel;
                m_DescriptorDesc.bindings.push_back(binding);
                m_Layout = m_Device->GetOrCreateDescriptorSetLayout(m_DescriptorDesc);
                m_bInitialized = m_Vertex && m_Fragment && m_Layout;
                return m_bInitialized;
            }

            void Shutdown() override
            {
                m_Receiver.CancelPending();
                m_PendingProbes.clear();
                m_RequestedProbes[0] = 0;
                m_RequestedProbes[1] = 0;
                m_Framebuffer.reset();
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

            void Setup(ViewRenderContext&) override
            {
            }

            void Execute(ViewRenderContext&) override
            {
            }

            void Declare(RenderGraphBuilder& builder) override
            {
                m_Target = {};
                Core::Identity name;
                switch (m_Spec.Stage)
                {
                case RenderExtensionStage::PostTemporalAA:
                    name = ResolveHdrChainHead(builder);
                    break;
                case RenderExtensionStage::PostTonemap:
                    name = RenderGraphResourceNames::ToneMappedColor;
                    break;
                default:
                    return;
                }
                builder.TryLoadStoreColorAttachment(name, m_Target,
                    RHI::AttachmentLoadOp::Load, RHI::AttachmentStoreOp::Store,
                    RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
                builder.PreserveInsertionOrder();
            }

            void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
            {
                const ExtStageMarkerParams* params = context.FindExtension(m_Spec.Key);
                if (!params || !m_bInitialized || !context.FrameResources || !context.CommandList || !m_Target.IsValid())
                {
                    // データが来なければ描かない。画像の判定で欠落を検出する。
                    return;
                }
                const auto target = resources.GetTexture(m_Target);
                if (!target || !PrepareTarget(target))
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
                scissor.left = m_Spec.Left;
                scissor.top = m_Spec.Top;
                scissor.right = m_Spec.Left + m_Spec.Size;
                scissor.bottom = m_Spec.Top + m_Spec.Size;
                auto* commands = context.CommandList;
                commands->BeginRenderPass(m_RenderPass, m_Framebuffer);
                commands->SetViewport(viewport);
                commands->SetScissor(scissor);
                commands->SetPipeline(m_Pipeline);
                commands->SetDescriptorSet(set);
                commands->Draw(3);
                commands->EndRenderPass();
                if (m_Spec.Stage == RenderExtensionStage::PostTemporalAA)
                {
                    RecordTemporalProbe(target, context, *params);
                }
            }

            void OnFrameReadback(const FrameReadbackResult& result) override
            {
                for (const PendingProbe& probe : m_PendingProbes)
                {
                    if (probe.RequestId != result.RequestId)
                    {
                        continue;
                    }
                    NorvesLib::Thread::ScopedLock lock(m_Evidence->Mutex);
                    const uint32_t group = probe.ColorIndex;
                    bool bPassed = result.Status == FrameReadbackStatus::Delivered && result.Data &&
                        result.Size >= static_cast<uint64_t>(probe.Width) * probe.Height * 8;
                    if (m_Evidence->Count[group] != 0 &&
                        probe.FrameNumber != m_Evidence->PreviousFrame[group] + 1)
                    {
                        bPassed = false;
                        std::cerr << GetName() << " 隣接フレームの期待=" << m_Evidence->PreviousFrame[group] + 1
                                  << " 実測=" << probe.FrameNumber << '\n';
                    }
                    float firstPixel[3] = {};
                    for (uint32_t y = 40; bPassed && y < 56; ++y)
                    {
                        for (uint32_t x = 40; bPassed && x < 56; ++x)
                        {
                            for (uint32_t channel = 0; channel < 3; ++channel)
                            {
                                uint16_t bits;
                                const size_t offset = ((static_cast<size_t>(y) * probe.Width + x) * 4 + channel) * 2;
                                std::memcpy(&bits, static_cast<const uint8_t*>(result.Data) + offset, sizeof(bits));
                                const float actual = DecodeIeee754Binary16(bits);
                                const float expected = TemporalColors[group].Color[channel];
                                firstPixel[channel] = actual;
                                if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00048828125f)
                                {
                                    std::cerr << GetName() << " 隣接フレーム=" << probe.FrameNumber
                                              << " 矩形=(40,40,16,16) 画素=(" << x << ',' << y << ") 成分=" << channel
                                              << " 期待=" << expected << " 実測=" << actual << " 許容=0.00048828125\n";
                                    bPassed = false;
                                    break;
                                }
                            }
                        }
                    }
                    m_Evidence->bFailed |= !bPassed;
                    m_Evidence->PreviousFrame[group] = probe.FrameNumber;
                    ++m_Evidence->Count[group];
                    std::cout << GetName() << " 隣接取得=" << m_Evidence->Count[group] << "/3 色=" << group
                              << " frame=" << probe.FrameNumber << " 矩形=(40,40,16,16) 期待=("
                              << TemporalColors[group].Color[0] << ',' << TemporalColors[group].Color[1] << ','
                              << TemporalColors[group].Color[2] << ") 実測=(" << firstPixel[0] << ',' << firstPixel[1]
                              << ',' << firstPixel[2] << ") 許容=0.00048828125 結果=" << (bPassed ? "PASS" : "FAIL") << '\n';
                    return;
                }
            }

        private:
            struct PendingProbe
            {
                uint64_t RequestId;
                uint64_t FrameNumber;
                uint32_t ColorIndex;
                uint32_t Width;
                uint32_t Height;
            };

            void RecordTemporalProbe(const RHI::TexturePtr& target, ViewRenderContext& context,
                                     const ExtStageMarkerParams& params)
            {
                const uint32_t group = params.Color[0] == TemporalColors[0].Color[0] ? 0 : 1;
                if (m_RequestedProbes[group] >= TemporalCaptureCount)
                {
                    return;
                }
                // 共通の取得要求は飛行中の提出の完了待ちで間隔が空く。ここはフレームの持ち主から借り、
                // 同じ色の隣接した 3 フレームの実際の HDR 添付を読み戻す。寿命は持ち主に任せる。
                if (target->GetFormat() != RHI::Format::R16G16B16A16_FLOAT ||
                    target->GetWidth() < 64 || target->GetHeight() < 64)
                {
                    NorvesLib::Thread::ScopedLock lock(m_Evidence->Mutex);
                    m_Evidence->bFailed = true;
                    return;
                }
                FrameReadbackRequest request;
                request.Receiver = &m_Receiver;
                request.Size = static_cast<uint64_t>(target->GetWidth()) * target->GetHeight() * 8;
                const auto readback = context.FrameResources->RequestReadback(request);
                if (!readback.IsValid())
                {
                    NorvesLib::Thread::ScopedLock lock(m_Evidence->Mutex);
                    m_Evidence->bFailed = true;
                    return;
                }
                m_PendingProbes.push_back({readback.RequestId, context.ExtensionFrame.FrameNumber,
                                          group, target->GetWidth(), target->GetHeight()});
                ++m_RequestedProbes[group];
                context.CommandList->TextureBarrier(target, RHI::ResourceState::ShaderResource, RHI::ResourceState::CopySource);
                context.CommandList->BufferBarrier(readback.Buffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
                context.CommandList->CopyTextureToBuffer(target, readback.Buffer, target->GetWidth(), target->GetHeight(), readback.Offset);
                context.CommandList->BufferBarrier(readback.Buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::HostRead);
                context.CommandList->TextureBarrier(target, RHI::ResourceState::CopySource, RHI::ResourceState::ShaderResource);
            }

            bool PrepareTarget(const RHI::TexturePtr& target)
            {
                if (!m_RenderPass || target->GetFormat() != m_Format)
                {
                    m_Format = target->GetFormat();
                    RHI::RenderPassDesc pass;
                    RHI::AttachmentDesc attachment;
                    attachment.format = m_Format;
                    attachment.clear = false;
                    attachment.loadOp = RHI::AttachmentLoadOp::Load;
                    attachment.storeOp = RHI::AttachmentStoreOp::Store;
                    attachment.initialState = RHI::ResourceState::RenderTarget;
                    attachment.finalState = RHI::ResourceState::ShaderResource;
                    pass.colorAttachments.push_back(attachment);
                    pass.hasDepthStencil = false;
                    m_RenderPass = m_Device->CreateRenderPass(pass);
                    RHI::GraphicsPipelineDesc pipeline;
                    pipeline.vertexShader = m_Vertex;
                    pipeline.pixelShader = m_Fragment;
                    pipeline.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
                    pipeline.rasterState.cullMode = RHI::CullMode::None;
                    pipeline.depthStencilState.depthTestEnable = false;
                    pipeline.depthStencilState.depthWriteEnable = false;
                    RHI::BlendAttachmentDesc blend;
                    blend.blendEnable = false;
                    blend.colorWriteMask = RHI::ColorWriteMask::All;
                    pipeline.blendState.attachments.push_back(blend);
                    pipeline.renderPass = m_RenderPass;
                    pipeline.descriptorSetLayouts.push_back(m_DescriptorDesc);
                    m_Pipeline = m_RenderPass ? m_Device->CreateGraphicsPipeline(pipeline) : nullptr;
                    m_Framebuffer.reset();
                }
                // 今の描画先を保持するフレームバッファを作る。破棄の時期は RHI が持つ。
                RHI::FramebufferDesc framebuffer;
                framebuffer.renderPass = m_RenderPass;
                framebuffer.colorTargets.push_back(target);
                framebuffer.width = target->GetWidth();
                framebuffer.height = target->GetHeight();
                m_Framebuffer = m_RenderPass ? m_Device->CreateFramebuffer(framebuffer) : nullptr;
                return m_Pipeline && m_Framebuffer;
            }

            const MarkerSpec& m_Spec;
            Container::TSharedPtr<TemporalEvidence> m_Evidence;
            FrameReadbackReceiver m_Receiver;
            Container::VariableArray<PendingProbe> m_PendingProbes;
            uint32_t m_RequestedProbes[2] = {};
            RHI::IDevice* m_Device = nullptr;
            ShaderManager m_Shaders;
            RHI::ShaderPtr m_Vertex;
            RHI::ShaderPtr m_Fragment;
            RHI::DescriptorSetDesc m_DescriptorDesc;
            RHI::DescriptorSetLayoutPtr m_Layout;
            RHI::RenderPassPtr m_RenderPass;
            RHI::PipelinePtr m_Pipeline;
            RHI::FramebufferPtr m_Framebuffer;
            RHI::Format m_Format = RHI::Format::UNKNOWN;
            RGTextureHandle m_Target;
        };

        Container::TUniquePtr<IViewPass> CreateMarker(void* context)
        {
            return Container::MakeUnique<MarkerPass>(*static_cast<MarkerRegistration*>(context));
        }

        float EncodeSrgb(float value)
        {
            return value <= 0.0031308f ? value * 12.92f
                : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        bool Fail(Container::String& reason, const char* message)
        {
            reason = Container::String(message);
            std::cerr << "拡張の段の検証失敗: " << message << '\n';
            return false;
        }
    } // namespace

    struct RenderExtensionStageScenario::State
    {
        Core::Container::TSharedPtr<TemporalEvidence> Evidence = Core::Container::MakeShared<TemporalEvidence>();
        MarkerRegistration Registrations[2] = {{&Markers[0], Evidence}, {&Markers[1], Evidence}};
    };

    RenderExtensionStageScenario::RenderExtensionStageScenario() : m_State(Core::Container::MakeUnique<State>())
    {
    }

    RenderExtensionStageScenario::~RenderExtensionStageScenario() = default;

    bool RenderExtensionStageScenario::ParseArgument(const Core::Container::String& argument,
                                                      Core::Container::String& reason)
    {
        if (m_bEnabled)
        {
            return Fail(reason, "拡張の段のシナリオが重複しています");
        }
        if (argument != TEXT("--extension-stage-scenario=post"))
        {
            return Fail(reason, "拡張の段のシナリオ値が不正です");
        }
        m_bEnabled = true;
        return true;
    }

    bool RenderExtensionStageScenario::IsEnabled() const
    {
        return m_bEnabled;
    }

    bool RenderExtensionStageScenario::Initialize(Core::Rendering::RenderWorld& world, bool bOutdoor,
                                                  Core::Rendering::FrameCaptureSourceKind source)
    {
        if (!bOutdoor || source != FrameCaptureSourceKind::SceneColor)
        {
            std::cerr << "拡張の段の検証には outdoor と scene-color が必要です\n";
            return false;
        }
        for (MarkerRegistration& registration : m_State->Registrations)
        {
            const MarkerSpec& marker = *registration.Spec;
            RenderExtensionDesc desc;
            desc.Name = marker.Name;
            desc.Stage = marker.Stage;
            if (!world.RegisterRenderExtension(desc, {CreateMarker, &registration}))
            {
                std::cerr << "拡張の目印を登録できません: " << marker.Name << '\n';
                return false;
            }
        }
        return true;
    }

    void RenderExtensionStageScenario::ApplyState(Core::Rendering::RenderWorld& world,
                                                  const Core::Rendering::CameraProxy& baseCamera)
    {
        CameraProxy camera = baseCamera;
        camera.AntiAliasing = CameraAntiAliasingMode::TemporalAA;
        camera.ExposureMode = CameraExposureMode::Manual;
        const float exposureScale = m_Stage == DisplayStage + 1 ? 4.0f : 1.0f;
        camera.Exposure *= exposureScale;
        camera.PreExposure *= exposureScale;
        camera.InvPreExposure /= exposureScale;
        camera.ExposureCompensation += m_Stage == DisplayStage + 1 ? 2.0f : 0.0f;
        world.SetMainCamera(camera);
        const uint32_t colorIndex = m_Stage < TemporalCaptureCount ? 0 : 1;
        world.SetExtensionData(TemporalKey, TemporalColors[colorIndex]);
        world.SetExtensionData(DisplayKey, DisplayColor);
    }

    bool RenderExtensionStageScenario::Evaluate(const Core::Rendering::CapturedFrame& frame,
                                                Core::Container::String& reason)
    {
        if (m_Stage >= CompleteStage || !frame.IsSuccess() ||
            (m_bHasFrame && frame.FrameNumber <= m_LastFrame))
        {
            return Fail(reason, "取得の段階またはフレームの順序が不正です");
        }
        m_LastFrame = frame.FrameNumber;
        m_bHasFrame = true;
        const bool bTemporal = m_Stage < DisplayStage;
        const MarkerSpec& marker = Markers[bTemporal ? 0 : 1];
        RgbaFloatImage image;
        const bool bBgra = frame.Format == RHI::Format::B8G8R8A8_UNORM ||
                           frame.Format == RHI::Format::B8G8R8A8_SRGB;
        if (bTemporal)
        {
            if (DecodeCapturedRgbaFloat(frame, image) != FloatImageStatus::Success)
            {
                return Fail(reason, "TAA 後の HDR 取得を読めません");
            }
        }
        else if ((!RHI::IsPresentationSrgbFormat(frame.Format) && !RHI::IsPresentationUnormFormat(frame.Format)) ||
                 frame.Transfer != RHI::PresentationTransfer::SRGB || frame.BytesPerPixel != 4 ||
                 frame.RowPitchBytes < frame.Width * 4 ||
                 frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height)
        {
            return Fail(reason, "BackBuffer の sRGB 取得の形式または格納範囲が不正です");
        }
        if (frame.Width < 144 || frame.Height < 80)
        {
            return Fail(reason, "目印の矩形を取得できる寸法ではありません");
        }
        const uint32_t colorIndex = m_Stage < TemporalCaptureCount ? 0 : 1;
        float expected[3];
        float actual[3] = {};
        // HDR の色は二進で正確な値。許容は 0.5 での binary16 の 1 ULP。
        // 表示は sRGB を最寄りの 8bit 値へ丸めて比較する。blit.frag の ±0.5 段のディザと
        // 機器の sRGB 符号化の丸めが隣の段へ振る分に、既存の表示試験と同じ ±1 段を許す。
        const float tolerance = bTemporal ? 0.00048828125f : 1.0f / 255.0f + 0.000001f;
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            expected[channel] = bTemporal ? TemporalColors[colorIndex].Color[channel]
                                          : std::round(EncodeSrgb(DisplayColor.Color[channel]) * 255.0f) / 255.0f;
        }
        // 境界の補間を避けて矩形の中央 16 x 16 画素を全て検査する。
        const uint32_t left = marker.Left + 8;
        const uint32_t top = marker.Top + 8;
        for (uint32_t y = top; y < top + 16; ++y)
        {
            for (uint32_t x = left; x < left + 16; ++x)
            {
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    const uint32_t byteChannel = bBgra && channel != 1 ? 2 - channel : channel;
                    const float value = bTemporal ? image.Values[(static_cast<size_t>(y) * image.Width + x) * 4 + channel]
                        : frame.Pixels[static_cast<size_t>(y) * frame.RowPitchBytes + x * 4 + byteChannel] / 255.0f;
                    if (!std::isfinite(value) || std::abs(value - expected[channel]) > tolerance)
                    {
                        std::cerr << marker.Name << " 段階=" << m_Stage << " 矩形=(" << left << ',' << top
                                  << ",16,16) 画素=(" << x << ',' << y << ") 成分=" << channel
                                  << " 期待=" << expected[channel] << " 実測=" << value << " 許容=" << tolerance << '\n';
                        return Fail(reason, "目印の色が期待値に一致しません");
                    }
                    actual[channel] += value / 256.0f;
                }
            }
        }
        float* first = bTemporal ? m_FirstTemporal[colorIndex] : m_FirstDisplay;
        const bool bFirst = bTemporal ? m_Stage % TemporalCaptureCount == 0 : m_Stage == DisplayStage;
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            if (bFirst)
            {
                first[channel] = actual[channel];
            }
            else if (std::abs(first[channel] - actual[channel]) > tolerance)
            {
                std::cerr << marker.Name << " 矩形=(" << left << ',' << top << ",16,16) 成分=" << channel
                          << " 初回=" << first[channel] << " 実測=" << actual[channel] << " 許容=" << tolerance << '\n';
                return Fail(reason, "連続取得または露出変更で目印の値が変わりました");
            }
        }
        if (bTemporal && m_Stage == DisplayStage - 1 &&
            std::abs(m_FirstTemporal[0][0] - m_FirstTemporal[1][0]) < 0.1f)
        {
            return Fail(reason, "拡張データ A/B の色が変化していません");
        }
        if (bTemporal && m_Stage % TemporalCaptureCount == TemporalCaptureCount - 1)
        {
            NorvesLib::Thread::ScopedLock lock(m_State->Evidence->Mutex);
            if (m_State->Evidence->bFailed || m_State->Evidence->Count[colorIndex] != TemporalCaptureCount)
            {
                std::cerr << marker.Name << " 隣接取得の期待=3 実測=" << m_State->Evidence->Count[colorIndex]
                          << " 失敗=" << m_State->Evidence->bFailed << '\n';
                return Fail(reason, "隣接した 3 フレームの HDR 矩形が一定ではありません");
            }
        }
        std::cout << marker.Name << " 段階=" << m_Stage << " frame=" << frame.FrameNumber
                  << " 矩形=(" << left << ',' << top << ",16,16) 期待=(" << expected[0] << ','
                  << expected[1] << ',' << expected[2] << ") 実測=(" << actual[0] << ',' << actual[1]
                  << ',' << actual[2] << ") 許容=" << tolerance << " 画素=256" << '\n';
        return true;
    }

    void RenderExtensionStageScenario::Advance()
    {
        ++m_Stage;
    }

    bool RenderExtensionStageScenario::RequestFollowup(Core::Rendering::FrameCaptureRequest& request) const
    {
        if (m_Stage >= CompleteStage)
        {
            return false;
        }
        request.SourceKind = m_Stage < DisplayStage ? FrameCaptureSourceKind::SceneColor
                                                   : FrameCaptureSourceKind::BackBuffer;
        return true;
    }
} // namespace NorvesLib::Test::RenderingValidation
