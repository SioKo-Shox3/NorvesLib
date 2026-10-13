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

        // 記録の方針（既定は Serial）。Parallel を返してよいのは、次を監査したパスだけ。
        // - グラフの Texture の layout は宣言のとおりにだけ変える（DeclaresAllLayoutChanges）。宣言の最終の状態が使う状態と
        //   違う Texture は、パスの中で状態を明示した TextureBarrier で最終の状態へ移してよい。
        // - バッファのバリアは layout を持たないので、パスの中で張ってよい（終わりは宣言の最終の状態にする）。
        // - パスだけが持つ Texture（グラフに取り込まず、同時に記録するほかのパスが触らない）の遷移は
        //   TransitionPassOwnedTexture で張り、共有の layout も書く。
        // - ViewRenderContext のメンバへの公開（PhysicalLighting・SkyAtmosphere など）は PrepareParallelRecord で行うか、
        //   FinishParallelRecord で写した context から戻す。Execute は区間ごとに写した context で呼ばれ、Execute での
        //   書き換えはそのままでは後のパスに届かない。
        // - Execute は単発の提出を伴う転送（Texture の Update・CPU から書けないバッファの Update）をしない（準備段で済ませる）。
        //   資源（バッファ・Texture・記述子セット）の作成と、デバイスのキャッシュが排他するパイプライン・render pass・
        //   framebuffer の作成は、提出・待ちを伴わないのでしてよい。
        // - 同じ並びのほかのパスと同時に書き換える CPU の状態を持たない（読むだけの共有は可）。
        // Parallel のパスは区間ごとの context（CommandList・PendingFrameCommands・FrameResources が区間のもの）で、
        // ワーカースレッドから呼ばれる。FrameResources は区間ごとの借りる口なので、フレームごとのデータ（定数・頂点・
        // 記述子セット）はそこから借りれば、ほかの区間と同じカーソル・pool を共有しない。保留のコマンドは、ほかの区間と
        // 同時に流せる並び（SceneRenderer::CanExecuteFrameCommandsConcurrently）ならワーカーで区間のリストへ流す。
        // 準備段（PrepareParallelRecord）は同時に記録する並びのときだけ呼ばれるので、Execute は準備段を経ない
        // フレーム（1 本への記録・並びが 1 つだけ）でも同じ結果を記録する。
        virtual RGRecordPolicy GetRecordPolicy() const
        {
            return RGRecordPolicy::Serial;
        }

        // 同じフレームで前に記録する earlierPass が Execute で書いた CPU の状態（表・結果・保留の仕事）を、準備段か
        // Execute で読むときだけ true を返す。RenderGraph は同時に記録する並びの前のパスに true を返すパスの前で並びを切り、
        // 前の並びの記録を終えてから、このパスの準備段と記録を始める（GPU の依存はグラフのバリアと提出の順が保つ）。
        virtual bool ReadsRecordingOf(const IRenderGraphPass& earlierPass) const
        {
            (void)earlierPass;
            return false;
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

        // 同時に記録した並びの後に、RenderThread でグラフの順に呼ぶ。recordedContext はこのパスの Execute が使った
        // 写しの context で、Execute が公開した値を context（後のパスが使う）へ戻す。同じ並びのほかのパスの公開は
        // 準備段で context に入っており、写しにも入っているので、写しの公開の構造体をそのまま戻してよい
        // （同じ並びで Execute の公開を戻すパスが同じ構造体を書かないこと）。
        virtual void FinishParallelRecord(const ViewRenderContext& recordedContext, ViewRenderContext& context)
        {
            (void)recordedContext;
            (void)context;
        }
    };

} // namespace NorvesLib::Core::Rendering
