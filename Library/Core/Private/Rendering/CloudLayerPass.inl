// 空に雲の層を描くパス（CloudLayerPass）の実装。Coreの登録済みのSceneView.cppから取り込む。
#include "Rendering/CloudLayerPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>

namespace NorvesLib::Core::Rendering
{
    namespace CloudLayerPassDetail
    {
        struct alignas(16) GPUCloudLayerParams
        {
            float InverseViewProjection[16];
            // xyz=カメラの位置（m）、w=事前露出
            float CameraPositionAndPreExposure[4];
            // xyz=太陽へ向かう向き、w=時間方向の雑音の位相（フレーム番号）
            float SunDirectionAndFrameIndex[4];
            // rgb=太陽の地表の照度（lux）、w=未使用
            float SunIlluminance[4];
            // x=惑星の半径、y=雲の底の高度、z=雲の頂の高度（m）、w=空を覆う割合
            float LayerAltitudesAndCoverage[4];
            // x=密度 1 の消散係数（1/m）、y=基本の形の 1/周期、z=細部の 1/周期、w=天気の模様の 1/周期（1/m）
            float ExtinctionAndInverseScales[4];
            // xy=基本の形の風のずれ（X・Z、m）、zw=天気の模様の風のずれ（m）
            float ShapeAndWeatherOffset[4];
            // xyz=細部の風のずれ（m）、w=未使用
            float DetailOffset[4];
            // rgb=地面のアルベド、w=未使用
            float GroundAlbedo[4];
        };
        static_assert(sizeof(GPUCloudLayerParams) == 192u);

        // 基本の形と細部の 3D ノイズの一辺の texel の数
        constexpr uint32_t ShapeNoiseSize = 64u;
        constexpr uint32_t DetailNoiseSize = 32u;
        // 細部の周期・天気の模様の周期の、基本の形の周期に対する比。互いに割り切れない比にして、重ねた模様の繰り返しを長くする
        constexpr float DetailScaleRatio = 1.0f / 8.3f;
        constexpr float WeatherScaleRatio = 3.7f;
        // 細部は風より少し速く流し、形が崩れながら進むように見せる
        constexpr float DetailWindRatio = 1.6f;

