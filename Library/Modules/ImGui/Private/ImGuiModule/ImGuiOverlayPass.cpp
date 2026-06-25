#include "ImGuiModule/ImGuiOverlayPass.h"
#include "ImGuiModule/ImGuiDrawSnapshot.h"

#include "Rendering/ViewRenderContext.h"
#include "Rendering/FramePacket.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/SceneRenderer.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IDescriptorSet.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

#include "imgui.h"

#include <cstddef>

namespace NorvesLib::Modules::Gui
{
    namespace
    {
        constexpr const char *kLogCategory = "ImGui";
        constexpr const char *kPassName = "ImGuiOverlayPass";

        // mesh2d UBO(set0 binding0): scale/translate(各 vec2)。ImDrawData の DisplayPos/Size から
        // NDC への線形変換係数。レイアウトは mesh2d.vert の Mesh2DTransform と一致。
        struct Mesh2DTransform
        {
            float Scale[2] = {1.0f, 1.0f};
            float Translate[2] = {0.0f, 0.0f};
        };

        // overlay seam が渡す生ポインタ(IRenderPass*/IFramebuffer*)を、所有を持たない
        // TSharedPtr(aliasing 構築)へ橋渡しするヘルパ。空の制御ブロックと別れて参照
        // カウントを持たないため破棄時に delete されない。RHI の BeginRenderPass は
        // TSharedPtr を要求するが、seam の生存は RenderingCoordinator が保証する。
        template <typename T>
        Core::Container::TSharedPtr<T> NonOwning(T *raw)
        {
            return Core::Container::TSharedPtr<T>(Core::Container::TSharedPtr<T>{}, raw);
        }

        // mesh2d パイプラインの DescriptorSet レイアウト記述子(binding0 UBO=Vertex /
        // binding1 CombinedImageSampler=Pixel)。mesh2d.vert/frag の set0 と一致させる。
        RHI::DescriptorSetDesc MakeMesh2DDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;

            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex;
            desc.bindings.push_back(uboBinding);

            RHI::DescriptorBinding texBinding;
            texBinding.binding = 1;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(texBinding);

