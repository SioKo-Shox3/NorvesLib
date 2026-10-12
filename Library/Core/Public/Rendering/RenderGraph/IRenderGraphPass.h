#pragma once

#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    class IRenderGraphPass
    {
    public:
        virtual ~IRenderGraphPass() = default;

        virtual const char* GetName() const = 0;
        virtual void Declare(RenderGraphBuilder& builder) = 0;
        virtual void Execute(RenderGraphResources& resources, ViewRenderContext& context) = 0;

        // Execute が宣言した最終の状態のとおりにだけ layout を変える（自分でバリアを張らず、
        // render pass の finalState が宣言と一致する）と監査したパスだけ true を返す。
        // true のパスは記録の前に最終の layout まで計画し、記録の最中は共有の layout を更新しない。
        virtual bool DeclaresAllLayoutChanges() const
        {
            return false;
        }

        // 記録の方針（既定は Serial）。Parallel を返してよいのは、Execute が記録だけで、自分でバリアを張らず、
        // CPU の状態（ViewRenderContext のメンバの書き換えを含む）を他のパスと共有しないと監査したパスだけ。
        // Parallel のパスは区間ごとの context（CommandList と PendingFrameCommands が区間のもの）で、
        // ワーカースレッドから呼ばれる。
        virtual RGRecordPolicy GetRecordPolicy() const
        {
            return RGRecordPolicy::Serial;
        }
    };

} // namespace NorvesLib::Core::Rendering
