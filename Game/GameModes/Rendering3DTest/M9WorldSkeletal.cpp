#include "GameModes/Rendering3DTest/M9WorldSkeletal.h"

#include "GameModes/Rendering3DTest/Rendering3DTestData.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"

#include <bit>
#include "Core/Public/Object/World.h"

namespace Game::GameModes
{
    bool AttachM9SkeletalAsset(
        NorvesLib::Core::World& world, NorvesLib::Core::GameMode::GameModeScope& scope,
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource>& asset,
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimationClipResource>& clip,
        NorvesLib::Core::Entity*& outObject, NorvesLib::Core::Component::SkinnedMeshComponent*& outComponent)
    {
        if (outObject || outComponent || !asset || !clip)
        {
            return false;
        }
        auto* object = world.SpawnObject<NorvesLib::Core::Entity>();
        if (!object)
        {
            return false;
        }
        try
        {
            scope.TrackObject(object);
        }
        catch (...)
        {
            world.RemoveObject(object);
            return false;
        }
        try
        {
            auto* component = world.CreateComponent<NorvesLib::Core::Component::SkinnedMeshComponent>(object);
            if (!component)
            {
                return false;
            }
            component->SetSkeletalAsset(asset);
            if (!component->SetAnimationClip(clip))
            {
                return false;
            }
            object->SetPosition(-1.25f, 0.0f, 0.0f);
            component->SetPlaying(false);
            component->SetAnimationTimeSeconds(0.0f);
            outObject = object;
            outComponent = component;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool InitializeM9WorldSkeletal(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                   Rendering3DTestData& data)
    {
        if (!data.m_M9WorldAcceptance || !data.m_M9WorldAcceptance->bAssetsReady ||
            !data.m_M9WorldAcceptance->SkeletalAsset || !data.m_M9WorldAcceptance->SelectedClip)
        {
            return false;
        }

        if (!AttachM9SkeletalAsset(ctx.WorldRef, ctx.ScopeRef, data.m_M9WorldAcceptance->SkeletalAsset,
                                   data.m_M9WorldAcceptance->SelectedClip, data.m_pM9SkinnedObject,
                                   data.m_pM9SkinnedMeshComponent))
        {
            return false;
        }
        const auto& mesh = data.m_M9WorldAcceptance->SkeletalAsset->GetMesh();
        if (!mesh)
        {
            return false;
        }

        const float resourceTranslationX = mesh->GetMeshNodeGlobalTransform()[12];
        const float componentTranslationX =
            data.m_pM9SkinnedMeshComponent->GetMeshNodeGlobalTransform().GetTranslationRow().x;
        LOG_INFO("M9_WORLD_SMOKE stage=skeletal_binding resource_translation_x=%f component_translation_x=%f",
                 resourceTranslationX,
                 componentTranslationX);
        EmitM9WorldSmokeMarker(
            "M9_WORLD_SMOKE stage=skeletal_binding resource_translation_x=%f component_translation_x=%f",
            resourceTranslationX,
            componentTranslationX);
        return true;
    }

    void SetM9WorldSkeletalAnimationTime(Rendering3DTestData& data, float seconds)
    {
        if (data.m_pM9SkinnedMeshComponent)
        {
            data.m_pM9SkinnedMeshComponent->SetAnimationTimeSeconds(seconds);
        }
    }

    bool BuildM9WorldSkeletalPoseFingerprint(Rendering3DTestData& data, uint64_t& outFingerprint)
    {
        outFingerprint = 0;
        NorvesLib::Core::Rendering::SkinnedMeshProxy proxy;
        if (data.m_pM9SkinnedMeshComponent == nullptr ||
            !data.m_pM9SkinnedMeshComponent->BuildSkinnedMeshProxy(proxy) || proxy.BonePalette.empty())
        {
            return false;
        }

        uint64_t fingerprint = 1469598103934665603ull;
        for (const auto& matrix : proxy.BonePalette)
        {
            for (uint32_t valueIndex = 0; valueIndex < 16; ++valueIndex)
            {
                fingerprint ^= std::bit_cast<uint32_t>(matrix.values[valueIndex]);
                fingerprint *= 1099511628211ull;
            }
        }
        outFingerprint = fingerprint;
        return true;
    }
} // namespace Game::GameModes