            return desc;
        }
    } // namespace

    const char *ImGuiOverlayPass::GetName() const
    {
        return kPassName;
    }

    bool ImGuiOverlayPass::InitializeGameThread(RHI::IDevice *device)
    {
        // MT 安全化①: GameThread・最初のフレーム投入前(=RenderThread アイドルで
        // グラフィックスキューを GameThread が専有)に呼ばれる。フォントアトラスの CPU
        // ピクセル(RGBA8)を ITexture へ一度だけアップロードし、サンプラーを生成する。
        // imgui 1.92 の動的テクスチャは使わず、レガシー単一アトラス(GetTexDataAsRGBA32)に
        // 固定するため、以後 RenderThread はテクスチャ(ImTextureData::Status)に触れない。
        if (m_bFontReady)
        {
            return true;
        }
        if (device == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory, "InitializeGameThread skipped: device null");
            return false;
        }

        ImGuiIO &io = ::ImGui::GetIO();
        if (io.Fonts == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory, "InitializeGameThread failed: io.Fonts null");
            return false;
        }

        // レガシー単一アトラスの RGBA8 ピクセルを取得する(必要なら内部で Build される)。
        unsigned char *pixels = nullptr;
        int width = 0;
        int height = 0;
        int bytesPerPixel = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            NORVES_LOG_WARNING(kLogCategory, "InitializeGameThread failed: font atlas pixels unavailable");
            return false;
        }

        // フォントテクスチャ(R8G8B8A8_UNORM・ShaderRead)を生成し CPU ピクセルをアップロードする
        // (CanvasView の白テクスチャと同経路: CreateTexture → Update(rowPitch, slicePitch))。
        RHI::TextureDesc textureDesc;
        textureDesc.Width = static_cast<uint32_t>(width);
        textureDesc.Height = static_cast<uint32_t>(height);
        textureDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        textureDesc.Usage = RHI::ResourceUsage::ShaderRead;
        textureDesc.DebugName = "ImGuiFontAtlas";
        m_FontTexture = device->CreateTexture(textureDesc);
        if (!m_FontTexture)
        {
            NORVES_LOG_WARNING(kLogCategory, "InitializeGameThread failed: CreateTexture(font)");
            return false;
        }
        const uint32_t rowPitch = static_cast<uint32_t>(width) * 4u;
        const uint32_t slicePitch = rowPitch * static_cast<uint32_t>(height);
        m_FontTexture->Update(pixels, rowPitch, slicePitch);

        // フォントアトラスの TexID に非 Invalid のセンチネルを GameThread で確定する。
        // 本パスはアトラスが 1 枚のみ(レガシー単一アトラス)で、Execute は cmd.GetTexID() を
        // 解決に使わず常に当該スロットの DescriptorSet をバインドするため、TexID の値自体は
        // 「ImDrawCmd が有効なテクスチャを指す(imgui のアサート/カリングを通す)」目印で足りる。
        // SetTexID は io.Fonts->TexRef._TexData が必要(GetTexDataAsRGBA32 が割当済み)で、
        // 全フレームのキャプチャ前(=最初のフレーム投入前)に GameThread で一度だけ設定する。
        if (io.Fonts->TexRef._TexData != nullptr)
        {
            io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        }

        // linear/clamp サンプラー(UI テクスチャの標準)。
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Linear;
        samplerDesc.filterMag = RHI::FilterMode::Linear;
        samplerDesc.filterMip = RHI::FilterMode::Linear;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_FontSampler = device->CreateSampler(samplerDesc);
        if (!m_FontSampler)
        {
            NORVES_LOG_WARNING(kLogCategory, "InitializeGameThread failed: CreateSampler(font)");
            m_FontTexture.reset();
            return false;
        }

        m_bFontReady = true;
        NORVES_LOG_INFO(kLogCategory,
                        "InitializeGameThread: font atlas uploaded on GameThread (%dx%d, legacy single atlas)",
                        width, height);
        return true;
    }

    bool ImGuiOverlayPass::Initialize(Core::Rendering::ViewRenderContext &context)
    {
        // seam②(RenderThread・録画窓内): mesh2d パイプライン + per-slot DescriptorSet +
        // VB/IB/UBO 用 DynamicBufferRing を生成する。ShaderManager は seam の
        // ViewRenderContext からのみ得られるためここで構築する。パイプライン/バッファ生成は
        // テクスチャ Status を触らず MT 安全(フォントテクスチャは①で GameThread が確定済み)。
        if (m_bInitialized)
        {
            return true;
        }
        if (!m_bFontReady || !m_FontTexture || !m_FontSampler)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "Initialize(seam): font atlas not ready on GameThread; overlay disabled");
            return false;
        }
        if (context.Device == nullptr || context.ShaderMgr == nullptr ||
            context.OverlayLoadRenderPass == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "Initialize(seam) skipped: Device/ShaderMgr/OverlayLoadRenderPass null");
            return false;
        }

        // ---- シェーダー ----
        RHI::ShaderPtr vertexShader = context.ShaderMgr->LoadShader("mesh2d.vert", RHI::ShaderStage::Vertex);
        RHI::ShaderPtr pixelShader = context.ShaderMgr->LoadShader("mesh2d.frag", RHI::ShaderStage::Pixel);
        if (!vertexShader || !pixelShader)
        {
            NORVES_LOG_WARNING(kLogCategory, "Initialize(seam) failed: mesh2d shader load");
            return false;
        }

        const RHI::DescriptorSetDesc descriptorSetDesc = MakeMesh2DDescriptorSetDesc();

        // ---- パイプライン(頂点レイアウト = ImDrawVert: pos@0 / uv@8 / col@16・stride 20) ----
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.pixelShader = pixelShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        // overlay load 経路は depth load。深度テスト/書き込みは無効。
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = static_cast<uint32_t>(sizeof(ImDrawVert));
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        RHI::VertexAttributeDesc posAttr;
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = RHI::Format::R32G32_FLOAT;
        posAttr.offset = static_cast<uint32_t>(offsetof(ImDrawVert, pos));
        pipelineDesc.vertexAttributes.push_back(posAttr);

        RHI::VertexAttributeDesc uvAttr;
        uvAttr.location = 1;
        uvAttr.binding = 0;
        uvAttr.format = RHI::Format::R32G32_FLOAT;
        uvAttr.offset = static_cast<uint32_t>(offsetof(ImDrawVert, uv));
        pipelineDesc.vertexAttributes.push_back(uvAttr);

        RHI::VertexAttributeDesc colAttr;
        colAttr.location = 2;
        colAttr.binding = 0;
        colAttr.format = RHI::Format::R8G8B8A8_UNORM;
        colAttr.offset = static_cast<uint32_t>(offsetof(ImDrawVert, col));
        pipelineDesc.vertexAttributes.push_back(colAttr);

        // ストレートαブレンド(ImGui の頂点色は非乗算済みα。mesh2d.frag もストレート出力)。
        RHI::BlendAttachmentDesc blend;
        blend.blendEnable = true;
        blend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
        blend.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blend.colorBlendOp = RHI::BlendOp::Add;
        blend.srcAlphaBlendFactor = RHI::BlendFactor::One;
        blend.dstAlphaBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blend.alphaBlendOp = RHI::BlendOp::Add;
        blend.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blend);

        pipelineDesc.renderPass = NonOwning(context.OverlayLoadRenderPass);
        pipelineDesc.descriptorSetLayouts.push_back(descriptorSetDesc);

        m_Pipeline = context.Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING(kLogCategory, "Initialize(seam) failed: CreateGraphicsPipeline");
            return false;
        }

        // ---- VB/IB/UBO リング(per-slot=FramePacket スロット数) ----
        // 初期容量は控えめに確保し、不足時に DynamicBufferRing が 2 倍リサイズする
        // (VB/IB は実描画量で伸びる。UBO は固定 16B なのでリサイズしない)。
        constexpr uint32_t slotCount = Core::Rendering::FRAME_PACKET_BUFFER_COUNT;
        const bool bRingsOk =
            m_VertexRing.Initialize(context.Device, slotCount, RHI::ResourceUsage::VertexBuffer,
                                    static_cast<uint64_t>(sizeof(ImDrawVert)) * 4096ull) &&
            m_IndexRing.Initialize(context.Device, slotCount, RHI::ResourceUsage::IndexBuffer,
                                   static_cast<uint64_t>(sizeof(ImDrawIdx)) * 8192ull) &&
            m_UniformRing.Initialize(context.Device, slotCount, RHI::ResourceUsage::ConstantBuffer,
                                     static_cast<uint64_t>(sizeof(Mesh2DTransform)));
        if (!bRingsOk)
        {
            NORVES_LOG_WARNING(kLogCategory, "Initialize(seam) failed: DynamicBufferRing init");
            m_Pipeline.reset();
            m_VertexRing.Shutdown();
            m_IndexRing.Shutdown();
            m_UniformRing.Shutdown();
            return false;
        }

        // ---- per-slot DescriptorSet(共有フォントテクスチャ/サンプラー + 当該スロットの UBO) ----
        // 各スロットの DescriptorSet を「binding1=共有フォントテクスチャ/サンプラー(不変)」+
        // 「binding0=当該スロットの固定 UBO buffer」で一度だけ束ねて Update する。UBO は固定 16B で
        // リサイズしないため DynamicBufferRing のスロット buffer は安定(Initialize 時の実体)。
        // 以後 Execute は UBO の中身のみ書き換え(CPU マップ)、DescriptorSet を再 Update しない
        // (=描画中に vkUpdateDescriptorSets を起こさず、in-flight 中の DescriptorSet 更新を避ける)。
        for (uint32_t i = 0; i < slotCount; ++i)
        {
            RHI::DescriptorSetPtr descriptorSet = context.Device->CreateDescriptorSet(descriptorSetDesc);
            if (!descriptorSet)
            {
                NORVES_LOG_WARNING(kLogCategory, "Initialize(seam) failed: CreateDescriptorSet slot %u", i);
                m_Pipeline.reset();
                m_VertexRing.Shutdown();
                m_IndexRing.Shutdown();
                m_UniformRing.Shutdown();
                for (uint32_t j = 0; j < slotCount; ++j)
                {
                    m_SlotDescriptorSets[j].reset();
                }
                return false;
            }

            // 当該スロットの安定 UBO buffer を取得する(Upload(slot, nullptr, 0) は書き込まず
            // スロット buffer を返す)。
            RHI::BufferPtr uboBuffer = m_UniformRing.Upload(i, nullptr, 0);
            descriptorSet->BindConstantBuffer(0, uboBuffer, 0,
                                              static_cast<uint32_t>(sizeof(Mesh2DTransform)));
            descriptorSet->BindTexture(1, m_FontTexture);
            descriptorSet->BindSampler(1, m_FontSampler);
            descriptorSet->Update();
            m_SlotDescriptorSets[i] = descriptorSet;
        }

        // フォントアトラスの TexID は InitializeGameThread(GameThread・全フレームキャプチャ前)で
        // 既にセンチネル確定済み。Execute は cmd.GetTexID() を解決に使わず常に当該スロットの
        // DescriptorSet をバインドするため、ここでの SetTexID 再設定は不要(RenderThread から
        // imgui コンテキスト状態を書き換えない=スレッド安全)。

        m_bInitialized = true;
        NORVES_LOG_INFO(kLogCategory, "Initialize(seam): mesh2d pipeline + rings + descriptor ready");
        return true;
    }

    void ImGuiOverlayPass::Shutdown()
    {
        // RenderThread 静止後・device 生存中に ImGuiModule から駆動される。全 RHI リソースを
        // 本パスが所有しているため明示解放する(順序は任意・参照カウントで安全)。
        for (uint32_t i = 0; i < Core::Rendering::FRAME_PACKET_BUFFER_COUNT; ++i)
        {
            m_SlotDescriptorSets[i].reset();
        }
        m_Pipeline.reset();
        m_VertexRing.Shutdown();
        m_IndexRing.Shutdown();
        m_UniformRing.Shutdown();
        m_FontSampler.reset();
        m_FontTexture.reset();
        m_PendingDrawData = nullptr;
        m_bInitialized = false;
        m_bFontReady = false;
        NORVES_LOG_INFO(kLogCategory, "ImGuiOverlayPass Shutdown");
    }

    void ImGuiOverlayPass::Setup(Core::Rendering::ViewRenderContext & /*context*/)
    {
        // 一時リソースは使わない。準備フェーズは no-op。
    }

    void ImGuiOverlayPass::OnAssignedToPacket(uint32_t slotIndex)
    {
        // GameThread。書き込み中パケットのスロット index が渡る。当該スロットは Writing
        // (GT 専有)で RT は読まないため、ここで m_SlotSnapshots[slotIndex] へライブ
        // ImDrawData をディープクローンしても RT の per-slot 読取と競合しない。
        if (slotIndex >= Core::Rendering::FRAME_PACKET_BUFFER_COUNT)
        {
            return;
        }
        m_SlotSnapshots[slotIndex].Capture(m_PendingDrawData);
    }

    void ImGuiOverlayPass::Execute(Core::Rendering::ViewRenderContext &context)
    {
        // 録画窓内・executor 外。CommandList と load render pass/framebuffer/Renderer が
        // 揃わない場合は何もしない(構造的に no-op)。
        if (context.CommandList == nullptr || context.OverlayLoadRenderPass == nullptr ||
            context.OverlayLoadFramebuffer == nullptr || context.Renderer == nullptr)
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "Execute skipped: CommandList/OverlayLoadRenderPass/Framebuffer/Renderer null");
            return;
        }
        if (!m_bInitialized)
        {
            return;
        }

        // RenderThread。処理中パケットのスロット index で per-slot クローンを選ぶ。当該
        // スロットは Reading(RT 専有)で GT は同スロットへ書き込まないため安全。
        const uint32_t slotIndex = context.OverlayPacketSlotIndex;
        if (slotIndex >= Core::Rendering::FRAME_PACKET_BUFFER_COUNT)
        {
            return;
        }
        const ImGuiDrawSnapshot &snapshot = m_SlotSnapshots[slotIndex];
        if (!snapshot.HasDrawData())
        {
            return;
        }
        const ::ImDrawData *drawData = snapshot.GetDrawData();
        if (drawData == nullptr || drawData->CmdListsCount <= 0)
        {
            return;
        }

        RHI::ICommandList *commandList = context.CommandList;

        // 経路依存の load render pass を Begin(Begin/EndRecording は呼ばない)。最終 blit
        // 後の back buffer へ load-blend する(legacy=PresentationLoad / composite=Graph)。
        commandList->BeginRenderPass(NonOwning(context.OverlayLoadRenderPass),
                                     NonOwning(context.OverlayLoadFramebuffer));

        RenderDrawData(context, drawData, slotIndex);

        commandList->EndRenderPass();
    }

    void ImGuiOverlayPass::RenderDrawData(Core::Rendering::ViewRenderContext &context,
                                          const ::ImDrawData *drawData,
                                          uint32_t slotIndex)
    {
        RHI::ICommandList *commandList = context.CommandList;

        const int totalVtx = drawData->TotalVtxCount;
        const int totalIdx = drawData->TotalIdxCount;
        if (totalVtx <= 0 || totalIdx <= 0)
        {
            return;
        }

        // 全 ImDrawList の頂点/インデックスを 1 本ずつへ連結する(imgui_impl_vulkan と同方式)。
        // ImDrawVert は mesh2d 頂点(pos/uv/col・stride 20)と完全一致のためそのままコピーできる。
        Core::Container::VariableArray<ImDrawVert> vertices;
        Core::Container::VariableArray<ImDrawIdx> indices;
        vertices.reserve(static_cast<size_t>(totalVtx));
        indices.reserve(static_cast<size_t>(totalIdx));
        for (int n = 0; n < drawData->CmdListsCount; ++n)
        {
            const ::ImDrawList *list = drawData->CmdLists[n];
            if (list == nullptr)
            {
                continue;
            }
            for (int v = 0; v < list->VtxBuffer.Size; ++v)
            {
                vertices.push_back(list->VtxBuffer[v]);
            }
            for (int i = 0; i < list->IdxBuffer.Size; ++i)
            {
                indices.push_back(list->IdxBuffer[i]);
            }
        }
        if (vertices.empty() || indices.empty())
        {
            return;
        }

        // 当該スロットの VB/IB へ Upload する(per-slot=FramePacket スロット排他で安全)。
        RHI::BufferPtr vertexBuffer = m_VertexRing.Upload(
            slotIndex, vertices.data(), static_cast<uint64_t>(vertices.size()) * sizeof(ImDrawVert));
        RHI::BufferPtr indexBuffer = m_IndexRing.Upload(
            slotIndex, indices.data(), static_cast<uint64_t>(indices.size()) * sizeof(ImDrawIdx));
        if (!vertexBuffer || !indexBuffer)
        {
            NORVES_LOG_WARNING(kLogCategory, "RenderDrawData: VB/IB upload failed");
            return;
        }

        // ---- UBO(scale/translate)を更新する ----
        // DisplaySize から NDC への線形変換。framebufferScale はピクセル↔フレームバッファの
        // 密度差(Retina 等)。Windows では通常 (1,1)。
        const ImVec2 displayPos = drawData->DisplayPos;
        const ImVec2 displaySize = drawData->DisplaySize;
        const ImVec2 fbScale = drawData->FramebufferScale;
        if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
        {
            return;
        }
        Mesh2DTransform transform;
        transform.Scale[0] = 2.0f / displaySize.x;
        transform.Scale[1] = 2.0f / displaySize.y;
        transform.Translate[0] = -1.0f - displayPos.x * transform.Scale[0];
        transform.Translate[1] = -1.0f - displayPos.y * transform.Scale[1];
        // UBO は固定 16B でリサイズしないため、スロット buffer は Initialize で DescriptorSet へ
        // 束ねた安定実体。ここでは中身のみ CPU マップ書き換えする(DescriptorSet は再 Update
        // しない=描画中の vkUpdateDescriptorSets を起こさない)。slot 排他のため当該 buffer は
        // 前フレームの GPU in-flight と競合しない(同一スロットは回収済みになるまで再書込みされない)。
        RHI::BufferPtr uniformBuffer = m_UniformRing.Upload(slotIndex, &transform, sizeof(transform));
        if (!uniformBuffer)
        {
            NORVES_LOG_WARNING(kLogCategory, "RenderDrawData: UBO upload failed");
            return;
        }
        RHI::DescriptorSetPtr descriptorSet = m_SlotDescriptorSets[slotIndex];
        if (!descriptorSet)
        {
            return;
        }

        // back buffer 全体を覆う viewport(Mesh2D 経路は viewport を設定しないため明示する)。
        const float fbWidth = static_cast<float>(context.OverlayLoadFramebuffer->GetWidth());
        const float fbHeight = static_cast<float>(context.OverlayLoadFramebuffer->GetHeight());
        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = fbWidth;
        viewport.height = fbHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        commandList->SetViewport(viewport);

        // ---- 各 ImDrawCmd を Mesh2D DrawCommand へ変換して発行する ----
        const int32_t fbWidthI = static_cast<int32_t>(fbWidth);
        const int32_t fbHeightI = static_cast<int32_t>(fbHeight);

        Core::Container::VariableArray<Core::Rendering::DrawCommand> commands;
        uint32_t globalVtxOffset = 0;
        uint32_t globalIdxOffset = 0;
        for (int n = 0; n < drawData->CmdListsCount; ++n)
        {
            const ::ImDrawList *list = drawData->CmdLists[n];
            if (list == nullptr)
            {
                continue;
            }
            for (int c = 0; c < list->CmdBuffer.Size; ++c)
            {
                const ImDrawCmd &cmd = list->CmdBuffer[c];
                if (cmd.UserCallback != nullptr)
                {
                    // コールバックは未対応(ImGui のデフォルト UI は使わない)。スキップする。
                    continue;
                }
                if (cmd.ElemCount == 0)
                {
                    continue;
                }

                // ClipRect(画面座標)→ framebuffer ピクセルの ScissorRect へ変換しクランプする。
                float clipMinX = (cmd.ClipRect.x - displayPos.x) * fbScale.x;
                float clipMinY = (cmd.ClipRect.y - displayPos.y) * fbScale.y;
                float clipMaxX = (cmd.ClipRect.z - displayPos.x) * fbScale.x;
                float clipMaxY = (cmd.ClipRect.w - displayPos.y) * fbScale.y;
                if (clipMinX < 0.0f) { clipMinX = 0.0f; }
                if (clipMinY < 0.0f) { clipMinY = 0.0f; }
                if (clipMaxX > fbWidth) { clipMaxX = fbWidth; }
                if (clipMaxY > fbHeight) { clipMaxY = fbHeight; }
                if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
                {
                    // 完全にクリップ外。発行しない。
                    continue;
                }

                Core::Rendering::DrawCommand command = Core::Rendering::DrawCommand::CreateMesh2D();
                command.Pipeline = m_Pipeline;
                command.DescriptorSet = descriptorSet;
                command.DescriptorSetSlot = 0;
                command.Mesh2D.VertexBuffer = vertexBuffer;
                command.Mesh2D.IndexBuffer = indexBuffer;
                command.Mesh2D.IndexCount = cmd.ElemCount;
                command.Mesh2D.IndexOffset = globalIdxOffset + cmd.IdxOffset;
                command.Mesh2D.VertexOffset = static_cast<int32_t>(globalVtxOffset + cmd.VtxOffset);
                static_assert(sizeof(ImDrawIdx) == 2,
                              "ImDrawIdx は 16bit 前提(Mesh2D の IndexType=Uint16)。変更時は IndexType を見直すこと。");
                command.Mesh2D.IndexType = RHI::IndexType::Uint16;
                command.HasScissor = true;
                command.Scissor.left = static_cast<int32_t>(clipMinX);
                command.Scissor.top = static_cast<int32_t>(clipMinY);
                command.Scissor.right = static_cast<int32_t>(clipMaxX);
                command.Scissor.bottom = static_cast<int32_t>(clipMaxY);
                // ScissorRect を framebuffer 範囲に最終クランプ(整数化後の保険)。
                if (command.Scissor.right > fbWidthI) { command.Scissor.right = fbWidthI; }
                if (command.Scissor.bottom > fbHeightI) { command.Scissor.bottom = fbHeightI; }
                commands.push_back(command);
            }
            globalVtxOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
            globalIdxOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
        }

        if (commands.empty())
        {
            return;
        }

        // Core の汎用 Mesh2D 経路で実行する(SceneRenderer::ExecuteDrawCommand の Mesh2D 分岐が
        // SetScissor→SetVertexBuffer→SetIndexBuffer(IndexType)→DrawIndexed を行う)。
        context.Renderer->ExecuteDrawCommands(commands, commandList);
    }
} // namespace NorvesLib::Modules::Gui
