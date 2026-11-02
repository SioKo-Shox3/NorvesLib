// VFX のメッシュ粒子を実シェーダーで描き、時間による溶けを画素から確かめる。
#include "Math/MatrixUtils.h"
#include "Particle/VfxEffectSystem.h"
#include "Particle/VfxSchema.h"
#include "Rendering/ShaderManager.h"
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Particle;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    namespace GpuTest = NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "MeshParticleDissolveVulkanTest";
    constexpr uint32_t Width = 128u;
    constexpr uint32_t Height = 128u;
    constexpr uint32_t PixelCount = Width * Height;

    struct Vertex
    {
        float Position[3];
        float UV[2];
    };

    // シェーダーの std430 配列要素と同じ並びにする。
    struct ShaderInstance
    {
        float World[16];
        float Color[4];
        float EdgeColor[4];
        float Dissolve[4];
    };
    static_assert(sizeof(ShaderInstance) == 112u);
    static_assert(offsetof(ShaderInstance, Color) == 64u);
    static_assert(offsetof(ShaderInstance, EdgeColor) == 80u);
    static_assert(offsetof(ShaderInstance, Dissolve) == 96u);

    struct PixelCounts
    {
        uint32_t Covered = 0u;
        uint32_t Body = 0u;
        uint32_t Edge = 0u;
    };

    bool MakeEffect(VfxEffectAsset& asset, float start, float duration)
    {
        Container::String error;
        VfxEmitterAsset emitter = MakeVfxDefaultEmitter("MeshEmitter");
        VfxModule spawn = MakeVfxDefaultModule(VfxModuleType::SpawnBurst);
        VfxModule lifetime = MakeVfxDefaultModule(VfxModuleType::InitLifetime);
        VfxModule color = MakeVfxDefaultModule(VfxModuleType::InitColor);
        VfxModule size = MakeVfxDefaultModule(VfxModuleType::InitSize);
        VfxModule renderer = MakeVfxDefaultModule(VfxModuleType::MeshRenderer);
        if (!SetVfxEmitterProperty(emitter, "Space", VfxValue::MakeEnum(static_cast<uint32_t>(VfxSimulationSpace::World)), &error) ||
            !SetVfxModuleParameter(spawn, "Count", VfxValue::MakeUInt(1u), &error) ||
            !SetVfxModuleParameter(lifetime, "Lifetime", VfxValue::MakeFloat(1.0f), &error) ||
            !SetVfxModuleParameter(color, "Color", VfxValue::MakeColor(Math::Vector4(0.0f, 1.0f, 0.0f, 1.0f)), &error) ||
            !SetVfxModuleParameter(size, "Size", VfxValue::MakeVector2(Math::Vector2(2.0f, 2.0f)), &error) ||
            !SetVfxModuleParameter(renderer, "Mesh", VfxValue::MakeName("test_mesh"), &error) ||
            !SetVfxModuleParameter(renderer, "AlignToVelocity", VfxValue::MakeBool(false), &error) ||
            !SetVfxModuleParameter(renderer, "DissolveStart", VfxValue::MakeFloat(start), &error) ||
            !SetVfxModuleParameter(renderer, "DissolveDuration", VfxValue::MakeFloat(duration), &error) ||
            !SetVfxModuleParameter(renderer, "EdgeWidth", VfxValue::MakeFloat(0.15f), &error) ||
            !SetVfxModuleParameter(renderer, "EdgeColor", VfxValue::MakeColor(Math::Vector4(1.0f, 0.0f, 0.0f, 1.0f)), &error))
        {
            std::cerr << TestName << " エフェクトの構成に失敗: " << error.c_str() << '\n';
            return false;
        }
        emitter.Modules.push_back(spawn);
        emitter.Modules.push_back(lifetime);
        emitter.Modules.push_back(color);
        emitter.Modules.push_back(size);
        emitter.Modules.push_back(renderer);
        asset.Name = "MeshParticleDissolve";
        asset.Emitters.push_back(emitter);
        return true;
    }

    bool CopyRecord(const VfxEffectSystem& system, VfxEffectHandle handle, float expectedAge,
                    float expectedThreshold, VfxMeshInstance& outRecord)
    {
        VariableArray<VfxParticleState> particles;
        VariableArray<VfxMeshInstance> records;
        if (!system.CopyParticles(handle, 0u, particles))
        {
            std::cerr << TestName << " 粒を取得できませんでした\n";
            return false;
        }
        system.AppendMeshInstances(records);
        if (particles.size() != 1u || records.size() != 1u ||
            std::fabs(particles[0].Age - expectedAge) > 0.00001f ||
            std::fabs(records[0].DissolveThreshold - expectedThreshold) > 0.00001f)
        {
            std::cerr << TestName << " 粒の年齢または描画記録が想定と異なります: 年齢=" << expectedAge
                      << " 粒=" << particles.size() << " 記録=" << records.size() << '\n';
            return false;
        }
        outRecord = records[0];
        return true;
    }

    bool MakeRecords(VfxMeshInstance (&records)[5])
    {
        VfxEffectAsset asset;
        if (!MakeEffect(asset, 0.0f, 1.0f))
        {
            return false;
        }
        VfxEffectSystem system;
        Container::String error;
        VfxInstanceDesc desc;
        desc.Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        desc.Mesh = Rendering::MeshDataHandle{101u};
        desc.Material = Rendering::MaterialHandle{202u};
        const VfxEffectHandle handle = system.CreateInstance(asset, desc, &error);
        if (!handle.IsValid())
        {
            std::cerr << TestName << " インスタンスを作れませんでした: " << error.c_str() << '\n';
            return false;
        }
        // 最初の固定刻みでバーストが生まれ、その時点の年齢は 0。
        system.Tick(0.25f);
        for (uint32_t i = 0u; i < 4u; ++i)
        {
            const float age = static_cast<float>(i) * 0.25f;
            if (!CopyRecord(system, handle, age, age, records[i]))
            {
                return false;
            }
            if (i < 3u)
            {
                system.Tick(0.25f);
            }
        }

        VfxEffectAsset instantAsset;
        if (!MakeEffect(instantAsset, 0.5f, 0.0f))
        {
            return false;
        }
        VfxEffectSystem instantSystem;
        const VfxEffectHandle instantHandle = instantSystem.CreateInstance(instantAsset, desc, &error);
        if (!instantHandle.IsValid())
        {
            std::cerr << TestName << " 即時に溶けるインスタンスを作れませんでした: " << error.c_str() << '\n';
            return false;
        }
        instantSystem.Tick(0.1f);
        for (uint32_t i = 0u; i < 6u; ++i)
        {
            instantSystem.Tick(0.1f);
        }
        return CopyRecord(instantSystem, instantHandle, 0.6f, 1.0f, records[4]);
    }

    BufferPtr CreateHostBuffer(const DevicePtr& device, const void* data, uint64_t bytes,
                               ResourceUsage usage, const char* name)
    {
        BufferPtr buffer = device->CreateBuffer(BufferDesc(bytes, usage, true, name));
        if (!buffer)
        {
            return nullptr;
        }
        void* mapped = buffer->Map(0u, bytes);
        if (!mapped)
        {
            return nullptr;
        }
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        buffer->Unmap();
        return buffer;
    }

    DescriptorSetDesc MakeDescriptorLayout()
    {
        DescriptorSetDesc layout;
        const ResourceBindType types[] = {ResourceBindType::ConstantBuffer, ResourceBindType::StructuredBuffer};
        for (uint32_t i = 0u; i < 2u; ++i)
        {
            DescriptorBinding binding;
            binding.binding = i;
            binding.type = types[i];
            binding.stages = ShaderStage::Vertex;
            layout.bindings.push_back(binding);
        }
        return layout;
    }

    bool RenderAndCount(const DevicePtr& device, const ShaderPtr& vertexShader, const ShaderPtr& fragmentShader,
                        const VfxMeshInstance& record, PixelCounts& counts)
    {
        const Vertex vertices[] = {
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
            {{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}},
        };
        float viewProjection[16];
        Math::MatrixUtils::CopyToShaderData(Math::Matrix4x4(), viewProjection);
        ShaderInstance instance{};
        Math::MatrixUtils::CopyToShaderData(record.World, instance.World);
        instance.Color[0] = record.Color.x;
        instance.Color[1] = record.Color.y;
        instance.Color[2] = record.Color.z;
        instance.Color[3] = record.Color.w;
        instance.EdgeColor[0] = record.EdgeColor.x;
        instance.EdgeColor[1] = record.EdgeColor.y;
        instance.EdgeColor[2] = record.EdgeColor.z;
        instance.EdgeColor[3] = record.EdgeColor.w;
        instance.Dissolve[0] = record.DissolveThreshold;
        instance.Dissolve[1] = record.EdgeWidth;

        BufferPtr vertexBuffer = CreateHostBuffer(device, vertices, sizeof(vertices), ResourceUsage::VertexBuffer, "MeshParticleVertices");
        BufferPtr viewBuffer = CreateHostBuffer(device, viewProjection, sizeof(viewProjection), ResourceUsage::ConstantBuffer, "MeshParticleView");
        BufferPtr instanceBuffer = CreateHostBuffer(device, &instance, sizeof(instance),
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead, "MeshParticleInstances");
        BufferPtr readback = device->CreateBuffer(BufferDesc(PixelCount * 4u, ResourceUsage::TransferDst, true, "MeshParticleReadback"));

        TextureDesc textureDesc;
        textureDesc.Width = Width;
        textureDesc.Height = Height;
        textureDesc.TextureFormat = Format::R8G8B8A8_UNORM;
        textureDesc.Usage = ResourceUsage::RenderTarget | ResourceUsage::TransferSrc;
        textureDesc.DebugName = "MeshParticleDissolveTarget";
        TexturePtr target = device->CreateTexture(textureDesc);

        AttachmentDesc attachment;
        attachment.format = textureDesc.TextureFormat;
        attachment.clear = true;
        attachment.clearColor[0] = 0.0f;
        attachment.clearColor[1] = 0.0f;
        attachment.clearColor[2] = 0.0f;
        attachment.clearColor[3] = 0.0f;
        attachment.loadOp = AttachmentLoadOp::Clear;
        attachment.storeOp = AttachmentStoreOp::Store;
        attachment.initialState = ResourceState::Undefined;
        attachment.finalState = ResourceState::RenderTarget;
        RenderPassDesc passDesc;
        passDesc.colorAttachments.push_back(attachment);
        passDesc.hasDepthStencil = false;
        RenderPassPtr renderPass = device->CreateRenderPass(passDesc);
        if (!vertexBuffer || !viewBuffer || !instanceBuffer || !readback || !target || !renderPass)
        {
            std::cerr << TestName << " 描画用の資源を作れませんでした\n";
            return false;
        }

        FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = renderPass;
        framebufferDesc.colorTargets.push_back(target);
        framebufferDesc.width = Width;
        framebufferDesc.height = Height;
        FramebufferPtr framebuffer = device->CreateFramebuffer(framebufferDesc);
        DescriptorSetDesc layout = MakeDescriptorLayout();
        DescriptorSetPtr descriptorSet = device->CreateDescriptorSet(layout);

        GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.pixelShader = fragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0u;
        vertexBinding.stride = sizeof(Vertex);
        vertexBinding.inputRate = VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);
        VertexAttributeDesc position;
        position.location = 0u;
        position.binding = 0u;
        position.format = Format::R32G32B32_FLOAT;
        position.offset = offsetof(Vertex, Position);
        pipelineDesc.vertexAttributes.push_back(position);
        VertexAttributeDesc uv;
        uv.location = 1u;
        uv.binding = 0u;
        uv.format = Format::R32G32_FLOAT;
        uv.offset = offsetof(Vertex, UV);
        pipelineDesc.vertexAttributes.push_back(uv);
        pipelineDesc.rasterState.polygonMode = PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = CullMode::None;
        pipelineDesc.rasterState.frontFace = FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;
        BlendAttachmentDesc blend;
        blend.blendEnable = false;
        blend.colorWriteMask = ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blend);
        pipelineDesc.renderPass = renderPass;
        pipelineDesc.descriptorSetLayouts.push_back(layout);
        PipelinePtr pipeline = device->CreateGraphicsPipeline(pipelineDesc);
        CommandListPtr commandList = device->CreateCommandList();
        if (!framebuffer || !descriptorSet || !pipeline || !commandList)
        {
            std::cerr << TestName << " 描画のパイプラインを作れませんでした\n";
            return false;
        }
        descriptorSet->BindConstantBuffer(0u, viewBuffer, 0u, sizeof(viewProjection));
        descriptorSet->BindStorageBuffer(1u, instanceBuffer, 0u, sizeof(instance));
        descriptorSet->Update();

        Viewport viewport;
        viewport.width = static_cast<float>(Width);
        viewport.height = static_cast<float>(Height);
        RHI::ScissorRect scissor;
        scissor.right = static_cast<int32_t>(Width);
        scissor.bottom = static_cast<int32_t>(Height);
        commandList->Begin();
        commandList->BeginRenderPass(renderPass, framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0u);
        commandList->SetVertexBuffer(vertexBuffer, 0u, 0u);
        commandList->DrawInstanced(6u, 1u, 0u, 0u);
        commandList->EndRenderPass();
        commandList->TextureBarrier(target, ResourceState::RenderTarget, ResourceState::CopySource);
        commandList->CopyTextureToBuffer(target, readback, Width, Height, 0u);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        const uint8_t* pixels = static_cast<const uint8_t*>(readback->Map(0u, PixelCount * 4u));
        if (!pixels)
        {
            std::cerr << TestName << " 画素を読み戻せませんでした\n";
            return false;
        }
        for (uint32_t i = 0u; i < PixelCount; ++i)
        {
            const uint8_t* pixel = pixels + i * 4u;
            counts.Covered += pixel[3] != 0u ? 1u : 0u;
            counts.Body += pixel[0] == 0u && pixel[1] == 255u && pixel[2] == 0u && pixel[3] == 255u ? 1u : 0u;
            counts.Edge += pixel[0] == 255u && pixel[1] == 0u && pixel[2] == 0u && pixel[3] == 255u ? 1u : 0u;
        }
        readback->Unmap();
        return true;
    }

    int RunTest()
    {
        if (GpuTest::IsForcedGpuTestSkipRequested())
        {
            return GpuTest::ReportGpuTestSkip(TestName, "NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
        }
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return GpuTest::ReportGpuTestSkip(TestName, "Vulkan デバイスを利用できません");
        }
        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << TestName << " ShaderManager を初期化できませんでした\n";
            return 1;
        }
        ShaderPtr vertexShader = shaderManager.LoadShader("mesh_particle_dissolve.vert", ShaderStage::Vertex);
        ShaderPtr fragmentShader = shaderManager.LoadShader("mesh_particle_dissolve.frag", ShaderStage::Pixel);
        if (!vertexShader || !fragmentShader)
        {
            std::cerr << TestName << " シェーダーをコンパイルできませんでした\n";
            return 1;
        }

        VfxMeshInstance records[5];
        PixelCounts counts[5];
        if (!MakeRecords(records))
        {
            return 1;
        }
        for (uint32_t i = 0u; i < 5u; ++i)
        {
            if (!RenderAndCount(device, vertexShader, fragmentShader, records[i], counts[i]))
            {
                return 1;
            }
        }
        const bool fullBody = counts[0].Covered == PixelCount && counts[0].Body == PixelCount;
        const bool decreasing = counts[1].Covered > counts[2].Covered && counts[2].Covered > counts[3].Covered;
        const float ratio = counts[0].Covered == 0u ? 0.0f : static_cast<float>(counts[2].Covered) / counts[0].Covered;
        const bool middle = ratio >= 0.2f && ratio <= 0.8f;
        const bool edges = counts[1].Edge > 0u && counts[2].Edge > 0u && counts[3].Edge > 0u;
        const bool dissolved = counts[4].Covered == 0u;
        std::cout << "1. 年齢 0: 覆う=" << counts[0].Covered << " 本体=" << counts[0].Body << " 全画素=" << PixelCount << '\n';
        std::cout << "2. 覆う画素 0.25/0.5/0.75: " << counts[1].Covered << '/' << counts[2].Covered << '/' << counts[3].Covered << '\n';
        std::cout << "3. 年齢 0.5 の割合: " << ratio << '\n';
        std::cout << "4. 縁の画素 0.25/0.5/0.75: " << counts[1].Edge << '/' << counts[2].Edge << '/' << counts[3].Edge << '\n';
        std::cout << "5. 閾値 1 で覆う画素: " << counts[4].Covered << '\n';
        shaderManager.Shutdown();
        return fullBody && decreasing && middle && edges && dissolved ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << " で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
