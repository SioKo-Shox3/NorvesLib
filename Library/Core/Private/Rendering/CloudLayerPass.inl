// 空に雲の層を描くパス（CloudLayerPass）の実装。Coreの登録済みのSceneView.cppから取り込む。
#include "Rendering/CloudLayerPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
            // xy=基本の形の風のずれ（X・Z、m）、zw=天気の模様（大きい方）のテクスチャ座標のずれ（0〜1）
            float ShapeAndWeatherOffset[4];
            // xyz=細部の風のずれ（m）、w=未使用
            float DetailOffset[4];
            // rgb=地面のアルベド、w=未使用
            float GroundAlbedo[4];
            // xy=天気の模様（細かい方）のテクスチャ座標のずれ（0〜1）、zw=未使用
            float WeatherHighOffset[4];
            // 雲までの区間の空気遠近（VolumetricsPass が地面に掛けるのと同じ空の媒質と表）
            // x=有効なら 1、y=距離の倍率、z=掛け始める距離（m）、w=ワールドの y = 0 の海面からの高度（m）
            float AerialPerspective[4];
            // rgb=高度 0 の Rayleigh の消散係数（1/m）、w=スケールハイト（m）
            float RayleighExtinctionAndScaleHeight[4];
            // x=高度 0 の Mie の消散係数（1/m）、y=スケールハイト（m）、z=高度 0 の Mie の散乱係数（1/m）、w=位相の異方性
            float MieExtinctionAndScattering[4];
            // x=高度ごとの表のいちばん上の段の海面からの高度（m）、yzw=未使用
            float AerialProfileRange[4];
            // 空気遠近の高度ごとの表（SkyAerialProfile）。[i] = 太陽の照度 × 透過率、[SkyAerialProfileSize + i] = 太陽の照度 × Ψ_ms
            float AerialProfile[2u * SkyAerialProfileSize][4];
        };
        static_assert(sizeof(GPUCloudLayerParams) == 272u + 2u * SkyAerialProfileSize * 16u);

        // 基本の形と細部の 3D ノイズの一辺の texel の数
        constexpr uint32_t ShapeNoiseSize = 64u;
        constexpr uint32_t DetailNoiseSize = 32u;
        // 細部の周期・天気の模様の周期の、基本の形の周期に対する比。互いに割り切れない比にして、重ねた模様の繰り返しを長くする
        constexpr float DetailScaleRatio = 1.0f / 8.3f;
        constexpr float WeatherScaleRatio = 3.7f;
        // 細部は風より少し速く流し、形が崩れながら進むように見せる
        constexpr float DetailWindRatio = 1.6f;
        // 天気の模様の向きの回転（cos, sin）と、細かい方の模様の倍率・位相。cloud_layer.frag と同じ値にする
        constexpr double WeatherRotationCos = 0.7986;
        constexpr double WeatherRotationSin = 0.6018;
        constexpr double WeatherHighScale = 2.7;
        constexpr double WeatherHighPhaseU = 0.31;
        constexpr double WeatherHighPhaseW = 0.53;
        // 細部を上へ動かす速さ（m/s。湧き上がるように形を変える）
        constexpr double DetailRiseVelocity = -1.5;

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

        // テクスチャ座標のずれを 0〜1 に折り返す（ノイズは 1 で繰り返すので見た目は同じ）
        float WrapUnit(double value)
        {
            const double wrapped = value - std::floor(value);
            return static_cast<float>(std::isfinite(wrapped) ? wrapped : 0.0);
        }

        // cloud_layer.frag の定数を詰める。CloudLayerPass と CloudProbePass（撮影の雲の書き出し）が同じ値を使う
        void BuildCloudLayerParams(const ViewRenderContext& context,
                                   const CameraProxy& camera,
                                   const CloudLayerParameters& cloudLayer,
                                   const SkyAtmosphereParameters& sky,
                                   const Math::Vector3& sunIlluminance,
                                   GPUCloudLayerParams& outParams)
        {
            GPUCloudLayerParams params{};
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(camera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyShaderInverseViewProjection(params.InverseViewProjection);
            params.CameraPositionAndPreExposure[0] = camera.PositionX;
            params.CameraPositionAndPreExposure[1] = camera.PositionY;
            params.CameraPositionAndPreExposure[2] = camera.PositionZ;
            params.CameraPositionAndPreExposure[3] = SafeCloudPreExposure(&camera);

            const Math::Vector3 sunDirection = MakeSunDirectionFromAltitudeAzimuth(sky.SunAltitudeDegrees,
                                                                                  sky.SunAzimuthDegrees);
            params.SunDirectionAndFrameIndex[0] = sunDirection.x;
            params.SunDirectionAndFrameIndex[1] = sunDirection.y;
            params.SunDirectionAndFrameIndex[2] = sunDirection.z;
            params.SunDirectionAndFrameIndex[3] = static_cast<float>(context.TemporalFrameIndex % 1024u);
            params.SunIlluminance[0] = std::isfinite(sunIlluminance.x) ? std::max(sunIlluminance.x, 0.0f) : 0.0f;
            params.SunIlluminance[1] = std::isfinite(sunIlluminance.y) ? std::max(sunIlluminance.y, 0.0f) : 0.0f;
            params.SunIlluminance[2] = std::isfinite(sunIlluminance.z) ? std::max(sunIlluminance.z, 0.0f) : 0.0f;

            params.LayerAltitudesAndCoverage[0] = sky.PlanetRadiusMeters;
            params.LayerAltitudesAndCoverage[1] = cloudLayer.BottomAltitudeMeters;
            params.LayerAltitudesAndCoverage[2] = cloudLayer.TopAltitudeMeters;
            params.LayerAltitudesAndCoverage[3] = cloudLayer.Coverage;

            const double shapeScale = cloudLayer.ShapeScaleMeters;
            const double detailScale = shapeScale * DetailScaleRatio;
            const double weatherScale = shapeScale * WeatherScaleRatio;
            params.ExtinctionAndInverseScales[0] = cloudLayer.ExtinctionPerMeter;
            params.ExtinctionAndInverseScales[1] = static_cast<float>(1.0 / shapeScale);
            params.ExtinctionAndInverseScales[2] = static_cast<float>(1.0 / detailScale);
            params.ExtinctionAndInverseScales[3] = static_cast<float>(1.0 / weatherScale);

            // 決定的な撮影では TotalTime が 1/60 秒の固定刻みなので、同じフレームの撮影で同じ位置に雲が来る
            const double timeSeconds = std::isfinite(context.TotalTime) ? context.TotalTime : 0.0;
            const double windX = cloudLayer.WindVelocityX;
            const double windZ = cloudLayer.WindVelocityZ;
            params.ShapeAndWeatherOffset[0] = WrapWindOffset(windX, timeSeconds, shapeScale);
            params.ShapeAndWeatherOffset[1] = WrapWindOffset(windZ, timeSeconds, shapeScale);
            // 天気の模様は回転と倍率を掛けた後のテクスチャ座標で折り返す。世界の軸の距離で折り返すと、
            // 回転・倍率の後では 1 の整数倍にならず、折り返した瞬間に雲の分布が飛ぶ
            const double weatherOffsetX = std::isfinite(windX * timeSeconds) ? windX * timeSeconds : 0.0;
            const double weatherOffsetZ = std::isfinite(windZ * timeSeconds) ? windZ * timeSeconds : 0.0;
            const double weatherU = (WeatherRotationCos * weatherOffsetX - WeatherRotationSin * weatherOffsetZ) / weatherScale;
            const double weatherV = (WeatherRotationSin * weatherOffsetX + WeatherRotationCos * weatherOffsetZ) / weatherScale;
            params.ShapeAndWeatherOffset[2] = WrapUnit(weatherU);
            params.ShapeAndWeatherOffset[3] = WrapUnit(weatherV);
            params.WeatherHighOffset[0] = WrapUnit(weatherV * WeatherHighScale + WeatherHighPhaseU);
            params.WeatherHighOffset[1] = WrapUnit(weatherU * WeatherHighScale + WeatherHighPhaseW);
            params.DetailOffset[0] = WrapWindOffset(windX * DetailWindRatio, timeSeconds, detailScale);
            // 細部は上へもゆっくり動かし、湧き上がるように形を変える
            params.DetailOffset[1] = WrapWindOffset(DetailRiseVelocity, timeSeconds, detailScale);
            params.DetailOffset[2] = WrapWindOffset(windZ * DetailWindRatio, timeSeconds, detailScale);

            params.GroundAlbedo[0] = sky.GroundAlbedo.x;
            params.GroundAlbedo[1] = sky.GroundAlbedo.y;
            params.GroundAlbedo[2] = sky.GroundAlbedo.z;

            // 雲までの区間の空気遠近は、VolumetricsPass（BuildVolumetricsParams）が地面に掛けるのと同じ媒質・同じ表で積む。
            // 空気遠近が無効か表が無ければ、地面と同じく何も掛けない
            const SceneProxy* scene = context.SnapshotScene;
            const AerialPerspectiveParameters aerial =
                scene ? SanitizeAerialPerspectiveParameters(scene->AerialPerspective) : AerialPerspectiveParameters{};
            const SkyAerialProfile& aerialProfile = context.SkyAtmosphere.AerialProfile;
            if (aerial.bEnabled && sky.bEnabled && aerialProfile.bValid)
            {
                const AtmosphereGroundExtinction extinction = GetAtmosphereGroundExtinction(sky);
                params.AerialPerspective[0] = 1.0f;
                params.AerialPerspective[1] = aerial.DistanceScale;
                params.AerialPerspective[2] = aerial.StartDistanceMeters;
                params.AerialPerspective[3] = aerial.OriginAltitudeMeters;
                params.RayleighExtinctionAndScaleHeight[0] = extinction.Rayleigh.x;
                params.RayleighExtinctionAndScaleHeight[1] = extinction.Rayleigh.y;
                params.RayleighExtinctionAndScaleHeight[2] = extinction.Rayleigh.z;
                params.RayleighExtinctionAndScaleHeight[3] = sky.RayleighScaleHeightMeters;
                params.MieExtinctionAndScattering[0] = extinction.Mie * aerial.MieDensityScale;
                params.MieExtinctionAndScattering[1] = sky.MieScaleHeightMeters;
                params.MieExtinctionAndScattering[2] = extinction.MieScattering * aerial.MieDensityScale;
                params.MieExtinctionAndScattering[3] = sky.MieAnisotropy;
                params.AerialProfileRange[0] = SkyAerialProfileMaxAltitudeMeters;
                std::memcpy(params.AerialProfile[0], aerialProfile.SunIlluminance, sizeof(aerialProfile.SunIlluminance));
                std::memcpy(params.AerialProfile[SkyAerialProfileSize], aerialProfile.MultipleScattering,
                            sizeof(aerialProfile.MultipleScattering));
            }
            outParams = params;
        }

        // 雲の 3D ノイズのテクスチャを作る（CloudLayerPass と CloudProbePass が同じ値を上げる）。作れなければ false
        bool CreateNoiseTextures(RHI::IDevice* device,
                                 const char* ownerName,
                                 RHI::TexturePtr& outShape,
                                 RHI::TexturePtr& outDetail)
        {
            const auto startTime = std::chrono::steady_clock::now();
            Container::VariableArray<uint8_t> shapeTexels;
            Container::VariableArray<uint8_t> detailTexels;
            GenerateShapeNoise(shapeTexels);
            GenerateDetailNoise(detailTexels);
            outShape = CreateNoiseTexture(device, ShapeNoiseSize, shapeTexels, "CloudLayer.ShapeNoise");
            outDetail = CreateNoiseTexture(device, DetailNoiseSize, detailTexels, "CloudLayer.DetailNoise");
            if (!outShape || !outDetail)
            {
                outShape.reset();
                outDetail.reset();
                return false;
            }
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                                         std::chrono::steady_clock::now() - startTime)
                                         .count();
            NORVES_LOG_INFO(ownerName,
                            "CLOUD_NOISE shape=%u detail=%u generate_ms=%.1f shape_fnv1a64=%016llx detail_fnv1a64=%016llx",
                            ShapeNoiseSize,
                            DetailNoiseSize,
                            elapsedMs,
                            static_cast<unsigned long long>(HashCloudNoiseTexels(shapeTexels)),
                            static_cast<unsigned long long>(HashCloudNoiseTexels(detailTexels)));
            return true;
        }
    } // namespace CloudLayerPassDetail

    void BuildCloudNoiseTexels(Container::VariableArray<uint8_t>& outShape, Container::VariableArray<uint8_t>& outDetail)
    {
        CloudLayerPassDetail::GenerateShapeNoise(outShape);
        CloudLayerPassDetail::GenerateDetailNoise(outDetail);
    }

    uint64_t HashCloudNoiseTexels(const Container::VariableArray<uint8_t>& texels)
    {
        uint64_t hash = 0xcbf29ce484222325ull;
        for (const uint8_t value : texels)
        {
            hash ^= value;
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

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

        if (!CloudLayerPassDetail::CreateNoiseTextures(m_Device, "CloudLayerPass", m_ShapeNoiseTexture, m_DetailNoiseTexture))
        {
            NORVES_LOG_ERROR("CloudLayerPass", "雲の 3D ノイズを作れないため、雲を描きません");
            m_bNoiseGenerationFailed = true;
            return false;
        }
        return true;
    }

    void CloudLayerPass::UpdateSunIlluminance()
    {
        if (!m_bSunIlluminanceValid || !AreSkyAtmosphereParametersEqual(m_SunIlluminanceSky, m_SkyParameters))
        {
            m_SunIlluminance = ComputeSunGroundIlluminance(m_SkyParameters);
            m_SunIlluminanceSky = m_SkyParameters;
            m_bSunIlluminanceValid = true;
        }
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

        UpdateSunIlluminance();
        GPUCloudLayerParams params{};
        BuildCloudLayerParams(context, *activeCamera, m_CloudLayer, m_SkyParameters, m_SunIlluminance, params);

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

#if NORVES_ENABLE_STATS
    namespace CloudLayerPassDetail
    {
        constexpr RHI::Format CloudProbeFormat = RHI::Format::R32G32B32A32_FLOAT;

        // cloud_layer_probe.frag の CloudProbeRays（std140）
        struct alignas(16) GPUCloudProbeRays
        {
            uint32_t RayCount[4];
            float TargetSize[4];
            float Pixels[FrameCaptureCloudMaxRays][4];
        };
        // 遠い雲の消える原因を分ける反実仮想の列: 区画の格子の上から kStageGridRows 行の視線を、同じ画素・同じ歩の端数のまま
        // 診断の切り替え（Common/CloudLayerCommon.glsl の kCloudDiagnostic*）を段ごとに足して評価し、格子の視線の後ろへ並べる。
        // 段は 1 = 打ち切りを外す、2 = さらに距離の溶かしを外す、3 = さらに独自の霞を外す で、列ごとに 1 → 2 → 3 と回す
        constexpr uint32_t kStageGridRows = 7u;
        constexpr uint32_t kGridRayCount = CloudProbePass::kRayColumns * CloudProbePass::kRayRows;
        constexpr uint32_t kStageRayCount = CloudProbePass::kRayColumns * kStageGridRows;
        constexpr uint32_t kProbeRayCount = kGridRayCount + kStageRayCount;
        constexpr float kStageDiagnosticFlags[3] = {1.0f, 3.0f, 7.0f};
        static_assert(kStageGridRows <= CloudProbePass::kRayRows);
        static_assert(kProbeRayCount <= FrameCaptureCloudMaxRays);
        static_assert(sizeof(GPUCloudLayerParams) == FrameCaptureCloudShaderParamCount * sizeof(float));

        RHI::DescriptorSetDesc CreateCloudProbeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc = CreateCloudLayerDescriptorSetDesc();
            RHI::DescriptorBinding raysBinding;
            raysBinding.binding = 5u;
            raysBinding.type = RHI::ResourceBindType::ConstantBuffer;
            raysBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(raysBinding);
            return desc;
        }
    } // namespace CloudLayerPassDetail

    namespace CloudLayerPassDetail
    {
        // 環境変数が 1 か
        bool IsEnvironmentFlagSet(const char* name)
        {
            char* value = nullptr;
            size_t length = 0;
            bool bRequested = false;
            if (_dupenv_s(&value, &length, name) == 0 && value)
            {
                bRequested = std::strcmp(value, "1") == 0;
            }
            std::free(value);
            return bRequested;
        }
    } // namespace CloudLayerPassDetail

    bool CloudProbePass::IsRequestedByEnvironment()
    {
        return CloudLayerPassDetail::IsEnvironmentFlagSet("NORVES_CLOUD_DUMP");
    }

    bool CloudProbePass::IsOpacityRequestedByEnvironment()
    {
        return CloudLayerPassDetail::IsEnvironmentFlagSet("NORVES_CLOUD_OPACITY");
    }

    CloudProbePass::~CloudProbePass()
    {
        Shutdown();
    }

    bool CloudProbePass::Initialize(ViewRenderContext& context)
    {
        using namespace CloudLayerPassDetail;
        m_Device = context.Device;
        m_bInitialized = true;
        if (!m_Device || !context.ShaderMgr)
        {
            return true;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("cloud_layer_probe.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_WARNING("CloudProbePass", "雲の書き出しのシェーダーの読み込みに失敗。このパスは何もしません");
            return true;
        }

        // サンプラーは CloudLayerPass と同じ設定（深度は点、空の LUT は線形、ノイズは線形で 3 方向とも繰り返す）
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

        m_DescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateCloudProbeDescriptorSetDesc());

        RHI::RenderPassDesc renderPassDesc;
        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = CloudProbeFormat;
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = false;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::DontCare;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::RenderTarget;
        colorAttachment.finalState = RHI::ResourceState::ShaderResource;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.hasDepthStencil = false;
        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (m_RenderPass && m_SceneDepthSampler && m_SkyRadianceSampler && m_NoiseSampler && m_DescriptorSetLayout)
        {
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
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
            pipelineDesc.renderPass = m_RenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(CreateCloudProbeDescriptorSetDesc());
            m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);

            // 全画素の雲の不透明度の画像: 同じ形式・同じ記述子セットのレイアウトで、フラグメントシェーダーだけ替える
            m_bOpacityRequested = IsOpacityRequestedByEnvironment();
            if (m_bOpacityRequested)
            {
                m_OpacityFragmentShader = context.ShaderMgr->LoadShader("cloud_layer_opacity.frag", RHI::ShaderStage::Pixel);
                if (m_OpacityFragmentShader)
                {
                    pipelineDesc.pixelShader = m_OpacityFragmentShader;
                    m_OpacityPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
                }
                if (m_OpacityPipeline)
                {
                    NORVES_LOG_INFO("CloudProbePass", "CLOUD_OPACITY 全画素の雲の不透明度の画像を書く");
                }
                else
                {
                    NORVES_LOG_WARNING("CloudProbePass", "雲の不透明度の画像のパイプラインの作成に失敗。不透明度の画像は書きません");
                }
            }
        }
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("CloudProbePass", "雲の書き出しのパイプラインの作成に失敗。このパスは何もしません");
        }
        return true;
    }

    void CloudProbePass::Shutdown()
    {
        m_OpacityPipeline.reset();
        m_OpacityFramebuffer.reset();
        m_OpacityFramebufferTarget = nullptr;
        m_OpacityFragmentShader.reset();
        m_bOpacityRequested = false;
        m_OpacityHandle = {};
        m_Pipeline.reset();
        m_Framebuffer.reset();
        m_FramebufferTarget = nullptr;
        m_RenderPass.reset();
        m_DescriptorSetLayout.reset();
        m_ShapeNoiseTexture.reset();
        m_DetailNoiseTexture.reset();
        m_bNoiseGenerationFailed = false;
        m_NoiseSampler.reset();
        m_SkyRadianceSampler.reset();
        m_SceneDepthSampler.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_TargetHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_bSunIlluminanceValid = false;
        m_LastInfo = FrameCaptureCloudInfo{};
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void CloudProbePass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void CloudProbePass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    bool CloudProbePass::EnsureNoiseTextures()
    {
        if (m_ShapeNoiseTexture && m_DetailNoiseTexture)
        {
            return true;
        }
        if (m_bNoiseGenerationFailed || !m_Device)
        {
            return false;
        }
        if (!CloudLayerPassDetail::CreateNoiseTextures(m_Device, "CloudProbePass", m_ShapeNoiseTexture, m_DetailNoiseTexture))
        {
            NORVES_LOG_WARNING("CloudProbePass", "雲の 3D ノイズを作れないため、雲を書き出しません");
            m_bNoiseGenerationFailed = true;
            return false;
        }
        return true;
    }

    void CloudProbePass::Declare(RenderGraphBuilder& builder)
    {
        m_TargetHandle = {};
        m_OpacityHandle = {};
        m_SceneDepthHandle = {};
        m_SkyAtmosphereRadianceHandle = {};
        m_LastInfo.bValid = false;

        // 初期化を済ませたのにパイプラインが無いときは何も宣言しない
        if (m_bInitialized && !m_Pipeline)
        {
            return;
        }

        // CloudLayerPass::Declare と同じ値（同じスナップショット）。CloudLayerPass が描かない構成では書き出さない
        const ViewRenderContext* context = builder.GetContext();
        const SceneProxy* snapshotScene = context ? context->SnapshotScene : nullptr;
        m_SkyParameters = snapshotScene
                              ? SanitizeSkyAtmosphereParameters(snapshotScene->SkyAtmosphere)
                              : SkyAtmosphereParameters{};
        m_CloudLayer = snapshotScene
                           ? SanitizeCloudLayerParameters(snapshotScene->CloudLayer)
                           : CloudLayerParameters{};
        m_CloudLayer.bEnabled = m_CloudLayer.bEnabled && m_SkyParameters.bEnabled;
        if (!m_CloudLayer.bEnabled)
        {
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
            return;
        }

        const uint32_t rayCount = CloudLayerPassDetail::kProbeRayCount;
        RGTextureDesc desc = RGTextureDesc::RenderTarget(rayCount,
                                                         FrameCaptureCloudProbeRows,
                                                         CloudLayerPassDetail::CloudProbeFormat,
                                                         "Capture_CloudProbe");
        desc.Usage = desc.Usage | RHI::ResourceUsage::TransferSrc;
        const RGTextureHandle targetHandle =
            builder.WriteTextureAttachment(RenderGraphResourceNames::CaptureCloudProbe,
                                           desc,
                                           RGAttachmentKind::Color,
                                           RHI::AttachmentLoadOp::DontCare,
                                           RHI::AttachmentStoreOp::Store,
                                           RHI::ResourceState::RenderTarget,
                                           RHI::ResourceState::ShaderResource);
        if (!targetHandle.IsValid())
        {
            return;
        }
        // 撮影の読み出しはグラフの実行の後なので、外へ出して寿命を延ばす
        builder.ExportTexture(RenderGraphResourceNames::CaptureCloudProbe, targetHandle);
        m_TargetHandle = targetHandle.ToResourceHandle();

        // 全画素の雲の不透明度の画像（雲を描く画面と同じ大きさ。Execute で深度と同じ大きさかを確かめる）
        const uint32_t renderWidth = context ? context->GetActiveRenderWidth() : 0u;
        const uint32_t renderHeight = context ? context->GetActiveRenderHeight() : 0u;
        if (m_bOpacityRequested && m_OpacityPipeline && renderWidth > 0u && renderHeight > 0u)
        {
            RGTextureDesc opacityDesc = RGTextureDesc::RenderTarget(renderWidth,
                                                                    renderHeight,
                                                                    CloudLayerPassDetail::CloudProbeFormat,
                                                                    "Capture_CloudOpacity");
            opacityDesc.Usage = opacityDesc.Usage | RHI::ResourceUsage::TransferSrc;
            const RGTextureHandle opacityHandle =
                builder.WriteTextureAttachment(RenderGraphResourceNames::CaptureCloudOpacity,
                                               opacityDesc,
                                               RGAttachmentKind::Color,
                                               RHI::AttachmentLoadOp::DontCare,
                                               RHI::AttachmentStoreOp::Store,
                                               RHI::ResourceState::RenderTarget,
                                               RHI::ResourceState::ShaderResource);
            if (opacityHandle.IsValid())
            {
                builder.ExportTexture(RenderGraphResourceNames::CaptureCloudOpacity, opacityHandle);
                m_OpacityHandle = opacityHandle.ToResourceHandle();
            }
        }
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();
        m_SkyAtmosphereRadianceHandle = skyRadianceHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void CloudProbePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        using namespace CloudLayerPassDetail;
        m_LastInfo.bValid = false;
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        const CameraProxy* camera = context.GetActiveCamera();
        if (!context.CommandList || !context.FrameResources || !m_Device || !m_Pipeline || !camera ||
            !m_TargetHandle.IsValid() || !m_SceneDepthHandle.IsValid() || !m_SkyAtmosphereRadianceHandle.IsValid() ||
            !EnsureNoiseTextures())
        {
            return;
        }

        const RHI::TexturePtr target = resources.GetTexture(m_TargetHandle);
        const RHI::TexturePtr sceneDepthTexture = resources.GetTexture(m_SceneDepthHandle);
        const RHI::TexturePtr skyRadianceTexture = resources.GetTexture(m_SkyAtmosphereRadianceHandle);
        if (!target || !sceneDepthTexture || !skyRadianceTexture)
        {
            return;
        }
        // 雲を描く画面（CloudLayerPass の描画先）は深度と同じ大きさ
        const uint32_t targetWidth = sceneDepthTexture->GetWidth();
        const uint32_t targetHeight = sceneDepthTexture->GetHeight();
        if (targetWidth == 0u || targetHeight == 0u)
        {
            return;
        }

        if (!m_bSunIlluminanceValid || !AreSkyAtmosphereParametersEqual(m_SunIlluminanceSky, m_SkyParameters))
        {
            m_SunIlluminance = ComputeSunGroundIlluminance(m_SkyParameters);
            m_SunIlluminanceSky = m_SkyParameters;
            m_bSunIlluminanceValid = true;
        }
        GPUCloudLayerParams params{};
        BuildCloudLayerParams(context, *camera, m_CloudLayer, m_SkyParameters, m_SunIlluminance, params);

        // 視線: 画面を横 kRayColumns × 縦 kRayRows に分けた区画の中心に最も近い画素の中心。その後ろに反実仮想の列
        GPUCloudProbeRays rays{};
        const uint32_t rayCount = kProbeRayCount;
        rays.RayCount[0] = rayCount;
        rays.TargetSize[0] = static_cast<float>(targetWidth);
        rays.TargetSize[1] = static_cast<float>(targetHeight);
        for (uint32_t row = 0; row < kRayRows; ++row)
        {
            for (uint32_t column = 0; column < kRayColumns; ++column)
            {
                const uint32_t rayIndex = row * kRayColumns + column;
                const uint32_t pixelX = std::min((2u * column + 1u) * targetWidth / (2u * kRayColumns), targetWidth - 1u);
                const uint32_t pixelY = std::min((2u * row + 1u) * targetHeight / (2u * kRayRows), targetHeight - 1u);
                rays.Pixels[rayIndex][0] = static_cast<float>(pixelX) + 0.5f;
                rays.Pixels[rayIndex][1] = static_cast<float>(pixelY) + 0.5f;
                m_LastInfo.RayPixels[rayIndex][0] = rays.Pixels[rayIndex][0];
                m_LastInfo.RayPixels[rayIndex][1] = rays.Pixels[rayIndex][1];
            }
        }
        for (uint32_t stageIndex = 0; stageIndex < kStageRayCount; ++stageIndex)
        {
            const uint32_t rayIndex = kGridRayCount + stageIndex;
            rays.Pixels[rayIndex][0] = rays.Pixels[stageIndex][0];
            rays.Pixels[rayIndex][1] = rays.Pixels[stageIndex][1];
            rays.Pixels[rayIndex][2] = kStageDiagnosticFlags[stageIndex % 3u];
            m_LastInfo.RayPixels[rayIndex][0] = rays.Pixels[rayIndex][0];
            m_LastInfo.RayPixels[rayIndex][1] = rays.Pixels[rayIndex][1];
        }

        FrameResourceLane* frameResources = context.FrameResources;
        const FrameUploadAllocation paramsAllocation = frameResources->AllocateConstants(params);
        const FrameUploadAllocation raysAllocation = frameResources->AllocateConstants(rays);
        const RHI::DescriptorSetPtr descriptorSet = frameResources->AllocateDescriptorSet(m_DescriptorSetLayout).Set;
        if (!paramsAllocation.IsValid() || !raysAllocation.IsValid() || !descriptorSet)
        {
            NORVES_LOG_WARNING("CloudProbePass", "定数か記述子セットを借りられないため、このフレームは雲を書き出しません");
            return;
        }
        descriptorSet->BindTexture(0u, sceneDepthTexture);
        descriptorSet->BindSampler(0u, m_SceneDepthSampler);
        descriptorSet->BindTexture(1u, skyRadianceTexture);
        descriptorSet->BindSampler(1u, m_SkyRadianceSampler);
        descriptorSet->BindConstantBuffer(2u, paramsAllocation.Buffer, paramsAllocation.GetOffset32(),
                                          sizeof(GPUCloudLayerParams));
        descriptorSet->BindTexture(3u, m_ShapeNoiseTexture);
        descriptorSet->BindSampler(3u, m_NoiseSampler);
        descriptorSet->BindTexture(4u, m_DetailNoiseTexture);
        descriptorSet->BindSampler(4u, m_NoiseSampler);
        descriptorSet->BindConstantBuffer(5u, raysAllocation.Buffer, raysAllocation.GetOffset32(),
                                          sizeof(GPUCloudProbeRays));
        descriptorSet->Update();

        if (!m_Framebuffer || m_FramebufferTarget != target.get())
        {
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_RenderPass;
            framebufferDesc.colorTargets.push_back(target);
            framebufferDesc.width = target->GetWidth();
            framebufferDesc.height = target->GetHeight();
            m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            m_FramebufferTarget = m_Framebuffer ? target.get() : nullptr;
        }
        if (!m_Framebuffer)
        {
            return;
        }

        RHI::Viewport viewport;
        viewport.width = static_cast<float>(target->GetWidth());
        viewport.height = static_cast<float>(target->GetHeight());
        RHI::ScissorRect scissor;
        scissor.right = static_cast<int32_t>(target->GetWidth());
        scissor.bottom = static_cast<int32_t>(target->GetHeight());
        RHI::ICommandList* commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0);
        commandList->Draw(3, 0);
        commandList->EndRenderPass();

        // 全画素の雲の不透明度の画像: 同じ記述子セット（同じ定数・同じ深度）で、雲を描く画面と同じ大きさの 1 枚へ書く。
        // gl_FragCoord を cloud_layer.frag と同じにするため、大きさが深度と違えば書かない
        m_LastInfo.bOpacity = false;
        const RHI::TexturePtr opacityTarget = m_OpacityHandle.IsValid() ? resources.GetTexture(m_OpacityHandle) : nullptr;
        if (opacityTarget && m_OpacityPipeline && opacityTarget->GetWidth() == targetWidth &&
            opacityTarget->GetHeight() == targetHeight)
        {
            if (!m_OpacityFramebuffer || m_OpacityFramebufferTarget != opacityTarget.get())
            {
                RHI::FramebufferDesc framebufferDesc;
                framebufferDesc.renderPass = m_RenderPass;
                framebufferDesc.colorTargets.push_back(opacityTarget);
                framebufferDesc.width = targetWidth;
                framebufferDesc.height = targetHeight;
                m_OpacityFramebuffer = m_Device->CreateFramebuffer(framebufferDesc);
                m_OpacityFramebufferTarget = m_OpacityFramebuffer ? opacityTarget.get() : nullptr;
            }
            if (m_OpacityFramebuffer)
            {
                RHI::Viewport opacityViewport;
                opacityViewport.width = static_cast<float>(targetWidth);
                opacityViewport.height = static_cast<float>(targetHeight);
                RHI::ScissorRect opacityScissor;
                opacityScissor.right = static_cast<int32_t>(targetWidth);
                opacityScissor.bottom = static_cast<int32_t>(targetHeight);
                commandList->BeginRenderPass(m_RenderPass, m_OpacityFramebuffer);
                commandList->SetViewport(opacityViewport);
                commandList->SetScissor(opacityScissor);
                commandList->SetPipeline(m_OpacityPipeline);
                commandList->SetDescriptorSet(descriptorSet, 0);
                commandList->Draw(3, 0);
                commandList->EndRenderPass();
                m_LastInfo.bOpacity = true;
            }
        }
        else if (m_OpacityHandle.IsValid())
        {
            NORVES_LOG_WARNING("CloudProbePass", "雲の不透明度の画像の大きさが深度（%ux%u）と違うため、このフレームは書きません",
                               targetWidth, targetHeight);
        }

        // 評価に使った値（書き出しの JSON に残す）
        std::memcpy(m_LastInfo.ShaderParams, &params, sizeof(params));
        m_LastInfo.Cloud = m_CloudLayer;
        m_LastInfo.Sky = m_SkyParameters;
        m_LastInfo.TimeSeconds = std::isfinite(context.TotalTime) ? context.TotalTime : 0.0;
        m_LastInfo.TemporalFrameIndex = context.TemporalFrameIndex;
        m_LastInfo.TargetWidth = targetWidth;
        m_LastInfo.TargetHeight = targetHeight;
        m_LastInfo.ShapeNoiseSize = ShapeNoiseSize;
        m_LastInfo.DetailNoiseSize = DetailNoiseSize;
        m_LastInfo.DetailScaleRatio = DetailScaleRatio;
        m_LastInfo.WeatherScaleRatio = WeatherScaleRatio;
        m_LastInfo.DetailWindRatio = DetailWindRatio;
        m_LastInfo.DetailRiseVelocity = static_cast<float>(DetailRiseVelocity);
        m_LastInfo.WeatherRotationCos = static_cast<float>(WeatherRotationCos);
        m_LastInfo.WeatherRotationSin = static_cast<float>(WeatherRotationSin);
        m_LastInfo.WeatherHighScale = static_cast<float>(WeatherHighScale);
        m_LastInfo.WeatherHighPhaseU = static_cast<float>(WeatherHighPhaseU);
        m_LastInfo.WeatherHighPhaseW = static_cast<float>(WeatherHighPhaseW);
        m_LastInfo.RayCount = rayCount;
        m_LastInfo.bValid = true;
    }
#endif
} // namespace NorvesLib::Core::Rendering
