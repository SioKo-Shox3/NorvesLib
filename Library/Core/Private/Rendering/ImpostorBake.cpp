#include "Rendering/ImpostorBake.h"

#include "Rendering/GpuResourceStore.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/ShaderManager.h"
#include "RHI/FormatInfo.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // シェーダーの std140/std430 と同じ、パディングの無い vec4 の並び。
        struct BakeCell
        {
            float Right[4];
            float Up[4];
            float ViewDirection[4];
        };
        struct BakeParams
        {
            float BoundsCenterRadius[4];
            float BaseColor[4];
            float MaterialFlags[4];
            float Params[4];
        };
        static_assert(sizeof(BakeCell) == 48);
        static_assert(sizeof(BakeParams) == 64);
        static_assert(sizeof(Mesh3DVertex) == 32);

        struct BakeSectionResources
        {
            ImpostorBakeSection Section;
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr Descriptors;
            RHI::TexturePtr Textures[5];
        };

        // 途中で失敗しても、提出済みの GPU 処理が終わるまで全資源を保持する。
        struct BakeResources
        {
            const ImpostorBakeRequest& Request;
            RHI::TexturePtr Atlases[4];
            TextureHandle Handles[4] = {TextureHandle::Invalid(), TextureHandle::Invalid(),
                                        TextureHandle::Invalid(), TextureHandle::Invalid()};
            RHI::TexturePtr Depth;
            Container::VariableArray<RHI::TexturePtr> Defaults;
            RHI::SamplerPtr Sampler;
            RHI::BufferPtr Vertices;
            RHI::BufferPtr Indices;
            RHI::BufferPtr Cells;
            Container::VariableArray<BakeSectionResources> Sections;
            RHI::RenderPassPtr RenderPass;
            RHI::PipelinePtr Pipeline;
            RHI::FramebufferPtr Framebuffer;
            RHI::CommandListPtr Commands;
            bool bSubmissionPending = false;
            bool bKeepAtlases = false;

            explicit BakeResources(const ImpostorBakeRequest& request) : Request(request) {}

            ~BakeResources()
            {
                // Submit 内で例外になった場合も、キューに渡った可能性のある参照を先に待つ。
                if (bSubmissionPending)
                {
                    Request.Device->WaitIdle();
                }
                if (Request.RenderWorldForSync)
                {
                    Request.RenderWorldForSync->WaitForRender();
                }
                Commands.reset();
                Framebuffer.reset();
                Pipeline.reset();
                RenderPass.reset();
                Sections.clear();
                Cells.reset();
                Indices.reset();
                Vertices.reset();
                Depth.reset();
                Defaults.clear();
                Sampler.reset();
                if (!bKeepAtlases)
                {
                    for (TextureHandle handle : Handles)
                    {
                        if (handle.IsValid())
                        {
                            Request.Textures->ReleaseTexture(handle);
                        }
                    }
                }
            }
        };

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFiniteMaterial(const ImpostorBakeMaterial& material)
        {
            return IsFinite(material.BaseColor) && std::isfinite(material.Metallic) &&
                   std::isfinite(material.Roughness) && std::isfinite(material.OcclusionStrength) &&
                   std::isfinite(material.AlphaCutoff) && std::isfinite(material.NormalScale);
        }

        bool HasAtlasDimensionOverflow(uint32_t cellResolution, uint32_t axisCellCount)
        {
            return axisCellCount > std::numeric_limits<uint32_t>::max() / cellResolution;
        }

        bool IsSupportedImpostorAtlasColorFormat(TextureCreateInfo::Format format)
        {
            const RHI::FormatInfo& info = RHI::GetFormatInfo(ToRHITextureFormat(format));
            if (info.bDepth || info.bCompressed || info.bUnsignedInteger)
            {
                return false;
            }

            // R16_UNORM・R16G16_UNORM・R10G10B10A2_UNORM・R11G11B10_FLOAT は、
            // 描画先としての可否が装置で未確認のため許可しない。
            return format == TextureCreateInfo::Format::RGBA8_UNORM ||
                   format == TextureCreateInfo::Format::RGBA8_SRGB ||
                   format == TextureCreateInfo::Format::RGBA16_FLOAT ||
                   format == TextureCreateInfo::Format::RGBA32_FLOAT ||
                   format == TextureCreateInfo::Format::R8_UNORM ||
                   format == TextureCreateInfo::Format::RG8_UNORM;
        }

        RHI::BufferPtr CreateBakeBuffer(RHI::IDevice* device, const void* data, uint64_t size,
                                        RHI::ResourceUsage usage, const char* name)
        {
            // 同期ベイク専用のバッファ。提出後には書き換えず、完了まで保持する。
            RHI::BufferDesc desc(size, usage, true, name);
            auto buffer = device->CreateBuffer(desc);
            if (buffer)
            {
                buffer->Update(data, size);
            }
            return buffer;
        }

        RHI::TexturePtr CreateBakePixel(BakeResources& resources, uint8_t r, uint8_t g, uint8_t b)
        {
            RHI::TextureDesc desc;
            desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            desc.DebugName = "ImpostorBakeMaterial1x1";
            auto texture = resources.Request.Device->CreateTexture(desc);
            if (texture)
            {
                // 更新で例外になった場合も、解放前の同期対象に含める。
                resources.Defaults.push_back(texture);
                const uint8_t pixel[4] = {r, g, b, 255};
                texture->Update(pixel, sizeof(pixel), sizeof(pixel));
            }
            return texture;
        }

        RHI::TexturePtr CreateBakeConstant(BakeResources& resources, float value)
        {
            const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            const auto gray = static_cast<uint8_t>(clamped * 255.0f + 0.5f);
            return CreateBakePixel(resources, gray, gray, gray);
        }
    } // namespace

    bool ImpostorBakeService::ValidateInput(const ImpostorBakeRequest& request,
                                           ImpostorBakeMetadata& outMetadata) const
    {
        outMetadata = ImpostorBakeMetadata{};
        if (!request.Textures || !request.Device || !request.Shaders)
        {
            NORVES_LOG_ERROR("ImpostorBake", "テクスチャ管理・デバイス・シェーダー管理が必要です");
            return false;
        }

        const ImpostorBakeInput& input = request.Input;
        if (input.Vertices.empty() || input.Indices.empty() || input.Indices.size() % 3u != 0u ||
            input.Vertices.size() > std::numeric_limits<uint32_t>::max() ||
            input.Indices.size() > std::numeric_limits<uint32_t>::max())
        {
            NORVES_LOG_ERROR("ImpostorBake", "メッシュには32ビットの範囲内の頂点と三角形インデックスが必要です");
            return false;
        }
        if (input.CellResolution == 0 || input.AxisCellCountX == 0 || input.AxisCellCountY == 0 ||
            HasAtlasDimensionOverflow(input.CellResolution, input.AxisCellCountX) ||
            HasAtlasDimensionOverflow(input.CellResolution, input.AxisCellCountY))
        {
            NORVES_LOG_ERROR("ImpostorBake", "セルの解像度・個数が0、またはアトラス寸法が32ビットを超えます");
            return false;
        }
        const uint64_t cellCount = uint64_t(input.AxisCellCountX) * input.AxisCellCountY;
        const uint64_t maxStorageBytes = request.Device->GetCapabilities().MaxStorageBufferRange;
        if (cellCount > std::numeric_limits<uint32_t>::max() / sizeof(BakeCell) ||
            (maxStorageBytes != 0 && cellCount > maxStorageBytes / sizeof(BakeCell)) ||
            input.CellResolution * input.AxisCellCountX > uint32_t(std::numeric_limits<int32_t>::max()) ||
            input.CellResolution * input.AxisCellCountY > uint32_t(std::numeric_limits<int32_t>::max()))
        {
            NORVES_LOG_ERROR("ImpostorBake", "セルのバッファまたはシザー矩形の上限を超えます");
            return false;
        }
        if (input.Layout != ImpostorOctahedralLayout::HemiOctahedral &&
            input.Layout != ImpostorOctahedralLayout::FullOctahedral)
        {
            NORVES_LOG_ERROR("ImpostorBake", "八面体の配置が無効です");
            return false;
        }
        if (!IsSupportedImpostorAtlasColorFormat(input.PixelFormat))
        {
            NORVES_LOG_ERROR("ImpostorBake", "アトラスには対応する色形式が必要です");
            return false;
        }
        for (const ImpostorBakeSection& section : input.Sections)
        {
            if (section.IndexOffset % 3u != 0u || section.IndexCount == 0 || section.IndexCount % 3u != 0u ||
                section.IndexOffset > input.Indices.size() ||
                section.IndexCount > input.Indices.size() - section.IndexOffset ||
                !IsFiniteMaterial(section.Material))
            {
                NORVES_LOG_ERROR("ImpostorBake", "区間のインデックス範囲または材質の定数が無効です");
                return false;
            }
        }

        Math::Vector3 boundsMin(std::numeric_limits<float>::max());
        Math::Vector3 boundsMax(-std::numeric_limits<float>::max());
        for (uint32_t index : input.Indices)
        {
            if (index >= input.Vertices.size())
            {
                NORVES_LOG_ERROR("ImpostorBake", "メッシュのインデックスが頂点の範囲を超えます");
                return false;
            }
            const auto& p = input.Vertices[index].Position;
            if (!IsFinite(Math::Vector3(p[0], p[1], p[2])))
            {
                NORVES_LOG_ERROR("ImpostorBake", "参照された頂点の位置が有限値ではありません");
                return false;
            }
            boundsMin.x = p[0] < boundsMin.x ? p[0] : boundsMin.x;
            boundsMin.y = p[1] < boundsMin.y ? p[1] : boundsMin.y;
            boundsMin.z = p[2] < boundsMin.z ? p[2] : boundsMin.z;
            boundsMax.x = p[0] > boundsMax.x ? p[0] : boundsMax.x;
            boundsMax.y = p[1] > boundsMax.y ? p[1] : boundsMax.y;
            boundsMax.z = p[2] > boundsMax.z ? p[2] : boundsMax.z;
        }
        // 中間の加算・距離の二乗は倍精度で計算し、有限な大きい座標も扱う。
        const Math::Vector3 center(
            static_cast<float>((double(boundsMin.x) + boundsMax.x) * 0.5),
            static_cast<float>((double(boundsMin.y) + boundsMax.y) * 0.5),
            static_cast<float>((double(boundsMin.z) + boundsMax.z) * 0.5));
        double radiusSquared = 0.0;
        for (uint32_t index : input.Indices)
        {
            const auto& p = input.Vertices[index].Position;
            const double x = double(p[0]) - center.x;
            const double y = double(p[1]) - center.y;
            const double z = double(p[2]) - center.z;
            const double distanceSquared = x * x + y * y + z * z;
            radiusSquared = distanceSquared > radiusSquared ? distanceSquared : radiusSquared;
        }
        const float radius = static_cast<float>(std::sqrt(radiusSquared) * 1.01);
        if (!(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(2.0f * radius))
        {
            NORVES_LOG_ERROR("ImpostorBake", "境界の半径が0、または正射影の範囲が有限値ではありません");
            return false;
        }

        outMetadata.CellResolution = input.CellResolution;
        outMetadata.AxisCellCountX = input.AxisCellCountX;
        outMetadata.AxisCellCountY = input.AxisCellCountY;
        outMetadata.AtlasWidth = input.CellResolution * input.AxisCellCountX;
        outMetadata.AtlasHeight = input.CellResolution * input.AxisCellCountY;
        outMetadata.VertexCount = static_cast<uint32_t>(input.Vertices.size());
        outMetadata.IndexCount = static_cast<uint32_t>(input.Indices.size());
        outMetadata.PixelFormat = input.PixelFormat;
        outMetadata.Layout = input.Layout;
        outMetadata.BoundsCenter = center;
        outMetadata.BoundsRadius = radius;
        outMetadata.BoundsMin = boundsMin;
        outMetadata.BoundsMax = boundsMax;
        return true;
    }

    bool ImpostorBakeService::BakeProceduralAtlas(const ImpostorBakeRequest& request,
                                                 ImpostorBakeResult& outResult) const
    {
        outResult = ImpostorBakeResult{};
        ImpostorBakeMetadata metadata;
        if (!ValidateInput(request, metadata))
        {
            return false;
        }

        BakeResources resources(request);
        try
        {
            const auto& input = request.Input;
            auto* device = request.Device;
            auto vertexShader = request.Shaders->LoadShader("impostor_bake.vert", RHI::ShaderStage::Vertex);
            auto pixelShader = request.Shaders->LoadShader("impostor_bake.frag", RHI::ShaderStage::Pixel);
            if (!vertexShader || !pixelShader)
            {
                NORVES_LOG_ERROR("ImpostorBake", "ベイクのシェーダーを読み込めません");
                return false;
            }

            const RHI::Format formats[4] = {ToRHITextureFormat(input.PixelFormat),
                RHI::Format::R8G8B8A8_UNORM, RHI::Format::R8G8B8A8_UNORM, RHI::Format::R8_UNORM};
            const char* suffixes[4] = {"_Albedo", "_Normal", "_Orm", "_Depth"};
            const char* debugNames[4] = {"ImpostorBakeAlbedoAtlas", "ImpostorBakeNormalAtlas",
                                         "ImpostorBakeOrmAtlas", "ImpostorBakeDepthAtlas"};
            RHI::RenderPassDesc renderPassDesc;
            RHI::FramebufferDesc framebufferDesc;
            for (uint32_t i = 0; i < 4; ++i)
            {
                if (!device->IsFormatSupported(formats[i], RHI::FormatUsage::RenderTarget) ||
                    !device->IsFormatSupported(formats[i], RHI::FormatUsage::Sampled))
                {
                    NORVES_LOG_ERROR("ImpostorBake", "デバイスがアトラスの描画・読み取り形式に対応していません");
                    return false;
                }
                const Container::String name = input.DebugName + suffixes[i];
                auto desc = RHI::TextureDesc::RenderTarget(metadata.AtlasWidth, metadata.AtlasHeight,
                                                           formats[i], debugNames[i]);
                desc.Usage = desc.Usage | RHI::ResourceUsage::TransferSrc;
                resources.Atlases[i] = device->CreateTexture(desc);
                if (!resources.Atlases[i])
                {
                    NORVES_LOG_ERROR("ImpostorBake", "アトラスのテクスチャを作れません");
                    return false;
                }
                resources.Handles[i] = request.Textures->RegisterExternalTexture(resources.Atlases[i], name);
                if (!resources.Handles[i].IsValid())
                {
                    NORVES_LOG_ERROR("ImpostorBake", "アトラスのテクスチャを登録できません");
                    return false;
                }
                RHI::AttachmentDesc attachment;
                attachment.format = formats[i];
                for (float& value : attachment.clearColor)
                {
                    value = i == 3 ? 1.0f : 0.0f;
                }
                attachment.initialState = RHI::ResourceState::Undefined;
                attachment.finalState = RHI::ResourceState::ShaderResource;
                renderPassDesc.colorAttachments.push_back(attachment);
                framebufferDesc.colorTargets.push_back(resources.Atlases[i]);
            }
            auto depthDesc = RHI::TextureDesc::DepthStencil(metadata.AtlasWidth, metadata.AtlasHeight,
                                                            RHI::Format::D32_FLOAT, "ImpostorBakeDepthBuffer");
            resources.Depth = device->CreateTexture(depthDesc);
            if (!resources.Depth)
            {
                NORVES_LOG_ERROR("ImpostorBake", "ベイクの深度バッファを作れません");
                return false;
            }
            renderPassDesc.hasDepthStencil = true;
            renderPassDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
            renderPassDesc.depthStencilAttachment.isDepthStencil = true;
            renderPassDesc.depthStencilAttachment.clearDepth = 1.0f;
            renderPassDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::DontCare;
            renderPassDesc.depthStencilAttachment.finalState = RHI::ResourceState::DepthWrite;
            resources.RenderPass = device->CreateRenderPass(renderPassDesc);
            if (!resources.RenderPass)
            {
                NORVES_LOG_ERROR("ImpostorBake", "ベイクのレンダーパスを作れません");
                return false;
            }
            framebufferDesc.renderPass = resources.RenderPass;
            framebufferDesc.depthStencilTarget = resources.Depth;
            framebufferDesc.width = metadata.AtlasWidth;
            framebufferDesc.height = metadata.AtlasHeight;
            resources.Framebuffer = device->CreateFramebuffer(framebufferDesc);
            if (!resources.Framebuffer)
            {
                NORVES_LOG_ERROR("ImpostorBake", "ベイクのフレームバッファを作れません");
                return false;
            }

            resources.Vertices = CreateBakeBuffer(device, input.Vertices.data(),
                uint64_t(metadata.VertexCount) * sizeof(Mesh3DVertex), RHI::ResourceUsage::VertexBuffer,
                "ImpostorBakeVertices");
            resources.Indices = CreateBakeBuffer(device, input.Indices.data(),
                uint64_t(metadata.IndexCount) * sizeof(uint32_t), RHI::ResourceUsage::IndexBuffer,
                "ImpostorBakeIndices");
            Container::VariableArray<BakeCell> cells;
            cells.reserve(size_t(input.AxisCellCountX) * input.AxisCellCountY);
            for (uint32_t cy = 0; cy < input.AxisCellCountY; ++cy)
            {
                for (uint32_t cx = 0; cx < input.AxisCellCountX; ++cx)
                {
                    const auto basis = ComputeImpostorCellBasis(ComputeImpostorCellDirection(
                        cx, cy, input.AxisCellCountX, input.AxisCellCountY, input.Layout));
                    cells.push_back(BakeCell{{basis.Right.x, basis.Right.y, basis.Right.z, 0.0f},
                        {basis.Up.x, basis.Up.y, basis.Up.z, 0.0f},
                        {basis.ViewDirection.x, basis.ViewDirection.y, basis.ViewDirection.z, 0.0f}});
                }
            }
            const auto cellBytes = static_cast<uint32_t>(cells.size() * sizeof(BakeCell));
            resources.Cells = CreateBakeBuffer(device, cells.data(), cellBytes,
                RHI::ResourceUsage::StorageBuffer, "ImpostorBakeCells");
            if (!resources.Vertices || !resources.Indices || !resources.Cells)
            {
                NORVES_LOG_ERROR("ImpostorBake", "頂点・インデックス・セルのバッファを作れません");
                return false;
            }

            RHI::DescriptorSetDesc descriptorDesc;
            descriptorDesc.bindings.push_back({0, RHI::ResourceBindType::ConstantBuffer,
                                               RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel});
            for (uint32_t binding = 1; binding <= 5; ++binding)
            {
                descriptorDesc.bindings.push_back({binding, RHI::ResourceBindType::CombinedImageSampler,
                                                   RHI::ShaderStage::Pixel});
            }
            descriptorDesc.bindings.push_back({6, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Vertex});
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Linear;
            samplerDesc.filterMag = RHI::FilterMode::Linear;
            samplerDesc.filterMip = RHI::FilterMode::Linear;
            samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = RHI::TextureAddressMode::Wrap;
            resources.Sampler = device->CreateSampler(samplerDesc);
            const auto white = CreateBakePixel(resources, 255, 255, 255);
            const auto flat = CreateBakePixel(resources, 128, 128, 255);
            const auto black = CreateBakePixel(resources, 0, 0, 0);
            if (!resources.Sampler || !white || !flat || !black)
            {
                NORVES_LOG_ERROR("ImpostorBake", "サンプラーまたは既定の材質テクスチャを作れません");
                return false;
            }

            const size_t sectionCount = input.Sections.empty() ? 1 : input.Sections.size();
            resources.Sections.resize(sectionCount);
            for (size_t i = 0; i < sectionCount; ++i)
            {
                auto& section = resources.Sections[i];
                if (input.Sections.empty())
                {
                    section.Section.IndexCount = metadata.IndexCount;
                }
                else
                {
                    section.Section = input.Sections[i];
                }
                const auto& material = section.Section.Material;
                const auto resolve = [&](TextureHandle handle, const RHI::TexturePtr& fallback) -> RHI::TexturePtr
                {
                    return handle.IsValid() ? request.Textures->GetRHITexturePtr(handle) : fallback;
                };
                section.Textures[0] = resolve(material.AlbedoTexture, white);
                section.Textures[1] = resolve(material.NormalTexture, flat);
                const auto orm = resolve(material.OrmTexture, {});
                if (material.OrmTexture.IsValid() && !orm)
                {
                    NORVES_LOG_ERROR("ImpostorBake", "指定された ORM テクスチャを解決できません");
                    return false;
                }
                if (orm)
                {
                    section.Textures[2] = section.Textures[3] = section.Textures[4] = orm;
                }
                else
                {
                    section.Textures[2] = material.Metallic >= 0.0f
                        ? CreateBakeConstant(resources, material.Metallic) : black;
                    section.Textures[3] = material.Roughness >= 0.0f
                        ? CreateBakeConstant(resources, material.Roughness) : white;
                    section.Textures[4] = material.OcclusionStrength == 1.0f
                        ? white : CreateBakeConstant(resources, material.OcclusionStrength);
                }
                for (const auto& texture : section.Textures)
                {
                    if (!texture)
                    {
                        NORVES_LOG_ERROR("ImpostorBake", "区間の材質テクスチャを準備できません");
                        return false;
                    }
                }
                const BakeParams params{
                    {metadata.BoundsCenter.x, metadata.BoundsCenter.y, metadata.BoundsCenter.z, metadata.BoundsRadius},
                    {material.BaseColor.x, material.BaseColor.y, material.BaseColor.z, 1.0f},
                    {orm ? 1.0f : 0.0f, material.bNormalTwoChannel ? 1.0f : 0.0f,
                     material.bApplyNormalScale ? 1.0f : 0.0f, material.NormalScale},
                    {material.AlphaCutoff, 0.0f, 0.0f, 0.0f}};
                section.Uniform = CreateBakeBuffer(device, &params, sizeof(params),
                    RHI::ResourceUsage::ConstantBuffer, "ImpostorBakeParams");
                section.Descriptors = device->CreateDescriptorSet(descriptorDesc);
                if (!section.Uniform || !section.Descriptors)
                {
                    NORVES_LOG_ERROR("ImpostorBake", "区間の定数バッファまたは記述子の組を作れません");
                    return false;
                }
                section.Descriptors->BindConstantBuffer(0, section.Uniform, 0, sizeof(params));
                for (uint32_t binding = 1; binding <= 5; ++binding)
                {
                    section.Descriptors->BindTexture(binding, section.Textures[binding - 1]);
                    section.Descriptors->BindSampler(binding, resources.Sampler);
                }
                section.Descriptors->BindStorageBuffer(6, resources.Cells, 0, cellBytes);
                section.Descriptors->Update();
            }

            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = vertexShader;
            pipelineDesc.pixelShader = pixelShader;
            pipelineDesc.vertexBindings.push_back({0, sizeof(Mesh3DVertex), RHI::VertexInputRate::Vertex});
            pipelineDesc.vertexAttributes.push_back({0, 0, RHI::Format::R32G32B32_FLOAT, 0});
            pipelineDesc.vertexAttributes.push_back({1, 0, RHI::Format::R32G32B32_FLOAT, 12});
            pipelineDesc.vertexAttributes.push_back({2, 0, RHI::Format::R32G32_FLOAT, 24});
            pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthTestEnable = true;
            pipelineDesc.depthStencilState.depthWriteEnable = true;
            // RHI は Less をそのまま比較する。深度 0 が近く、クリア値 1 より小さい面を残す。
            pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;
            for (uint32_t i = 0; i < 4; ++i)
            {
                RHI::BlendAttachmentDesc blend;
                blend.blendEnable = false;
                pipelineDesc.blendState.attachments.push_back(blend);
            }
            pipelineDesc.renderPass = resources.RenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(descriptorDesc);
            resources.Pipeline = device->CreateGraphicsPipeline(pipelineDesc);
            resources.Commands = device->CreateCommandList();
            if (!resources.Pipeline || !resources.Commands)
            {
                NORVES_LOG_ERROR("ImpostorBake", "ベイクのパイプラインまたはコマンドリストを作れません");
                return false;
            }

            auto& commands = resources.Commands;
            commands->Begin();
            commands->BeginRenderPass(resources.RenderPass, resources.Framebuffer);
            commands->SetPipeline(resources.Pipeline);
            commands->SetVertexBuffer(resources.Vertices);
            commands->SetIndexBuffer(resources.Indices);
            for (uint32_t cy = 0; cy < input.AxisCellCountY; ++cy)
            {
                for (uint32_t cx = 0; cx < input.AxisCellCountX; ++cx)
                {
                    const uint32_t x = cx * input.CellResolution;
                    const uint32_t y = cy * input.CellResolution;
                    const uint32_t cellIndex = cy * input.AxisCellCountX + cx;
                    commands->SetViewport({float(x), float(y), float(input.CellResolution),
                                           float(input.CellResolution), 0.0f, 1.0f});
                    commands->SetScissor({int32_t(x), int32_t(y), int32_t(x + input.CellResolution),
                                          int32_t(y + input.CellResolution)});
                    for (const auto& section : resources.Sections)
                    {
                        commands->SetDescriptorSet(section.Descriptors);
                        commands->DrawIndexedInstanced(section.Section.IndexCount, 1,
                            section.Section.IndexOffset, 0, cellIndex);
                    }
                }
            }
            // レンダーパスの finalState で4枚とも ShaderResource に遷移する。
            commands->EndRenderPass();
            commands->End();
            resources.bSubmissionPending = true;
            commands->Submit(true);
            resources.bSubmissionPending = false;

            outResult.AtlasTexture = resources.Handles[0];
            outResult.NormalAtlasTexture = resources.Handles[1];
            outResult.OrmAtlasTexture = resources.Handles[2];
            outResult.DepthAtlasTexture = resources.Handles[3];
            outResult.Metadata = metadata;
            resources.bKeepAtlases = true;
            return true;
        }
        catch (...)
        {
            NORVES_LOG_ERROR("ImpostorBake", "ベイクの資源作成または描画で例外が発生しました");
            return false;
        }
    }

} // namespace NorvesLib::Core::Rendering
