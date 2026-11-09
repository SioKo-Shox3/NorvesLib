#pragma once

#include "Rendering/IViewPass.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class View;

    /** @brief 遅延描画で拡張を差し込める段。 */
    enum class RenderExtensionStage : uint8_t
    {
        GBuffer,
        AfterLighting,
        Transparent,
        PostTemporalAA,
        PostTonemap,
        ShadowCaster
    };

    struct RenderExtensionDesc
    {
        Container::String Name;
        RenderExtensionStage Stage = RenderExtensionStage::GBuffer;
        int32_t Order = 0;
    };

    using RenderExtensionCreateFn = Container::TUniquePtr<IViewPass> (*)(void* context);

    /** @brief Context は登録解除の反映まで有効に保つ。ファクトリから登録表へ再入しない。 */
    struct RenderExtensionFactory
    {
        RenderExtensionCreateFn Create = nullptr;
        void* Context = nullptr;
    };

    /** @brief 拡張の登録と、描画スレッドでの反映を管理する。パスの所有者は View。 */
    class RenderExtensionRegistry
    {
    public:
        explicit RenderExtensionRegistry(View* owner = nullptr);
        ~RenderExtensionRegistry();

        RenderExtensionRegistry(const RenderExtensionRegistry&) = delete;
        RenderExtensionRegistry& operator=(const RenderExtensionRegistry&) = delete;

        /** @brief 初期化時または描画停止中に設定する。旧所有先のパスは先に破棄しておく。 */
        void SetOwner(View* owner);

        /** @brief ゲームスレッドから登録する。最初の ApplyPending より前だけ即時反映する。 */
        bool Register(const RenderExtensionDesc& desc, const RenderExtensionFactory& factory);
        bool Unregister(const char* name);
        bool SetEnabled(const char* name, bool bEnabled);

        /** @brief 描画スレッドで保留操作を反映し、初期化時の即時反映を終了する。 */
        void ApplyPending();

        /** @brief 遅延描画の再構築後、失われたパスだけを再生成する。描画スレッド専用。 */
        void ReinsertAll();

        /** @brief パストレーシングでは挿入を止め、登録があれば一度だけ通知する。 */
        void NotePathTracingPipeline();

        /** @brief 保留中の登録も含む件数。解除は反映した時点で件数から除く。 */
        uint32_t GetRegisteredCount() const;

    private:
        struct State;
        Container::TUniquePtr<State> m_State;
    };
} // namespace NorvesLib::Core::Rendering
