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
        void OnTickGroup(ETickGroup group, float deltaTime) override;

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

        // dirtyな姿勢だけ評価する。成功した再評価ごとにserialを進める。
        // 同じResourceの内容を直接編集した場合はSetSkeletalAssetで再設定して無効化する。
        [[nodiscard]] bool EvaluatePose();
        uint64_t GetPoseSerial() const { return m_PoseSerial; }
        int32_t FindJointIndex(Identity name) const;
        // 自動評価しない。未評価/dirty/無効資産/範囲外はfalseで出力を変更しない。
        [[nodiscard]] bool TryGetJointModelMatrix(uint32_t index, Math::Matrix4x4& outMatrix) const;
        // Ownerのworldは既存のWorld変換確定境界の値を使い、読み取り時に合成する。
        [[nodiscard]] bool TryGetJointWorldMatrix(uint32_t index, Math::Matrix4x4& outMatrix) const;
        // CreateWorldRowVectorで再構成できる正のscaleのTRSだけを返す。
        // shear/反転/退化はfalse。完全な行列表現には上の行列APIを使う。
        [[nodiscard]] bool TryGetJointWorldTransform(uint32_t index, Math::Transform& outTransform) const;

    private:
        bool HasValidPoseResources() const;
        bool HasCurrentPose() const;
        Math::Matrix4x4 BuildOwnerWorldTransform() const;

        Container::TSharedPtr<SkeletalAssetResource> m_SkeletalAsset;
        Math::Matrix4x4 m_MeshNodeGlobalTransform;
        bool m_bMeshNodeTransformOverridden = false;
        Animation::SkeletalPoseSnapshot m_Pose;
        uint64_t m_PoseSerial = 0;
        Container::TWeakPtr<SkinnedMeshResource> m_EvaluatedMesh;
        Container::TWeakPtr<SkeletonResource> m_EvaluatedSkeleton;
        Container::TWeakPtr<AnimationClipResource> m_EvaluatedClip;
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
