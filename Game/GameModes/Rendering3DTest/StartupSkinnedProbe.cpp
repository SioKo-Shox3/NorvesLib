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
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Resource/SkinnedMeshResource.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace Game::GameModes
{
    namespace
    {
        // パネルの格子の分割数（辺あたり）と大きさ（m）。置く位置は地面（Y=-1）に下の辺が着く高さ。
        constexpr uint32_t kGridCells = 8u;
        constexpr float kPanelHalfSize = 1.0f;
        constexpr float kPanelPositionX = 2.8f;
        constexpr float kPanelPositionY = 0.0f;
        constexpr float kPanelPositionZ = 1.5f;
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
            bool bReversed)
        {
            const uint32_t stride = kGridCells + 1u;
            for (uint32_t row = 0u; row < kGridCells; ++row)
            {
                for (uint32_t column = 0u; column < kGridCells; ++column)
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
        if (!data.m_CobbleStoneMaterial.IsValid())
        {
            LOG_ERROR("STARTUP_SKINNED_PROBE_SKIPPED 石畳の材質が無いので骨付きのパネルを置けない");
            return false;
        }

        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Skeletal::SkeletalVertex> vertices;
        NorvesLib::Core::Container::VariableArray<uint32_t> indices;
        AppendGridVertices(vertices, 1.0f);
        AppendGridVertices(vertices, -1.0f);
        const uint32_t faceVertexCount = (kGridCells + 1u) * (kGridCells + 1u);
        AppendGridIndices(indices, 0u, false);
        AppendGridIndices(indices, faceVertexCount, true);

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

        NorvesLib::Core::Entity* entity = ctx.WorldRef.SpawnObject<NorvesLib::Core::Entity>();
        if (entity == nullptr)
        {
            return false;
        }
        ctx.ScopeRef.TrackObject(entity);
        entity->SetPosition(kPanelPositionX, kPanelPositionY, kPanelPositionZ);
        auto* skinned = ctx.WorldRef.CreateComponent<NorvesLib::Core::Component::SkinnedMeshComponent>(entity);
        if (skinned == nullptr)
        {
            return false;
        }
        skinned->SetSkeletalAsset(asset);
        skinned->SetMaterial(data.m_CobbleStoneMaterial);
        // 再生はせず、UpdateStartupSkinnedProbe が時刻から姿勢を決める（撮影の時刻をそろえるため）。
        skinned->SetPlaying(false);
        skinned->SetLooping(true);
        skinned->SetAnimationTimeSeconds(0.0f);
        skinned->SetCastShadow(true);
        skinned->SetVisible(true);
        data.m_StartupSkinnedProbeAsset = asset;
        data.m_pStartupSkinnedProbeComponent = skinned;
        LOG_INFO("STARTUP_SKINNED_PROBE placed x=%.2f y=%.2f z=%.2f", kPanelPositionX, kPanelPositionY, kPanelPositionZ);
        return true;
    }

    void UpdateStartupSkinnedProbe(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data)
    {
        if (data.m_pStartupSkinnedProbeComponent == nullptr)
        {
            return;
        }
        const auto& deterministicCapture = ctx.EngineRef.GetDeterministicCapture();
        const float seconds = deterministicCapture.IsEnabled()
                                  ? static_cast<float>(deterministicCapture.GetEpochSeconds())
                                  : data.m_ElapsedTime;
        float poseSeconds = std::fmod(seconds, kSwayPeriodSeconds);
        if (!std::isfinite(poseSeconds) || poseSeconds < 0.0f)
        {
            poseSeconds = 0.0f;
        }
        data.m_pStartupSkinnedProbeComponent->SetAnimationTimeSeconds(poseSeconds);
    }
} // namespace Game::GameModes