        RHI::DescriptorSetDesc CreateCloudLayerDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[5] = {
                RHI::ResourceBindType::CombinedImageSampler, // 0: シーンの深度
                RHI::ResourceBindType::CombinedImageSampler, // 1: 空の放射輝度 LUT
                RHI::ResourceBindType::ConstantBuffer,       // 2: 定数
                RHI::ResourceBindType::CombinedImageSampler, // 3: 基本の形の 3D ノイズ
                RHI::ResourceBindType::CombinedImageSampler, // 4: 細部の 3D ノイズ
            };
            for (uint32_t bindingIndex = 0u; bindingIndex < 5u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Pixel;
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        float SafeCloudPreExposure(const CameraProxy* camera)
        {
            if (!camera || !std::isfinite(camera->PreExposure) || camera->PreExposure <= 0.0f)
            {
                return 1.0f;
            }
            return std::clamp(camera->PreExposure, 1.0e-6f, 1.0e6f);
        }

        uint32_t HashCell(int32_t x, int32_t y, int32_t z, uint32_t seed)
        {
            uint32_t hash = seed;
            hash ^= static_cast<uint32_t>(x) * 0x8da6b343u;
            hash ^= static_cast<uint32_t>(y) * 0xd8163841u;
            hash ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
            hash = (hash ^ (hash >> 16u)) * 0x7feb352du;
            hash = (hash ^ (hash >> 15u)) * 0x846ca68bu;
            return hash ^ (hash >> 16u);
        }

        float HashToUnit(uint32_t hash)
        {
            return static_cast<float>(hash & 0xFFFFFFu) / 16777216.0f;
        }

        int32_t WrapCell(int32_t value, int32_t period)
        {
            const int32_t wrapped = value % period;
            return wrapped < 0 ? wrapped + period : wrapped;
        }

        float Fade(float t)
        {
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }

        float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        float Remap(float value, float low, float high, float newLow, float newHigh)
        {
            const float range = high - low;
            if (std::abs(range) < 1.0e-6f)
            {
                return newLow;
            }
            return newLow + (value - low) / range * (newHigh - newLow);
        }

        // 格子点の勾配（立方体の辺の中点の 12 方向）と、そこからの差の内積
        float GradientDot(uint32_t hash, float dx, float dy, float dz)
        {
            switch (hash % 12u)
            {
            case 0u: return dx + dy;
            case 1u: return -dx + dy;
            case 2u: return dx - dy;
            case 3u: return -dx - dy;
            case 4u: return dx + dz;
            case 5u: return -dx + dz;
            case 6u: return dx - dz;
            case 7u: return -dx - dz;
            case 8u: return dy + dz;
            case 9u: return -dy + dz;
            case 10u: return dy - dz;
            default: return -dy - dz;
            }
        }

        // 1 周期（0〜1）に period 個の格子を置き、端で繋がる勾配ノイズ（おおよそ -1〜1）
        float TileablePerlin(float x, float y, float z, int32_t period, uint32_t seed)
        {
            const float px = x * static_cast<float>(period);
            const float py = y * static_cast<float>(period);
            const float pz = z * static_cast<float>(period);
            const int32_t ix = static_cast<int32_t>(std::floor(px));
            const int32_t iy = static_cast<int32_t>(std::floor(py));
            const int32_t iz = static_cast<int32_t>(std::floor(pz));
            const float fx = px - static_cast<float>(ix);
            const float fy = py - static_cast<float>(iy);
            const float fz = pz - static_cast<float>(iz);

            float corners[8];
            for (uint32_t corner = 0u; corner < 8u; ++corner)
            {
                const int32_t cx = (corner & 1u) ? 1 : 0;
                const int32_t cy = (corner & 2u) ? 1 : 0;
                const int32_t cz = (corner & 4u) ? 1 : 0;
                const uint32_t hash = HashCell(WrapCell(ix + cx, period),
                                               WrapCell(iy + cy, period),
                                               WrapCell(iz + cz, period),
                                               seed);
                corners[corner] = GradientDot(hash,
                                              fx - static_cast<float>(cx),
                                              fy - static_cast<float>(cy),
                                              fz - static_cast<float>(cz));
            }
            const float ux = Fade(fx);
            const float uy = Fade(fy);
            const float uz = Fade(fz);
            const float x00 = Lerp(corners[0], corners[1], ux);
            const float x10 = Lerp(corners[2], corners[3], ux);
            const float x01 = Lerp(corners[4], corners[5], ux);
            const float x11 = Lerp(corners[6], corners[7], ux);
            return Lerp(Lerp(x00, x10, uy), Lerp(x01, x11, uy), uz);
        }

        // 1 周期に cells 個の格子を置き、各格子に 1 つの特徴点を置いた端で繋がる Worley。
        // 最も近い特徴点までの距離を格子の大きさで割り、0〜1 に丸めて返す
        float TileableWorley(float x, float y, float z, int32_t cells, uint32_t seed)
        {
            const float px = x * static_cast<float>(cells);
            const float py = y * static_cast<float>(cells);
            const float pz = z * static_cast<float>(cells);
            const int32_t ix = static_cast<int32_t>(std::floor(px));
            const int32_t iy = static_cast<int32_t>(std::floor(py));
            const int32_t iz = static_cast<int32_t>(std::floor(pz));
            float nearestSquared = 3.0f;
            for (int32_t oz = -1; oz <= 1; ++oz)
            {
                for (int32_t oy = -1; oy <= 1; ++oy)
                {
                    for (int32_t ox = -1; ox <= 1; ++ox)
                    {
                        const int32_t cx = ix + ox;
                        const int32_t cy = iy + oy;
                        const int32_t cz = iz + oz;
                        const uint32_t hash = HashCell(WrapCell(cx, cells),
                                                       WrapCell(cy, cells),
                                                       WrapCell(cz, cells),
                                                       seed);
                        const float featureX = static_cast<float>(cx) + HashToUnit(hash);
                        const float featureY = static_cast<float>(cy) + HashToUnit(hash * 0x9e3779b9u + 1u);
                        const float featureZ = static_cast<float>(cz) + HashToUnit(hash * 0x85ebca6bu + 2u);
                        const float dx = featureX - px;
                        const float dy = featureY - py;
                        const float dz = featureZ - pz;
                        nearestSquared = std::min(nearestSquared, dx * dx + dy * dy + dz * dz);
                    }
                }
            }
            return std::min(std::sqrt(nearestSquared), 1.0f);
        }

        // 反転した Worley の fbm（特徴点の近くほど 1。周波数 cells・2 倍・4 倍を 0.625・0.25・0.125 で重ねる）
        float InvertedWorleyFbm(const float (&worley)[4], uint32_t firstOctave)
        {
            const float fbm = worley[firstOctave] * 0.625f +
                              worley[std::min(firstOctave + 1u, 3u)] * 0.25f +
                              worley[std::min(firstOctave + 2u, 3u)] * 0.125f;
            return std::clamp(1.0f - fbm, 0.0f, 1.0f);
        }

        uint8_t ToUnorm8(float value)
        {
            return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        // 基本の形: R=Perlin-Worley（勾配ノイズの fbm を Worley で膨らませた、もくもくした塊）、
        // G・B・A=格子 4・8・16 から始まる反転 Worley の fbm
        void GenerateShapeNoise(Container::VariableArray<uint8_t>& outTexels)
        {
            const uint32_t size = ShapeNoiseSize;
            outTexels.resize(static_cast<size_t>(size) * size * size * 4u);
            const int32_t worleyCells[4] = {4, 8, 16, 32};
            for (uint32_t z = 0u; z < size; ++z)
            {
                for (uint32_t y = 0u; y < size; ++y)
                {
                    for (uint32_t x = 0u; x < size; ++x)
                    {
                        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                        const float w = (static_cast<float>(z) + 0.5f) / static_cast<float>(size);

                        float perlin = 0.0f;
                        float amplitude = 1.0f;
                        float amplitudeSum = 0.0f;
                        for (int32_t octave = 0; octave < 4; ++octave)
                        {
                            perlin += amplitude * TileablePerlin(u, v, w, 4 << octave, 0x1234u + static_cast<uint32_t>(octave));
                            amplitudeSum += amplitude;
                            amplitude *= 0.5f;
                        }
                        const float perlin01 = std::clamp(perlin / amplitudeSum * 0.5f + 0.5f, 0.0f, 1.0f);

                        float worley[4];
                        for (uint32_t octave = 0u; octave < 4u; ++octave)
                        {
                            worley[octave] = TileableWorley(u, v, w, worleyCells[octave], 0x5678u + octave);
                        }
                        const float worleyLow = InvertedWorleyFbm(worley, 0u);
                        const float perlinWorley = std::clamp(Remap(perlin01, worleyLow - 1.0f, 1.0f, 0.0f, 1.0f),
                                                              0.0f,
                                                              1.0f);

                        const size_t offset = ((static_cast<size_t>(z) * size + y) * size + x) * 4u;
                        outTexels[offset + 0u] = ToUnorm8(perlinWorley);
                        outTexels[offset + 1u] = ToUnorm8(worleyLow);
                        outTexels[offset + 2u] = ToUnorm8(InvertedWorleyFbm(worley, 1u));
                        outTexels[offset + 3u] = ToUnorm8(InvertedWorleyFbm(worley, 2u));
                    }
                }
            }
        }

        // 細部: R・G・B=格子 2・4・8 から始まる反転 Worley の fbm（雲の縁を削る）
        void GenerateDetailNoise(Container::VariableArray<uint8_t>& outTexels)
        {
            const uint32_t size = DetailNoiseSize;
            outTexels.resize(static_cast<size_t>(size) * size * size * 4u);
            const int32_t worleyCells[4] = {2, 4, 8, 16};
            for (uint32_t z = 0u; z < size; ++z)
            {
                for (uint32_t y = 0u; y < size; ++y)
                {
                    for (uint32_t x = 0u; x < size; ++x)
                    {
                        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                        const float w = (static_cast<float>(z) + 0.5f) / static_cast<float>(size);
                        float worley[4];
                        for (uint32_t octave = 0u; octave < 4u; ++octave)
                        {
                            worley[octave] = TileableWorley(u, v, w, worleyCells[octave], 0x9abcu + octave);
                        }
                        const size_t offset = ((static_cast<size_t>(z) * size + y) * size + x) * 4u;
                        outTexels[offset + 0u] = ToUnorm8(InvertedWorleyFbm(worley, 0u));
                        outTexels[offset + 1u] = ToUnorm8(InvertedWorleyFbm(worley, 1u));
                        outTexels[offset + 2u] = ToUnorm8(InvertedWorleyFbm(worley, 2u));
                        outTexels[offset + 3u] = 255u;
                    }
                }
            }
        }

        RHI::TexturePtr CreateNoiseTexture(RHI::IDevice* device,
                                           uint32_t size,
                                           const Container::VariableArray<uint8_t>& texels,
                                           const char* debugName)
        {
            RHI::TextureDesc desc;
            desc.Width = size;
            desc.Height = size;
            desc.Depth = size;
            desc.Dimension = RHI::TextureDimension::Texture3D;
            desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            desc.DebugName = debugName;
            const uint32_t rowPitch = size * 4u;
            const uint32_t slicePitch = rowPitch * size;
            RHI::TexturePtr texture;
            try
            {
                texture = device->CreateTexture(desc);
                if (!texture)
                {
                    return nullptr;
                }
                texture->Update(texels.data(), rowPitch, slicePitch);
            }
            catch (const std::exception& exception)
            {
                NORVES_LOG_ERROR("CloudLayerPass", "雲の 3D ノイズの作成または転送に失敗しました: %s", exception.what());
                return nullptr;
            }
            return texture;
        }

        // 風で流したずれを、そのノイズの周期で割った余りにする（周期で繰り返すので見た目は同じで、値が大きくならない）
        float WrapWindOffset(double velocity, double timeSeconds, double period)
        {
            const double offset = std::fmod(velocity * timeSeconds, period);
            return static_cast<float>(std::isfinite(offset) ? offset : 0.0);
        }
    } // namespace CloudLayerPassDetail

    CloudLayerPass::~CloudLayerPass()
    {
        Shutdown();
    }

    bool CloudLayerPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "Device か ShaderManager がありません");
            return false;
        }

        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("cloud_layer.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "雲のシェーダーを読み込めませんでした");
            Shutdown();
            return false;
        }

