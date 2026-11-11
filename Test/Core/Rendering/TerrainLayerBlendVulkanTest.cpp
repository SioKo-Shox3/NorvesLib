// 地形スプラットを合成した面で標本し、象限の色・崖の投影・濡れの性質を GPU の数値で確かめる。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "FileStream/FileStream.h"
#include "Rendering/TerrainBlendShading.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>

namespace NorvesLib::RHI::Vulkan
{
    void BeginVulkanValidationErrorCaptureForTesting() noexcept;
    void EndVulkanValidationErrorCaptureForTesting() noexcept;
    uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "TerrainLayerBlendVulkanTest";
    constexpr uint32_t GridSize = 64u;
    constexpr uint32_t StripeSize = 64u;
    constexpr uint32_t SlopeSamples = 2048u;
    constexpr float Pi = 3.14159265358979f;
    constexpr float Sin60 = 0.86602540378444f;
    // UNORM の量子化（1/255）と補間の誤差に余裕を持たせる。層の取り違え・sRGB の戻し忘れより十分小さい。
    constexpr float ColorTolerance = 0.02f;
    constexpr uint8_t LayerColors[4][3] = {{204, 51, 26}, {26, 179, 77}, {51, 77, 230}, {179, 153, 51}};

    struct ProbeInput
    {
        float PositionDistance[4];
        float WorldDx[4];
        float WorldDy[4];
        float Normal[4];
    };
    struct ProbeOutput
    {
        float AlbedoRoughness[4];
        float NormalCliffWeight[4];
    };
    static_assert(sizeof(ProbeInput) == 64u);
    static_assert(sizeof(ProbeOutput) == 32u);

    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << ": " << message << '\n';
        }
        return condition;
    }

    // ルートを基準に include を再帰展開し、実ファイルの BOM だけを読み込み時に除く。
    bool ExpandShaderText(const String& text, String& expanded, uint32_t depth = 0u, bool bBefore = false)
    {
        if (!Check(depth < 16u, "シェーダーの include が深すぎます")) return false;
        size_t start = 0;
        while (start < text.size())
        {
            const size_t newline = text.find('\n', start);
            const size_t end = newline == String::npos ? text.size() : newline;
            const String line = text.substr(start, end - start);
            const size_t directive = line.find("#include");
            if (directive != String::npos)
            {
                const size_t first = line.find('"', directive + 8u);
                const size_t last = first == String::npos ? String::npos : line.find('"', first + 1u);
                if (!Check(last != String::npos, "include の引用符を読めません")) return false;
                const String include = line.substr(first + 1u, last - first - 1u);
                // 写しは 411e72cf のバイトをそのまま保存する。差し替えるのはこの 1 ファイルだけ。
                const String path = bBefore && include == "Common/TerrainLayerBlend.glsl"
                    ? String(NORVES_SOURCE_ROOT "/Test/Core/Rendering/TerrainLayerBlendBefore.glsl")
                    : String(NORVES_SOURCE_ROOT "/Assets/Shaders/") + include;
                FileStream::FileStream file;
                if (!Check(file.Open(path, FileStream::FileMode::Read, FileStream::FileAccess::Read),
                           "シェーダーの共通ファイルを開けません")) return false;
                const int64_t length = file.GetSize();
                if (!Check(length >= 0, "シェーダーの長さを読めません")) return false;
                VariableArray<char> bytes(static_cast<size_t>(length) + 1u, '\0');
                if (!Check(file.Read(bytes.data(), static_cast<size_t>(length)) == static_cast<size_t>(length),
                           "シェーダーを最後まで読めません")) return false;
                const bool bBom = length >= 3 && static_cast<uint8_t>(bytes[0]) == 0xefu &&
                    static_cast<uint8_t>(bytes[1]) == 0xbbu && static_cast<uint8_t>(bytes[2]) == 0xbfu;
                if (!ExpandShaderText(String(bytes.data() + (bBom ? 3u : 0u)), expanded, depth + 1u, bBefore)) return false;
            }
            else
            {
                expanded += line;
                expanded += "\n";
            }
            start = end + 1u;
        }
        return true;
    }

    bool BuildProbeSource(String& source)
    {
        // megageometry.frag と同じ依存順。標本は計算用の Core を使い、画面微分を明示した勾配に置き換える。
        const String probe = R"glsl(#version 450
#define NORVES_MATERIAL_SAMPLING_EXPLICIT_GRADIENT 1
#define NORVES_VT_FEEDBACK_COMPUTE 1
#undef NORVES_VT_FEEDBACK
#include "Common/GBufferEncoding.glsl"
#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#include "Common/VirtualTextureFeedback.glsl"
#include "Common/PbrMaterialTextureSamplingCore.glsl"
#include "Common/TerrainLayerBlend.glsl"
layout(local_size_x = 64) in;
struct ProbeInput { vec4 positionDistance; vec4 dx; vec4 dy; vec4 normal; };
struct ProbeOutput { vec4 albedoRoughness; vec4 normalCliffWeight; };
layout(std430, set = 0, binding = 0) readonly buffer Inputs { ProbeInput values[]; } inputs;
layout(std430, set = 0, binding = 1) writeonly buffer Outputs { ProbeOutput values[]; } outputs;
layout(set = 0, binding = 18) uniform sampler2DArray albedoArray;
layout(set = 0, binding = 19) uniform sampler2DArray normalArray;
layout(set = 0, binding = 20) uniform sampler2DArray armArray;
layout(set = 0, binding = 21) uniform sampler2DArray splatArray;
layout(std140, set = 0, binding = 23) uniform Params { vec4 terrainSplat[12]; } params;
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(inputs.values.length())) return;
    ProbeInput p = inputs.values[index];
    TerrainSurfaceInput surface;
    surface.WorldPosition = p.positionDistance.xyz;
    surface.CameraDistance = p.positionDistance.w;
    surface.WorldDx = p.dx.xyz;
    surface.WorldDy = p.dy.xyz;
    surface.GeometricNormal = p.normal.xyz;
    TerrainSplatParams terrain = MakeTerrainSplatParams(params.terrainSplat);
    vec3 worldNormal;
    PbrMaterialTextureSamples result = ShadeTerrainSplat(terrain, albedoArray, normalArray,
        armArray, splatArray, surface, worldNormal);
    float weights[8];
    vec2 uv = TerrainSplatUv(terrain, surface.WorldPosition.xz);
    TerrainSplatLayerWeights(terrain, textureLod(splatArray, vec3(uv, 0.0), 0.0),
        textureLod(splatArray, vec3(uv, 1.0), 0.0), acos(clamp(normalize(p.normal.xyz).y, -1.0, 1.0)), weights);
    int cliff = int(terrain.shape.y);
    outputs.values[index].albedoRoughness = vec4(result.Albedo.rgb, result.Material.y);
    outputs.values[index].normalCliffWeight = vec4(worldNormal, cliff >= 0 ? weights[cliff] : 0.0);
}
)glsl";
        return ExpandShaderText(probe, source);
    }

    DescriptorSetDesc MakeLayout()
    {
        DescriptorSetDesc layout;
        const uint32_t indices[] = {0u, 1u, TERRAIN_SPLAT_MEGA_ALBEDO_BINDING, TERRAIN_SPLAT_MEGA_NORMAL_BINDING,
            TERRAIN_SPLAT_MEGA_ARM_BINDING, TERRAIN_SPLAT_MEGA_SPLAT_BINDING, TERRAIN_SPLAT_MEGA_PARAMS_BINDING};
        for (uint32_t i = 0; i < 7u; ++i)
        {
            DescriptorBinding binding;
            binding.binding = indices[i];
            binding.type = i == 0u ? ResourceBindType::StructuredBuffer : i == 1u ? ResourceBindType::RWBuffer :
                i == 6u ? ResourceBindType::ConstantBuffer : ResourceBindType::CombinedImageSampler;
            binding.stages = RHI::ShaderStage::Compute;
            layout.bindings.push_back(binding);
        }
        return layout;
    }

    PipelinePtr CreateProbePipeline(IDevice& device)
    {
        String source;
        if (!BuildProbeSource(source)) return {};
        ShaderCompilerPtr compiler = device.CreateShaderCompiler();
        if (!Check(compiler != nullptr, "シェーダーコンパイラを作れません")) return {};
        const auto compiled = compiler->CompileFromSource(source, RHI::ShaderStage::Compute, TestName);
        if (!compiled.bSuccess)
        {
            std::cerr << "計算シェーダーをコンパイルできません: " << compiled.ErrorMessage.c_str() << '\n';
            return {};
        }
        ShaderDesc shaderDesc;
        shaderDesc.stage = RHI::ShaderStage::Compute;
        shaderDesc.byteCode = compiled.ByteCode;
        ShaderPtr shader = device.CreateShader(shaderDesc);
        if (!Check(shader != nullptr, "計算シェーダーを作れません")) return {};
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = shader;
        pipelineDesc.descriptorSetLayouts.push_back(MakeLayout());
        return device.CreateComputePipeline(pipelineDesc);
    }

    // 各層を別のコピーで上げる。1 ミップの配列にして、標本の分解能がミップ選択に依存しないようにする。
    TexturePtr UploadArray(IDevice& device, uint32_t size, uint32_t layers, Format format,
                           const void* data, uint32_t layerBytes)
    {
        TextureDesc desc;
        desc.Width = size;
        desc.Height = size;
        desc.ArraySize = layers;
        desc.MipLevels = 1u;
        desc.TextureFormat = format;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.DebugName = "TerrainLayerBlendTest.Array";
        TexturePtr texture = device.CreateTexture(desc);
        BufferPtr upload = device.CreateBuffer(BufferDesc(layerBytes * layers, ResourceUsage::TransferSrc, true,
            "TerrainLayerBlendTest.Upload"));
        CommandListPtr commands = device.CreateCommandList();
        if (!Check(texture && upload && commands, "配列のアップロード資源を作れません")) return {};
        upload->Update(data, layerBytes * layers);
        commands->Begin();
        commands->TextureBarrier(texture, ResourceState::Undefined, ResourceState::CopyDest);
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            commands->CopyBufferToTexture(upload, texture, size, size, layer * layerBytes, 0u, layer);
        }
        commands->TextureBarrier(texture, ResourceState::CopyDest, ResourceState::ShaderResource);
        commands->End();
        commands->Submit(true);
        device.WaitIdle();
        return texture;
    }

    struct ProbeResources
    {
        DevicePtr Device;
        PipelinePtr Pipeline;
        SamplerPtr RepeatSampler;
        SamplerPtr ClampSampler;
        TexturePtr Albedo;
        TexturePtr Normal;
        TexturePtr Arm;
        TexturePtr Splat;
    };

    bool ReadShade(ProbeResources& resources, const MegaGeometry::MegaTerrainSplat& splat, uint32_t flags, float wetness,
                   const VariableArray<ProbeInput>& inputs, VariableArray<ProbeOutput>& outputs)
    {
        const uint32_t inputBytes = static_cast<uint32_t>(inputs.size() * sizeof(ProbeInput));
        const uint32_t outputBytes = static_cast<uint32_t>(inputs.size() * sizeof(ProbeOutput));
        float packed[TERRAIN_SPLAT_PARAM_FLOATS];
        PackTerrainSplatParams(splat, true, flags, wetness, packed);
        BufferPtr input = resources.Device->CreateBuffer(BufferDesc(inputBytes, ResourceUsage::StorageBuffer |
            ResourceUsage::ShaderRead, true, "TerrainLayerBlendTest.Input"));
        BufferPtr params = resources.Device->CreateBuffer(BufferDesc(sizeof(packed), ResourceUsage::ConstantBuffer,
            true, "TerrainLayerBlendTest.Params"));
        BufferPtr result = resources.Device->CreateBuffer(BufferDesc(outputBytes, ResourceUsage::StorageBuffer |
            ResourceUsage::TransferSrc, true, "TerrainLayerBlendTest.Result"));
        BufferPtr readback = resources.Device->CreateBuffer(BufferDesc(outputBytes, ResourceUsage::TransferDst,
            true, "TerrainLayerBlendTest.Readback"));
        DescriptorSetPtr set = resources.Device->CreateDescriptorSet(MakeLayout());
        CommandListPtr commands = resources.Device->CreateCommandList();
        if (!Check(input && params && result && readback && set && commands, "標本と読み戻しの資源を作れません")) return false;
        input->Update(inputs.data(), inputBytes);
        params->Update(packed, sizeof(packed));
        outputs.resize(inputs.size());
        for (ProbeOutput& value : outputs)
        {
            for (float& component : value.AlbedoRoughness) component = std::numeric_limits<float>::quiet_NaN();
            for (float& component : value.NormalCliffWeight) component = std::numeric_limits<float>::quiet_NaN();
        }
        result->Update(outputs.data(), outputBytes);
        set->BindStorageBuffer(0u, input, 0u, inputBytes);
        set->BindStorageBuffer(1u, result, 0u, outputBytes);
        set->BindConstantBuffer(TERRAIN_SPLAT_MEGA_PARAMS_BINDING, params, 0u, sizeof(packed));
        const TexturePtr textures[] = {resources.Albedo, resources.Normal, resources.Arm, resources.Splat};
        const uint32_t bindings[] = {TERRAIN_SPLAT_MEGA_ALBEDO_BINDING, TERRAIN_SPLAT_MEGA_NORMAL_BINDING,
            TERRAIN_SPLAT_MEGA_ARM_BINDING, TERRAIN_SPLAT_MEGA_SPLAT_BINDING};
        for (uint32_t i = 0; i < 4u; ++i)
        {
            set->BindTexture(bindings[i], textures[i]);
            set->BindSampler(bindings[i], i == 3u ? resources.ClampSampler : resources.RepeatSampler);
        }
        set->Update();
        commands->Begin();
        commands->BufferBarrier(result, ResourceState::Undefined, ResourceState::UnorderedAccess);
        commands->SetPipeline(resources.Pipeline);
        commands->SetDescriptorSet(set);
        commands->Dispatch((static_cast<uint32_t>(inputs.size()) + 63u) / 64u, 1u, 1u);
        commands->BufferBarrier(result, ResourceState::UnorderedAccess, ResourceState::CopySource);
        commands->CopyBuffer(result, readback, outputBytes);
        commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
        commands->End();
        commands->Submit(true);
        resources.Device->WaitIdle();
        const void* mapped = readback->Map(0u, outputBytes);
        if (!Check(mapped != nullptr, "読み戻しを写像できません")) return false;
        std::memcpy(outputs.data(), mapped, outputBytes);
        readback->Unmap();
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            const ProbeOutput& value = outputs[i];
            for (float component : value.AlbedoRoughness)
                if (!Check(std::isfinite(component), "アルベド・粗さが非有限、または未書き込みです")) return false;
            for (float component : value.NormalCliffWeight)
                if (!Check(std::isfinite(component), "法線・崖の重みが非有限、または未書き込みです")) return false;
            float normalError = 0.0f;
            for (uint32_t c = 0; c < 3u; ++c)
                normalError += std::abs(value.NormalCliffWeight[c] - inputs[i].Normal[c]);
            // 法線マップは厳密な (0.5, 0.5, 1)。正規化の丸めだけを許す。
            if (!Check(normalError < 1.0e-4f, "平坦な法線マップで幾何の法線が変わりました")) return false;
        }
        return true;
    }

    MegaGeometry::MegaTerrainSplat MakeSplat(uint32_t layers)
    {
        MegaGeometry::MegaTerrainSplat splat;
        splat.bEnabled = true;
        splat.LayerCount = layers;
        splat.SplatMinXZ[0] = -2.0f;
        splat.SplatMinXZ[1] = -3.0f;
        splat.SplatMaxXZ[0] = 6.0f;
        splat.SplatMaxXZ[1] = 5.0f;
        splat.UvRotationWavelength = 0.0f;
        splat.UvRotationDegrees = 0.0f;
        for (uint32_t i = 0; i < layers; ++i)
        {
            splat.Layers[i].TileMeters = 1.0f;
            splat.Layers[i].Wettability = 1.0f;
        }
        return splat;
    }

    bool MakeTextures(ProbeResources& resources, bool bSlope)
    {
        const uint32_t layers = bSlope ? 2u : 4u;
        const uint32_t size = bSlope ? StripeSize : 1u;
        VariableArray<uint8_t> colors(layers * size * size * 4u, 255u);
        for (uint32_t layer = 0; layer < layers; ++layer)
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                {
                    const uint32_t offset = ((layer * size + y) * size + x) * 4u;
                    if (bSlope)
                    {
                        // 同じ周期の滑らかな縞。青の一定値だけで層を識別し、輝度差の分散には影響させない。
                        const auto stripe = static_cast<uint8_t>(128.0f + 76.0f * std::cos(8.0f * Pi * (x + 0.5f) / size));
                        colors[offset] = stripe;
                        colors[offset + 1u] = stripe;
                        colors[offset + 2u] = layer == 0u ? 26u : 230u;
                    }
                    else
                    {
                        for (uint32_t c = 0; c < 3u; ++c) colors[offset + c] = LayerColors[layer][c];
                    }
                }
        resources.Albedo = UploadArray(*resources.Device, size, layers, Format::R8G8B8A8_UNORM,
            colors.data(), size * size * 4u);
        // half の 0.5 と 1 は厳密に表せる。線形フィルター可能な RGBA16F で中立の法線を作る。
        VariableArray<uint16_t> normal(layers * 4u, 0u);
        VariableArray<uint8_t> arm(layers * 4u, 0u);
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            normal[layer * 4u] = 0x3800u;
            normal[layer * 4u + 1u] = 0x3800u;
            normal[layer * 4u + 2u] = 0x3c00u;
            arm[layer * 4u] = 255u;
            arm[layer * 4u + 1u] = 204u;
            arm[layer * 4u + 3u] = 128u;
        }
        resources.Normal = UploadArray(*resources.Device, 1u, layers, Format::R16G16B16A16_FLOAT, normal.data(), 4u * sizeof(uint16_t));
        resources.Arm = UploadArray(*resources.Device, 1u, layers, Format::R8G8B8A8_UNORM, arm.data(), 4u);
        VariableArray<uint8_t> paint(8u * 8u * 2u * 4u, 0u);
        for (uint32_t y = 0; y < 8u; ++y)
            for (uint32_t x = 0; x < 8u; ++x)
            {
                const uint32_t layer = bSlope ? 0u : (x >= 4u ? 1u : 0u) + (y >= 4u ? 2u : 0u);
                paint[(y * 8u + x) * 4u + layer] = 255u;
            }
        resources.Splat = UploadArray(*resources.Device, 8u, 2u, Format::R8G8B8A8_UNORM, paint.data(), 8u * 8u * 4u);
        return resources.Albedo && resources.Normal && resources.Arm && resources.Splat;
    }

    VariableArray<ProbeInput> MakeGrid()
    {
        VariableArray<ProbeInput> inputs(GridSize * GridSize);
        for (uint32_t y = 0; y < GridSize; ++y)
            for (uint32_t x = 0; x < GridSize; ++x)
            {
                ProbeInput& p = inputs[y * GridSize + x];
                p = {};
                p.PositionDistance[0] = -2.0f + (x + 0.5f) * 8.0f / GridSize;
                p.PositionDistance[2] = -3.0f + (y + 0.5f) * 8.0f / GridSize;
                p.PositionDistance[3] = 10.0f;
                p.WorldDx[0] = 8.0f / GridSize;
                p.WorldDy[2] = 8.0f / GridSize;
                p.Normal[1] = 1.0f;
            }
        return inputs;
    }

    float SrgbToLinear(float value)
    {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    bool CheckQuadrants(ProbeResources& resources)
    {
        if (!MakeTextures(resources, false)) return false;
        const auto inputs = MakeGrid();
        const auto splat = MakeSplat(4u);
        bool passed = true;
        for (uint32_t decode = 0; decode < 2u; ++decode)
        {
            VariableArray<ProbeOutput> outputs;
            if (!ReadShade(resources, splat, decode ? TERRAIN_SPLAT_FLAG_ALBEDO_SRGB_DECODE : 0u, 0.0f, inputs, outputs)) return false;
            for (uint32_t layer = 0; layer < 4u; ++layer)
            {
                double sums[3] = {};
                uint32_t count = 0;
                // 1 象限は 32 画素。地図の境・外縁から 8 画素（地図の 1 テクセル）離れた内側だけを見る。
                for (uint32_t y = 8u + (layer / 2u) * 32u; y < 24u + (layer / 2u) * 32u; ++y)
                    for (uint32_t x = 8u + (layer % 2u) * 32u; x < 24u + (layer % 2u) * 32u; ++x, ++count)
                        for (uint32_t c = 0; c < 3u; ++c) sums[c] += outputs[y * GridSize + x].AlbedoRoughness[c];
                for (uint32_t c = 0; c < 3u; ++c)
                {
                    float expected = LayerColors[layer][c] / 255.0f;
                    if (decode) expected = SrgbToLinear(expected);
                    const double mean = sums[c] / count;
                    const double error = std::abs(mean - expected);
                    std::cout << "quadrant=" << layer << " decode=" << decode << " channel=" << c << " mean=" << mean
                        << " expected=" << expected << " error=" << error << " limit=" << ColorTolerance << '\n';
                    passed = Check(error <= ColorTolerance, "象限の平均アルベドが許容差を超えました") && passed;
                }
            }
        }
        return passed;
    }

    double Luminance(const ProbeOutput& p)
    {
        return 0.2126 * p.AlbedoRoughness[0] + 0.7152 * p.AlbedoRoughness[1] + 0.0722 * p.AlbedoRoughness[2];
    }

    double DifferenceVariance(const VariableArray<ProbeOutput>& outputs, uint32_t offset)
    {
        double sum = 0.0;
        double sumSquares = 0.0;
        // 両端の 64 標本を捨て、隣り合う標本の符号付き輝度差の分散を求める。
        constexpr uint32_t Begin = 64u;
        constexpr uint32_t End = SlopeSamples - 64u - 1u;
        for (uint32_t i = Begin; i < End; ++i)
        {
            const double difference = Luminance(outputs[offset + i + 1u]) - Luminance(outputs[offset + i]);
            sum += difference;
            sumSquares += difference * difference;
        }
        const double mean = sum / (End - Begin);
        return sumSquares / (End - Begin) - mean * mean;
    }

    bool CheckSlope(ProbeResources& resources)
    {
        if (!MakeTextures(resources, true)) return false;
        auto splat = MakeSplat(2u);
        splat.CliffLayer = 1;
        VariableArray<ProbeInput> inputs(2u * SlopeSamples);
        for (uint32_t face = 0; face < 2u; ++face)
            for (uint32_t i = 0; i < SlopeSamples; ++i)
            {
                const float t = (i + 0.5f) * 8.0f / SlopeSamples;
                const float step = 8.0f / SlopeSamples;
                ProbeInput& p = inputs[face * SlopeSamples + i];
                p = {};
                p.PositionDistance[0] = t * (face ? 0.5f : 1.0f);
                p.PositionDistance[1] = face ? -t * Sin60 : 0.0f;
                p.PositionDistance[2] = 0.25f;
                p.PositionDistance[3] = 10.0f;
                p.WorldDx[0] = step * (face ? 0.5f : 1.0f);
                p.WorldDx[1] = face ? -step * Sin60 : 0.0f;
                p.WorldDy[2] = step;
                p.Normal[0] = face ? Sin60 : 0.0f;
                p.Normal[1] = face ? 0.5f : 1.0f;
            }
        VariableArray<ProbeOutput> outputs;
        if (!ReadShade(resources, splat, 0u, 0.0f, inputs, outputs)) return false;
        double maxWeightError = 0.0;
        double maxMarkerError = 0.0;
        for (uint32_t face = 0; face < 2u; ++face)
            for (uint32_t i = 64u; i < SlopeSamples - 64u; ++i)
            {
                const auto& value = outputs[face * SlopeSamples + i];
                const double weightError = std::abs(value.NormalCliffWeight[3] - static_cast<double>(face));
                const double markerError = std::abs(value.AlbedoRoughness[2] - (face ? 230.0 / 255.0 : 26.0 / 255.0));
                if (weightError > maxWeightError) maxWeightError = weightError;
                if (markerError > maxMarkerError) maxMarkerError = markerError;
            }
        const double flatVariance = DifferenceVariance(outputs, 0u);
        const double slopeVariance = DifferenceVariance(outputs, SlopeSamples);
        const double ratio = flatVariance > 0.0 ? slopeVariance / flatVariance : 0.0;
        std::cout << "slope flatVariance=" << flatVariance << " slopeVariance=" << slopeVariance << " ratio=" << ratio
            << " range=[0.45,1.25] maxWeightError=" << maxWeightError << " maxMarkerError=" << maxMarkerError << '\n';
        // 60 度の面の接線に沿う長さを共通にする。真上だけなら周期の密度は半分で分散比は約 0.25。
        // 側面の投影なら密度は sin(60 度) 倍で約 0.75。投影の混合と有限 ROI の位相差を含めて 0.45..1.25 を許す。
        // 縞が消えた比較で偶然合格しないよう、平面の分散にも十分小さい正の下限を置く。
        return Check(flatVariance > 1.0e-5 && ratio >= 0.45 && ratio <= 1.25, "崖の細かさの分散比が範囲外です") &
            Check(maxWeightError <= 1.0e-6, "崖の重みが平面で 0、60 度で 1 になりません") &
            Check(maxMarkerError <= ColorTolerance, "傾斜で選ばれる層の色が違います");
    }

    bool CheckRotation(ProbeResources& resources)
    {
        if (!MakeTextures(resources, true))
        {
            return false;
        }
        constexpr uint32_t Size = 32u;
        constexpr uint32_t Samples = Size * Size;
        constexpr float Step = 1.0f / 64.0f;
        constexpr float Origins[9][2] = {{0.5f, 0.25f}, {300.0f, 0.0f}, {0.0f, 300.0f},
            {-300.0f, 0.0f}, {0.0f, -300.0f}, {212.0f, 212.0f}, {-212.0f, 212.0f},
            {212.0f, -212.0f}, {-212.0f, -212.0f}};
        auto splat = MakeSplat(2u);
        splat.CliffLayer = 1;
        VariableArray<ProbeOutput> reference, rotated;
        for (uint32_t batch = 0; batch < 2u; ++batch)
        {
            const uint32_t count = batch == 0u ? 1u : 8u;
            VariableArray<ProbeInput> inputs(count * Samples);
            for (uint32_t location = 0; location < count; ++location)
            {
                const uint32_t origin = batch == 0u ? 0u : location + 1u;
                for (uint32_t z = 0; z < Size; ++z)
                {
                    for (uint32_t x = 0; x < Size; ++x)
                    {
                        ProbeInput& p = inputs[location * Samples + z * Size + x];
                        p = {};
                        p.PositionDistance[0] = Origins[origin][0] + x * Step;
                        p.PositionDistance[2] = Origins[origin][1] + z * Step;
                        p.PositionDistance[3] = 10.0f;
                        p.WorldDx[0] = Step;
                        p.WorldDy[2] = Step;
                        p.Normal[1] = 1.0f;
                    }
                }
            }
            splat.UvRotationWavelength = batch == 0u ? 0.0f : 23.0f;
            splat.UvRotationDegrees = batch == 0u ? 0.0f : 180.0f;
            if (!ReadShade(resources, splat, 0u, 0.0f, inputs, batch == 0u ? reference : rotated))
            {
                return false;
            }
        }

        // 符号付き輝度差の分散を X・Z それぞれで求めて足す。向きが変わっても縞の細かさを測れる。
        const auto energy = [=](const VariableArray<ProbeOutput>& outputs, uint32_t offset)
        {
            double result = 0.0;
            for (uint32_t axis = 0; axis < 2u; ++axis)
            {
                double sum = 0.0;
                double sumSquares = 0.0;
                for (uint32_t z = 0; z < Size; ++z)
                {
                    for (uint32_t x = 0; x < Size; ++x)
                    {
                        if ((axis == 0u && x + 1u == Size) || (axis == 1u && z + 1u == Size))
                        {
                            continue;
                        }
                        const uint32_t index = offset + z * Size + x;
                        const uint32_t next = index + (axis == 0u ? 1u : Size);
                        const double difference = Luminance(outputs[next]) - Luminance(outputs[index]);
                        sum += difference;
                        sumSquares += difference * difference;
                    }
                }
                constexpr double Count = Size * (Size - 1u);
                const double mean = sum / Count;
                result += sumSquares / Count - mean * mean;
            }
            return result;
        };
        const double referenceEnergy = energy(reference, 0u);
        std::cout << "rotation E_ref=" << referenceEnergy << '\n';
        if (!Check(referenceEnergy > 1.0e-5, "UV の回転の基準となる縞の輝度差が小さすぎます"))
        {
            return false;
        }
        bool passed = true;
        double ratioSum = 0.0;
        for (uint32_t location = 0; location < 8u; ++location)
        {
            const double ratio = energy(rotated, location * Samples) / referenceEnergy;
            ratioSum += ratio;
            std::cout << "rotation x=" << Origins[location + 1u][0] << " z=" << Origins[location + 1u][1]
                << " ratio=" << ratio << " max=1.5" << '\n';
            passed = Check(ratio <= 1.5, "原点から遠い平面で UV の回転が模様を潰しています") && passed;
        }
        const double meanRatio = ratioSum / 8.0;
        std::cout << "rotation meanRatio=" << meanRatio << " min=0.3" << '\n';
        // 二つの縞を混ぜると一部で振幅が落ちるので、消失の下限は全地点の平均に掛ける。
        return Check(meanRatio >= 0.3, "UV の回転で模様が消えました") && passed;
    }

    bool CheckWetness(ProbeResources& resources)
    {
        if (!MakeTextures(resources, false)) return false;
        const auto inputs = MakeGrid();
        auto splat = MakeSplat(4u);
        VariableArray<ProbeOutput> dry, wet, nonWettable;
        if (!ReadShade(resources, splat, 0u, 0.0f, inputs, dry) ||
            !ReadShade(resources, splat, 0u, 1.0f, inputs, wet)) return false;
        for (uint32_t layer = 0; layer < splat.LayerCount; ++layer) splat.Layers[layer].Wettability = 0.0f;
        if (!ReadShade(resources, splat, 0u, 1.0f, inputs, nonWettable)) return false;
        uint32_t identityFailures = 0, darkeningFailures = 0, roughnessFailures = 0;
        double minLuminanceDrop = 1.0, minRoughnessDrop = 1.0;
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            if (std::memcmp(&dry[i], &nonWettable[i], sizeof(ProbeOutput)) != 0) ++identityFailures;
            const double luminanceDrop = Luminance(dry[i]) - Luminance(wet[i]);
            const double roughnessDrop = dry[i].AlbedoRoughness[3] - wet[i].AlbedoRoughness[3];
            if (!(luminanceDrop > 0.0)) ++darkeningFailures;
            // 入力の粗さは 204/255 = 0.8。水膜の上限 0.15 より十分大きいことも出力から確かめる。
            if (!(dry[i].AlbedoRoughness[3] > 0.15f && roughnessDrop > 0.0)) ++roughnessFailures;
            if (luminanceDrop < minLuminanceDrop) minLuminanceDrop = luminanceDrop;
            if (roughnessDrop < minRoughnessDrop) minRoughnessDrop = roughnessDrop;
        }
        std::cout << "wetness identityFailures=" << identityFailures << " darkeningFailures=" << darkeningFailures
            << " roughnessFailures=" << roughnessFailures << " minLuminanceDrop=" << minLuminanceDrop
            << " minRoughnessDrop=" << minRoughnessDrop << '\n';
        // 恒等性は全ビット一致。濡れの効果は式の係数を写さず、厳密な単調減少で縛る。
        return Check(identityFailures == 0u, "濡れ度 0 と濡れやすさ 0 の出力がビット一致しません") &
            Check(darkeningFailures == 0u, "濡れてもアルベドの輝度が下がりません") &
            Check(roughnessFailures == 0u, "濡れても粗さが下がりません");
    }

    // (d) は独立した本文・束縛・資源で、同じ入力に対する 3 層の新旧の全出力を比べる。
    struct ThreeLayerOutput
    {
        float Albedo[4];
        float Material[4];
        float TangentNormal[4];
        float WorldNormal[4];
    };
    static_assert(sizeof(ThreeLayerOutput) == 64u);

    bool BuildThreeLayerSource(String& source, bool bBefore)
    {
        const String probe = R"glsl(#version 450
#define NORVES_MATERIAL_SAMPLING_EXPLICIT_GRADIENT 1
#define NORVES_VT_FEEDBACK_COMPUTE 1
#undef NORVES_VT_FEEDBACK
#include "Common/GBufferEncoding.glsl"
#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#include "Common/VirtualTextureFeedback.glsl"
#include "Common/PbrMaterialTextureSamplingCore.glsl"
#include "Common/TerrainLayerBlend.glsl"
layout(local_size_x = 64) in;
struct ProbeInput { vec4 positionDistance; vec4 dx; vec4 dy; vec4 normal; };
struct ThreeLayerOutput { vec4 albedo; vec4 material; vec4 tangentNormal; vec4 worldNormal; };
layout(std430, set = 0, binding = 0) readonly buffer Inputs { ProbeInput values[]; } inputs;
layout(std430, set = 0, binding = 1) writeonly buffer Outputs { ThreeLayerOutput values[]; } outputs;
layout(set = 0, binding = 2) uniform sampler2D sandAlbedo;
layout(set = 0, binding = 3) uniform sampler2D sandNormal;
layout(set = 0, binding = 4) uniform sampler2D sandOrm;
layout(set = 0, binding = 5) uniform sampler2D dirtAlbedo;
layout(set = 0, binding = 6) uniform sampler2D dirtNormal;
layout(set = 0, binding = 7) uniform sampler2D dirtOrm;
layout(set = 0, binding = 8) uniform sampler2D rockAlbedo;
layout(set = 0, binding = 9) uniform sampler2D rockNormal;
layout(set = 0, binding = 10) uniform sampler2D rockOrm;
layout(set = 0, binding = 11) uniform sampler2D surfaceMask;
layout(set = 0, binding = 12) uniform sampler2D farMap;
layout(std140, set = 0, binding = 13) uniform Params { vec4 terrain[10]; } params;
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(inputs.values.length())) return;
    ProbeInput p = inputs.values[index];
    TerrainSurfaceInput surface;
    surface.WorldPosition = p.positionDistance.xyz;
    surface.CameraDistance = p.positionDistance.w;
    surface.WorldDx = p.dx.xyz;
    surface.WorldDy = p.dy.xyz;
    surface.GeometricNormal = p.normal.xyz;
    TerrainBlendParams terrain;
    terrain.slope = params.terrain[0];
    terrain.height = params.terrain[1];
    terrain.noise = params.terrain[2];
    terrain.layers = params.terrain[3];
    terrain.macro = params.terrain[4];
    terrain.detail = params.terrain[5];
    terrain.mask = params.terrain[6];
    terrain.maskMix = params.terrain[7];
    terrain.farMap = params.terrain[8];
    terrain.farMix = params.terrain[9];
    vec3 worldNormal;
    PbrMaterialTextureSamples result = ShadeTerrainLayers(terrain,
        sandAlbedo, sandNormal, sandOrm, p.normal.w > 0.5,
        dirtAlbedo, dirtNormal, dirtOrm, rockAlbedo, rockNormal, rockOrm,
        surfaceMask, farMap, surface, false, worldNormal);
    outputs.values[index].albedo = result.Albedo;
    outputs.values[index].material = vec4(result.Material, 0.0);
    outputs.values[index].tangentNormal = vec4(result.TangentNormal, 0.0);
    outputs.values[index].worldNormal = vec4(worldNormal, 0.0);
}
)glsl";
        return ExpandShaderText(probe, source, 0u, bBefore);
    }

    DescriptorSetDesc MakeThreeLayerLayout()
    {
        DescriptorSetDesc layout;
        for (uint32_t i = 0; i < 14u; ++i)
        {
            DescriptorBinding binding;
            binding.binding = i;
            binding.type = i == 0u ? ResourceBindType::StructuredBuffer : i == 1u ? ResourceBindType::RWBuffer :
                i == 13u ? ResourceBindType::ConstantBuffer : ResourceBindType::CombinedImageSampler;
            binding.stages = RHI::ShaderStage::Compute;
            layout.bindings.push_back(binding);
        }
        return layout;
    }

    PipelinePtr CreateThreeLayerPipeline(IDevice& device, bool bBefore)
    {
        String source;
        if (!BuildThreeLayerSource(source, bBefore))
        {
            return {};
        }
        ShaderCompilerPtr compiler = device.CreateShaderCompiler();
        if (!Check(compiler != nullptr, "3 層のコンパイラを作れません"))
        {
            return {};
        }
        const auto compiled = compiler->CompileFromSource(source, RHI::ShaderStage::Compute,
            bBefore ? "TerrainLayerBlend.Before" : "TerrainLayerBlend.Current");
        if (!compiled.bSuccess)
        {
            std::cerr << "3 層の計算シェーダーをコンパイルできません: " << compiled.ErrorMessage.c_str() << '\n';
            return {};
        }
        ShaderDesc shaderDesc;
        shaderDesc.stage = RHI::ShaderStage::Compute;
        shaderDesc.byteCode = compiled.ByteCode;
        ShaderPtr shader = device.CreateShader(shaderDesc);
        if (!Check(shader != nullptr, "3 層のシェーダーを作れません"))
        {
            return {};
        }
        ComputePipelineDesc desc;
        desc.computeShader = shader;
        desc.descriptorSetLayouts.push_back(MakeThreeLayerLayout());
        return device.CreateComputePipeline(desc);
    }

    struct ThreeLayerResources
    {
        DevicePtr Device;
        PipelinePtr Current;
        PipelinePtr Before;
        SamplerPtr RepeatSampler;
        SamplerPtr ClampSampler;
        TexturePtr Textures[11];
    };

    TexturePtr UploadThreeLayerTexture(IDevice& device, uint32_t width, uint32_t height,
                                      const VariableArray<uint8_t>& pixels)
    {
        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1u;
        desc.TextureFormat = Format::R8G8B8A8_UNORM;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.DebugName = "TerrainLayerBlendTest.ThreeLayerTexture";
        TexturePtr texture = device.CreateTexture(desc);
        const uint32_t bytes = static_cast<uint32_t>(pixels.size());
        BufferPtr upload = device.CreateBuffer(BufferDesc(bytes, ResourceUsage::TransferSrc, true,
            "TerrainLayerBlendTest.ThreeLayerUpload"));
        CommandListPtr commands = device.CreateCommandList();
        if (!Check(texture && upload && commands, "3 層のテクスチャ資源を作れません"))
        {
            return {};
        }
        upload->Update(pixels.data(), bytes);
        commands->Begin();
        commands->TextureBarrier(texture, ResourceState::Undefined, ResourceState::CopyDest);
        commands->CopyBufferToTexture(upload, texture, width, height);
        commands->TextureBarrier(texture, ResourceState::CopyDest, ResourceState::ShaderResource);
        commands->End();
        commands->Submit(true);
        device.WaitIdle();
        return texture;
    }

    bool MakeThreeLayerTextures(ThreeLayerResources& resources, bool bAtlas)
    {
        for (uint32_t i = 0; i < 11u; ++i)
        {
            const uint32_t height = i == 9u ? 128u : i == 10u ? 64u : 8u;
            const uint32_t width = bAtlas && i < 6u ? height * 2u : height;
            VariableArray<uint8_t> pixels(width * height * 4u);
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    uint8_t* p = pixels.data() + (y * width + x) * 4u;
                    const uint32_t pattern = (x * 17u + y * 29u + i * 37u) % 97u;
                    if (i == 9u)
                    {
                        // 露岩・崖錐・細粒・礫・不足分の砂・混合の 6 区画。補間する境も標本する。
                        const uint8_t masks[6][4] = {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0},
                            {0, 0, 0, 255}, {0, 0, 0, 0}, {48, 64, 80, 32}};
                        std::memcpy(p, masks[(x * 3u / width) + (y >= height / 2u ? 3u : 0u)], 4u);
                    }
                    else if (i == 10u)
                    {
                        p[0] = static_cast<uint8_t>(104u + x / 2u);
                        p[1] = static_cast<uint8_t>(112u + y / 3u);
                        p[2] = static_cast<uint8_t>(24u + pattern);
                        p[3] = static_cast<uint8_t>(32u + pattern / 2u);
                    }
                    else if (i % 3u == 1u)
                    {
                        // 中立でない法線にして、復号とトライプラナーの向きの違いも出力へ残す。
                        p[0] = static_cast<uint8_t>(104u + pattern / 2u);
                        p[1] = static_cast<uint8_t>(108u + (96u - pattern) / 3u);
                        p[2] = 244u;
                        p[3] = 255u;
                    }
                    else
                    {
                        p[0] = static_cast<uint8_t>(72u + pattern + i * 3u);
                        p[1] = static_cast<uint8_t>(56u + (pattern * 3u) % 127u);
                        p[2] = static_cast<uint8_t>(24u + (pattern * 5u + i * 11u) % 151u);
                        p[3] = static_cast<uint8_t>(144u + pattern);
                    }
                }
            }
            resources.Textures[i] = UploadThreeLayerTexture(*resources.Device, width, height, pixels);
            if (!resources.Textures[i])
            {
                return false;
            }
        }
        return true;
    }

    MegaGeometry::MegaTerrainBlend MakeThreeLayerBlend(bool bTwoChannel)
    {
        // WastelandRoutine の荒野と同じ値。地図だけを合成データの外縁へ合わせる。
        MegaGeometry::MegaTerrainBlend blend;
        blend.bEnabled = true;
        blend.Dirt.UVScale = 1.5f / 2.0f;
        blend.Rock.UVScale = 1.5f / 1.8f;
        blend.Dirt.bNormalTwoChannel = bTwoChannel;
        blend.Rock.bNormalTwoChannel = bTwoChannel;
        blend.DirtSlopeDegrees[0] = 24.0f;
        blend.DirtSlopeDegrees[1] = 33.0f;
        blend.RockSlopeDegrees[0] = 37.0f;
        blend.RockSlopeDegrees[1] = 48.0f;
        blend.DirtHeightRange[0] = 12.0f;
        blend.DirtHeightRange[1] = 50.0f;
        blend.DirtHeightBiasDegrees = 10.0f;
        blend.BlendDepth = 0.3f;
        blend.MacroWavelengths[0] = 80.0f;
        blend.MacroWavelengths[1] = 300.0f;
        blend.MacroBrightness = 0.2f;
        blend.MacroHue = 0.05f;
        blend.DetailStartDistance = 10.0f;
        blend.DetailDistanceRatio = 3.0f;
        blend.DetailMaxLevel = 0.0f;
        blend.TextureTileMeters = 1.5f;
        blend.DetailSlopeVariance[0] = 0.393f;
        blend.DetailSlopeVariance[1] = 0.623f;
        blend.DetailSlopeVariance[2] = 0.283f;
        for (uint32_t axis = 0; axis < 2u; ++axis)
        {
            blend.SurfaceMaskMinXZ[axis] = -128.0f;
            blend.SurfaceMaskMaxXZ[axis] = 128.0f;
            blend.FarMapMinXZ[axis] = -512.0f;
            blend.FarMapMaxXZ[axis] = 512.0f;
        }
        blend.SurfaceMaskGravelToDirt = 0.5f;
        blend.SurfaceMaskEdgeFadeMeters = 32.0f;
        blend.SurfaceMaskWarpMeters = 2.0f;
        blend.FarMapTexelMeters = 16.0f;
        return blend;
    }

    VariableArray<ProbeInput> MakeThreeLayerInputs(bool bTwoChannel)
    {
        const float positions[][2] = {{-513.0f, 0.25f}, {-512.0f, 0.25f}, {-511.0f, 0.25f},
            {-129.0f, 0.25f}, {-128.0f, 0.25f}, {-127.0f, 0.25f}, {-112.0f, 0.25f}, {-95.0f, 0.25f},
            {-80.0f, -60.0f}, {0.25f, -60.0f}, {80.0f, -60.0f}, {-80.0f, 60.0f}, {0.25f, 60.0f},
            {80.0f, 60.0f}, {0.25f, 0.25f}, {127.0f, 0.25f}, {128.0f, 0.25f}, {129.0f, 0.25f},
            {511.0f, 0.25f}, {512.0f, 0.25f}, {513.0f, 0.25f}, {0.25f, -129.0f}, {0.25f, 129.0f}};
        const float slopes[] = {0.0f, 23.9f, 24.0f, 28.5f, 33.0f, 37.0f, 42.5f, 48.0f, 60.0f, 89.0f};
        const float heights[] = {-4.0f, 12.0f, 31.0f, 50.0f, 80.0f};
        // 距離と画素幅は別の入力。遠景の 16/6 m・16/2 m の境の両側と途中を含む。
        const float distanceFootprints[][2] = {{0.0f, 0.005f}, {9.99f, 0.02f}, {10.0f, 0.05f},
            {10.01f, 0.1f}, {30.0f, 0.25f}, {100.0f, 1.0f}, {300.0f, 2.65f}, {301.0f, 16.0f / 6.0f},
            {302.0f, 2.68f}, {600.0f, 4.0f}, {1000.0f, 7.99f}, {1001.0f, 8.0f}, {1002.0f, 8.01f}, {3000.0f, 16.0f}};
        VariableArray<ProbeInput> inputs;
        for (uint32_t location = 0; location < sizeof(positions) / sizeof(positions[0]); ++location)
        {
            for (float slope : slopes)
            {
                const float angle = slope * Pi / 180.0f;
                const float azimuth = static_cast<float>(location % 4u) * Pi * 0.5f + 0.31f;
                for (float height : heights)
                {
                    for (const auto& sample : distanceFootprints)
                    {
                        ProbeInput p = {};
                        p.PositionDistance[0] = positions[location][0];
                        p.PositionDistance[1] = height;
                        p.PositionDistance[2] = positions[location][1];
                        p.PositionDistance[3] = sample[0];
                        p.Normal[0] = std::sin(angle) * std::cos(azimuth);
                        p.Normal[1] = std::cos(angle);
                        p.Normal[2] = std::sin(angle) * std::sin(azimuth);
                        p.Normal[3] = bTwoChannel ? 1.0f : 0.0f;
                        // 幾何の接平面の直交する勾配。どちらの長さも指定の画素幅にする。
                        p.WorldDx[0] = std::cos(angle) * std::cos(azimuth) * sample[1];
                        p.WorldDx[1] = -std::sin(angle) * sample[1];
                        p.WorldDx[2] = std::cos(angle) * std::sin(azimuth) * sample[1];
                        p.WorldDy[0] = -std::sin(azimuth) * sample[1];
                        p.WorldDy[2] = std::cos(azimuth) * sample[1];
                        inputs.push_back(p);
                    }
                }
            }
        }
        return inputs;
    }

    bool ReadThreeLayerShade(ThreeLayerResources& resources, const PipelinePtr& pipeline, const float* packed,
                            const VariableArray<ProbeInput>& inputs, VariableArray<ThreeLayerOutput>& outputs)
    {
        const uint32_t inputBytes = static_cast<uint32_t>(inputs.size() * sizeof(ProbeInput));
        const uint32_t outputBytes = static_cast<uint32_t>(inputs.size() * sizeof(ThreeLayerOutput));
        constexpr uint32_t ParamBytes = TERRAIN_BLEND_PARAM_FLOATS * sizeof(float);
        BufferPtr input = resources.Device->CreateBuffer(BufferDesc(inputBytes, ResourceUsage::StorageBuffer |
            ResourceUsage::ShaderRead, true, "TerrainLayerBlendTest.ThreeLayerInput"));
        BufferPtr params = resources.Device->CreateBuffer(BufferDesc(ParamBytes, ResourceUsage::ConstantBuffer,
            true, "TerrainLayerBlendTest.ThreeLayerParams"));
        BufferPtr result = resources.Device->CreateBuffer(BufferDesc(outputBytes, ResourceUsage::StorageBuffer |
            ResourceUsage::TransferSrc, true, "TerrainLayerBlendTest.ThreeLayerResult"));
        BufferPtr readback = resources.Device->CreateBuffer(BufferDesc(outputBytes, ResourceUsage::TransferDst,
            true, "TerrainLayerBlendTest.ThreeLayerReadback"));
        DescriptorSetPtr set = resources.Device->CreateDescriptorSet(MakeThreeLayerLayout());
        CommandListPtr commands = resources.Device->CreateCommandList();
        if (!Check(input && params && result && readback && set && commands, "3 層の評価資源を作れません"))
        {
            return false;
        }
        input->Update(inputs.data(), inputBytes);
        params->Update(packed, ParamBytes);
        outputs.resize(inputs.size());
        for (auto& value : outputs)
        {
            for (float* row : {value.Albedo, value.Material, value.TangentNormal, value.WorldNormal})
            {
                for (uint32_t c = 0; c < 4u; ++c)
                {
                    row[c] = std::numeric_limits<float>::quiet_NaN();
                }
            }
        }
        result->Update(outputs.data(), outputBytes);
        set->BindStorageBuffer(0u, input, 0u, inputBytes);
        set->BindStorageBuffer(1u, result, 0u, outputBytes);
        set->BindConstantBuffer(13u, params, 0u, ParamBytes);
        for (uint32_t i = 0; i < 11u; ++i)
        {
            set->BindTexture(i + 2u, resources.Textures[i]);
            set->BindSampler(i + 2u, i >= 9u ? resources.ClampSampler : resources.RepeatSampler);
        }
        set->Update();
        commands->Begin();
        commands->BufferBarrier(result, ResourceState::Undefined, ResourceState::UnorderedAccess);
        commands->SetPipeline(pipeline);
        commands->SetDescriptorSet(set);
        commands->Dispatch((static_cast<uint32_t>(inputs.size()) + 63u) / 64u, 1u, 1u);
        commands->BufferBarrier(result, ResourceState::UnorderedAccess, ResourceState::CopySource);
        commands->CopyBuffer(result, readback, outputBytes);
        commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
        commands->End();
        commands->Submit(true);
        resources.Device->WaitIdle();
        const void* mapped = readback->Map(0u, outputBytes);
        if (!Check(mapped != nullptr, "3 層の読み戻しを写像できません"))
        {
            return false;
        }
        std::memcpy(outputs.data(), mapped, outputBytes);
        readback->Unmap();
        for (const auto& value : outputs)
        {
            for (const float* row : {value.Albedo, value.Material, value.TangentNormal, value.WorldNormal})
            {
                for (uint32_t c = 0; c < 4u; ++c)
                {
                    if (!Check(std::isfinite(row[c]), "3 層の出力が非有限、または未書き込みです"))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool CheckThreeLayerIdentity(const DevicePtr& device)
    {
        ThreeLayerResources resources;
        resources.Device = device;
        resources.Current = CreateThreeLayerPipeline(*device, false);
        resources.Before = CreateThreeLayerPipeline(*device, true);
        SamplerDesc sampler;
        sampler.filterMin = sampler.filterMag = FilterMode::Linear;
        sampler.filterMip = FilterMode::Point;
        sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressMode::Wrap;
        resources.RepeatSampler = device->CreateSampler(sampler);
        sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressMode::Clamp;
        resources.ClampSampler = device->CreateSampler(sampler);
        if (!Check(resources.Current && resources.Before && resources.RepeatSampler && resources.ClampSampler,
                   "3 層のパイプライン・サンプラーを作れません"))
        {
            return false;
        }
        bool passed = true;
        for (uint32_t atlas = 0; atlas < 2u; ++atlas)
        {
            if (!MakeThreeLayerTextures(resources, atlas != 0u))
            {
                return false;
            }
            for (uint32_t encoding = 0; encoding < 4u; ++encoding)
            {
                const bool bTwoChannel = (encoding & 2u) != 0u;
                const auto blend = MakeThreeLayerBlend(bTwoChannel);
                const auto inputs = MakeThreeLayerInputs(bTwoChannel);
                const uint32_t decode = (encoding & 1u) != 0u ? TERRAIN_BLEND_SAND_ALBEDO_SRGB_DECODE |
                    TERRAIN_BLEND_DIRT_ALBEDO_SRGB_DECODE | TERRAIN_BLEND_ROCK_ALBEDO_SRGB_DECODE : 0u;
                for (uint32_t maps = 0; maps < 4u; ++maps)
                {
                    float packed[TERRAIN_BLEND_PARAM_FLOATS];
                    PackTerrainBlendParams(blend, true, (maps & 1u) != 0u, (maps & 2u) != 0u, decode, packed);
                    // 環境変数による確認用の色分けに依存せず、実際のアルベドを比べる。
                    packed[31] = 0.0f;
                    VariableArray<ThreeLayerOutput> current, before;
                    if (!ReadThreeLayerShade(resources, resources.Current, packed, inputs, current) ||
                        !ReadThreeLayerShade(resources, resources.Before, packed, inputs, before))
                    {
                        return false;
                    }
                    uint32_t mismatches = 0u;
                    for (size_t i = 0; i < inputs.size(); ++i)
                    {
                        if (std::memcmp(&current[i], &before[i], sizeof(ThreeLayerOutput)) != 0)
                        {
                            if (mismatches == 0u)
                            {
                                const auto& p = inputs[i];
                                std::cerr << "threeLayer firstMismatch=" << i << " position=(" << p.PositionDistance[0]
                                    << ',' << p.PositionDistance[1] << ',' << p.PositionDistance[2] << ") distance="
                                    << p.PositionDistance[3] << '\n';
                            }
                            ++mismatches;
                        }
                    }
                    std::cout << "threeLayer atlas=" << atlas << " encoding=" << encoding << " maps=" << maps
                        << " samples=" << inputs.size() << " bitMismatches=" << mismatches << '\n';
                    passed = Check(mismatches == 0u, "3 層のアルベド・材質・法線が変更前とビット一致しません") && passed;
                }
            }
        }
        return passed;
    }

    class ValidationCapture
    {
    public:
        ValidationCapture() { Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~ValidationCapture() { Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t Hits() const { return Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    int RunTest()
    {
        char* forceSkip = nullptr;
        size_t length = 0;
        _dupenv_s(&forceSkip, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP");
        const bool bForceSkip = forceSkip != nullptr && std::strcmp(forceSkip, "1") == 0;
        free(forceSkip);
        if (bForceSkip)
        {
            std::cout << TestName << ": 環境変数の指定で省略します\n";
            return 125;
        }
        ValidationCapture capture;
        RHIDeviceDesc desc;
        desc.Api = GraphicsAPI::Vulkan;
        desc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(desc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            std::cout << TestName << ": Vulkan デバイスを利用できないので省略します\n";
            return 125;
        }
        bool passed = false;
        {
            ProbeResources resources;
            resources.Device = device;
            resources.Pipeline = CreateProbePipeline(*device);
            SamplerDesc sampler;
            sampler.filterMin = FilterMode::Linear;
            sampler.filterMag = FilterMode::Linear;
            sampler.filterMip = FilterMode::Point;
            sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressMode::Wrap;
            resources.RepeatSampler = device->CreateSampler(sampler);
            sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressMode::Clamp;
            resources.ClampSampler = device->CreateSampler(sampler);
            if (!Check(resources.Pipeline && resources.RepeatSampler && resources.ClampSampler,
                       "計算パイプライン・サンプラーを作れません")) return 1;
            const bool quadrants = CheckQuadrants(resources);
            const bool slope = CheckSlope(resources);
            const bool wetness = CheckWetness(resources);
            const bool rotation = CheckRotation(resources);
            const bool threeLayer = CheckThreeLayerIdentity(device);
            std::cout << "quadrants=" << quadrants << " slope=" << slope << " wetness=" << wetness
                << " rotation=" << rotation << " threeLayer=" << threeLayer << '\n';
            passed = quadrants && slope && wetness && rotation && threeLayer;
            device->WaitIdle();
        }
        const uint32_t errors = capture.Hits();
        std::cout << "VUID_COUNT=" << errors << '\n';
        passed = Check(errors == 0u, "Vulkan の検証エラーを検出しました") && passed;
        std::cout << (passed ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return passed ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    try
    {
        // GPU を作らず、実行時と同じ組み立てを glslc で検証できるようにする。
        if (argc == 3 && std::strcmp(argv[1], "--dump-shader") == 0)
        {
            String source;
            if (!BuildProbeSource(source)) return 1;
            NorvesLib::FileStream::FileStream file;
            if (!file.Open(argv[2], NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write)) return 1;
            return file.Write(source.c_str(), source.size()) == source.size() ? 0 : 1;
        }
        if (argc == 3 && (std::strcmp(argv[1], "--dump-shader-current") == 0 ||
                          std::strcmp(argv[1], "--dump-shader-before") == 0))
        {
            String source;
            if (!BuildThreeLayerSource(source, std::strcmp(argv[1], "--dump-shader-before") == 0))
            {
                return 1;
            }
            NorvesLib::FileStream::FileStream file;
            if (!file.Open(argv[2], NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write))
            {
                return 1;
            }
            return file.Write(source.c_str(), source.size()) == source.size() ? 0 : 1;
        }
        return RunTest();
    }
    catch (const std::exception& error)
    {
        std::cerr << TestName << " で例外が出ました: " << error.what() << '\n';
        return 1;
    }
}
