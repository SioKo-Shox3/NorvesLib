#pragma once

#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletalAssetResource.h"
#include "Component.h"
#include "Math/Matrix4x4.h"
#include "Rendering/SkinnedMeshTypes.h"

namespace NorvesLib::Core::Component
{
    class SkinnedMeshComponent : public Component
    {
        REFLECTION_CLASS(SkinnedMeshComponent, Component)

    public:
        SkinnedMeshComponent();
        explicit SkinnedMeshComponent(const FieldInitializer* initializer);
        explicit SkinnedMeshComponent(const IUnknown* sourceObject);
        ~SkinnedMeshComponent() override;

        void Initialize() override;
        void Finalize() override;
        void Tick(float deltaTime) override;

        void SetSkeletalAsset(const Container::TSharedPtr<SkeletalAssetResource>& asset);
        const Container::TSharedPtr<SkeletalAssetResource>& GetSkeletalAsset() const;

        void SetMeshNodeGlobalTransform(const Math::Matrix4x4& transform);
        const Math::Matrix4x4& GetMeshNodeGlobalTransform() const;

        void SetAnimationTimeSeconds(float timeSeconds);
        float GetAnimationTimeSeconds() const;
        void SetPlaying(bool bPlaying);
        bool IsPlaying() const;
        void SetLooping(bool bLooping);
        bool IsLooping() const;
        void SetPlaybackRate(float playbackRate);
        float GetPlaybackRate() const;
        void SetMaterial(Rendering::MaterialHandle material);
        Rendering::MaterialHandle GetMaterial() const;
        void SetCastShadow(bool bCastShadow);
        bool CastsShadow() const;
        void SetVisible(bool bVisible);
        bool IsVisible() const;

        [[nodiscard]] bool BuildSkinnedMeshProxy(Rendering::SkinnedMeshProxy& outProxy);

    private:
        bool RefreshPose();
        Math::Matrix4x4 BuildOwnerWorldTransform() const;

        Container::TSharedPtr<SkeletalAssetResource> m_SkeletalAsset;
        Math::Matrix4x4 m_MeshNodeGlobalTransform;
        Animation::SkeletalPoseSnapshot m_Pose;
        Rendering::MaterialHandle m_Material;
        float m_AnimationTimeSeconds = 0.0f;
        float m_PlaybackRate = 1.0f;
        bool m_bPlaying = true;
        bool m_bCastShadow = true;
        bool m_bLooping = true;
        bool m_bVisible = true;
        bool m_bPoseDirty = true;
    };
} // namespace NorvesLib::Core::Component