        RHI::SamplerDesc depthSamplerDesc;
        depthSamplerDesc.filterMin = RHI::FilterMode::Point;
        depthSamplerDesc.filterMag = RHI::FilterMode::Point;
        depthSamplerDesc.filterMip = RHI::FilterMode::Point;
        depthSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        depthSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        depthSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_SceneDepthSampler = m_Device->CreateSampler(depthSamplerDesc);

        RHI::SamplerDesc skyRadianceSamplerDesc;
        skyRadianceSamplerDesc.filterMin = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.filterMag = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.filterMip = RHI::FilterMode::Linear;
        skyRadianceSamplerDesc.addressU = RHI::TextureAddressMode::Wrap;
        skyRadianceSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        skyRadianceSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_SkyRadianceSampler = m_Device->CreateSampler(skyRadianceSamplerDesc);

        RHI::SamplerDesc noiseSamplerDesc = skyRadianceSamplerDesc;
        noiseSamplerDesc.addressV = RHI::TextureAddressMode::Wrap;
        noiseSamplerDesc.addressW = RHI::TextureAddressMode::Wrap;
        m_NoiseSampler = m_Device->CreateSampler(noiseSamplerDesc);

        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(
            CloudLayerPassDetail::CreateCloudLayerDescriptorSetDesc());
        if (!m_SceneDepthSampler || !m_SkyRadianceSampler || !m_NoiseSampler || !m_DescriptorSetLayout)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "サンプラか記述子セットのレイアウトを作れませんでした");
            Shutdown();
            return false;
        }

        m_bInitialized = true;
        return true;
    }

    void CloudLayerPass::Shutdown()
    {
        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();
        m_UnownedDescriptorSets.clear();
        m_UnownedBuffers.clear();
        m_DescriptorSetLayout.reset();
        m_ShapeNoiseTexture.reset();
        m_DetailNoiseTexture.reset();
        m_bNoiseGenerationFailed = false;
        m_NoiseSampler.reset();
        m_SkyRadianceSampler.reset();
        m_SceneDepthSampler.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_Device = nullptr;
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_bSunIlluminanceValid = false;
        m_CurrentWidth = 0u;
        m_CurrentHeight = 0u;
        m_CurrentFormat = RHI::Format::UNKNOWN;
        m_FramebufferSceneColorTexture = nullptr;
        m_bInitialized = false;
    }

    void CloudLayerPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void CloudLayerPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    bool CloudLayerPass::EnsureNoiseTextures()
    {
        if (m_ShapeNoiseTexture && m_DetailNoiseTexture)
        {
            return true;
        }
        if (m_bNoiseGenerationFailed || !m_Device)
        {
            return false;
        }

        const auto startTime = std::chrono::steady_clock::now();
        Container::VariableArray<uint8_t> shapeTexels;
        Container::VariableArray<uint8_t> detailTexels;
        CloudLayerPassDetail::GenerateShapeNoise(shapeTexels);
        CloudLayerPassDetail::GenerateDetailNoise(detailTexels);
        m_ShapeNoiseTexture = CloudLayerPassDetail::CreateNoiseTexture(m_Device,
                                                                       CloudLayerPassDetail::ShapeNoiseSize,
                                                                       shapeTexels,
                                                                       "CloudLayer.ShapeNoise");
        m_DetailNoiseTexture = CloudLayerPassDetail::CreateNoiseTexture(m_Device,
                                                                        CloudLayerPassDetail::DetailNoiseSize,
                                                                        detailTexels,
                                                                        "CloudLayer.DetailNoise");
        if (!m_ShapeNoiseTexture || !m_DetailNoiseTexture)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "雲の 3D ノイズを作れないため、雲を描きません");
            m_ShapeNoiseTexture.reset();
            m_DetailNoiseTexture.reset();
            m_bNoiseGenerationFailed = true;
            return false;
        }
        const double elapsedMs = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - startTime)
                                     .count();
        NORVES_LOG_INFO("CloudLayerPass",
                        "CLOUD_NOISE shape=%u detail=%u generate_ms=%.1f",
                        CloudLayerPassDetail::ShapeNoiseSize,
                        CloudLayerPassDetail::DetailNoiseSize,
                        elapsedMs);
        return true;
    }

    void CloudLayerPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        const SceneProxy* snapshotScene = context ? context->SnapshotScene : nullptr;
        m_SkyParameters = snapshotScene
                              ? SanitizeSkyAtmosphereParameters(snapshotScene->SkyAtmosphere)
                              : SkyAtmosphereParameters{};
        m_CloudLayer = snapshotScene
                           ? SanitizeCloudLayerParameters(snapshotScene->CloudLayer)
                           : CloudLayerParameters{};
        // 雲の光は空の LUT と空の太陽から求めるので、空が無効なら描かない
        m_CloudLayer.bEnabled = m_CloudLayer.bEnabled && m_SkyParameters.bEnabled;
        if (!m_CloudLayer.bEnabled || !EnsureNoiseTextures())
        {
            m_CloudLayer.bEnabled = false;
            return;
        }

        RGTextureHandle sceneDepthHandle;
        RGTextureHandle skyRadianceHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth,
                                    sceneDepthHandle,
                                    RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereRadiance,
                                    skyRadianceHandle,
                                    RHI::ResourceState::ShaderResource))
        {
            m_CloudLayer.bEnabled = false;
            return;
        }
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();
        m_SkyAtmosphereRadianceHandle = skyRadianceHandle.ToResourceHandle();

        // SSR があればその出力へ、無ければ照明の色へ重ねる（フォグはこの後に重なる）
        RGTextureHandle sceneColorHandle;
        const bool bHasSceneColor =
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SSRSceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource) ||
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource);
        if (!bHasSceneColor)
        {
            m_SceneDepthHandle = {};
            m_SkyAtmosphereRadianceHandle = {};
            m_CloudLayer.bEnabled = false;
            return;
        }
        m_SceneColorHandle = sceneColorHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void CloudLayerPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        using namespace CloudLayerPassDetail;
        if (!m_CloudLayer.bEnabled || !m_SceneColorHandle.IsValid() || !m_SceneDepthHandle.IsValid() ||
            !m_SkyAtmosphereRadianceHandle.IsValid())
        {
            return;
        }

        RHI::TexturePtr sceneColorTexture = resources.GetTexture(m_SceneColorHandle);
        RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        RHI::TexturePtr skyRadianceTexture = resources.GetTexture(m_SkyAtmosphereRadianceHandle);
        if (!sceneColorTexture)
        {
            return;
        }
        if (!PrepareResources(sceneColorTexture))
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }
        const CameraProxy* activeCamera = context.GetActiveCamera();
        if (!sceneDepthTexture || !skyRadianceTexture || !m_DescriptorSetLayout || !activeCamera ||
            !m_ShapeNoiseTexture || !m_DetailNoiseTexture)
        {
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        if (!m_bSunIlluminanceValid || !AreSkyAtmosphereParametersEqual(m_SunIlluminanceSky, m_SkyParameters))
        {
            m_SunIlluminance = ComputeSunGroundIlluminance(m_SkyParameters);
            m_SunIlluminanceSky = m_SkyParameters;
            m_bSunIlluminanceValid = true;
        }

        GPUCloudLayerParams params{};
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
        cameraConstants.CopyShaderInverseViewProjection(params.InverseViewProjection);
        params.CameraPositionAndPreExposure[0] = activeCamera->PositionX;
        params.CameraPositionAndPreExposure[1] = activeCamera->PositionY;
        params.CameraPositionAndPreExposure[2] = activeCamera->PositionZ;
        params.CameraPositionAndPreExposure[3] = SafeCloudPreExposure(activeCamera);

        const Math::Vector3 sunDirection = MakeSunDirectionFromAltitudeAzimuth(m_SkyParameters.SunAltitudeDegrees,
                                                                              m_SkyParameters.SunAzimuthDegrees);
        params.SunDirectionAndFrameIndex[0] = sunDirection.x;
        params.SunDirectionAndFrameIndex[1] = sunDirection.y;
        params.SunDirectionAndFrameIndex[2] = sunDirection.z;
        params.SunDirectionAndFrameIndex[3] = static_cast<float>(context.TemporalFrameIndex % 1024u);
        params.SunIlluminance[0] = std::isfinite(m_SunIlluminance.x) ? std::max(m_SunIlluminance.x, 0.0f) : 0.0f;
        params.SunIlluminance[1] = std::isfinite(m_SunIlluminance.y) ? std::max(m_SunIlluminance.y, 0.0f) : 0.0f;
        params.SunIlluminance[2] = std::isfinite(m_SunIlluminance.z) ? std::max(m_SunIlluminance.z, 0.0f) : 0.0f;

        params.LayerAltitudesAndCoverage[0] = m_SkyParameters.PlanetRadiusMeters;
        params.LayerAltitudesAndCoverage[1] = m_CloudLayer.BottomAltitudeMeters;
        params.LayerAltitudesAndCoverage[2] = m_CloudLayer.TopAltitudeMeters;
        params.LayerAltitudesAndCoverage[3] = m_CloudLayer.Coverage;

        const double shapeScale = m_CloudLayer.ShapeScaleMeters;
        const double detailScale = shapeScale * DetailScaleRatio;
        const double weatherScale = shapeScale * WeatherScaleRatio;
        params.ExtinctionAndInverseScales[0] = m_CloudLayer.ExtinctionPerMeter;
        params.ExtinctionAndInverseScales[1] = static_cast<float>(1.0 / shapeScale);
        params.ExtinctionAndInverseScales[2] = static_cast<float>(1.0 / detailScale);
        params.ExtinctionAndInverseScales[3] = static_cast<float>(1.0 / weatherScale);

        // 決定的な撮影では TotalTime が 1/60 秒の固定刻みなので、同じフレームの撮影で同じ位置に雲が来る
        const double timeSeconds = std::isfinite(context.TotalTime) ? context.TotalTime : 0.0;
        const double windX = m_CloudLayer.WindVelocityX;
        const double windZ = m_CloudLayer.WindVelocityZ;
        params.ShapeAndWeatherOffset[0] = WrapWindOffset(windX, timeSeconds, shapeScale);
        params.ShapeAndWeatherOffset[1] = WrapWindOffset(windZ, timeSeconds, shapeScale);
        params.ShapeAndWeatherOffset[2] = WrapWindOffset(windX, timeSeconds, weatherScale);
        params.ShapeAndWeatherOffset[3] = WrapWindOffset(windZ, timeSeconds, weatherScale);
        params.DetailOffset[0] = WrapWindOffset(windX * DetailWindRatio, timeSeconds, detailScale);
        // 細部は上へもゆっくり動かし、湧き上がるように形を変える
        params.DetailOffset[1] = WrapWindOffset(-1.5, timeSeconds, detailScale);
        params.DetailOffset[2] = WrapWindOffset(windZ * DetailWindRatio, timeSeconds, detailScale);

        params.GroundAlbedo[0] = m_SkyParameters.GroundAlbedo.x;
        params.GroundAlbedo[1] = m_SkyParameters.GroundAlbedo.y;
        params.GroundAlbedo[2] = m_SkyParameters.GroundAlbedo.z;

        RHI::DescriptorSetPtr descriptorSet;
        RHI::BufferPtr paramsBuffer;
        uint32_t paramsOffset = 0u;
        if (FrameResourceLane* frameResources = context.FrameResources)
        {
            const FrameUploadAllocation paramsAllocation = frameResources->AllocateConstants(params);
            if (paramsAllocation.IsValid())
            {
                paramsBuffer = paramsAllocation.Buffer;
                paramsOffset = paramsAllocation.GetOffset32();
            }
            descriptorSet = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout).Set;
        }
        else if (m_Device)
        {
            paramsBuffer = m_Device->CreateBuffer(RHI::BufferDesc(sizeof(GPUCloudLayerParams),
                                                                  RHI::ResourceUsage::ConstantBuffer,
                                                                  true,
                                                                  "CloudLayerParams.Unowned"));
            if (paramsBuffer)
            {
                paramsBuffer->Update(&params, sizeof(params));
                m_UnownedBuffers.push_back(paramsBuffer);
            }
            descriptorSet = m_Device->CreateDescriptorSet(CreateCloudLayerDescriptorSetDesc());
            if (descriptorSet)
            {
                m_UnownedDescriptorSets.push_back(descriptorSet);
            }
        }
        if (!paramsBuffer || !descriptorSet)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "定数か記述子セットを借りられないため、このフレームは雲を描きません");
            EnqueueEmptyNativePass(context, sceneColorTexture);
            return;
        }

        descriptorSet->BindTexture(0u, sceneDepthTexture);
        descriptorSet->BindSampler(0u, m_SceneDepthSampler);
        descriptorSet->BindTexture(1u, skyRadianceTexture);
        descriptorSet->BindSampler(1u, m_SkyRadianceSampler);
        descriptorSet->BindConstantBuffer(2u, paramsBuffer, paramsOffset, sizeof(GPUCloudLayerParams));
        descriptorSet->BindTexture(3u, m_ShapeNoiseTexture);
        descriptorSet->BindSampler(3u, m_NoiseSampler);
        descriptorSet->BindTexture(4u, m_DetailNoiseTexture);
        descriptorSet->BindSampler(4u, m_NoiseSampler);
        descriptorSet->Update();

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      m_Pipeline,
                                      descriptorSet);
    }

    void CloudLayerPass::EnqueueEmptyNativePass(ViewRenderContext& context,
                                                const RHI::TexturePtr& sceneColorTexture) const
    {
        if (m_RenderPass && m_Framebuffer && m_FramebufferSceneColorTexture == sceneColorTexture.get())
        {
            context.EnqueueFullscreenPass(m_RenderPass,
                                          m_Framebuffer,
                                          context.GetActiveLocalViewport(),
                                          context.GetActiveLocalScissor(),
                                          nullptr,
                                          nullptr);
            return;
        }
        if (sceneColorTexture)
        {
            context.EnqueueTextureBarrier(sceneColorTexture,
                                          RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
        }
    }

    bool CloudLayerPass::PrepareResources(const RHI::TexturePtr& sceneColorTexture)
    {
        if (!m_Device || !sceneColorTexture)
        {
            return false;
        }

        const uint32_t width = sceneColorTexture->GetWidth();
        const uint32_t height = sceneColorTexture->GetHeight();
        const RHI::Format format = sceneColorTexture->GetFormat();
        if (width == 0u || height == 0u)
        {
            return false;
        }
        const bool bResourcesChanged = !m_RenderPass || !m_Framebuffer || !m_Pipeline ||
                                       m_CurrentWidth != width || m_CurrentHeight != height ||
                                       m_CurrentFormat != format ||
                                       m_FramebufferSceneColorTexture != sceneColorTexture.get();
        if (!bResourcesChanged)
        {
            return true;
        }

        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_RenderPass.reset();

        RHI::RenderPassDesc renderPassDesc;
        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = format;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::RenderTarget;
        colorAttachment.finalState = RHI::ResourceState::ShaderResource;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "レンダーパスを作れませんでした");
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        framebufferDesc.colorTargets.push_back(sceneColorTexture);
        framebufferDesc.width = width;
        framebufferDesc.height = height;
        m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "フレームバッファを作れませんでした");
            m_RenderPass.reset();
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_VertexShader;
        pipelineDesc.pixelShader = m_FragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        // 前乗算のアルファ: 雲の散乱光を足し、下の空と太陽の円盤を透過率の分だけ残す
        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = RHI::BlendFactor::One;
        blendAttachment.dstColorBlendFactor = RHI::BlendFactor::InvSrcAlpha;
        blendAttachment.colorBlendOp = RHI::BlendOp::Add;
        blendAttachment.srcAlphaBlendFactor = RHI::BlendFactor::Zero;
        blendAttachment.dstAlphaBlendFactor = RHI::BlendFactor::One;
        blendAttachment.alphaBlendOp = RHI::BlendOp::Add;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::R | RHI::ColorWriteMask::G | RHI::ColorWriteMask::B;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);
        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(CloudLayerPassDetail::CreateCloudLayerDescriptorSetDesc());
        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("CloudLayerPass", "パイプラインを作れませんでした");
            m_Framebuffer.reset();
            m_RenderPass.reset();
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_CurrentFormat = format;
        m_FramebufferSceneColorTexture = sceneColorTexture.get();
        return true;
    }
} // namespace NorvesLib::Core::Rendering
