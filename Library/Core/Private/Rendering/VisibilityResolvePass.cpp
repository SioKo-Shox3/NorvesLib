#include "Rendering/VisibilityResolvePass.h"
#include <cmath>

#include "Logging/LogMacros.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/TerrainBlendShading.h"
#include "Rendering/TerrainSamplerOverride.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VirtualTextureFeedbackMaterial.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/VisibilityRasterPass.h"
#include "Rendering/VisibilityResolveMaterialBuild.h"

#include <cstring>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t ParamsBytes = static_cast<uint32_t>(sizeof(VisibilityResolveGeometry::ResolveParams));
        // 空の表（読まれない）の大きさ。MegaInstance（192 バイト）・InstanceData（208 バイト）より大きい
        constexpr uint64_t PlaceholderBytes = 256;

        // 材質ごとの形の 1 回の dispatch の定数（シェーダーの ResolveTileParams と同じ 192 バイト。
        // tile = 横のタイル数・材質の番号・フラグ・予約、vt = VT のフィードバックのパラメータ（アルベド・法線・ORM・高さ）、
        // terrain = 地形の混ぜ方の値（vec4 × 10。TerrainBlendShading.h の並び）
        constexpr uint32_t TileParamsWords = 8u + VisibilityResolveGeometry::TERRAIN_PARAM_FLOATS;
        constexpr uint32_t TileParamsBytes = TileParamsWords * sizeof(uint32_t);
        static_assert(VisibilityResolveGeometry::TERRAIN_PARAM_FLOATS == TERRAIN_BLEND_PARAM_FLOATS,
                      "シェーダーの TerrainBlendParams と同じ数でなければならない");

        // 共通の束縛の数（0..8、材質の GBuffer の 9、発光の GBuffer の 10。検証用の版はさらに 11 の書き出し）
        constexpr uint32_t CommonBindingCount = 11u;
        constexpr uint32_t DumpBindingIndex = 11u;
        // MegaGeometry のメッシュの表（Common/MegaInstanceTable.glsl の MEGA_MESH_BINDING。材質ごとの形の追加の束縛より後ろ）
        constexpr uint32_t MegaMeshTableBindingIndex = 22u;
        // 地形の 2・3 層目のテクスチャと地表の分類の地図と遠景の地図（シェーダーの VIS_TERRAIN_BINDING から 8 枚。材質ごとの形だけ。
        // メッシュの表より後ろ）
        constexpr uint32_t TerrainTextureBindingFirst = 23u;

        // bFeedback: 材質ごとの形の最後に VT の要求のバッファ（シェーダーの VT_FEEDBACK_BINDING）を足す（bTiles のときだけ）
        RHI::DescriptorSetDesc MakeDescriptorSetDesc(bool bDump, bool bTiles, bool bFeedback)
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::ConstantBuffer,       // 0 パラメータ
                RHI::ResourceBindType::StructuredBuffer,     // 1 描画の記録の表
                RHI::ResourceBindType::CombinedImageSampler, // 2 VisBuffer.Id
                RHI::ResourceBindType::StructuredBuffer,     // 3 材質の表
                RHI::ResourceBindType::StructuredBuffer,     // 4 MegaGeometry のインスタンスの表
                RHI::ResourceBindType::StructuredBuffer,     // 5 描画のインスタンスの表
                RHI::ResourceBindType::RWTexture,            // 6 GBuffer.Albedo
                RHI::ResourceBindType::RWTexture,            // 7 GBuffer.Normal
                RHI::ResourceBindType::RWTexture,            // 8 GBuffer.Velocity
                RHI::ResourceBindType::RWTexture,            // 9 GBuffer.Material
                RHI::ResourceBindType::RWTexture,            // 10 GBuffer.Emissive
                RHI::ResourceBindType::RWBuffer,             // 11 検証用の書き出し（検証用の版だけ）
            };
            const uint32_t commonCount = bDump ? CommonBindingCount + 1u : CommonBindingCount;
            for (uint32_t bindingIndex = 0; bindingIndex < commonCount; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(binding);
            }
            if (bTiles)
            {
                // 材質ごとの形の追加の束縛（シェーダーの VIS_TILE_BINDING から並ぶ）: 1 回の dispatch の定数・引数・一覧
                const RHI::ResourceBindType tileTypes[] = {
                    RHI::ResourceBindType::ConstantBuffer,
                    RHI::ResourceBindType::StructuredBuffer,
                    RHI::ResourceBindType::StructuredBuffer,
                };
                for (uint32_t tileBinding = 0; tileBinding < 3u; ++tileBinding)
                {
                    RHI::DescriptorBinding binding;
                    binding.binding = commonCount + tileBinding;
                    binding.type = tileTypes[tileBinding];
                    binding.stages = RHI::ShaderStage::Compute;
                    desc.bindings.push_back(binding);
                }
                // その材質のテクスチャ（アルベド・法線・金属度・粗さ・AO・高さ。シェーダーの VIS_TILE_BINDING + 3 から）
                for (uint32_t textureBinding = 0; textureBinding < VisibilityResolveGeometry::MATERIAL_TEXTURE_COUNT;
                     ++textureBinding)
                {
                    RHI::DescriptorBinding binding;
                    binding.binding = commonCount + 3u + textureBinding;
                    binding.type = RHI::ResourceBindType::CombinedImageSampler;
                    binding.stages = RHI::ShaderStage::Compute;
                    desc.bindings.push_back(binding);
                }
                if (bFeedback)
                {
                    RHI::DescriptorBinding binding;
                    binding.binding = commonCount + 3u + VisibilityResolveGeometry::MATERIAL_TEXTURE_COUNT;
                    binding.type = RHI::ResourceBindType::RWBuffer;
                    binding.stages = RHI::ShaderStage::Compute;
                    desc.bindings.push_back(binding);
                }
                // 地形の 2・3 層目のテクスチャと地表の分類の地図と遠景の地図
                for (uint32_t textureBinding = 0; textureBinding < VisibilityResolveGeometry::TERRAIN_TEXTURE_COUNT;
                     ++textureBinding)
                {
                    RHI::DescriptorBinding binding;
                    binding.binding = TerrainTextureBindingFirst + textureBinding;
                    binding.type = RHI::ResourceBindType::CombinedImageSampler;
                    binding.stages = RHI::ShaderStage::Compute;
                    desc.bindings.push_back(binding);
                }
            }
            {
                RHI::DescriptorBinding binding;
                binding.binding = MegaMeshTableBindingIndex;
                binding.type = RHI::ResourceBindType::StructuredBuffer;
                binding.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        uint32_t ClampBindSize(uint64_t size)
        {
            return size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                               : static_cast<uint32_t>(size);
        }

        // 使っているバイト数（0 なら全体）をバッファの大きさに収めた束ねる大きさ
        uint32_t BindBytes(const RHI::BufferPtr& buffer, uint64_t usedBytes)
        {
            const uint64_t size = buffer->GetSize();
            return ClampBindSize(usedBytes != 0 && usedBytes < size ? usedBytes : size);
        }
    } // namespace

    namespace
    {
        RHI::TexturePtr ResolveMaterialTexture(const TextureResources* textures, TextureHandle handle)
        {
            if (!textures || !handle.IsValid())
            {
                return nullptr;
            }
            return textures->GetRHITexturePtr(handle);
        }

        // 地形の混ぜ合わせ（材質の表と同じ添え字の値）を解決の入力へ写す。2・3 層目の 6 枚がすべて引けたときだけ有効にする。
        // 地表の分類の地図・遠景の地図は引けたときだけ張る（引けなければ傾斜の式・画素ごとの式で混ぜる）
        void AttachTerrainBlend(const TextureResources* textures,
                                const MegaGeometry::MegaTerrainBlend* terrain,
                                VisibilityResolveMaterial& material)
        {
            if (!terrain || !terrain->bEnabled)
            {
                return;
            }
            const TextureHandle handles[VisibilityResolveGeometry::TERRAIN_LAYER_TEXTURE_COUNT] = {
                terrain->Dirt.AlbedoTexture, terrain->Dirt.NormalTexture, terrain->Dirt.ORMTexture,
                terrain->Rock.AlbedoTexture, terrain->Rock.NormalTexture, terrain->Rock.ORMTexture};
            for (uint32_t index = 0; index < VisibilityResolveGeometry::TERRAIN_LAYER_TEXTURE_COUNT; ++index)
            {
                material.TerrainTextures[index] = ResolveMaterialTexture(textures, handles[index]);
                if (!material.TerrainTextures[index])
                {
                    for (RHI::TexturePtr& texture : material.TerrainTextures)
                    {
                        texture.reset();
                    }
                    return;
                }
            }
            material.bTerrainBlend = true;
            RHI::TexturePtr& mask = material.TerrainTextures[VisibilityResolveGeometry::TERRAIN_SURFACE_MASK_INDEX];
            mask = ResolveMaterialTexture(textures, terrain->SurfaceMaskTexture);
            RHI::TexturePtr& farMap = material.TerrainTextures[VisibilityResolveGeometry::TERRAIN_FAR_MAP_INDEX];
            farMap = ResolveMaterialTexture(textures, terrain->FarMapTexture);
            // 3 層のアルベドが UNORM で張られていれば、シェーダーが sRGB から線形へ戻す（ラスタと同じ判定）
            const uint32_t albedoDecodeBits = ResolveTerrainAlbedoDecodeBits(
                material.Albedo.get(), material.TerrainTextures[0].get(), material.TerrainTextures[3].get());
            PackTerrainBlendParams(*terrain, true, mask != nullptr, farMap != nullptr, albedoDecodeBits,
                                   material.TerrainParams);
        }

    } // namespace

    namespace VisibilityResolveGeometry
    {
        VisibilityResolveMaterial MakeResolveMaterial(const TextureResources* textures,
                                                      const VisibilityBuffer::MaterialEntry& entry,
                                                      const TextureResources::VirtualTextureFeedbackTarget& feedbackTarget)
        {
            const VisibilityBuffer::MaterialTextureHandles handles = VisibilityBuffer::ReadMaterialTextureHandles(entry);
            const TextureHandle albedo = handles.Albedo;
            const TextureHandle normal = handles.Normal;
            const TextureHandle metallic = handles.Metallic;
            const TextureHandle roughness = handles.Roughness;
            const TextureHandle ao = handles.AO;
            const TextureHandle orm = handles.ORM;
            const TextureHandle height = handles.Height;

            VisibilityResolveMaterial material;
            material.bMegaGeometry = (entry.Header[0] & VisibilityBuffer::MATERIAL_FLAG_MEGA_GEOMETRY) != 0u;
            material.Albedo = ResolveMaterialTexture(textures, albedo);
            material.Normal = ResolveMaterialTexture(textures, normal);
            material.Metallic = ResolveMaterialTexture(textures, metallic);
            material.Roughness = ResolveMaterialTexture(textures, roughness);
            material.AO = ResolveMaterialTexture(textures, ao);
            material.ORM = ResolveMaterialTexture(textures, orm);
            material.Height = ResolveMaterialTexture(textures, height);
            if (material.bMegaGeometry)
            {
                std::memcpy(&material.AOConstant, &entry.TexturesD[2], sizeof(float));
            }
            // スカラー値は、そのテクスチャの指定（ハンドル）が無いときだけ 1x1
            // のテクスチャにする
            if (entry.Scalars[0] >= 0.0f && (material.bMegaGeometry ? !material.Metallic : !metallic.IsValid()))
            {
                material.MetallicConstant = entry.Scalars[0];
            }
            if (entry.Scalars[1] >= 0.0f && (material.bMegaGeometry ? !material.Roughness : !roughness.IsValid()))
            {
                material.RoughnessConstant = entry.Scalars[1];
            }
            // VT のフィードバックのパラメータ（GBufferPass の材質の descriptor と同じ。ORM は金属度の枠に張るテクスチャ）
            material.FeedbackAlbedo =
                ResolveVirtualTextureFeedbackParam(textures, albedo, material.Albedo.get(), feedbackTarget);
            material.FeedbackNormal =
                ResolveVirtualTextureFeedbackParam(textures, normal, material.Normal.get(), feedbackTarget);
            material.FeedbackORM = ResolveVirtualTextureFeedbackParam(textures, orm, material.ORM.get(), feedbackTarget);
            material.FeedbackHeight =
                ResolveVirtualTextureFeedbackParam(textures, height, material.Height.get(), feedbackTarget);
            return material;
        }

        ResolveParams BuildParams(const CameraViewConstants& current,
                                  const CameraViewConstants* previous,
                                  const RHI::Viewport& viewport,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t materialCount)
        {
            ResolveParams params;
            current.CopyShaderInverseView(params.InvView);
            current.CopyShaderInverseProjection(params.InvProj);
            current.CopyCameraPosition(params.CameraPosition);
            if (previous)
            {
                previous->CopyShaderViewProjection(params.PreviousViewProj);
            }
            params.Viewport[0] = viewport.x;
            params.Viewport[1] = viewport.y;
            params.Viewport[2] = viewport.width;
            params.Viewport[3] = viewport.height;
            params.Screen[0] = width;
            params.Screen[1] = height;
            params.Screen[2] = previous ? FLAG_PREVIOUS_VALID : 0u;
            params.Screen[3] = materialCount;
            return params;
        }

        bool IsSupported(const RHI::DeviceCapabilities& capabilities)
        {
            return capabilities.bGeometryShader && capabilities.bDrawIndirectFirstInstance &&
                   capabilities.bBufferDeviceAddress && capabilities.bShaderStorageImageExtendedFormats;
        }

        const char* GetFallbackReasonName(FallbackReason reason)
        {
            switch (reason)
            {
            case FallbackReason::None:
                return "none";
            case FallbackReason::PassUnavailable:
                return "pass_unavailable";
            case FallbackReason::DeviceUnsupported:
                return "device_unsupported";
            case FallbackReason::RasterUnavailable:
                return "raster_unavailable";
            case FallbackReason::ResolveUnavailable:
                return "resolve_unavailable";
            case FallbackReason::SkinningComputeUnavailable:
                return "skinning_compute_unavailable";
            }
            return "unknown";
        }
    } // namespace VisibilityResolveGeometry

    // ========================================
    // VisibilityResolve
    // ========================================

    VisibilityResolve::VisibilityResolve() = default;

    VisibilityResolve::~VisibilityResolve()
    {
        Shutdown();
    }

    bool VisibilityResolve::Initialize(RHI::IDevice* device, ShaderManager* shaderManager, bool bDump, bool bTiles)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }

        m_Shader = shaderManager->LoadShader(bDump ? "visbuffer_resolve_dump.comp" : "visbuffer_resolve.comp",
                                             RHI::ShaderStage::Compute);
        if (!m_Shader)
        {
            NORVES_LOG_WARNING("VisibilityResolve", "ビジビリティバッファの幾何の解決の計算シェーダーの読み込みに失敗");
            return false;
        }

        // ID は整数なのでフィルターしない（texelFetch で読む）
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = device->CreateSampler(samplerDesc);
        if (!m_Sampler)
        {
            NORVES_LOG_WARNING("VisibilityResolve", "ビジビリティバッファの幾何の解決のサンプラーの作成に失敗");
            Shutdown();
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_Shader;
        pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc(bDump, false, false));
        m_Pipeline = device->CreateComputePipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("VisibilityResolve", "ビジビリティバッファの幾何の解決の計算パイプラインの作成に失敗");
            Shutdown();
            return false;
        }

        if (bTiles)
        {
            m_TileShader = shaderManager->LoadShader(
                bDump ? "visbuffer_resolve_tiles_dump.comp" : "visbuffer_resolve_tiles.comp", RHI::ShaderStage::Compute);
            if (m_TileShader)
            {
                RHI::ComputePipelineDesc tilePipelineDesc;
                tilePipelineDesc.computeShader = m_TileShader;
                tilePipelineDesc.descriptorSetLayouts.push_back(
                    MakeDescriptorSetDesc(bDump, true, UsesVirtualTextureFeedbackBinding(device)));
                m_TilePipeline = device->CreateComputePipeline(tilePipelineDesc);
            }
            if (!m_TilePipeline)
            {
                NORVES_LOG_WARNING("VisibilityResolve", "ビジビリティバッファの幾何の解決（材質ごとのタイル）"
                                                        "の計算パイプラインの作成に失敗");
                Shutdown();
                return false;
            }
        }

        m_Device = device;
        m_bDump = bDump;
        m_bFeedback = UsesVirtualTextureFeedbackBinding(device);
        return true;
    }

    void VisibilityResolve::Shutdown()
    {
        m_TileUseScratch.clear();
        m_Uses.Clear();
        m_TilePipeline.reset();
        m_TileShader.reset();
        m_Pipeline.reset();
        m_Sampler.reset();
        m_Placeholder.reset();
        m_DefaultWhite.reset();
        m_DefaultFlatNormal.reset();
        m_DefaultBlack.reset();
        m_DefaultMidGray.reset();
        m_MaterialSampler.reset();
        m_MegaMaterialSampler.reset();
        m_TerrainMaterialSampler.reset();
        m_ConstantGrayTextures.clear();
        m_Shader.reset();
        m_Device = nullptr;
        m_bDump = false;
        m_bFeedback = false;
    }

    void VisibilityResolve::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool VisibilityResolve::EnsurePlaceholder()
    {
        if (!m_Placeholder)
        {
            m_Placeholder = m_Device->CreateBuffer(RHI::BufferDesc(
                PlaceholderBytes, RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead, true,
                "VisibilityResolvePlaceholder"));
            if (m_Placeholder)
            {
                // 読まれないが、未初期化の値を残さない
                uint8_t zero[PlaceholderBytes] = {};
                m_Placeholder->Update(zero, PlaceholderBytes);
            }
        }
        return m_Placeholder != nullptr;
    }

    bool VisibilityResolve::EnsureMaterialDefaults()
    {
        if (m_DefaultWhite && m_DefaultFlatNormal && m_DefaultBlack && m_DefaultMidGray && m_MaterialSampler &&
            m_MegaMaterialSampler && m_TerrainMaterialSampler)
        {
            return true;
        }
        if (!m_Device)
        {
            return false;
        }
        if (!m_bTextureCreationAllowed)
        {
            // 区間ごとの記録の最中は作らない（準備段で作れなかったフレーム）
            return false;
        }

        // GBufferPass::Initialize の既定のテクスチャとサンプラーと同じ値（ラスタとテクスチャの枠の既定を揃える）
        auto createDefault1x1 = [this](const char* debugName, uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> RHI::TexturePtr
        {
            RHI::TextureDesc texDesc;
            texDesc.Width = 1;
            texDesc.Height = 1;
            texDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            texDesc.Usage = RHI::ResourceUsage::ShaderRead;
            texDesc.DebugName = debugName;
            RHI::TexturePtr texture = m_Device->CreateTexture(texDesc);
            if (texture)
            {
                const uint8_t pixel[4] = {r, g, b, a};
                texture->Update(pixel, 4, 4);
            }
            return texture;
        };
        if (!m_DefaultWhite)
        {
            m_DefaultWhite = createDefault1x1("VisibilityResolveDefaultWhite1x1", 255, 255, 255, 255);
        }
        if (!m_DefaultFlatNormal)
        {
            m_DefaultFlatNormal = createDefault1x1("VisibilityResolveDefaultFlatNormal1x1", 128, 128, 255, 255);
        }
        if (!m_DefaultBlack)
        {
            m_DefaultBlack = createDefault1x1("VisibilityResolveDefaultBlack1x1", 0, 0, 0, 255);
        }
        if (!m_DefaultMidGray)
        {
            m_DefaultMidGray = createDefault1x1("VisibilityResolveDefaultMidGray1x1", 128, 128, 128, 255);
        }
        if (!m_MaterialSampler)
        {
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Anisotropic;
            samplerDesc.filterMag = RHI::FilterMode::Anisotropic;
            samplerDesc.filterMip = RHI::FilterMode::Anisotropic;
            samplerDesc.addressU = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressV = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressW = RHI::TextureAddressMode::Wrap;
            samplerDesc.maxAnisotropy = 4;
            m_MaterialSampler = m_Device->CreateSampler(samplerDesc);
        }
        if (!m_MegaMaterialSampler)
        {
            // MegaGeometryPass の既定のサンプラーと同じ作り（等方の Linear。maxAnisotropy は指定しない）
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Linear;
            samplerDesc.filterMag = RHI::FilterMode::Linear;
            samplerDesc.filterMip = RHI::FilterMode::Linear;
            samplerDesc.addressU = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressV = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressW = RHI::TextureAddressMode::Wrap;
            m_MegaMaterialSampler = m_Device->CreateSampler(samplerDesc);
        }
        if (!m_TerrainMaterialSampler)
        {
            // MegaGeometryPass の地形のサンプラーと同じ作り（異方性 16。地面を浅い角度で見てもぼけない）
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Anisotropic;
            samplerDesc.filterMag = RHI::FilterMode::Anisotropic;
            samplerDesc.filterMip = RHI::FilterMode::Anisotropic;
            samplerDesc.addressU = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressV = RHI::TextureAddressMode::Wrap;
            samplerDesc.addressW = RHI::TextureAddressMode::Wrap;
            samplerDesc.maxAnisotropy = 16;
            ApplyTerrainSamplerOverride(samplerDesc, "VisibilityResolve");
            m_TerrainMaterialSampler = m_Device->CreateSampler(samplerDesc);
        }
        return m_DefaultWhite && m_DefaultFlatNormal && m_DefaultBlack && m_DefaultMidGray && m_MaterialSampler &&
               m_MegaMaterialSampler && m_TerrainMaterialSampler;
    }

    RHI::TexturePtr VisibilityResolve::GetConstantGrayTexture(float value)
    {
        if (!std::isfinite(value))
        {
            return nullptr;
        }
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        const uint32_t level = static_cast<uint32_t>(clamped * 255.0f + 0.5f);
        const auto found = m_ConstantGrayTextures.find(level);
        if (found != m_ConstantGrayTextures.end())
        {
            return found->second;
        }
        if (!m_Device)
        {
            return nullptr;
        }
        if (!m_bTextureCreationAllowed)
        {
            // 区間ごとの記録の最中は作らない。準備段（PrepareMaterialTextures）と同じ材質を読む限り、ここへは来ない
            if (!m_bLoggedTextureCreationBlocked)
            {
                m_bLoggedTextureCreationBlocked = true;
                NORVES_LOG_ERROR("VisibilityResolve",
                                 "区間ごとの記録の最中に、準備段で作っていない定数のテクスチャ（%u）が要りました",
                                 level);
            }
            return nullptr;
        }
        RHI::TextureDesc texDesc;
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        texDesc.Usage = RHI::ResourceUsage::ShaderRead;
        texDesc.DebugName = "VisibilityResolveConstantGray1x1";
        RHI::TexturePtr texture = m_Device->CreateTexture(texDesc);
        if (!texture)
        {
            return nullptr;
        }
        const uint8_t gray = static_cast<uint8_t>(level);
        const uint8_t pixel[4] = {gray, gray, gray, 255};
        texture->Update(pixel, 4, 4);
        m_ConstantGrayTextures[level] = texture;
        return texture;
    }

    bool VisibilityResolve::PrepareMaterialTextures(const Container::VariableArray<VisibilityResolveMaterial>& materials)
    {
        // 準備段は記録の前の直列の段なので、作成を許可する（前のフレームの区間の記録が止めたままにしない）
        m_bTextureCreationAllowed = true;
        if (!EnsureMaterialDefaults())
        {
            return false;
        }
        // RecordTiles が定数の 1x1 のテクスチャを引く条件と同じ（作ったものは m_ConstantGrayTextures に残る）
        bool bReady = true;
        for (const VisibilityResolveMaterial& input : materials)
        {
            if (input.MetallicConstant >= 0.0f && !input.Metallic)
            {
                GetConstantGrayTexture(input.MetallicConstant);
            }
            if (input.RoughnessConstant >= 0.0f && !input.Roughness)
            {
                GetConstantGrayTexture(input.RoughnessConstant);
            }
            if (input.AOConstant != 1.0f && !input.AO)
            {
                bReady = GetConstantGrayTexture(input.AOConstant) != nullptr && bReady;
            }
        }
        return bReady;
    }

    bool VisibilityResolve::Record(RHI::ICommandList* commandList, const VisibilityResolveDispatch& dispatch)
    {
        const VisibilityResolveGeometry::ResolveParams& params = dispatch.Params;
        const uint32_t width = params.Screen[0];
        const uint32_t height = params.Screen[1];
        if (!m_Device || !m_Pipeline || !commandList || !dispatch.IdTexture || !dispatch.RecordTable ||
            !dispatch.MaterialTable || !dispatch.Albedo || !dispatch.Normal || !dispatch.Material || !dispatch.Velocity ||
            !dispatch.Emissive || width == 0 ||
            height == 0 || (m_bDump && !dispatch.Dump))
        {
            return false;
        }

        // 画面の外の画素は読み書きしないが、画面は入力の ID と出力の大きさに収まること
        if (width > dispatch.IdTexture->GetWidth() || height > dispatch.IdTexture->GetHeight() ||
            width > dispatch.Albedo->GetWidth() || height > dispatch.Albedo->GetHeight() ||
            width > dispatch.Normal->GetWidth() || height > dispatch.Normal->GetHeight() ||
            width > dispatch.Material->GetWidth() || height > dispatch.Material->GetHeight() ||
            width > dispatch.Velocity->GetWidth() || height > dispatch.Velocity->GetHeight() ||
            width > dispatch.Emissive->GetWidth() || height > dispatch.Emissive->GetHeight())
        {
            return false;
        }
        if (m_bDump &&
            dispatch.Dump->GetSize() < static_cast<uint64_t>(width) * height * VisibilityResolveGeometry::DUMP_STRIDE_BYTES)
        {
            return false;
        }
        if (!EnsurePlaceholder())
        {
            return false;
        }

        if (dispatch.TileArgs && dispatch.TileList)
        {
            return RecordTiles(commandList, dispatch);
        }

        Use& use = m_Uses.Acquire();
        if (!use.Uniform)
        {
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            use.Uniform = m_Device->CreateBuffer(
                RHI::BufferDesc(ParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisibilityResolveParams"));
        }
        if (!use.DescriptorSet)
        {
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            use.DescriptorSet = m_Device->CreateDescriptorSet(MakeDescriptorSetDesc(m_bDump, false, false));
        }
        if (!use.Uniform || !use.DescriptorSet)
        {
            return false;
        }

        use.Uniform->Update(&params, ParamsBytes);
        BindCommon(*use.DescriptorSet, use.Uniform, dispatch);
        use.DescriptorSet->Update();

        const uint32_t tile = VisibilityResolveGeometry::TILE_SIZE;
        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(use.DescriptorSet, 0);
        commandList->Dispatch((width + tile - 1) / tile, (height + tile - 1) / tile, 1u);
        return true;
    }

    // 直接版と材質ごとの版で共通の束縛（0..10 と、検証用の版の 11、メッシュの表の 22）
    void VisibilityResolve::BindCommon(RHI::IDescriptorSet& set,
                                       const RHI::BufferPtr& paramsUniform,
                                       const VisibilityResolveDispatch& dispatch) const
    {
        set.BindConstantBuffer(0, paramsUniform, 0, ParamsBytes);
        set.BindStorageBuffer(1, dispatch.RecordTable, 0, BindBytes(dispatch.RecordTable, dispatch.RecordTableBytes));
        set.BindTexture(2, dispatch.IdTexture);
        set.BindSampler(2, m_Sampler);
        set.BindStorageBuffer(3, dispatch.MaterialTable, 0, BindBytes(dispatch.MaterialTable, dispatch.MaterialTableBytes));
        const RHI::BufferPtr& megaInstances = dispatch.MegaInstances ? dispatch.MegaInstances : m_Placeholder;
        set.BindStorageBuffer(4, megaInstances, 0,
                              dispatch.MegaInstances ? BindBytes(megaInstances, dispatch.MegaInstancesBytes)
                                                     : ClampBindSize(PlaceholderBytes));
        const RHI::BufferPtr& megaMeshes = dispatch.MegaMeshes ? dispatch.MegaMeshes : m_Placeholder;
        set.BindStorageBuffer(MegaMeshTableBindingIndex, megaMeshes, 0,
                              dispatch.MegaMeshes ? BindBytes(megaMeshes, dispatch.MegaMeshesBytes)
                                                  : ClampBindSize(PlaceholderBytes));
        const RHI::BufferPtr& drawInstances = dispatch.DrawInstances ? dispatch.DrawInstances : m_Placeholder;
        set.BindStorageBuffer(5, drawInstances, 0,
                              dispatch.DrawInstances ? BindBytes(drawInstances, dispatch.DrawInstancesBytes)
                                                     : ClampBindSize(PlaceholderBytes));
        set.BindStorageTexture(6, dispatch.Albedo);
        set.BindStorageTexture(7, dispatch.Normal);
        set.BindStorageTexture(8, dispatch.Velocity);
        set.BindStorageTexture(9, dispatch.Material);
        set.BindStorageTexture(10, dispatch.Emissive);
        if (m_bDump)
        {
            set.BindStorageBuffer(DumpBindingIndex, dispatch.Dump, 0, ClampBindSize(dispatch.Dump->GetSize()));
        }
    }

    bool VisibilityResolve::RecordTiles(RHI::ICommandList* commandList, const VisibilityResolveDispatch& dispatch)
    {
        if (!m_TilePipeline)
        {
            return false;
        }
        // 引数は IndirectBuffer の用途で作られていること（間接 dispatch が断るので、何かを記録する前に確かめる）
        if ((dispatch.TileArgs->GetUsage() & RHI::ResourceUsage::IndirectBuffer) != RHI::ResourceUsage::IndirectBuffer)
        {
            return false;
        }
        // 引数の表に収まる材質の数（引数 1 つ = 材質 1 つ）
        const uint64_t argsCapacity = dispatch.TileArgs->GetSize() / MaterialTiles::ARGS_STRIDE_BYTES;
        const uint64_t materialCount =
            dispatch.TileMaterialCount != 0 && dispatch.TileMaterialCount < argsCapacity ? dispatch.TileMaterialCount : argsCapacity;
        if (materialCount == 0)
        {
            return false;
        }

        const uint32_t width = dispatch.Params.Screen[0];
        const uint32_t tilesX = (width + VisibilityResolveGeometry::TILE_SIZE - 1) / VisibilityResolveGeometry::TILE_SIZE;
        const uint32_t firstBinding = m_bDump ? CommonBindingCount + 1u : CommonBindingCount;
        if (!EnsureMaterialDefaults())
        {
            return false;
        }

        // 1 回の間接 dispatch ごとに、材質の番号を持つ別の UBO と別のディスクリプタセットを使う
        // （提出前に上書きしない。1 フレームに何回 Record しても枠の次の資源へ進む）。
        // コマンドリストへ記録する前に、全部の資源をそろえる（作れなければ何も記録せず false）
        struct BoundMaterial
        {
            RHI::TexturePtr Textures[VisibilityResolveGeometry::MATERIAL_TEXTURE_COUNT];
            RHI::TexturePtr TerrainTextures[VisibilityResolveGeometry::TERRAIN_TEXTURE_COUNT];
            float TerrainParams[VisibilityResolveGeometry::TERRAIN_PARAM_FLOATS] = {};
            uint32_t Flags = 0;
            uint32_t Feedback[4] = {};
            bool bMegaGeometry = false;
        };
        Container::VariableArray<BoundMaterial> boundMaterials;
        Container::VariableArray<Use*>& uses = m_TileUseScratch;
        uses.clear();
        uses.reserve(static_cast<size_t>(materialCount));
        for (uint64_t material = 0; material < materialCount; ++material)
        {
            // GBufferPass の材質の descriptor と同じ規則で、指定の無い枠を既定のテクスチャで埋める
            const VisibilityResolveMaterial empty;
            const VisibilityResolveMaterial& input =
                material < dispatch.Materials.size() ? dispatch.Materials[static_cast<size_t>(material)] : empty;
            BoundMaterial bound;
            bound.bMegaGeometry = input.bMegaGeometry;
            RHI::TexturePtr metallicDefault = m_DefaultBlack;
            if (input.MetallicConstant >= 0.0f && !input.Metallic)
            {
                if (RHI::TexturePtr constant = GetConstantGrayTexture(input.MetallicConstant))
                {
                    metallicDefault = constant;
                }
            }
            // 粗さの既定は、手続き・スキニング（GBufferPass）が中間灰、MegaGeometry（MegaGeometryPass）が白
            RHI::TexturePtr roughnessDefault = input.bMegaGeometry ? m_DefaultWhite : m_DefaultMidGray;
            if (input.RoughnessConstant >= 0.0f && !input.Roughness)
            {
                if (RHI::TexturePtr constant = GetConstantGrayTexture(input.RoughnessConstant))
                {
                    roughnessDefault = constant;
                }
            }
            bound.Textures[0] = input.Albedo ? input.Albedo : m_DefaultWhite;
            bound.Textures[1] = input.Normal ? input.Normal : m_DefaultFlatNormal;
            bound.Textures[2] = input.Metallic ? input.Metallic : metallicDefault;
            bound.Textures[3] = input.Roughness ? input.Roughness : roughnessDefault;
            RHI::TexturePtr aoDefault = m_DefaultWhite;
            if (input.AOConstant != 1.0f && !input.AO)
            {
                aoDefault = GetConstantGrayTexture(input.AOConstant);
                if (!aoDefault)
                {
                    return false;
                }
            }
            bound.Textures[4] = input.AO ? input.AO : aoDefault;
            if (input.ORM)
            {
                // シェーダーは ORM のとき金属度の枠だけを読むが、3 つの枠は同じテクスチャで埋める
                bound.Textures[2] = input.ORM;
                bound.Textures[3] = input.ORM;
                bound.Textures[4] = input.ORM;
                bound.Flags |= VisibilityResolveGeometry::TILE_FLAG_ORM;
            }
            bound.Textures[5] = input.Height ? input.Height : m_DefaultBlack;
            bound.Feedback[0] = input.FeedbackAlbedo;
            bound.Feedback[1] = input.FeedbackNormal;
            bound.Feedback[2] = input.FeedbackORM;
            bound.Feedback[3] = input.FeedbackHeight;
            for (uint32_t index = 0; index < VisibilityResolveGeometry::MATERIAL_TEXTURE_COUNT; ++index)
            {
                if (!bound.Textures[index])
                {
                    return false;
                }
                if (bound.Textures[index]->IsSparse())
                {
                    bound.Flags |= VisibilityResolveGeometry::TILE_FLAG_SPARSE;
                }
            }
            // 地形の 2・3 層目と地表の分類の地図と遠景の地図。地形でない材質と、地図の無い地形の材質は、読まない枠を 1 層目の
            // アルベド・法線・金属度（ORM）の枠で埋める
            for (const uint32_t mapIndex : {VisibilityResolveGeometry::TERRAIN_SURFACE_MASK_INDEX,
                                            VisibilityResolveGeometry::TERRAIN_FAR_MAP_INDEX})
            {
                const RHI::TexturePtr& map = input.TerrainTextures[mapIndex];
                bound.TerrainTextures[mapIndex] = input.bTerrainBlend && map ? map : bound.Textures[0];
                if (bound.TerrainTextures[mapIndex]->IsSparse())
                {
                    bound.Flags |= VisibilityResolveGeometry::TILE_FLAG_SPARSE;
                }
            }
            for (uint32_t layer = 0; layer < 2u; ++layer)
            {
                for (uint32_t slot = 0; slot < 3u; ++slot)
                {
                    const uint32_t index = layer * 3u + slot;
                    bound.TerrainTextures[index] = input.bTerrainBlend ? input.TerrainTextures[index] : bound.Textures[slot];
                    if (!bound.TerrainTextures[index])
                    {
                        return false;
                    }
                    if (bound.TerrainTextures[index]->IsSparse())
                    {
                        bound.Flags |= VisibilityResolveGeometry::TILE_FLAG_SPARSE;
                    }
                }
            }
            if (input.bTerrainBlend)
            {
                bound.Flags |= VisibilityResolveGeometry::TILE_FLAG_TERRAIN;
                std::memcpy(bound.TerrainParams, input.TerrainParams, sizeof(bound.TerrainParams));
            }
            boundMaterials.push_back(bound);

            Use& use = m_Uses.Acquire();
            if (!use.TileUniform)
            {
                RHI::FrameResourceProtectedCreationScope protectedCreation;
                use.TileUniform = m_Device->CreateBuffer(
                    RHI::BufferDesc(TileParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisibilityResolveTileParams"));
            }
            if (!use.TileDescriptorSet)
            {
                RHI::FrameResourceProtectedCreationScope protectedCreation;
                use.TileDescriptorSet = m_Device->CreateDescriptorSet(MakeDescriptorSetDesc(m_bDump, true, m_bFeedback));
            }
            if (!use.TileUniform || !use.TileDescriptorSet)
            {
                return false;
            }
            uses.push_back(&use);
        }

        // 解決の定数（ResolveParams）は全部の dispatch で同じなので、最初の資源の UBO を共有して 1 回だけ書く
        Use& first = *uses[0];
        if (!first.Uniform)
        {
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            first.Uniform = m_Device->CreateBuffer(
                RHI::BufferDesc(ParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisibilityResolveParams"));
        }
        if (!first.Uniform)
        {
            return false;
        }
        first.Uniform->Update(&dispatch.Params, ParamsBytes);

        commandList->SetPipeline(m_TilePipeline);
        for (uint64_t material = 0; material < materialCount; ++material)
        {
            Use& use = *uses[static_cast<size_t>(material)];
            const BoundMaterial& bound = boundMaterials[static_cast<size_t>(material)];
            uint32_t tileParams[TileParamsWords] = {tilesX, static_cast<uint32_t>(material), bound.Flags, 0u,
                                                    bound.Feedback[0], bound.Feedback[1], bound.Feedback[2], bound.Feedback[3]};
            std::memcpy(&tileParams[8], bound.TerrainParams, sizeof(bound.TerrainParams));
            use.TileUniform->Update(tileParams, TileParamsBytes);
            RHI::IDescriptorSet& set = *use.TileDescriptorSet;
            // 地形の混ぜ合わせの材質は、3 層とも異方性のサンプラーで標本する（ラスタの MegaGeometryPass と同じ）
            const RHI::SamplerPtr& materialSampler =
                (bound.Flags & VisibilityResolveGeometry::TILE_FLAG_TERRAIN) != 0u
                    ? m_TerrainMaterialSampler
                    : (bound.bMegaGeometry ? m_MegaMaterialSampler : m_MaterialSampler);
            BindCommon(set, first.Uniform, dispatch);
            set.BindConstantBuffer(firstBinding, use.TileUniform, 0, TileParamsBytes);
            set.BindStorageBuffer(firstBinding + 1, dispatch.TileArgs, 0, ClampBindSize(dispatch.TileArgs->GetSize()));
            set.BindStorageBuffer(firstBinding + 2, dispatch.TileList, 0, ClampBindSize(dispatch.TileList->GetSize()));
            for (uint32_t index = 0; index < VisibilityResolveGeometry::MATERIAL_TEXTURE_COUNT; ++index)
            {
                set.BindTexture(firstBinding + 3u + index, bound.Textures[index]);
                set.BindSampler(firstBinding + 3u + index, materialSampler);
            }
            for (uint32_t index = 0; index < VisibilityResolveGeometry::TERRAIN_TEXTURE_COUNT; ++index)
            {
                set.BindTexture(TerrainTextureBindingFirst + index, bound.TerrainTextures[index]);
                set.BindSampler(TerrainTextureBindingFirst + index, materialSampler);
            }
            if (m_bFeedback)
            {
                // 要求のバッファ。無いときは小さな代替（パラメータが 0 の材質は書かない。シェーダーも容量が足りなければ書かない）
                const uint32_t feedbackBinding = firstBinding + 3u + VisibilityResolveGeometry::MATERIAL_TEXTURE_COUNT;
                if (dispatch.Feedback)
                {
                    set.BindStorageBuffer(feedbackBinding, dispatch.Feedback, 0, BindBytes(dispatch.Feedback, dispatch.FeedbackBytes));
                }
                else
                {
                    set.BindStorageBuffer(feedbackBinding, m_Placeholder, 0, ClampBindSize(PlaceholderBytes));
                }
            }
            set.Update();

            commandList->SetDescriptorSet(use.TileDescriptorSet, 0);
            if (!commandList->DispatchIndirect(dispatch.TileArgs, material * MaterialTiles::ARGS_STRIDE_BYTES))
            {
                return false;
            }
        }
        return true;
    }

    // ========================================
    // VisibilityResolvePass
    // ========================================

    VisibilityResolvePass::VisibilityResolvePass() = default;

    VisibilityResolvePass::~VisibilityResolvePass()
    {
        Shutdown();
    }

    bool VisibilityResolvePass::Initialize(ViewRenderContext& context)
    {
        // 計算パイプラインを作れなくても描画全体は止めない（Execute が何もしない）。
        // 分類のパスがあれば材質ごとの形のパイプラインも作る。それが作れないときは、画面全体の直接 dispatch の形だけで動かす
        bool bReady = false;
        if (m_ClassifyPass)
        {
            bReady = m_Resolve.Initialize(context.Device, context.ShaderMgr, false, true);
            if (!bReady)
            {
                NORVES_LOG_WARNING("VisibilityResolvePass",
                                   "VISBUFFER_RESOLVE_TILES used=0 reason=tile_pipeline_unavailable "
                                   "材質ごとの解決を使えないので、画面全体の解決へ戻します");
            }
        }
        if (!bReady)
        {
            bReady = m_Resolve.Initialize(context.Device, context.ShaderMgr);
        }
        if (!bReady)
        {
            NORVES_LOG_WARNING("VisibilityResolvePass", "ビジビリティバッファの幾何の解決を使えないので、このパスは何もしない");
        }
        m_bInitialized = true;
        return true;
    }

    void VisibilityResolvePass::Shutdown()
    {
        m_Resolve.Shutdown();
        m_IdHandle = {};
        m_AlbedoHandle = {};
        m_NormalHandle = {};
        m_MaterialHandle = {};
        m_VelocityHandle = {};
        m_EmissiveHandle = {};
        m_TileArgsHandle = {};
        m_TileListHandle = {};
        m_TileStatsHandle = {};
        m_bResolved = false;
        m_bResolvedWithTiles = false;
        m_LastTileDispatchCount = 0;
        m_bInitialized = false;
    }

    VisibilityResolveGeometry::FallbackReason VisibilityResolvePass::GetFallbackReason(const RHI::IDevice* device,
                                                                                       DebugViewMode mode) const
    {
        using VisibilityResolveGeometry::FallbackReason;
        if (!m_bEnabled || !m_bInitialized)
        {
            return FallbackReason::PassUnavailable;
        }
        if (!device || !VisibilityResolveGeometry::IsSupported(device->GetCapabilities()))
        {
            return FallbackReason::DeviceUnsupported;
        }
        if (!m_RasterPass || !m_RasterPass->IsDrawReady(mode))
        {
            return FallbackReason::RasterUnavailable;
        }
        if (!m_Resolve.IsReady())
        {
            return FallbackReason::ResolveUnavailable;
        }
        // 計算スキニングが作れないと、スキニングの頂点が無く ID のラスタがスキニングの塊を描けない。解決へ進むとスキニングの物が消える
        if (m_SkinningComputePass && m_SkinningComputePass->IsEnabled() && !m_SkinningComputePass->IsComputeReady())
        {
            return FallbackReason::SkinningComputeUnavailable;
        }
        return FallbackReason::None;
    }

    void VisibilityResolvePass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VisibilityResolvePass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void VisibilityResolvePass::Declare(RenderGraphBuilder& builder)
    {
        m_IdHandle = {};
        m_AlbedoHandle = {};
        m_NormalHandle = {};
        m_MaterialHandle = {};
        m_VelocityHandle = {};
        m_EmissiveHandle = {};
        m_TileArgsHandle = {};
        m_TileListHandle = {};
        m_TileStatsHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        if (!context || !context->Device)
        {
            return;
        }
        // 使えない理由があるとき（装置の非対応・パイプラインが作れていない）は、GBufferPass・MegaGeometryPass が同じ判定で
        // 描画を止めていない。何も宣言しない
        const VisibilityResolveGeometry::FallbackReason fallbackReason =
            GetFallbackReason(context->Device, context->GetActiveDebugMode());
        if (fallbackReason != VisibilityResolveGeometry::FallbackReason::None)
        {
            if (!m_bLoggedFallback)
            {
                m_bLoggedFallback = true;
                NORVES_LOG_WARNING("VisibilityResolvePass",
                                   "VISBUFFER_FALLBACK reason=%s "
                                   "ビジビリティバッファの解決を使えないので、従来の GBuffer "
                                   "の描画のまま動かします",
                                   VisibilityResolveGeometry::GetFallbackReasonName(fallbackReason));
            }
            return;
        }

        // ID は VisibilityRasterPass が書いたもの。無ければ（描画のパスが何も宣言しなかった）何も宣言しない
        RGTextureHandle idHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::VisBufferId, idHandle, RHI::ResourceState::ShaderResource))
        {
            return;
        }

        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle material;
        RGTextureHandle velocity;
        RGTextureHandle emissive;
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferAlbedo, albedo) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normal) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferMaterial, material) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferVelocity, velocity) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferEmissive, emissive))
        {
            return;
        }

        // GBuffer の 5 枚を storage image として書く。終わった後の状態も UnorderedAccess にして、後のパスの読み取りの前に
        // グラフが ShaderResource への遷移を足すようにする
        m_AlbedoHandle = albedo.ToResourceHandle();
        m_NormalHandle = normal.ToResourceHandle();
        m_MaterialHandle = material.ToResourceHandle();
        m_VelocityHandle = velocity.ToResourceHandle();
        m_EmissiveHandle = emissive.ToResourceHandle();
        builder.Write(m_AlbedoHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        builder.Write(m_NormalHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        builder.Write(m_MaterialHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        builder.Write(m_VelocityHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        builder.Write(m_EmissiveHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);

        // スキニングの変形した頂点（今・前）はデバイスアドレスで読む。計算シェーダーの書き込みを、この読み取りへ見せる
        if (m_SkinningComputePass)
        {
            const RGResourceHandle currentVertices = m_SkinningComputePass->GetCurrentVerticesHandle();
            if (currentVertices.IsValid())
            {
                builder.Read(currentVertices, RHI::ResourceState::GenericRead);
            }
            const RGResourceHandle previousVertices = m_SkinningComputePass->GetPreviousVerticesHandle();
            if (previousVertices.IsValid())
            {
                builder.Read(previousVertices, RHI::ResourceState::GenericRead);
            }
        }

        // 材質ごとの形: 分類の引数・一覧・統計を GenericRead で読む（分類のパスの最終のバリアの後に読まれる）。
        // 分類のパスが無効・何も宣言しなかったフレーム（パイプラインが無い）は、ハンドルが無効なので直接 dispatch の形になる
        if (m_ClassifyPass && m_ClassifyPass->IsEnabled() && m_Resolve.IsTileReady())
        {
            const RGResourceHandle args = m_ClassifyPass->GetArgsHandle();
            const RGResourceHandle list = m_ClassifyPass->GetListHandle();
            const RGResourceHandle stats = m_ClassifyPass->GetStatsHandle();
            if (args.IsValid() && list.IsValid() && stats.IsValid())
            {
                builder.Read(args, RHI::ResourceState::GenericRead);
                builder.Read(list, RHI::ResourceState::GenericRead);
                builder.Read(stats, RHI::ResourceState::GenericRead);
                m_TileArgsHandle = args;
                m_TileListHandle = list;
                m_TileStatsHandle = stats;
            }
        }

        m_IdHandle = idHandle;
        builder.PreserveInsertionOrder();
    }

    void VisibilityResolvePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        // 準備段を通ったフレーム（区間ごとの記録）は、記録の最中にテクスチャを作らない。準備段の無い Serial の
        // フレームは今までどおり、要るものをここで作る
        const bool bPreparedForParallelRecord = m_bPreparedForParallelRecord;
        m_bPreparedForParallelRecord = false;
        m_Resolve.SetTextureCreationAllowed(!bPreparedForParallelRecord);
        // 作成を止めるのはこの記録の間だけ。どの戻り方でも許可へ戻し、次のフレーム（準備段・Serial）へ持ち越さない
        struct TextureCreationRestore
        {
            VisibilityResolve& Resolve;
            ~TextureCreationRestore()
            {
                Resolve.SetTextureCreationAllowed(true);
            }
        } textureCreationRestore{m_Resolve};

        m_bResolved = false;
        m_bResolvedWithTiles = false;
        m_LastTileDispatchCount = 0;
        if (!m_IdHandle.IsValid() || !m_AlbedoHandle.IsValid() || !context.CommandList)
        {
            return;
        }
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        if (!m_Resolve.IsReady() || !m_RasterPass)
        {
            return;
        }

        // 記録の表は、ラスタのパスが描いたフレームだけある（何も描かれなかったフレームは解決するものが無い）
        const RHI::BufferPtr recordTable = m_RasterPass->GetRecordTable();
        const RHI::BufferPtr materialTable = m_RasterPass->GetMaterialTable();
        const CameraProxy* camera = context.GetActiveCamera();
        if (!recordTable || !materialTable || !camera)
        {
            return;
        }

        VisibilityResolveDispatch dispatch;
        dispatch.IdTexture = resources.GetTexture(m_IdHandle);
        dispatch.Albedo = resources.GetTexture(m_AlbedoHandle);
        dispatch.Normal = resources.GetTexture(m_NormalHandle);
        dispatch.Material = resources.GetTexture(m_MaterialHandle);
        dispatch.Velocity = resources.GetTexture(m_VelocityHandle);
        dispatch.Emissive = resources.GetTexture(m_EmissiveHandle);
        if (!dispatch.IdTexture || !dispatch.Albedo || !dispatch.Normal || !dispatch.Material || !dispatch.Velocity ||
            !dispatch.Emissive)
        {
            return;
        }
        dispatch.RecordTable = recordTable;
        dispatch.RecordTableBytes = m_RasterPass->GetRecordTableBytes();
        dispatch.MaterialTable = materialTable;
        dispatch.MaterialTableBytes =
            static_cast<uint64_t>(m_RasterPass->GetMaterialTableCount()) * sizeof(VisibilityBuffer::MaterialEntry);
        dispatch.MegaInstances = m_RasterPass->GetMegaInstanceBuffer();
        dispatch.MegaInstancesBytes = m_RasterPass->GetMegaInstanceBufferBytes();
        dispatch.MegaMeshes = m_RasterPass->GetMegaMeshBuffer();
        dispatch.MegaMeshesBytes = m_RasterPass->GetMegaMeshBufferBytes();
        dispatch.DrawInstances = context.InstanceDataBuffer;
        dispatch.DrawInstancesBytes = context.InstanceDataBuffer ? context.InstanceDataBuffer->GetSize() : 0;

        // 材質ごとの形が束ねる、材質の表の 1 件ごとのテクスチャ（ハンドル → RHI のテクスチャ）
        const TextureResources* textures = context.Resources.Textures;
        // VT の要求を書く先（このフレームのバッファ。対応しないデバイスでは null）。GBufferPass・MegaGeometryPass と同じ
        const TextureResources::VirtualTextureFeedbackTarget feedbackTarget =
            textures ? textures->GetVirtualTextureFeedbackTarget() : TextureResources::VirtualTextureFeedbackTarget{};
        dispatch.Feedback = feedbackTarget.Buffer;
        dispatch.FeedbackBytes = feedbackTarget.Bytes;
        const Container::VariableArray<VisibilityBuffer::MaterialEntry>& materialEntries = m_RasterPass->GetMaterialEntries();
        for (size_t index = 0; index < materialEntries.size(); ++index)
        {
            dispatch.Materials.push_back(
                VisibilityResolveGeometry::MakeResolveMaterial(textures, materialEntries[index], feedbackTarget));
            AttachTerrainBlend(textures, m_RasterPass->GetMaterialTerrainBlend(index), dispatch.Materials.back());
        }

        // ラスタ（GBufferPass）と同じカメラの定数。前のカメラが無いときは速度を 0 にする
        const float aspect = context.GetActiveAspectRatio();
        const CameraViewConstants current = CameraViewConstants::BuildForDevice(*camera, aspect, context.Device);
        const CameraProxy* previousCamera = context.GetPreviousCamera();
        CameraViewConstants previous;
        if (previousCamera)
        {
            previous = CameraViewConstants::BuildForDevice(*previousCamera, aspect, context.Device);
        }
        dispatch.Params = VisibilityResolveGeometry::BuildParams(current,
                                                                 previousCamera ? &previous : nullptr,
                                                                 context.GetActiveLocalViewport(),
                                                                 dispatch.IdTexture->GetWidth(),
                                                                 dispatch.IdTexture->GetHeight(),
                                                                 m_RasterPass->GetMaterialTableCount());
        // 発光はプリエクスポージャ後の値で GBuffer_Emissive へ書く（GBufferPass・MegaGeometryPass・LightingPass と同じ値）
        dispatch.Params.Frame[0] = ResolveSceneColorPreExposure(camera);
        // MegaGeometry のデバッグの表示（クラスタの色・LOD の段）は、描画の記録の payload から色を作る（MegaGeometryPass が
        // 同じ表示の選択でカリングの payload をクラスタの番号・LOD の段に切り替える）
        dispatch.Params.Frame[1] = VisibilityResolveGeometry::ResolveDebugViewCode(context.GetActiveDebugMode());

        // 材質ごとの形で解決できるかを決める。使えない理由があれば、画面全体の直接 dispatch へ戻す（画面を空にしない）
        const uint32_t materialCount = m_RasterPass->GetMaterialTableCount();
        const char* tileReason = nullptr;
        if (m_TileArgsHandle.IsValid() && m_TileListHandle.IsValid())
        {
            const uint32_t tilesX = (dispatch.Params.Screen[0] + VisibilityResolveGeometry::TILE_SIZE - 1) /
                                    VisibilityResolveGeometry::TILE_SIZE;
            if (!m_ClassifyPass->WasClassified())
            {
                tileReason = "classify_not_recorded";
            }
            else if (m_ClassifyPass->GetClassifiedTilesX() != tilesX)
            {
                // 一覧のタイルの番号は分類の横のタイル数で作られている。違う幅で読むと別のタイルを解決してしまう
                tileReason = "tiles_x_mismatch";
            }
            else if (materialCount == 0 || materialCount > m_ClassifyPass->GetLayout().MaxMaterials)
            {
                // 0 は「引数の表の件数ぶん全部」の意味になり、上限以上の材質はどの一覧にも入らない
                tileReason = "material_count_out_of_range";
            }
            else
            {
                dispatch.TileArgs = resources.GetBuffer(m_TileArgsHandle);
                dispatch.TileList = resources.GetBuffer(m_TileListHandle);
                if (!dispatch.TileArgs || !dispatch.TileList)
                {
                    dispatch.TileArgs = {};
                    dispatch.TileList = {};
                    tileReason = "tile_buffers_unavailable";
                }
                // 起動画面は 17〜24 材質。引数の表の件数ぶんではなく、そのフレームの材質の表の数だけ dispatch する
                dispatch.TileMaterialCount = materialCount;
            }
        }
        else
        {
            tileReason = m_ClassifyPass ? "classify_not_declared" : "no_classify_pass";
        }

        m_Resolve.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        bool bRecorded = m_Resolve.Record(context.CommandList, dispatch);
        if (!bRecorded && dispatch.TileArgs)
        {
            // 断られたとき、dispatch は 1 回も記録されていない（最初の間接 dispatch を断られたときに残るのは、
            // パイプラインとディスクリプタセットの設定だけ。直接 dispatch が設定し直す）ので、直接 dispatch でやり直せる
            dispatch.TileArgs = {};
            dispatch.TileList = {};
            dispatch.TileMaterialCount = 0;
            tileReason = "tile_record_failed";
            bRecorded = m_Resolve.Record(context.CommandList, dispatch);
        }
        m_bResolved = bRecorded;
        m_bResolvedWithTiles = bRecorded && dispatch.TileArgs != nullptr;
        m_LastTileDispatchCount = m_bResolvedWithTiles ? materialCount : 0u;

        // 解決の形（材質ごと / 画面全体）と間接 dispatch の数は、変わったときだけログへ出す。
        // 分類のパスを持たない構成（既定の直接 dispatch）では出さない
        if (m_ClassifyPass && bRecorded &&
            (!m_bLoggedTileState || m_bLoggedTileUsed != m_bResolvedWithTiles ||
             m_LoggedTileMaterials != m_LastTileDispatchCount || m_LoggedTileReason != tileReason))
        {
            m_bLoggedTileState = true;
            m_bLoggedTileUsed = m_bResolvedWithTiles;
            m_LoggedTileMaterials = m_LastTileDispatchCount;
            m_LoggedTileReason = tileReason;
            if (m_bResolvedWithTiles)
            {
                NORVES_LOG_INFO("VisibilityResolvePass",
                                "VISBUFFER_RESOLVE_TILES used=1 dispatches=%u materials=%u",
                                m_LastTileDispatchCount,
                                materialCount);
            }
            else
            {
                NORVES_LOG_WARNING("VisibilityResolvePass",
                                   "VISBUFFER_RESOLVE_TILES used=0 reason=%s "
                                   "画面全体の解決（直接 dispatch）で記録しました",
                                   tileReason ? tileReason : "none");
            }
        }
    }

    bool VisibilityResolvePass::DeclaresAllLayoutChanges() const
    {
        // Execute が読む他のパスの状態（ラスタの表・分類の結果）は前の Serial のパスが書き終えたもので、書き込むのは
        // 自分のメンバと自分のディスクリプタセット・定数のリングだけ。初期化・パイプラインが無いフレームは Serial に残す。
        return IsInitialized() && m_Resolve.IsReady() && m_RasterPass != nullptr && m_IdHandle.IsValid() &&
               m_AlbedoHandle.IsValid();
    }

    RGRecordPolicy VisibilityResolvePass::GetRecordPolicy() const
    {
        return DeclaresAllLayoutChanges() ? RGRecordPolicy::Parallel : RGRecordPolicy::Serial;
    }

    void VisibilityResolvePass::PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context)
    {
        (void)resources;
        // 準備段が作れなかったものも、同じフレームの記録の最中には作らない（Execute が作成を止める）
        m_bPreparedForParallelRecord = true;
        if (!m_bInitialized || !m_Resolve.IsReady() || !m_RasterPass)
        {
            return;
        }

        // Execute と同じ材質の並び（ラスタのパスは前の Serial の記録で材質を集め終えている）から、
        // 既定・定数のテクスチャを作る。作成・転送は単発のコマンドの提出・待ちとバリアを伴うので、ここで直列に行う
        const TextureResources* textures = context.Resources.Textures;
        const TextureResources::VirtualTextureFeedbackTarget feedbackTarget =
            textures ? textures->GetVirtualTextureFeedbackTarget() : TextureResources::VirtualTextureFeedbackTarget{};
        Container::VariableArray<VisibilityResolveMaterial>& materials = m_PrepareMaterialScratch;
        materials.clear();
        for (const VisibilityBuffer::MaterialEntry& entry : m_RasterPass->GetMaterialEntries())
        {
            materials.push_back(VisibilityResolveGeometry::MakeResolveMaterial(textures, entry, feedbackTarget));
        }
        if (!m_Resolve.PrepareMaterialTextures(materials))
        {
            NORVES_LOG_WARNING("VisibilityResolvePass", "区間ごとの記録の前に、材質の既定・定数のテクスチャをそろえられませんでした");
        }
        materials.clear();
    }

    bool VisibilityResolvePass::ReadsRecordingOf(const IRenderGraphPass& earlierPass) const
    {
        // ラスタの表（記録の表・材質・MegaGeometry の instance）と、材質のタイルの分類の結果を準備段と Execute で読む
        return (m_RasterPass && &earlierPass == static_cast<const IRenderGraphPass*>(m_RasterPass)) ||
               (m_ClassifyPass && &earlierPass == static_cast<const IRenderGraphPass*>(m_ClassifyPass));
    }

} // namespace NorvesLib::Core::Rendering
