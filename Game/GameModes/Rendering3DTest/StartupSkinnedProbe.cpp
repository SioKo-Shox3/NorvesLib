#include "GameModes/Rendering3DTest/StartupSkinnedProbe.h"

#include "GameModes/Rendering3DTest/Rendering3DTestData.h"
#include "Core/Public/Animation/AnimationClipResource.h"
#include "Core/Public/Animation/SkeletalAssetResource.h"
#include "Core/Public/Animation/SkeletonResource.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Container/Containers.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Vector4.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/CameraViewConstants.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/Rendering/Screen.h"
#include "Core/Public/Resource/SkinnedMeshResource.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Game::GameModes
{
    namespace
    {
        // パネルの格子の分割数（辺あたり）と大きさ（m）。置く位置は地面（Y=-1）に下の辺が着く高さ。
        constexpr uint32_t kGridCells = 8u;
        constexpr float kPanelHalfSize = 1.0f;
        // RGBA8_UNORM は線形で標本される。204/255 = 0.800、26/255 ≈ 0.102、51/255 = 0.200。
        constexpr uint8_t kMaterialColors[2][3] = {
            {204u, 26u, 26u},
            {26u, 51u, 204u},
        };
        constexpr uint32_t kMaterialTextureSize = 4u;

        // 置く体の位置と、揺れの位相（秒。周期のずらし）。同じ資産を 2 体置く。
        // 計算スキニングの割り当ては体ごとに頂点・骨の基点が後ろへ進むので、1 体目の基点は必ず 0 になる。
        // 2 体目は基点が 0 にならない体で、記録の頂点の基点の二重加算のような取り違えは 2 体目の描画だけに出る。
        // 位相は体ごとに変え、2 体の姿勢（と速度）を別にする。
        struct PanelPlacement
        {
            float X;
            float Y;
            float Z;
            float PhaseSeconds;
        };
        constexpr PanelPlacement kPanelPlacements[] = {
            {2.8f, 0.0f, 1.5f, 0.0f},
            {-2.6f, 0.0f, 2.5f, 0.5f},
        };
        constexpr size_t kPanelCount = sizeof(kPanelPlacements) / sizeof(kPanelPlacements[0]);
        // 揺れの振れ幅（Y 軸まわり。度）と周期（秒）。
        constexpr float kSwayDegrees = 35.0f;
        constexpr float kSwayPeriodSeconds = 2.0f;
        constexpr float kDegreesToRadians = 3.14159265f / 180.0f;

        // 表（法線 +Z）か裏（法線 -Z）の頂点を格子状に足す。全頂点を骨 0 に重み 1 で結ぶ。
        void AppendGridVertices(
            NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Skeletal::SkeletalVertex>& vertices,
            float normalZ)
        {
            for (uint32_t row = 0u; row <= kGridCells; ++row)
            {
                for (uint32_t column = 0u; column <= kGridCells; ++column)
                {
                    const float u = static_cast<float>(column) / static_cast<float>(kGridCells);
                    const float v = static_cast<float>(row) / static_cast<float>(kGridCells);
                    NorvesLib::Core::Skeletal::SkeletalVertex vertex{};
                    vertex.Position.X = (u * 2.0f - 1.0f) * kPanelHalfSize;
                    vertex.Position.Y = (v * 2.0f - 1.0f) * kPanelHalfSize;
                    vertex.Position.Z = 0.0f;
                    vertex.Normal.Z = normalZ;
                    vertex.TexCoord.U = u;
                    vertex.TexCoord.V = v;
                    vertex.JointIndices = {0u, 0u, 0u, 0u};
                    vertex.JointWeights = {1.0f, 0.0f, 0.0f, 0.0f};
                    vertices.push_back(vertex);
                }
            }
        }

        // 格子の三角形を足す。裏は巻き方向を逆にする（頂点の並びは表と同じ格子で、基点の添え字だけ違う）。
        void AppendGridIndices(
            NorvesLib::Core::Container::VariableArray<uint32_t>& indices,
            uint32_t baseIndex,
            bool bReversed,
            uint32_t firstColumn = 0u,
            uint32_t endColumn = kGridCells)
        {
            const uint32_t stride = kGridCells + 1u;
            for (uint32_t row = 0u; row < kGridCells; ++row)
            {
                for (uint32_t column = firstColumn; column < endColumn; ++column)
                {
                    const uint32_t i00 = baseIndex + row * stride + column;
                    const uint32_t i10 = i00 + 1u;
                    const uint32_t i01 = i00 + stride;
                    const uint32_t i11 = i01 + 1u;
                    if (!bReversed)
                    {
                        indices.push_back(i00);
                        indices.push_back(i10);
                        indices.push_back(i01);
                        indices.push_back(i10);
                        indices.push_back(i11);
                        indices.push_back(i01);
                    }
                    else
                    {
                        indices.push_back(i00);
                        indices.push_back(i01);
                        indices.push_back(i10);
                        indices.push_back(i10);
                        indices.push_back(i01);
                        indices.push_back(i11);
                    }
                }
            }
        }

        // Y 軸まわりに degrees 回したクォータニオン（X・Y・Z・W の順。glTF と同じ）をサンプルへ入れる。
        NorvesLib::Core::Skeletal::SkeletalAnimationSample MakeSwaySample(float timeSeconds, float degrees)
        {
            const float halfRadians = degrees * kDegreesToRadians * 0.5f;
            NorvesLib::Core::Skeletal::SkeletalAnimationSample sample;
            sample.TimeSeconds = timeSeconds;
            sample.Value.X = 0.0f;
            sample.Value.Y = std::sin(halfRadians);
            sample.Value.Z = 0.0f;
            sample.Value.W = std::cos(halfRadians);
            return sample;
        }
    } // namespace

    bool SpawnStartupSkinnedProbe(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data)
    {
        if (data.m_StartupSkinnedProbeAsset)
        {
            return true;
        }
        const bool bTwoMaterials = data.m_StartupSkinnedProbeMaterialCount == 2u;
        if (!bTwoMaterials && !data.m_CobbleStoneMaterial.IsValid())
        {
            LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 石畳の材質が無いので骨付きのパネルを置けない");
            return false;
        }

        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Skeletal::SkeletalVertex> vertices;
        NorvesLib::Core::Container::VariableArray<uint32_t> indices;
        AppendGridVertices(vertices, 1.0f);
        AppendGridVertices(vertices, -1.0f);
        const uint32_t faceVertexCount = (kGridCells + 1u) * (kGridCells + 1u);
        if (bTwoMaterials)
        {
            const uint32_t middleColumn = kGridCells / 2u;
            AppendGridIndices(indices, 0u, false, 0u, middleColumn);
            AppendGridIndices(indices, faceVertexCount, true, 0u, middleColumn);
            AppendGridIndices(indices, 0u, false, middleColumn, kGridCells);
            AppendGridIndices(indices, faceVertexCount, true, middleColumn, kGridCells);
        }
        else
        {
            AppendGridIndices(indices, 0u, false);
            AppendGridIndices(indices, faceVertexCount, true);
        }

        // 骨は 1 本（逆バインド行列は単位行列）。
        NorvesLib::Core::Skeletal::SkeletalJoint joint;
        joint.Name = TEXT("Root");
        joint.ParentIndex = -1;
        joint.InverseBindMatrix = {1.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 1.0f};
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Skeletal::SkeletalJoint> joints;
        joints.push_back(std::move(joint));

        // 0 → +35° → 0 → -35° → 0 の揺れ（ループ）。
        NorvesLib::Core::Skeletal::SkeletalAnimationChannel channel;
        channel.JointIndex = 0u;
        channel.Path = NorvesLib::Core::Skeletal::SkeletalAnimationPath::Rotation;
        channel.Interpolation = NorvesLib::Core::Skeletal::SkeletalAnimationInterpolation::Linear;
        const float quarter = kSwayPeriodSeconds * 0.25f;
        channel.Samples.push_back(MakeSwaySample(0.0f, 0.0f));
        channel.Samples.push_back(MakeSwaySample(quarter, kSwayDegrees));
        channel.Samples.push_back(MakeSwaySample(quarter * 2.0f, 0.0f));
        channel.Samples.push_back(MakeSwaySample(quarter * 3.0f, -kSwayDegrees));
        channel.Samples.push_back(MakeSwaySample(kSwayPeriodSeconds, 0.0f));
        NorvesLib::Core::Skeletal::SkeletalAnimationClip clip;
        clip.Name = TEXT("Sway");
        clip.DurationSeconds = kSwayPeriodSeconds;
        clip.Channels.push_back(std::move(channel));

        auto& registry = NorvesLib::Core::GEngine.GetResourceRegistry();
        auto mesh = registry.CreateTransient<NorvesLib::Core::SkinnedMeshResource>(TEXT("StartupSkinnedProbeMesh"));
        auto skeleton = registry.CreateTransient<NorvesLib::Core::SkeletonResource>(TEXT("StartupSkinnedProbeSkeleton"));
        auto animation = registry.CreateTransient<NorvesLib::Core::AnimationClipResource>(TEXT("StartupSkinnedProbeClip"));
        auto asset = registry.CreateTransient<NorvesLib::Core::SkeletalAssetResource>(TEXT("StartupSkinnedProbeAsset"));
        if (!mesh || !skeleton || !animation || !asset)
        {
            LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きのパネルの資源を作れない");
            return false;
        }
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(std::move(indices));
        if (bTwoMaterials)
        {
            NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Skeletal::SkeletalSubMesh> submeshes;
            NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Skeletal::SkeletalMaterialSlot> slots;
            const uint32_t indicesPerHalf = kGridCells * kGridCells * 3u;
            for (uint32_t slot = 0u; slot < 2u; ++slot)
            {
                NorvesLib::Core::Skeletal::SkeletalSubMesh submesh;
                submesh.IndexStart = slot * indicesPerHalf;
                submesh.IndexCount = indicesPerHalf;
                submesh.MaterialSlot = slot;
                submesh.VertexCount = faceVertexCount * 2u;
                submesh.BoundsCenter[0] = slot == 0u ? -0.5f : 0.5f;
                submesh.BoundsRadius = 1.119f;
                submeshes.push_back(submesh);

                NorvesLib::Core::Skeletal::SkeletalMaterialSlot materialSlot;
                materialSlot.Name = slot == 0u ? TEXT("Left") : TEXT("Right");
                slots.push_back(std::move(materialSlot));
            }
            mesh->SetSubmeshTables(std::move(submeshes), std::move(slots));
        }
        skeleton->SetJoints(std::move(joints));
        animation->SetClip(std::move(clip));
        if (!mesh->Load() || !skeleton->Load() || !animation->Load())
        {
            LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きのパネルの資源を読み込めない");
            return false;
        }
        asset->SetResources(mesh, skeleton, animation);
        if (!asset->IsLoaded())
        {
            LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きのパネルの資産が読み込み済みにならない");
            return false;
        }

        NorvesLib::Core::Rendering::MaterialHandle probeMaterials[2];
        if (bTwoMaterials)
        {
            for (uint32_t slot = 0u; slot < 2u; ++slot)
            {
                uint8_t pixels[kMaterialTextureSize * kMaterialTextureSize * 4u];
                for (uint32_t pixel = 0u; pixel < kMaterialTextureSize * kMaterialTextureSize; ++pixel)
                {
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        pixels[pixel * 4u + channel] = kMaterialColors[slot][channel];
                    }
                    pixels[pixel * 4u + 3u] = 255u;
                }
                NorvesLib::Core::Rendering::TextureCreateInfo textureInfo;
                textureInfo.Width = kMaterialTextureSize;
                textureInfo.Height = kMaterialTextureSize;
                textureInfo.PixelFormat = NorvesLib::Core::Rendering::TextureCreateInfo::Format::RGBA8_UNORM;
                textureInfo.DebugName = slot == 0u ? TEXT("StartupSkinnedProbeLeft") : TEXT("StartupSkinnedProbeRight");
                const auto texture = ctx.RenderResourcesRef.Textures().CreateTexture(textureInfo, pixels, sizeof(pixels));
                if (!texture.IsValid())
                {
                    LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きパネルのテクスチャを作れない slot=%u", slot);
                    return false;
                }
                ctx.ScopeRef.TrackTexture(texture);

                NorvesLib::Core::Rendering::MaterialCreateData material;
                material.DebugName = slot == 0u ? TEXT("StartupSkinnedProbeLeft") : TEXT("StartupSkinnedProbeRight");
                material.AlbedoTexture = texture;
                probeMaterials[slot] = ctx.RenderResourcesRef.Materials().Create(material);
                if (!probeMaterials[slot].IsValid())
                {
                    LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きパネルの材質を作れない slot=%u", slot);
                    return false;
                }
                ctx.ScopeRef.TrackMaterial(probeMaterials[slot]);
            }
        }

        data.m_StartupSkinnedProbeAsset = asset;
        for (size_t index = 0u; index < kPanelCount; ++index)
        {
            const PanelPlacement& placement = kPanelPlacements[index];
            NorvesLib::Core::Entity* entity = ctx.WorldRef.SpawnObject<NorvesLib::Core::Entity>();
            if (entity == nullptr)
            {
                LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きのパネルの実体を作れない index=%zu", index);
                return false;
            }
            ctx.ScopeRef.TrackObject(entity);
            entity->SetPosition(placement.X, placement.Y, placement.Z);
            auto* skinned = ctx.WorldRef.CreateComponent<NorvesLib::Core::Component::SkinnedMeshComponent>(entity);
            if (skinned == nullptr)
            {
                LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きのパネルの部品を作れない index=%zu", index);
                return false;
            }
            skinned->SetSkeletalAsset(asset);
            if (bTwoMaterials)
            {
                if (!skinned->SetMaterial(0u, probeMaterials[0]) || !skinned->SetMaterial(1u, probeMaterials[1]))
                {
                    LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 骨付きパネルに材質を設定できない index=%zu", index);
                    return false;
                }
            }
            else
            {
                skinned->SetMaterial(data.m_CobbleStoneMaterial);
            }
            // 再生はせず、UpdateStartupSkinnedProbe が時刻から姿勢を決める（撮影の時刻をそろえるため）。
            skinned->SetPlaying(false);
            skinned->SetLooping(true);
            const float poseSeconds = bTwoMaterials ? 0.0f : placement.PhaseSeconds;
            skinned->SetAnimationTimeSeconds(poseSeconds);
            skinned->SetCastShadow(true);
            skinned->SetVisible(true);
            data.m_StartupSkinnedProbeComponents.push_back(skinned);
            LOG_INFO("STARTUP_SKINNED_PROBE placed index=%zu x=%.2f y=%.2f z=%.2f phase=%.2f",
                     index, placement.X, placement.Y, placement.Z, poseSeconds);
        }
        LOG_INFO("STARTUP_SKINNED_PROBE bodies=%zu", kPanelCount);
        if (bTwoMaterials)
        {
            LOG_INFO("STARTUP_SKINNED_PROBE materials=2 slots=2 submeshes=2");
        }
        return true;
    }

    void UpdateStartupSkinnedProbe(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data)
    {
        // M9 の受入れは何も変えない 2 フレームの一致を陰性対照に取るので、揺れを止めて生成時の姿勢のままにする。
        if (data.m_StartupSkinnedProbeComponents.empty() || data.m_StartupSkinnedProbeMaterialCount == 2u ||
            (data.m_M9WorldAcceptance && data.m_M9WorldAcceptance->bRequested))
        {
            return;
        }
        const auto& deterministicCapture = ctx.EngineRef.GetDeterministicCapture();
        const float seconds = deterministicCapture.IsEnabled()
                                  ? static_cast<float>(deterministicCapture.GetEpochSeconds())
                                  : data.m_ElapsedTime;
        for (size_t index = 0u; index < data.m_StartupSkinnedProbeComponents.size() && index < kPanelCount; ++index)
        {
            float poseSeconds = std::fmod(seconds + kPanelPlacements[index].PhaseSeconds, kSwayPeriodSeconds);
            if (!std::isfinite(poseSeconds) || poseSeconds < 0.0f)
            {
                poseSeconds = 0.0f;
            }
            data.m_StartupSkinnedProbeComponents[index]->SetAnimationTimeSeconds(poseSeconds);
        }
    }

