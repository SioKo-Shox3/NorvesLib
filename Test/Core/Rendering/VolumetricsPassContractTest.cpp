#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    std::string ReadRepositoryFile(const std::string& relativePath)
    {
        const std::string path = std::string(NORVES_SOURCE_DIR) + "/" + relativePath;
        std::ifstream file(path, std::ios::binary);
        assert(file.is_open());
        return std::string(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
    }

    std::size_t RequirePosition(const std::string& source, const std::string& text)
    {
        const std::size_t position = source.find(text);
        assert(position != std::string::npos);
        return position;
    }

    void RequireInOrder(const std::string& source,
                        const std::string& first,
                        const std::string& second,
                        const std::string& third)
    {
        const std::size_t firstPosition = RequirePosition(source, first);
        const std::size_t secondPosition = RequirePosition(source, second);
        const std::size_t thirdPosition = RequirePosition(source, third);
        assert(firstPosition < secondPosition);
        assert(secondPosition < thirdPosition);
    }
} // namespace

int main()
{
    std::cout << "VolumetricsPassContractTest start\n";

    const std::string header = ReadRepositoryFile("Library/Core/Public/Rendering/VolumetricsPass.h");
    const std::string implementation =
        ReadRepositoryFile("Library/Core/Private/Rendering/VolumetricsPass.cpp");
    const std::string shader = ReadRepositoryFile("Assets/Shaders/volumetrics.frag");
    const std::string sceneView = ReadRepositoryFile("Library/Core/Private/Rendering/SceneView.cpp");
    const std::string resourceNames =
        ReadRepositoryFile("Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h");
    const std::string renderContext =
        ReadRepositoryFile("Library/Core/Public/Rendering/ViewRenderContext.h");
    const std::string coordinator =
        ReadRepositoryFile("Library/Core/Private/Rendering/RenderingCoordinator.cpp");
    const std::string coordinatorHeader =
        ReadRepositoryFile("Library/Core/Public/Rendering/RenderingCoordinator.h");
    const std::string renderWorldHeader =
        ReadRepositoryFile("Library/Core/Public/Rendering/RenderWorld.h");
    const std::string renderWorldImplementation =
        ReadRepositoryFile("Library/Core/Private/Rendering/RenderWorld.cpp");
    const std::string sceneProxy =
        ReadRepositoryFile("Library/Core/Public/Rendering/SceneProxy.h");
    const std::string lightingImplementation =
        ReadRepositoryFile("Library/Core/Private/Rendering/LightingPass.cpp");

    assert(header.find("class VolumetricsPass : public IViewPass, public IRenderGraphPass") !=
           std::string::npos);
    assert(header.find("GetName() const override { return \"VolumetricsPass\"; }") !=
           std::string::npos);

    const std::size_t disabledGuard =
        RequirePosition(implementation, "if (!m_FogParameters.bEnabled)");
    const std::size_t depthRead =
        RequirePosition(implementation, "TryReadTexture(RenderGraphResourceNames::SceneDepth");
    const std::size_t skyRead =
        RequirePosition(implementation,
                        "TryReadTexture(RenderGraphResourceNames::SkyAtmosphereRadiance");
    const std::size_t sceneColorAttachment =
        RequirePosition(implementation,
                        "TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor");
    assert(disabledGuard < depthRead);
    assert(depthRead < skyRead);
    assert(skyRead < sceneColorAttachment);
    assert(implementation.find("RHI::AttachmentLoadOp::Load") != std::string::npos);
    assert(implementation.find("RHI::AttachmentStoreOp::Store") != std::string::npos);
    assert(implementation.find("builder.WriteTexture(") == std::string::npos);
    assert(implementation.find("CreateTexture(") == std::string::npos);
    assert(implementation.find("RHI/Vulkan/") == std::string::npos);
    assert(implementation.find("EnqueueEmptyNativePass(context, sceneColorTexture)") !=
           std::string::npos);
    assert(implementation.find("context.EnqueueFullscreenPass(m_RenderPass,") !=
           std::string::npos);
    assert(implementation.find("context.EnqueueTextureBarrier(sceneColorTexture,") !=
           std::string::npos);

    assert(resourceNames.find("SkyAtmosphere.Radiance") != std::string::npos);
    assert(resourceNames.find("Scene.Color") != std::string::npos);
    assert(resourceNames.find("Scene.Depth") != std::string::npos);
    assert(implementation.find("sceneDepthBinding.binding = 0u") != std::string::npos);
    assert(implementation.find("skyRadianceBinding.binding = 1u") != std::string::npos);
    assert(implementation.find("paramsBinding.binding = 2u") != std::string::npos);
    assert(implementation.find("shadowMapBinding.binding = 3u") != std::string::npos);
    assert(implementation.find("sceneDepthBinding.type = RHI::ResourceBindType::CombinedImageSampler") !=
           std::string::npos);
    assert(implementation.find("skyRadianceBinding.type = RHI::ResourceBindType::CombinedImageSampler") !=
           std::string::npos);
    assert(implementation.find("paramsBinding.type = RHI::ResourceBindType::ConstantBuffer") !=
           std::string::npos);
    assert(shader.find("layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture") !=
           std::string::npos);
    assert(shader.find("layout(set = 0, binding = 1) uniform sampler2D skyAtmosphereRadiance") !=
           std::string::npos);
    assert(shader.find("layout(std140, set = 0, binding = 2) uniform VolumetricsParams") !=
           std::string::npos);
    assert(shader.find("layout(set = 0, binding = 3) uniform sampler2DArray cascadedShadowMap") !=
           std::string::npos);
    assert(shader.find("sceneColorTexture") == std::string::npos);

    assert(shader.find("float originExponent = -falloff * (originHeight - baseHeight);") !=
           std::string::npos);
    assert(shader.find("float logOpticalDepth = log(density) + originExponent + logIntegral;") !=
           std::string::npos);
    assert(shader.find("return exp(-opticalDepth);") != std::string::npos);
    assert(shader.find("depth >= 0.999999") != std::string::npos);
    assert(shader.find("texture(skyAtmosphereRadiance,") != std::string::npos);
    assert(shader.find("EquirectangularUV(rayDirection)") != std::string::npos);
    assert(shader.find("float opacity = clamp(1.0 - transmittance, 0.0, 1.0);") !=
           std::string::npos);
    assert(shader.find("outColor = vec4(fogRadiance * opacity, opacity);") !=
           std::string::npos ||
           shader.find("fogRadiance * opacity + singleScatteringRadiance") != std::string::npos);
    assert(implementation.find("static_assert(sizeof(GPUVolumetricsParams) == 704u)") !=
           std::string::npos);
    assert(implementation.find("TryReadTexture(RenderGraphResourceNames::ShadowMap") !=
           std::string::npos);
    assert(implementation.find("GetArraySize() != PhysicalLightingShadowCascadeCount") !=
           std::string::npos);
    assert(implementation.find("RHI::Format::D32_FLOAT") != std::string::npos);
    assert(implementation.find("HasCompleteCascadedShadow()") != std::string::npos);
    assert(implementation.find("light.LightId != context.PhysicalLighting.CascadedShadow.LightId") !=
           std::string::npos);
    assert(implementation.find("params.CascadeSplitDistances") != std::string::npos);
    assert(renderContext.find("bool HasCompleteCascadedShadow() const") != std::string::npos);
    assert(coordinator.find("viewContext.SnapshotLightProxies = &packet->Scene.LightProxies;") !=
           std::string::npos);
    assert(shader.find("float stepLength = rayDistance / 24.0;") != std::string::npos);
    assert(shader.find("for (int stepIndex = 0; stepIndex < 24; ++stepIndex)") !=
           std::string::npos);
    assert(shader.find("CalculateVolumetricShadowVisibility(samplePosition)") !=
           std::string::npos);
    assert(shader.find("textureLod(cascadedShadowMap,") != std::string::npos);
    assert(shader.find("directionalLightDirectionAndAnisotropy.xyz,") != std::string::npos);
    assert(renderWorldHeader.find(
               "SetVolumetricFogParameters(const VolumetricFogParameters& parameters)") !=
           std::string::npos);
    assert(renderWorldImplementation.find(
               "m_RenderingCoordinator.SetVolumetricFogParameters(parameters);") !=
           std::string::npos);
    assert(coordinatorHeader.find("VolumetricFogParameters m_VolumetricFog;") !=
           std::string::npos);
    assert(coordinator.find(
               "m_CurrentPacket->Scene.SetVolumetricFogParameters(m_VolumetricFog);") !=
           std::string::npos);
    assert(coordinator.find(
               "m_VolumetricFog = SanitizeVolumetricFogParameters(parameters);") !=
           std::string::npos);
    assert(sceneProxy.find("VolumetricFog = SanitizeVolumetricFogParameters(parameters);") !=
           std::string::npos);
    assert(renderContext.find("ShadowMapFallbackTexture") != std::string::npos);
    assert(renderContext.find("ShadowMapFallbackSampler") != std::string::npos);
    assert(lightingImplementation.find(
               "PublishShadowMapFallback(m_DefaultShadowMapArrayTexture,") !=
           std::string::npos);
    assert(implementation.find("IsValidShadowMapArrayFallbackTexture") !=
           std::string::npos);
    assert(implementation.find("shadowMapTextureForSampling") != std::string::npos);
    assert(implementation.find("bActualShadowMapAvailable &&") != std::string::npos);
    assert(implementation.find("blendAttachment.srcColorBlendFactor = RHI::BlendFactor::One") !=
           std::string::npos);
    assert(implementation.find("blendAttachment.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha") !=
           std::string::npos);
    assert(implementation.find("blendAttachment.colorWriteMask = RHI::ColorWriteMask::R |") !=
           std::string::npos);

    RequireInOrder(sceneView,
                   "AddPass(std::move(lightingPass));",
                   "AddPass(MakeUnique<VolumetricsPass>());",
                   "AddPass(std::move(transparentForwardPass));");

    std::cout << "VolumetricsPassContractTest passed\n";
    return 0;
}
