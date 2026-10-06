#pragma once

#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletalAssetResource.h"
#include "Component.h"
#include "Math/Matrix4x4.h"
#include "Rendering/SkinnedMeshTypes.h"
#include "Resource/SkeletalMaterialBindings.h"

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
        // 同aggregateに属する明示clipだけを受理する。失敗時は旧選択を保持する。
        [[nodiscard]] bool SetAnimationClip(const Container::TSharedPtr<AnimationClipResource>& clip);
        // 明示選択が無い場合だけ旧先頭別名を使う。membership喪失時は空でfallbackしない。
        Container::TSharedPtr<AnimationClipResource> GetAnimationClip() const;

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
        // 旧APIはslot0の別名。未指定slotはslot0へ落ち、無効handleでoverrideを解除する。
        [[nodiscard]] bool SetMaterial(uint32_t slot, Rendering::MaterialHandle material);
        Rendering::MaterialHandle GetMaterial(uint32_t slot) const;
        uint32_t GetMaterialSlotCount() const;
        int32_t FindMaterialSlot(Container::StringView name) const;
        [[nodiscard]] bool SetSlotMaterial(uint32_t slot, Rendering::MaterialHandle material);
        [[nodiscard]] bool SetSlotMaterial(Container::StringView name, Rendering::MaterialHandle material);
        [[nodiscard]] bool TryGetSlotMaterial(uint32_t slot, Rendering::MaterialHandle& out) const;
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
        Container::TSharedPtr<const Rendering::SkinnedMeshAssetLease> GetMaterialBindingLease() const;
        bool HasValidPoseResources() const;
        bool HasCurrentPose() const;
        Math::Matrix4x4 BuildOwnerWorldTransform() const;

        Container::TSharedPtr<SkeletalAssetResource> m_SkeletalAsset;
        Container::TSharedPtr<AnimationClipResource> m_SelectedClip;
        Math::Matrix4x4 m_MeshNodeGlobalTransform;
        bool m_bMeshNodeTransformOverridden = false;
        Animation::SkeletalPoseSnapshot m_Pose;
        uint64_t m_PoseSerial = 0;
        Container::TWeakPtr<SkinnedMeshResource> m_EvaluatedMesh;
        Container::TWeakPtr<SkeletonResource> m_EvaluatedSkeleton;
        Container::TWeakPtr<AnimationClipResource> m_EvaluatedClip;
        Rendering::MaterialHandle m_Material;
        Skeletal::SkeletalMaterialBindings m_SlotMaterials;
        float m_AnimationTimeSeconds = 0.0f;
        float m_PlaybackRate = 1.0f;
        bool m_bPlaying = true;
        bool m_bCastShadow = true;
        bool m_bLooping = true;
        bool m_bVisible = true;
        bool m_bPoseDirty = true;
    };
} // namespace NorvesLib::Core::Component