#if NORVES_BUILD_DEVELOPMENT
    void LogStartupSkinnedProbeMaterialSamples(const NorvesLib::Core::Rendering::CameraProxy& camera, size_t bodyCount)
    {
        const auto& screen = NorvesLib::Core::GEngine.GetScreen();
        const uint32_t width = screen.GetWidth();
        const uint32_t height = screen.GetHeight();
        auto& coordinator = NorvesLib::Core::GEngine.GetRenderingCoordinator();
        const auto device = coordinator.GetDevice();
        if (width == 0u || height == 0u || !device)
        {
            return;
        }

        // 描画側と同じ丸めで内部解像度のアスペクト比を求め、画素は最終出力の大きさに直す。
        const double scale = static_cast<double>(coordinator.GetRenderScale());
        const uint32_t renderWidth = std::max(1u, static_cast<uint32_t>(std::lround(width * scale)));
        const uint32_t renderHeight = std::max(1u, static_cast<uint32_t>(std::lround(height * scale)));
        const float aspect = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);
        const auto viewProjection = NorvesLib::Core::Rendering::CameraViewConstants::BuildForDevice(
            camera, aspect, device.get()).ViewProjectionMatrix;

        for (size_t body = 0u; body < bodyCount && body < kPanelCount; ++body)
        {
            const PanelPlacement& placement = kPanelPlacements[body];
            for (uint32_t slot = 0u; slot < 2u; ++slot)
            {
                const float x = placement.X + (slot == 0u ? -0.5f : 0.5f);
                const float y = placement.Y;
                const float z = placement.Z;
                const auto clip = viewProjection * NorvesLib::Math::Vector4(x, y, z, 1.0f);
                const float forward = (x - camera.PositionX) * camera.ForwardX +
                                      (y - camera.PositionY) * camera.ForwardY +
                                      (z - camera.PositionZ) * camera.ForwardZ;
                int px = -1;
                int py = -1;
                bool bInside = false;
                if (std::isfinite(clip.x) && std::isfinite(clip.y) && std::isfinite(clip.w) &&
                    std::abs(clip.w) > 1.0e-6f)
                {
                    const double pixelX = (static_cast<double>(clip.x) / clip.w * 0.5 + 0.5) * width;
                    // Vulkan の Y 反転済み行列と正の viewport 高さでは、NDC -1 が画像の上端。
                    const double pixelY = (static_cast<double>(clip.y) / clip.w * 0.5 + 0.5) * height;
                    if (std::isfinite(pixelX) && std::isfinite(pixelY))
                    {
                        px = static_cast<int>(std::floor(std::clamp(pixelX, -1000000000.0, 1000000000.0)));
                        py = static_cast<int>(std::floor(std::clamp(pixelY, -1000000000.0, 1000000000.0)));
                        bInside = forward > 0.0f && pixelX >= 0.0 && pixelX < width && pixelY >= 0.0 && pixelY < height;
                    }
                }
                LOG_INFO("STARTUP_SKINNED_PROBE_MATERIAL_SAMPLE body=%zu slot=%u px=%d py=%d inside=%d r=%.3f g=%.3f b=%.3f",
                         body, slot, px, py, bInside ? 1 : 0,
                         kMaterialColors[slot][0] / 255.0f, kMaterialColors[slot][1] / 255.0f,
                         kMaterialColors[slot][2] / 255.0f);
            }
        }
    }
#endif
} // namespace Game::GameModes
