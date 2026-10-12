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
        // Parallel のパスは区間ごとの context（CommandList・PendingFrameCommands・FrameResources が区間のもの）で、
        // ワーカースレッドから呼ばれる。FrameResources は区間ごとの借りる口なので、フレームごとのデータ（定数・頂点・
        // 記述子セット）はそこから借りれば、ほかの区間と同じカーソル・pool を共有しない。
        virtual RGRecordPolicy GetRecordPolicy() const
        {
            return RGRecordPolicy::Serial;
        }

        // Parallel で記録する並びの直前に、RenderThread で直列に呼ぶ準備段（前の Serial のパスを記録した後）。
        // テクスチャの作成・転送（単発のコマンドの提出・待ちとバリアを伴う）や、作り直しの要る render pass・
        // framebuffer・パイプラインはここで済ませ、同じフレームの Execute を記録だけにする。
        // 例外として、Execute（ワーカー）が context.FrameResources から借りるときに、その区間の口のページ・記述子の
        // pool が足りなければワーカーで足してよい。足すのはデバイスでの作成（バッファ・メモリ・マップ・pool）だけで、
        // 提出・待ち・バリアを伴わず、ほかのワーカーと同じ口を使わないため。足した物は持ち主が持ち、回収も手放すのも
        // RenderThread（FrameResourceLane を参照）。
        virtual void PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context)
        {
            (void)resources;
            (void)context;
        }
    };

} // namespace NorvesLib::Core::Rendering
