#pragma once

#include "Core/Public/Debug/DebugConfig.h"

// 骨格つき資産の確認用に、複数の読み込みと Entity への取り付けをまとめる。
// 確認用シーンは開発ビルドだけ（シーンの一覧と GameMode の登録と同じ条件）。
#if NORVES_BUILD_DEVELOPMENT

#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Math/GeometryTypes.h"
#include "Core/Public/Rendering/MaterialTypes.h"

namespace NorvesLib::Core
{
    class SkeletalAssetRuntime;
    class SkeletalAssetResource;
    class AnimationClipResource;
}
namespace NorvesLib::Core::Component
{
    class SkinnedMeshComponent;
}
namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}
namespace NorvesLib::Core::ResourceIO
{
    class SkinnedMaterialBuilder;
}

namespace Game::GameModes
{
    /// 骨格つき資産 1 本の読み込みの結果。
    struct AssetPreviewSkeletalResult
    {
        NorvesLib::Core::Container::String Path; // 指定された論理パスをそのまま保持する。
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> Asset;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimationClipResource> Clip;
        NorvesLib::Core::Container::String MissingReason; // 空なら欠けなし。資産はクリップが欠けても残す。
        NorvesLib::Math::AABB BindBounds = NorvesLib::Math::AABB::CreateInvalid();
    };

    /// GameThread で要求を出し、指定順の結果を保持する。runtime は Cancel まで借用する。
    class AssetPreviewSkeletalLoads final
    {
    public:
        AssetPreviewSkeletalLoads() = default;
        ~AssetPreviewSkeletalLoads();
        AssetPreviewSkeletalLoads(const AssetPreviewSkeletalLoads&) = delete;
        AssetPreviewSkeletalLoads& operator=(const AssetPreviewSkeletalLoads&) = delete;

        /// 前の要求を取り消して開始する。名前が空ならクリップを選択しない。
        void Start(NorvesLib::Core::SkeletalAssetRuntime& runtime,
                   const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& paths,
                   const NorvesLib::Core::Container::String& clipName);
        void FailAll(const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& paths,
                     const char* reason);
        /// 未配送の要求を取り消し、結果と runtime の借用を捨てる。
        void Cancel();
        /// 全件の結果が揃ったか。開始前と空の入力は true。
        bool IsComplete() const;
        /// 入力順の結果。未完の項目もあるため、利用前に IsComplete を確認する。
        const NorvesLib::Core::Container::VariableArray<AssetPreviewSkeletalResult>& GetResults() const;

    private:
        struct State;
        NorvesLib::Core::Container::TSharedPtr<State> m_State;
        NorvesLib::Core::Container::VariableArray<AssetPreviewSkeletalResult> m_EmptyResults;
    };

    /// ScopeRef で追跡する Entity に資産・材質・姿勢を設定する。
    NorvesLib::Core::Component::SkinnedMeshComponent* AttachAssetPreviewSkeletal(
        NorvesLib::Core::GameMode::GameModeContext& ctx,
        NorvesLib::Core::ResourceIO::SkinnedMaterialBuilder& builder,
        const AssetPreviewSkeletalResult& result,
        NorvesLib::Core::Rendering::MaterialHandle fallbackMaterial,
        const NorvesLib::Math::Vector3& position,
        float timeSeconds);
} // namespace Game::GameModes

#endif
