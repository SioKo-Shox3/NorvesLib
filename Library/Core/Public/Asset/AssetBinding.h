#pragma once

#include "Asset/AssetRef.h"
#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Delegate/Delegate.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Thread/Atomic.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class SkeletalAssetResource;
    struct SkeletalAssetCompletion;
}

namespace NorvesLib::Core::Asset
{
    enum class AssetBindingStatus : uint8_t
    {
        Unresolved,
        Pending,
        Ready,
        Failed
    };

    /// 1回の要求の結果。結果を書いた後にStatusをreleaseで公開し、読む側はacquireする。
    struct AssetBindingState
    {
        AssetBindingState() = default;
        ~AssetBindingState();
        AssetBindingState(const AssetBindingState&) = delete;
        AssetBindingState& operator=(const AssetBindingState&) = delete;

        Thread::Atomic<uint8_t> Status{static_cast<uint8_t>(AssetBindingStatus::Unresolved)};
        AssetKind Kind = AssetKind::Unknown;
        Container::String LogicalPath;
        Rendering::ModelHandle Model = Rendering::ModelHandle::Invalid();
        Rendering::TextureHandle Texture = Rendering::TextureHandle::Invalid();
        Container::TSharedPtr<SkeletalAssetResource> Skeletal;
        // 所有するモデルだけに設定する。最後の状態参照が消えるときに解放する。
        void (*ReleaseModel)(Rendering::ModelHandle) = nullptr;
    };

    /// nullの欄はエンジンの既定の口へつなぐ。戻り値0は通知の来ない拒否を表す。
    struct AssetBindingLoaders
    {
        using ModelCallback = Delegate<void, Rendering::ModelHandle>;
        using TextureCallback = Delegate<void, Rendering::TextureHandle>;
        using SkeletalCallback = Delegate<void, const SkeletalAssetCompletion&>;

        uint32_t (*LoadModel)(const Container::String&, ModelCallback) = nullptr;
        void (*ReleaseModel)(Rendering::ModelHandle) = nullptr;
        Rendering::MegaGeometry::MegaMeshHandle (*GetModelMegaMesh)(Rendering::ModelHandle) = nullptr;
        uint32_t (*LoadTexture)(const Container::String&, TextureCallback) = nullptr;
        uint64_t (*LoadSkeletal)(const Container::String&, SkeletalCallback) = nullptr;
    };

    /// 表はコピーする。GameThreadで、未完了の通知を配送し終えてから差し替える。nullで既定へ戻す。
    void SetAssetBindingLoaders(const AssetBindingLoaders* loaders);

    /**
     * @brief 論理パスからの要求と結果の所有をまとめる、Componentが値で持つ非反映の状態。
     *
     * 操作と通知はGameThreadで行う。通知はWorldやComponentに触らず、共有状態だけを更新する。
     * モデルはTakeReady後も束縛が所有するため、描画から結果を外してからResetまたは破棄する。
     * テクスチャの所有者はキャッシュであり、この束縛では解放しない。
     */
    class AssetBinding
    {
    public:
        AssetBinding() = default;
        ~AssetBinding();
        AssetBinding(const AssetBinding&) = delete;
        AssetBinding& operator=(const AssetBinding&) = delete;

        void RequestModel(const ModelAssetRef& reference);
        void RequestTexture(const TextureAssetRef& reference);
        void RequestSkeletal(const SkeletalAssetRef& reference);
        /// 外部所有のハンドルを記録する。TakeReadyでは返さず、解放もしない。
        void MarkExternal(AssetKind kind, Container::StringView logicalPath);

        bool TakeReadyModel(Rendering::ModelHandle& outModel);
        bool TakeReadyTexture(Rendering::TextureHandle& outTexture);
        bool TakeReadySkeletal(Container::TSharedPtr<SkeletalAssetResource>& outSkeletal);

        /// モデルの結果を描画から外した後に呼ぶ。未完了の要求は後着通知で片付ける。
        void Reset();
        AssetBindingStatus GetStatus() const;
        bool IsPending() const;
        bool IsWaiting() const;
        bool Matches(AssetKind kind, Container::StringView path) const;
        Container::StringView GetLogicalPath() const;
        Rendering::MegaGeometry::MegaMeshHandle ResolveModelMegaMesh(Rendering::ModelHandle model) const;

    private:
        bool BeginRequest(AssetKind kind, Container::StringView path);
        bool CanTake(AssetKind kind) const;

        Container::TSharedPtr<AssetBindingState> m_State;
        bool m_bTaken = false;
        bool m_bExternal = false;
    };
} // namespace NorvesLib::Core::Asset
