#pragma once

#include "Rendering/FrameReadback.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/HiZPyramidPass.h"
#include "Rendering/IViewPass.h"
#include "Math/Matrix4x4.h"
#include "Rendering/MegaGeometry/GeometryPageTable.h"
#include "Rendering/MegaGeometry/MegaGeometryCullUniforms.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SwRasterMode.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Debug/DebugConfig.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Debug/DebugConfig.h"
#if NORVES_BUILD_DEVELOPMENT
#include "Thread/Mutex.h"
#endif
#include <cstddef>

#if NORVES_BUILD_DEVELOPMENT
namespace NorvesLib::RHI
{
    struct BufferDesc;
}
#endif

namespace NorvesLib::Core::Rendering
{
    class ConstantMaterialTextureCache;
    class SceneView;
    struct MegaGeometryProxy;
    class SceneRenderer;
    class VisibilityRasterPass;
    class VisibilityResolvePass;
    struct MegaGeometryPassCommand;
    class MegaGeometryResources;

    /** @brief 影の表（binding 18）の印: 影を落とす（bCastShadow）・境界（WorldBounds）が分かる */
    inline constexpr uint32_t MegaGeometryShadowFlagCaster = 1u;
    inline constexpr uint32_t MegaGeometryShadowFlagBounds = 2u;
    /**
     * @brief 影の表の印: 影の LOD を細かく選ぶ（影を詰める地形の面のメッシュ〔MegaTerrainBlend::IsTightSunShadowSurface〕と、
     * 材質の MegaMeshMaterial::bFineShadowLod のメッシュ）
     *
     * 地形は自分の面に自分の影を落とす受け手なので、影の LOD を粗くすると小さな起伏の頂が低くなり、影が短くなる。
     * 細かな起伏の岩（荒野の崖錐の岩）も、粗い段の滑らかな面が光に浅い面の自分への細かな影を消す。
     * この印のインスタンスだけ、許容を VirtualShadowMapMegaCullDispatch::LodThresholdTexels でなく
     * Common/VirtualShadowMapMegaCull.glsl の VSM_MEGA_FINE_LOD_TEXELS にする（太陽の段だけ）。
     */
    inline constexpr uint32_t MegaGeometryShadowFlagFineLod = 4u;

    /**
     * @brief 影の表の1要素（GPU送信用。Common/VirtualShadowMapMegaCull.glsl の ShadowInstance と一致）。インスタンスの表と同じ並び（同じ添字）
     *
     * FirstGroup は影の判定のワークグループ（64 クラスタ）の先頭。BVH と空行には、出し入れに備えた余裕を含む。
     * 影を落とさない行や平らな空行にも、再利用のため以前の割り当てを残すことがある。
     */
    struct alignas(16) MegaGeometryShadowInstance
    {
        float BoundsSphere[4]; // インスタンスの境界（ワールド）の中心 xyz + 半径
        uint32_t FirstGroup;   // 影の判定の最初のワークグループの通し番号
        uint32_t Flags;        // MegaGeometryShadowFlag*
        uint32_t Reserved[2];
        // 頂点・インデックスを持つプールの塊のバッファのデバイスアドレス（下位・上位の順。引けないインスタンスは 0）。
        // クラスタの記録（VSM の影の塊）が、ビジビリティバッファの MegaGeometry の記録と同じ読み方（塊の先頭 + 基点）で頂点を引くのに使う
        uint32_t VertexAddress[2];
        uint32_t IndexAddress[2];
    };
    static_assert(sizeof(MegaGeometryShadowInstance) == 48, "Common/VirtualShadowMapMegaCull.glsl の ShadowInstance と大きさが一致しません");

    /**
     * @brief HZB を作ったときの契約。次のフレームが前のフレームの HZB を遮蔽の判定に使えるかを、これと今のフレームの値で決める
     *
     * HZB 自身が「どのフレームの、どの VP・寸法・シーンで描いた深度か」を持つ。前のゲームパケットのカメラ
     * （PreviousMainCamera）には頼らない（描画がゲームのフレームを飛ばす・描き直すと、前に描いたものと一致しない）。
     */
    struct MegaHiZContract
    {
        bool bValid = false;          // 最後まで作れた HZB か（今のフレームの値として使うときは、作れる見込みがあるか）
        uint64_t RenderFrame = 0;     // 作った記録のフレームの数え（フレームの通し番号が変わるたびに 1 進む）
        uint64_t GameFrame = 0;       // 描いたゲームのフレームの番号（パケットの FrameNumber）
        Math::Matrix4x4 ViewProjection = Math::Matrix4x4::Identity; // 深度を描いた VP（TAA のジッタ込み）
        uint32_t DepthWidth = 0;      // 元の深度の寸法
        uint32_t DepthHeight = 0;
        uint64_t SceneGeneration = 0; // シーンの世代（差分の同期の写しの世代。全量の同期では 0）
        uint64_t SceneResyncCount = 0; // インスタンスを全部作り直した回数（全量の再同期で進む）
        uint64_t CameraId = 0;
        uint64_t SourceCameraId = 0;  // GameThread のカメラの識別（替わったらカメラの切れ目）
        uint32_t ViewId = UINT32_MAX;
        uint32_t ViewportId = UINT32_MAX;
    };

    /** @brief 前のフレームの HZB が使えるか（Ready）と、使えない理由 */
    enum class MegaPrevHiZStatus : uint8_t
    {
        Ready,      // 使える
        Epoch,      // 決定的な撮影のエポックの先頭（時間的な状態を捨てる）
        Missing,    // 前の HZB が無い（最初のフレーム・作れなかった）
        View,       // 別のビュー・ビューポートが作った
        Resize,     // 深度の寸法が替わった
        Stale,      // 直前の記録のフレームで作っていない（遮蔽を使わなかったフレーム・描かなかったフレームを挟んだ）
        PacketGap,  // ゲームのフレームが飛んだ・戻った（パケットの破棄・描画の見送りを挟んだ）
        Generation, // シーンの世代が替わった
        Resync,     // インスタンスを全部作り直した（全量の再同期）
        CameraCut,  // カメラが替わった
    };

    /** @brief ログの理由の文字列（MEGA_PREV_HIZ の reason=） */
    inline const char *ToString(MegaPrevHiZStatus status)
    {
        switch (status)
        {
        case MegaPrevHiZStatus::Ready: return "ready";
        case MegaPrevHiZStatus::Epoch: return "epoch";
        case MegaPrevHiZStatus::Missing: return "missing";
        case MegaPrevHiZStatus::View: return "view";
        case MegaPrevHiZStatus::Resize: return "resize";
        case MegaPrevHiZStatus::Stale: return "stale";
        case MegaPrevHiZStatus::PacketGap: return "packet_gap";
        case MegaPrevHiZStatus::Generation: return "generation";
        case MegaPrevHiZStatus::Resync: return "resync";
        case MegaPrevHiZStatus::CameraCut: return "camera_cut";
        }
        return "unknown";
    }

    /**
     * @brief 前のフレームの HZB（previous の契約）を、今のフレーム（current）の遮蔽の判定に使えるかを決める
     * @param bEpochStart 決定的な撮影のエポックの先頭のフレーム
     *
     * 同じゲームのフレームを描き直す（GameFrame が同じ）のは飛びとしない。VP の違い（カメラの動き・ジッタ）は理由にしない
     * （判定の側が前の VP で投影し直す）。
     */
    inline MegaPrevHiZStatus EvaluatePreviousHiZ(const MegaHiZContract &previous, const MegaHiZContract &current,
                                                 bool bEpochStart)
    {
        if (bEpochStart)
        {
            return MegaPrevHiZStatus::Epoch;
        }
        if (!previous.bValid)
        {
            return MegaPrevHiZStatus::Missing;
        }
        if (previous.ViewId != current.ViewId || previous.ViewportId != current.ViewportId)
        {
            return MegaPrevHiZStatus::View;
        }
        if (previous.DepthWidth != current.DepthWidth || previous.DepthHeight != current.DepthHeight)
        {
            return MegaPrevHiZStatus::Resize;
        }
        if (previous.RenderFrame + 1u != current.RenderFrame)
        {
            return MegaPrevHiZStatus::Stale;
        }
        if (current.GameFrame < previous.GameFrame || current.GameFrame > previous.GameFrame + 1u)
        {
            return MegaPrevHiZStatus::PacketGap;
        }
        if (previous.SceneGeneration != current.SceneGeneration)
        {
            return MegaPrevHiZStatus::Generation;
        }
        if (previous.SceneResyncCount != current.SceneResyncCount)
        {
            return MegaPrevHiZStatus::Resync;
        }
        if (previous.CameraId != current.CameraId || previous.SourceCameraId != current.SourceCameraId)
        {
            return MegaPrevHiZStatus::CameraCut;
        }
        return MegaPrevHiZStatus::Ready;
    }

    /** @brief VSM のキャッシュで描き直すワールドの範囲（AABB） */
    struct MegaGeometryShadowBox
    {
        float Min[3] = {};
        float Max[3] = {};
    };

    /**
     * @brief VSM のキャッシュの無効化の入力。MegaGeometryPass が変わった席・メッシュの変化から積み、VSM が取り出す
     *
     * 席の変換・メッシュ・影の設定が変わったインスタンスは、前に影の表へ載せた境界と今の境界を合わせた箱を 1 つ積む。
     * 静止した席でも、そのメッシュのページの常駐・メッシュの世代（区画・境界）が変わったら、そのメッシュを使う席の境界を積む。
     * 積む数は変わった席の数に比例する（全部の席を毎フレーム比べない）。
     */
    struct MegaGeometryShadowInvalidation
    {
        Container::VariableArray<MegaGeometryShadowBox> Boxes;
        /** @brief 境界の分からない投影物が変わった・積んだ数が上限を超えた（全ページを描き直す） */
        bool bInvalidateAll = false;
    };

    /**
     * @brief VSM の投影物のカリングが読む、このフレームのインスタンスの表などの入力（無ければ bValid が false）
     *
     * 表を書く前段（MegaGeometryPass::Execute。読み手のどのパスよりも前）が作る。
     * インスタンス・影・メッシュの表は device-local の常駐の表で、前段が差分を書いた後に読み取りの状態へ戻してある。
     * ページの表はフレームスロットの host-visible のバッファ。どれも主の経路と同じものを読み取りだけで使うので、
     * 状態の遷移は要らない。
     */
    struct MegaGeometryShadowCasterInputs
    {
        bool bValid = false;
        /** @brief インスタンスの表（GPUMegaInstance[]）と、同じ並びの影の表（MegaGeometryShadowInstance[]） */
        RHI::BufferPtr InstanceBuffer;
        RHI::BufferPtr ShadowInstanceBuffer;
        /** @brief メッシュの表（GPUMegaMesh[]。インスタンスの表の行が番号で指す） */
        RHI::BufferPtr MeshBuffer;
        /** @brief ページの表（ジオメトリのページの常駐。GeometryPageTable::Entry の並び） */
        RHI::BufferPtr PageTableBuffer;
        uint32_t InstanceCount = 0;
        /** @brief 影を落とすインスタンスの数 */
        uint32_t CasterCount = 0;
        /** @brief 影の判定の全ワークグループの数（影を落とすインスタンスのクラスタ ÷ 64 の切り上げの合計） */
        uint32_t TotalGroups = 0;
    };

    /**
     * @brief MegaGeometryパス設定
     */
    struct MegaGeometryPassSettings
    {
        /**
         * @brief 材質の区間1つあたりの IndirectDraw コマンドの最大数（1パスで、その材質を使う全インスタンスの合計）
         *
         * 区間の確保は、その材質を使うインスタンスのクラスタ数の合計とこの値の小さい方。可視のクラスタが
         * これを超えた分は描かれない（開発ビルドは超えた数を MEGA_CAPACITY の draw_dropped に出す。0 が合格）。
         * 既定はバナナ 3 万本の見渡しの視点（可視のクラスタ 約 220 万）に余裕を持たせた値。メモリはコマンド 1 つあたり
         * 間接描画 20 バイト × 2 パス + 描画情報 16 バイト（+ ビジビリティバッファの記録の枠）で、4194304 のとき約 235 MB。
         * 確保は材質の全クラスタ数との小さい方なので小さなシーンでは増えない。
         * 環境変数 NORVES_MEGA_MAX_DRAW_COUNT（正の整数）で替えられる（本数を増やした測定用）。
         */
        uint32_t MaxDrawCount = 4194304;

        /**
         * @brief LODの段を選ぶ誤差の閾値（画素）
         *
         * 段で失われる形の誤差を画面へ投影した大きさがこの値以下になる最も粗い段を描く。
         * 環境変数 NORVES_MEGA_LOD_ERROR_PX（正の数）で替えられる（撮り比べ用）。
         * 1 画素は起動画面の撮り比べで決めた値: 近接（2.5 m）は LOD0、低角度（6 m）は LOD1 が選ばれ、
         * 変更前の段と同じで見た目の差が出ない。これより粗い段（低角度の LOD2）は目地の陰影が目に見えて変わる。
         */
        float LODBias = 1.0f;

        /**
         * @brief グループの BVH（NVMESH v1.1 の焼き込み、または読み込み時に実行時に作ったもの）を持つメッシュで、BVH を
         * たどって判定するか（既定。BVH を作れなかったメッシュだけが平らなクラスタの列で判定する）
         *
         * false なら全メッシュを平らなクラスタの列（cluster_cull.comp）で判定する（BVH をたどった選択と平らな選択の撮り比べ用）。
         * 起動引数 --mega-flat-path、または環境変数 NORVES_MEGA_BVH が 0 か off なら false にする。
         */
        bool bUseGroupBVH = true;

        /**
         * @brief MEGA_OCCLUSION を30フレームごとではなく毎フレーム出すか（フレームごとの揺れを測る撮り比べ用）
         *
         * 環境変数 NORVES_MEGA_STATS_EVERY_FRAME が 1 なら true にする。
         */
        bool bStatsEveryFrame = false;

        /**
         * @brief インスタンスの判定の距離（m）。0 より大きければ、締まった球（範囲は AABB）の最も近い点がこれより遠い席・範囲を描かない
         *
         * 既定は 0（距離では落とさない）。環境変数 NORVES_MEGA_INSTANCE_CULL_DISTANCE（正の数）で替えられる。
         */
        float InstanceCullDistance = 0.0f;

        /**
         * @brief 席の締まった球を包む矩形が画素の中心を 1 つも含まないインスタンスを描かないか（既定。画像は変わらない）
         *
         * 環境変数 NORVES_MEGA_INSTANCE_PIXEL_CULL が 0 か off なら false にする（撮り比べ用）。
         */
        bool bInstancePixelCull = true;

        /**
         * @brief 描画・記録・ソフトの一覧を、可視クラスタの binning の結果で詰めて書くか（既定）
         *
         * true なら判定は全体の列へ積むだけで、接頭和の後に mega_bin_scatter.comp が詰めた並びの位置で書く
         * （ハードの間接描画はハードの区画の分だけ。ソフトへ回したクラスタのコマンドを空振りにしない）。
         * 起動引数 --mega-binning=off なら false（判定が区間ごとのコマンドを書く旧の経路。開発ビルドは新旧を照合する）。
         */
        bool bUseBinning = true;

    };

    /**
     * @brief Mega Geometry 描画パス
     *
     * GPU駆動カリングとIndirect Drawによるクラスタベースジオメトリ描画。
     *
     * パイプラインの位置: ShadowMap → NeuralMaterialDecode → **MegaGeometry** → GBuffer
     *
     * 動作フロー:
     * 1. Setup(): 登録済みMegaMeshリソースを収集
     * 2. 表を書く前段（Execute。読み手のどのパスよりも前）: インスタンスの表（変換・前のフレームの変換・メッシュの表の番号など）・
     *    メッシュの表（クラスタ配列・材質の区間の番号・頂点とインデックスの基点・BVH など）・影の表を書く。
     *    シェーダーは LoadMegaInstance（Common/MegaInstanceTable.glsl）で 2 つの表を合わせて読む
     * 3. 記録（RecordFrameCommand）:
     *    a. インスタンスは材質（と、メッシュを置くプールの塊）ごとの「区間」に分かれ、区間ごとに IndirectDraw
     *       コマンドの連続した範囲とカウンタを持つ（前段を通らなかった記録は、ここで表も書く）
     *    b. 区間ごとのカウンタをゼロクリアし、クラスタカリングを全インスタンスに対して1回ディスパッチ
     *    c. バリア: Compute → IndirectDraw
     *    d. GBufferレンダーパス内で、区間ごとに DrawIndexedIndirectCount を発行
     *
     * 遮蔽カリング（既定で有効。--mega-occlusion=off で上の従来の1回の判定に戻る）は2パスで行う:
     * 1. 1パス目: 視錐台・法線のコーン・LODの判定を通り、前のフレームで見えたクラスタだけを描く（遮蔽の判定はしない）。
     * 2. その時点の深度（GBufferPassの不透明＋1パス目）からHZBを作る。ビジビリティバッファの幾何の解決が GBuffer を書くとき
     *    （--visibility-buffer=on）は GBuffer の描画を止めるので、ID のラスタ（VisibilityRasterPass）が記録を駆動し、
     *    ID・深度へ描いた手続き・スキニングの塊と1パス目の深度から HZB を作る（IDrawSink）。
     * 3. 2パス目: 判定を通った全クラスタをHZBで判定し直し、1パス目で描かなかったもののうち遮蔽されないものを描く。
     *    全クラスタの「見えた」ビット（インスタンスごとに持続する）を更新し、次のフレームの1パス目が使う。
     * 影（CSM・点光源）はこのパスを通らず、カメラの深度で省かない。
     *
     * MegaMeshが未登録の場合はパスが自動的にスキップされます。
     */
    class MegaGeometryPass : public IViewPass, public IRenderGraphPass, private IFrameReadbackReceiver
    {
    public:
        explicit MegaGeometryPass(const MegaGeometryPassSettings &settings = MegaGeometryPassSettings{});
        ~MegaGeometryPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "MegaGeometryPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;
        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;
        bool DeclaresAllLayoutChanges() const override;
        RGRecordPolicy GetRecordPolicy() const override;
        void PrepareParallelRecord(RenderGraphResources &resources, ViewRenderContext &context) override;

        class IDrawSink;

        /**
         * @brief フレームの記録（クラスタカリングと描画）
         * @param sink null なら従来どおり GBuffer の render pass へ描く。null でないとき、GBuffer の添付の状態遷移
         *        （render pass の開始と終了）は呼び出し側が済ませてあるものとして、このパスは GBuffer の render pass を開かず、
         *        2パスの遮蔽のときの描画を sink が行う。1回の判定（2パスにならなかったとき）は sink を呼ばず、
         *        描画の写し（TakeVisibilityDrawPlan）を残して終える
         */
        void RecordFrameCommand(const MegaGeometryPassCommand &command,
                                RHI::ICommandList *commandList,
                                IDrawSink *sink = nullptr);

        // ========================================
        // SceneView連携
        // ========================================

        void SetSceneView(SceneView *sceneView) { m_SceneView = sceneView; }
        void SetSceneRenderer(SceneRenderer *renderer) { m_SceneRenderer = renderer; }

        // ========================================
        // MegaMesh登録
        // ========================================

        /**
         * @brief 描画対象のMegaMeshを追加
         * @param handle MegaMeshHandle
         * @param worldMatrix ワールド変換行列（列優先）
         */
        void AddMegaMeshInstance(MegaGeometry::MegaMeshHandle handle, const float *worldMatrix);

        /**
         * @brief 描画対象のMegaMeshをクリア
         */
        void ClearMegaMeshInstances();

        // ========================================
        // ビジビリティバッファ連携
        // ========================================

        /**
         * @brief そのフレームの描画（クラスタカリングの結果）をビジビリティバッファのラスタが引くための写し
         *
         * 1パス目・2パス目の IndirectDraw コマンド（区間ごとの連続した範囲）と、それを描くのに要るバッファ。
         * RecordFrameCommand が、SetVisibilityDrawPlanEnabled(true) のときだけ作る。
         */
        struct VisibilityDrawPlan
        {
            /** @brief 材質の区間1つ分（頂点・インデックスはプールの塊の先頭から引く） */
            struct Section
            {
                RHI::BufferPtr VertexBuffer;
                RHI::BufferPtr IndexBuffer;
                uint32_t Capacity = 0;    // 1パスのコマンドの最大数
                uint32_t CommandBase = 0; // 1パスの範囲での先頭（コマンドの位置）
                MegaGeometry::MegaMeshMaterial Material; // 区間の材質（値。ビジビリティバッファの材質の表が引く）
            };

            bool bValid = false;
            uint32_t PassCount = 0;        // 1（従来の1回の判定）か 2（2パスの遮蔽）
            uint32_t SectionCount = 0;
            uint32_t CommandsPerPass = 0;  // 2パス目の範囲は、1パス目の範囲の後ろ
            uint32_t CommandsTotal = 0;    // 全パスのコマンドの数
            /**
             * 記録の枠の数（MegaGeometry の分。記録の番号は 1 + 位置）。旧の経路はコマンドの通しの位置で CommandsTotal、
             * 詰めた経路（bCompactRecords）は可視クラスタの詰めた並びの位置 P（ハードとソフトが同じ記録を引く）
             */
            uint32_t RecordSlots = 0;
            uint32_t RecordSlotCeiling = 0; // 詰めた経路の記録枠の天井（CPU の総数を可視列の容量で抑える）
            bool bCompactRecords = false;
            uint32_t RecordCommandBase = 0; // 記録を書く計算が読むコマンドの位置の足し分（詰めた経路は P 番の記録用のコマンドの先頭）
            /**
             * 記録を書く計算の区間の表（uvec2。区間の表の要素ごと）。旧の経路は SectionBuffer（コマンドの先頭・最大数）、
             * 詰めた経路は CountBuffer の後ろの記録の区間の表（P の先頭・数）
             */
            RHI::BufferPtr RecordSectionBuffer;
            uint64_t RecordSectionOffset = 0;
            uint64_t RecordSectionBytes = 0;
            bool bUseIndirectCount = false;
            RHI::BufferPtr IndirectBuffer; // DrawIndexedIndirectCommand[]（IndirectArgument の状態で渡す）
            RHI::BufferPtr CountBuffer;    // 区間ごとのカウンタ（添え字は パス × 区間の数 + 区間。IndirectArgument の状態で渡す）
            RHI::BufferPtr DrawInfoBuffer; // コマンドごとの描画情報（uvec2: インスタンスの番号・payload。GenericRead の状態で渡す）
            RHI::BufferPtr InstanceBuffer; // インスタンスの表（GPUMegaInstance[]。ホストが書いたまま）
            uint64_t InstanceBufferBytes = 0;
            RHI::BufferPtr MeshBuffer;     // メッシュの表（GPUMegaMesh[]。ホストが書いたまま）
            uint64_t MeshBufferBytes = 0;
            RHI::BufferPtr SectionBuffer;  // 区間の表（uvec2: コマンドの先頭・最大数。パス × 区間。ホストが書いたまま）
            uint64_t SectionBufferBytes = 0;
            /**
             * ソフトウェアラスタの一覧（振り分けを行ったフレームだけ。行わなければ null・容量 0）。UnorderedAccess の状態で渡す。
             * パスごとの頭 4 語（間接 dispatch の引数）・8 語の余白の後に、パスごとにコマンドの位置の列が並ぶ（cluster_cull.comp の SwRasterBuffer）
             */
            RHI::BufferPtr SwRasterBuffer;
            uint32_t SwRasterCapacity = 0; // 一覧のパスごとの容量（クラスタ数。パスごとのコマンド数まで）
            float SwRasterMaxPixels = 0.0f; // 振り分けたしきい値（画面上の半径、画素）。ソフトの矩形の上限を決める
            /**
             * 統計のバッファ（UnorderedAccess の状態。bStatsEnabled が偽のときは代わりのバッファ）。ソフトのラスタが、描かなかった三角形・走ったワークグループを数える
             */
            RHI::BufferPtr StatsBuffer;
            bool bStatsEnabled = false;
            Container::VariableArray<Section> Sections;
        };

        /**
         * @brief 2パスの遮蔽の途中で、ID・深度へ描く側（VisibilityRasterPass）が呼ばれる受け口
         *
         * 幾何の解決が GBuffer を書くとき、2パスの遮蔽の HZB は GBuffer ではなく ID のラスタの深度から作る。そのため
         * RecordFrameCommand が次の順で呼び出す（sink を渡したときだけ）:
         *   1パス目のカリング → RecordFirstPassDraws → RecordMergeBeforeHiZ → HZB の生成 → 2パス目のカリング → RecordSecondPassDraws
         * 描画の写し plan は両方の呼び出しで同じ内容（1パス目のカリングの前に作る）。バッファの状態は、1回目の呼び出しの時点で
         * 1パス目の範囲が IndirectArgument（描画情報は GenericRead）、2回目の時点で2パス目の範囲まで書き終えている。
         * 呼び出しの間に sink が必要とする状態の遷移は sink が行い、RecordFrameCommand が戻った後の戻し
         * （ReleaseVisibilityDrawBuffers）も sink 側の責任になる。
         *
         * ソフトウェアラスタの一覧（plan.SwRasterBuffer）は、sink が読むとき（間接 dispatch の引数と一覧の読み取り）だけ GenericRead にし、
         * 戻る時点では UnorderedAccess に戻す（次のカリングが続けて書き、最後に RecordFrameCommand が Common へ戻す）。
         */
        class IDrawSink
        {
        public:
            virtual ~IDrawSink() = default;

            /**
             * @brief このフレームのソフトウェアラスタ（一覧のクラスタを計算シェーダーで 64bit のバッファへ描く）を準備し、確実に走るかを返す
             *
             * RecordFrameCommand が振り分けるフレームのカリングの前に 1 回だけ呼ぶ。sink は dispatch と合流に要る資源を
             * ここで作り、作れたときだけ true を返す。true を返したら、sink は 2 回の呼び出しの中で一覧を dispatch する責任を負い、
             * 一覧へ積めたクラスタはハードが描かない（旧の経路はコマンドを空振り（instanceCount = 0）にし、詰めた経路はハードのコマンドを書かない）。
             * false なら、一覧へ積むだけでハードがすべてのクラスタを描く（振り分けの統計だけが取れる）。
             */
            virtual bool PrepareSwRaster() = 0;

            /** @brief PrepareSwRaster が準備できたと答えたフレームか（記録の途中で何度でも問い合わせてよい） */
            virtual bool IsSwRasterAvailable() const = 0;

            /** @brief これまでに記録したソフトウェアラスタの dispatch の累計（確認用。フレームの差を統計に載せる）。ソフトを持たない sink は 0 */
            virtual uint32_t GetSwRasterDispatchCount() const { return 0; }

            /** @brief 1パス目のカリングの後。ID・深度へ手続き・スキニングの塊と1パス目を描く（深度は描いた後 ShaderResource になる） */
            virtual void RecordFirstPassDraws(RHI::ICommandList *commandList, const VisibilityDrawPlan &plan) = 0;
            /**
             * @brief 1回目の描画の後・HZB を作る前。ハードのラスタの外で書かれた ID・深度（ソフトウェアラスタの 64bit のバッファ）を、
             *        HZB の元の深度へ合流させる。2 パスの遮蔽の構成では毎フレーム呼ばれる（RecordFirstPassDraws が描画を記録できなかったフレームは、sink 側で何もしない）
             *
             * 深度は呼ばれる時点と戻る時点のどちらも ShaderResource の状態（HZB がそのまま読む）。
             */
            virtual void RecordMergeBeforeHiZ(RHI::ICommandList *commandList, const VisibilityDrawPlan &plan) = 0;
            /** @brief 2パス目のカリングの後。ID・深度へ2パス目を描く */
            virtual void RecordSecondPassDraws(RHI::ICommandList *commandList, const VisibilityDrawPlan &plan) = 0;
        };

        /**
         * @brief ビジビリティバッファの描画の写しを作るか（既定は作らない）
         *
         * 作るとき、RecordFrameCommand は IndirectDraw・カウンタ・描画情報のバッファを次のフレーム用に戻さず、
         * ビジビリティバッファのラスタが読めるまま残す（ラスタが使い終わったら ReleaseVisibilityDrawBuffers で戻す）。
         */
        void SetVisibilityDrawPlanEnabled(bool bEnabled) { m_bVisibilityPlanEnabled = bEnabled; }
        bool IsVisibilityDrawPlanEnabled() const { return m_bVisibilityPlanEnabled; }

        /**
         * @brief ソフトウェアラスタの振り分けを要求するか（--sw-raster=on。既定は要求しない）
         *
         * 要求しても、64bit アトミックが使えない・ビジビリティバッファのラスタが無い・2 パスの遮蔽の判定を使えないときは、
         * 振り分けず SW_RASTER_FALLBACK reason=<理由> を 1 回ログへ出す。使えるとき、カリングは画面上の半径（画素）が
         * maxPixels 以下で近平面と交わらないクラスタを、パスごとのソフトの一覧（コマンドの位置）へ積み、計算シェーダーの
         * 間接 dispatch の引数（2 次元）を書く。sink（IDrawSink::IsSwRasterAvailable）がソフトのラスタを走らせると答えたフレームは、
         * 一覧へ積めたクラスタのハードのコマンドを空振り（instanceCount = 0）にする。答えなかったフレームは、
         * ハードがすべてのクラスタを描く（コマンドは減らさない）。
         */
        void SetSwRasterBinning(bool bRequested, float maxPixels)
        {
            m_bSwRasterRequested = bRequested;
            m_SwRasterMaxPixels = maxPixels;
        }
        bool IsSwRasterBinningRequested() const { return m_bSwRasterRequested; }

        /**
         * @brief 設定されている振り分けのしきい値（画面上の半径、画素）
         *
         * 製品の経路はこの getter を通らない（カリングの定数と VisibilityDrawPlan へ内部の値を直接入れる）。
         * SceneView が組んだ設定が MegaGeometryPass まで届いたことを、テストが読んで確かめるために置いている
         */
        float GetSwRasterMaxPixels() const { return m_SwRasterMaxPixels; }

        /** @brief 最後の RecordFrameCommand がソフトウェアラスタの振り分けを行ったか */
        bool DidSwRasterBin() const { return m_bSwRasterBinned; }

        /** @brief ソフトの一覧のパスごとの容量（クラスタ数）。振り分けを行っていなければ 0 */
        uint32_t GetSwRasterListCapacity() const { return m_SwRasterCapacity; }

        /**
         * @brief GBuffer への描画を止めるか（既定は止めない）
         *
         * ビジビリティバッファの幾何の解決が GBuffer を書くとき（--visibility-buffer=on）に true にする。止めても、
         * カリング・遮蔽の判定・描画の写しの作成・render pass の開始と終了（GBuffer の添付の状態遷移）は行い、描画の呼び出しだけを省く。
         * 解決が実際に使えないとき（SetVisibilityResolvePass の相手の GetFallbackReason が None 以外。相手を渡していないときは
         * 装置が VisibilityResolveGeometry::IsSupported を満たさないとき）は、true でも描く。
         *
         * 2 パスの遮蔽の HZB は、止めている間は ID のラスタの深度から作る（SetVisibilityRasterPass で渡した相手が描くとき。
         * IsFrameRecordDeferred）。渡していない・相手が描けないときは GBuffer の深度から作るので、1 パス目のクラスタが
         * 深度に入らず、判定は隠れていない側（描く側）に倒れるだけで、見える物は欠けない。
         */
        void SetSkipGBufferDraw(bool bSkip) { m_bSkipGBufferDraw = bSkip; }
        bool IsSkipGBufferDraw() const { return m_bSkipGBufferDraw; }

        /** @brief 記録したフレームの数（フレームの通し番号が変わるたびに 1 進む。通し番号が無い記録は記録ごとに 1 進む） */
        uint64_t GetRenderFrameCount() const { return m_RenderFrameCount; }
        /** @brief 最後に記録した 2 パスのフレームで、前のフレームの HZB が使えたか（Ready）と使えない理由 */
        MegaPrevHiZStatus GetPreviousHiZStatus() const { return m_PrevHiZStatus; }
        /** @brief フレームごとの資源の組の数（選んだ飛行中のフレームの番号の枠。検査用） */
        uint32_t GetFrameSlotCapacity() const { return m_FrameSlots.GetCapacity(); }
        /** @brief 手放して GPU の使い終わりを待っているバッファの数（検査用） */
        size_t GetRetiredBufferCount() const { return m_RetiredBuffers.size(); }

#if NORVES_BUILD_DEVELOPMENT
        /** @brief GPU Scene の確保を弱参照で記録する。同じ所有権のバッファは重複して数えない */
        void NoteGpuSceneBuffer(const RHI::BufferPtr& buffer, const RHI::BufferDesc& desc, bool bDevelopmentOnly);
#endif
        /**
         * @brief ID・深度へ描く相手（同じ View の VisibilityRasterPass）。渡すと、解決が GBuffer を書くフレームは、
         *        相手の Execute が記録を駆動する（Execute(context) は記録をキューへ積まず、GBuffer の添付の状態遷移だけを行う）
         */
        void SetVisibilityRasterPass(const VisibilityRasterPass *pass) { m_VisibilityRaster = pass; }
        const VisibilityRasterPass *GetVisibilityRasterPass() const { return m_VisibilityRaster; }

        /**
         * @brief このフレームの記録を ID のラスタへ移したか（Execute(context) が決める。取り出すと false に戻る）
         *
         * true のとき、ラスタが BuildMegaGeometryPassCommand で作ったコマンドを RecordFrameCommand（sink 付き）へ渡す。
         */
        bool TakeFrameRecordDeferred()
        {
            const bool bDeferred = m_bFrameRecordDeferred;
            m_bFrameRecordDeferred = false;
            return bDeferred;
        }
        bool IsFrameRecordDeferred() const { return m_bFrameRecordDeferred; }

        /** @brief 解決が使えるかの問い合わせ先（同じ View の VisibilityResolvePass。null なら装置の機能だけで判定） */
        void SetVisibilityResolvePass(const VisibilityResolvePass* pass) { m_ResolvePass = pass; }
        const VisibilityResolvePass* GetVisibilityResolvePass() const { return m_ResolvePass; }

        /**
         * @brief 最後の RecordFrameCommand が作った描画の写しを取り出す（取り出すと空になる）
         * @return 写しがあれば true。そのフレームに描いたクラスタが無い・写しを作らない設定なら false
         */
        bool TakeVisibilityDrawPlan(VisibilityDrawPlan &outPlan);

        /**
         * @brief 残してあった IndirectDraw・カウンタ・描画情報のバッファを、次のフレーム用の状態（Common）へ戻す
         *
         * ビジビリティバッファのラスタが使い終わった後に呼ぶ。次の RecordFrameCommand は、残っていれば自分で戻す。
         * @param indirectState IndirectDraw・カウンタの今の状態（渡されたまま触らなければ IndirectArgument）。
         *        描画情報は GenericRead
         */
        void ReleaseVisibilityDrawBuffers(RHI::ICommandList *commandList,
                                          RHI::ResourceState indirectState = RHI::ResourceState::IndirectArgument);

        RGResourceHandle GetIndirectDrawBufferHandle() const { return m_IndirectDrawBufferHandle; }
        RGResourceHandle GetDrawCountBufferHandle() const { return m_DrawCountBufferHandle; }
        RGResourceHandle GetMegaGeometryCompleteHandle() const { return m_MegaGeometryCompleteHandle; }

        /**
         * @brief VSM の投影物（bCastShadow のインスタンス）のカリングの入力。表を書く前段（Execute）か記録が作る
         *
         * Setup（フレームの最初）で無効に戻り、インスタンスの表と影の表を書けたときだけ有効になる。
         */
        const MegaGeometryShadowCasterInputs &GetShadowCasterInputs() const { return m_ShadowCasterInputs; }

        /**
         * @brief 前に取り出してから積んだ VSM のキャッシュの無効化を out へ移し、積んだ分を空にする
         *
         * 表を作るたびに積み、VSM が取り出すまで溜める（VSM が無い・見送ったフレームの分も次に取り出す）。
         */
        void TakeShadowInvalidation(MegaGeometryShadowInvalidation &out);

    private:
        /** @brief カリング用ユニフォームデータ（Common/MegaGeometryCull.glsl の CullUniforms と一致。VSM の投影物のカリングと同じ構造を使う） */
        using CullUniformData = MegaGeometry::CullUniformData;

        /**
         * @brief 描く順のインスタンスの表の1要素（Common/MegaInstanceTable.glsl の MegaInstanceRow と一致）
         *
         * インスタンスごとに変わる値だけを持つ。メッシュごとに決まる値はメッシュの表（GPUMegaMesh）に置き、MeshIndex で指す。
         * シェーダーは LoadMegaInstance(index) で 2 つの表を合わせて読む。この表は GPU が席の表（GPUMegaSlotRow）と
         * 描く順の記録（GPUMegaEntry）から組む（mega_scene_scatter.comp）。ホストはこの形で書かない。
         */
        struct alignas(16) GPUMegaInstance
        {
            float WorldMatrix[16];         // ワールド変換（行ベクトル規約の列優先の並びのまま）
            float PreviousWorldMatrix[16]; // 直前のフレームの変換（velocity 用）
            uint32_t MeshIndex;            // メッシュの表の番号
            uint32_t FirstGroup;           // このインスタンスの最初のワークグループの通し番号
            uint32_t Reserved0;
            uint32_t Reserved1;
        };
        static_assert(sizeof(GPUMegaInstance) == 144, "Common/MegaInstanceTable.glsl の MegaInstanceRow と大きさが一致しません");

        /**
         * @brief メッシュの表の1要素（GPU送信用。Common/MegaInstanceTable.glsl の MegaMeshEntry と一致）
         */
        struct alignas(16) GPUMegaMesh
        {
            float LODSphere[4];            // LODの選択に使うメッシュ共通の境界球（ローカル。半径0ならクラスタごと）
            uint32_t ClusterAddressLow;    // クラスタ配列のデバイスアドレス
            uint32_t ClusterAddressHigh;
            uint32_t ClusterCount;
            float MainLodFloorPixels;      // 主のカメラの LOD の許容の下限（画素。0 なら無い。MegaMeshMaterial::MainLodFloorPixels）
            uint32_t SectionIndex;         // 材質の区間の番号
            uint32_t VertexBase;           // 頂点の基点（プールの塊の先頭から。頂点単位）
            uint32_t IndexBase;            // インデックスの基点（プールの塊の先頭から。インデックス単位）
            uint32_t Reserved1;
            uint32_t BvhAddressLow;        // グループの BVH の節の配列のデバイスアドレス（BVH をたどらないメッシュは 0）
            uint32_t BvhAddressHigh;
            uint32_t BvhNodeCount;
            uint32_t PageTableBase;        // ページの表の中の、このメッシュのページの範囲の先頭
            float BoundsSphere[4];         // メッシュ全体の締まった境界球（ローカル。半径 0 なら無い）。インスタンスと範囲の判定が使う
        };
        static_assert(sizeof(GPUMegaMesh) == 80, "Common/MegaInstanceTable.glsl の MegaMeshEntry と大きさが一致しません");

        /**
         * @brief 席の表の1要素（席ごと 128 B。mega_scene_scatter.comp の SceneSlotRows と一致）
         *
         * 席の番号は行の席（MegaGeometryProxy::SceneSlot）。0 は空行のために予約する。行と一緒に動き、その実体が消えるまで変わらないので、
         * 動いた席の行だけを書き直せば、描く順が変わっても行を送り直さずに済む。
         */
        struct alignas(16) GPUMegaSlotRow
        {
            float WorldMatrix[16];         // ワールド変換（行ベクトル規約の列優先の並びのまま）
            float PreviousWorldMatrix[16]; // 直前のフレームの変換（velocity 用）
        };
        static_assert(sizeof(GPUMegaSlotRow) == 128, "mega_scene_scatter.comp の席の表の行と大きさが一致しません");

        /** @brief 描く順の表の 1 行の出所（席・メッシュの表の番号・ワークグループ）。CPU 側の値 */
        struct GPUMegaEntry
        {
            uint32_t Slot = 0;
            uint32_t MeshIndex = 0;
            uint32_t FirstGroup = 0;
        };

        /**
         * @brief MegaMeshインスタンス情報
         */
        struct MegaMeshInstance
        {
            // 影のキャッシュの記録を引き継ぐ鍵（プロキシのObjectId。0のインスタンスは並びの番号で代用する）
            uint64_t ObjectId = 0;
            // コンポーネントの世代（作り直されたら変わる）。同じ ObjectId でも別のコンポーネントなら引き継がない
            uint64_t ComponentId = 0;
            MegaGeometry::MegaMeshHandle Handle;
            float WorldMatrix[16];
            float PreviousWorldMatrix[16];
            // 影: 影を落とすか（プロキシの bCastShadow）と、ワールドの境界（プロキシの WorldBounds。半径が 0 以下なら分からない）
            bool bCastShadow = true;
            BoundingSphere WorldBounds;
            // 行の席（プロキシの SceneSlot。席の番号 + 1、0 は席を持たない）と、席の表の番号（AssignSceneTableSlots が決める）
            uint32_t SceneSlot = 0;
            uint32_t TableSlot = 0;
        };

        /** @brief 表を作るまで残す、インスタンスの並びの変化 */
        enum class SceneInstanceDeltaKind : uint8_t
        {
            Removed,
            Moved,
            Added
        };
        struct SceneInstanceDelta
        {
            SceneInstanceDeltaKind Kind = SceneInstanceDeltaKind::Added;
            uint32_t PreviousPosition = 0xFFFFFFFFu;
            uint32_t PreviousRow = 0xFFFFFFFFu;
            uint32_t Position = 0xFFFFFFFFu;
        };

        /** @brief proxy から描くインスタンスを作る */
        static MegaMeshInstance MakeMegaMeshInstance(const MegaGeometryProxy &proxy);

        /**
         * @brief RenderThread の写しの、変わった席のインスタンスだけを直す
         *
         * 移動・解放・追加は変わった位置と切った末尾だけで見分け、位置ごとの記録を運ぶ。
         * 席の欠落・重複や追跡できない並びは、状態を変えずに false を返す（呼び出し側が作り直す）。
         */
        bool PatchInstancesFromChangedSlots(const ViewRenderContext &context);

        /**
         * @brief カリング用GPUリソースを作成
         */
        bool CreateCullResources(RHI::IDevice *device);

        /** @brief カリングのディスクリプタセットの形（cluster_cull.comp の binding 0〜13）。パイプラインとセットが同じ形を使う */
        static RHI::DescriptorSetDesc BuildCullDescriptorSetDesc();

        /** @brief 描画のディスクリプタセットの形（megageometry.vert/frag の binding 0〜9）。パイプラインとセットが同じ形を使う */
        RHI::DescriptorSetDesc BuildDrawDescriptorSetDesc() const;

        /**
         * @brief GBuffer互換のグラフィックスパイプラインを作成
         */
        bool CreateDrawPipeline(ViewRenderContext &context,
                                bool bRequireDrawPipeline = true,
                                bool bUseRenderGraphAttachmentStates = false);
        bool CreateDrawPipelineVariant(RHI::PolygonMode polygonMode, RHI::PipelinePtr &outPipeline);
        RHI::PipelinePtr SelectDrawPipeline(DebugViewMode mode) const;

        /**
         * @brief メッシュ共通のLOD球を持つメッシュの選ばれる段を、GPUと同じ式で求めて変わったときに記録する
         */
        void LogUniformLODSelection(const MegaMeshInstance &instance,
                                    const MegaGeometry::MegaMeshGPUData &gpuData,
                                    const CullUniformData &uniformData);

        /** @brief 材質の区間1つ分の描画資源（フレームスロットが持つ） */
        struct SectionDraw
        {
            RHI::BufferPtr Uniform;              // 材質の区間ごとの定数（PerObject UBO）
            RHI::DescriptorSetPtr DescriptorSet; // 描画用（UBO・材質のテクスチャ・VTの要求・インスタンスの表・描画情報）
            uint64_t BufferRevision = 0; // 最後に結んだ GPU Scene バッファの版
        };

        /** @brief BVH のたどりの1段分の資源（カリング用UBOと、その UBO を結んだディスクリプタセット） */
        struct BvhStageDraw
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
            uint64_t BufferRevision = 0; // 最後に結んだ GPU Scene バッファの版
        };

        /** @brief ホストが毎フレーム書く資源の組。1 回の記録（ビューポート）ごとに FrameUseRing から 1 組使う */
        struct FrameSlot
        {
            // 常駐の表へ散在書き込みする記録の列（host-visible の staging）と、段ごとのディスクリプタセット
            RHI::BufferPtr SceneStaging;
            uint64_t SceneStagingCapacity = 0; // バイト数
            RHI::DescriptorSetPtr SceneScatterDescriptorSet[4];
            uint64_t SceneScatterBufferRevision[4] = {};
            // この組を使った記録のメッシュの表が指すメッシュの区画（頂点・インデックス・クラスタ・BVH）の持ち主。
            // 組を次に使うとき（その記録の提出の GPU の仕事が終わった後）まで保ち、区画を空きへ戻さない
            Container::VariableArray<Container::TSharedPtr<void>> MeshRegionOwners;
            // 開発ビルドの検査: 表を広げた更新の後に読み戻した常駐の表と、そのときの写し（組を次に使うときに照合する）
            RHI::BufferPtr SceneCheckReadback;
            uint64_t SceneCheckCapacity = 0; // バイト数
            bool bSceneCheckPending = false;
            uint32_t SceneCheckSlotRows = 0;  // 席の表の先頭から照合する行の数
            uint32_t SceneCheckEntryRows = 0; // 描く順の表・影の表の先頭から照合する行の数
            uint32_t SceneCheckMeshRows = 0;  // メッシュの表の先頭から照合する行の数
            Container::VariableArray<uint8_t> SceneCheckExpected; // 読み戻しと同じ並びの期待するバイト列
            Container::VariableArray<uint8_t> SceneCheckSlotMask; // 席の行ごとに照合するか（GPU の表にある行だけ 1）
            RHI::BufferPtr SectionBuffer;  // 区間の表（uvec2: コマンドの先頭・最大数。host-visible）
            uint32_t SectionCapacity = 0;  // 要素数
            // ページの表（GeometryPageTable::Entry の並び。host-visible）。このフレームの常駐を固定して、
            // 2パスの間で判定が食い違わないようにする。版が変わったときだけホストが書き直す（GPU は要求の印を書く）
            RHI::BufferPtr PageTableBuffer;
            uint32_t PageTableCapacity = 0; // 要素数
            uint64_t PageTableVersion = 0;  // バッファの中身の版（PageTableBuffer が無いときは意味を持たない）
            RHI::BufferPtr CullUniform[2]; // パスごとのカリング用UBO
            RHI::DescriptorSetPtr CullDescriptorSet[2];
            uint64_t CullBufferRevision[2] = {};
            // BVH のたどり: パスごとに、節の判定の段 + 葉のクラスタの判定の段 + 範囲の段（2 パス目はさらに回した範囲・節・クラスタの段）
            // の数だけ（段ごとにUBOの中身が違うため別々に持つ）
            Container::VariableArray<BvhStageDraw> BvhStages[2];
            Container::VariableArray<SectionDraw> Sections;
        };

        /** @brief GPU が読み終えた組から、置き換え前のバッファを結んだセットを手放す */
        void ReleaseStaleFrameSlotDescriptors(FrameSlot &slot);

        /** @brief フレームスロットに、このフレームの区間の数を収める資源（区間の表・カリングと描画の UBO・ディスクリプタセット）を用意する */
        bool EnsureFrameSlot(FrameSlot &slot, uint32_t sectionTableEntries, uint32_t sectionCount);

        /** @brief 常駐の表の散在書き込みのディスクリプタセットの形（mega_scene_scatter.comp の binding 0〜5） */
        static RHI::DescriptorSetDesc BuildSceneScatterDescriptorSetDesc();

        /**
         * @brief 常駐の表（device-local）を、required 個の要素を収める大きさにする
         *
         * 足りないときだけ作り直す。古いバッファがあれば、その中身を新しいバッファの先頭へ GPU でコピーする記録を
         * commandList へ積み（同じキューの前のフレームの書き込みの後、読み手の前）、古いバッファは m_RetiredBuffers へ回す
         * （前のフレームの GPU がまだ読んでいるかもしれない）。新しいバッファは読み取りの状態（GenericRead）で返す。
         * @param bOutGrown 作り直したら true
         * @param bOutCopied 古いバッファの中身をコピーしたら true（false で作り直したなら、新しいバッファは中身を持たない）
         * @return false ならバッファを作れなかった
         */
        bool EnsureResidentTable(RHI::BufferPtr &buffer, uint32_t &capacity, uint32_t required, uint32_t minimumCapacity,
                                 uint64_t elementBytes, const char *debugName, RHI::ICommandList *commandList,
                                 bool &bOutGrown, bool &bOutCopied);

        /**
         * @brief 材質の区間: 同じ材質（値とテクスチャ）で、メッシュを置くプールの塊が同じインスタンスの集まり。区間ごとに
         * IndirectDraw コマンドの連続した範囲・カウンタ・描画のディスクリプタセットを持ち、1回の間接描画で描く
         */
        struct SceneSection
        {
            const MegaGeometry::MegaMeshGPUData *Representative = nullptr; // 材質・プールの塊の持ち主
            uint64_t ClusterTotal = 0;                                     // 区間のインスタンスのクラスタ数の合計
            uint32_t Capacity = 0;                                         // 1パスのコマンドの最大数（記録が決める）
            uint32_t CommandBase = 0;                                      // 1パス目の範囲での先頭（コマンドの位置。記録が決める）
        };

        /** @brief インスタンスの表の1行の出所（m_Instances の番号と、ロード済みのメッシュ） */
        struct SceneDrawable
        {
            size_t InstanceIndex = 0;
            const MegaGeometry::MegaMeshGPUData *GpuData = nullptr;
        };

        /**
         * @brief 1 フレームの描く順の表・メッシュの表・影の表と、記録がそれから決める規模（CPU 側の値）
         *
         * 表は読み手のどのパス（主のカリングと描画・ビジビリティバッファ・VSM の影）よりも前の共通の前段
         * （PrepareSceneTables）で作り、常駐の表へ差分だけを書く。記録はそれを使い、前段を通らなかった記録
         * （同じフレームの 2 つ目のビューポート・コマンドを直接組んだ呼び出し）は自分で作って差分を書く。
         */
        struct SceneTables
        {
            Container::VariableArray<SceneSection> Sections;
            Container::VariableArray<SceneDrawable> Drawables;
            // 描く順の表の行の出所（GPU が席の表から MegaInstanceRow を組む）
            Container::VariableArray<GPUMegaEntry> Entries;
            Container::VariableArray<GPUMegaMesh> Meshes;
            // メッシュの表の行と同じ並びの、メッシュの区画の持ち主（MegaMeshGPUData::RegionOwner）
            Container::VariableArray<Container::TSharedPtr<void>> MeshOwners;
            // 影の表（インスタンスの表と同じ並び）。BVH の領域は一律、平らな行は投影物だけに割り当てる
            Container::VariableArray<MegaGeometryShadowInstance> ShadowInstances;
            uint64_t ShadowTotalGroups = 0;
            uint32_t ShadowCasterCount = 0;
            // 平らな判定の全ワークグループの数（BVH をたどるインスタンスは持たない）
            uint64_t TotalGroups = 0;
            // BVH のたどりの規模: BVH を持つインスタンスの数と、段ごとの節の数・葉の数のインスタンスの合計（列の大きさになる）
            uint32_t BvhInstanceCount = 0;
            // BVH の直後に置く空行の数と、BVH の領域の 1 行あたりの影の割り当て
            uint32_t BvhSpareCount = 0;
            uint32_t BvhShadowGroups = 0;
            // 空行だけが読む固定変換の席（実インスタンスの席とは共有しない）
            uint32_t EmptySlot = 0;
            // 一律の影の割り当てが 32 ビットに収まらないときは、予備なしで投影物だけを詰める
            bool bUniformBvhShadow = true;
            bool bHasEmptyMesh = false;
            Container::VariableArray<uint64_t> BvhLevelCapacity;
            uint64_t BvhLeafCapacity = 0;
        };

        /** @brief 前段で表を書いたフレームの印（記録が同じフレーム・同じ枠なら、その表とスロットを使う） */
        struct PreparedSceneTables
        {
            bool bPrepared = false;
            uint64_t FrameSerial = 0;
            uint32_t InFlightIndex = 0;
            FrameSlot *Slot = nullptr;
        };

        /**
         * @brief m_Instances とロード済みのメッシュから、このフレームの表を作る（CPU だけ。GPU のバッファには書かない）
         *
         * インスタンスの並びは BVH をたどるインスタンスが先。
         * 前に作った表（m_SceneTables）を持ち続け、メッシュの準備や中身・BVH の状態が変わらないときは、出し入れと
         * 直したインスタンスの行だけを書き換える（PatchSceneTables）。
         */
        void BuildSceneTables(const MegaGeometryPassCommand &command, SceneTables &tables);

        /** @brief 表の差分更新: 表へ載せたメッシュ 1 つの出所（全部を作ったときの中身。変わったら表を作り直す） */
        struct SceneMeshSource
        {
            MegaGeometry::MegaMeshHandle Handle;
            const MegaGeometry::MegaMeshGPUData *GpuData = nullptr;
            const void *VertexBuffer = nullptr;
            const void *IndexBuffer = nullptr;
            const void *ClusterBuffer = nullptr;
            const void *RegionOwner = nullptr;
            GPUMegaMesh Row{};
            float TotalBounds[4] = {};
            bool bTraverseBvh = false;
            uint32_t BvhLeafCount = 0;
            Container::VariableArray<uint32_t> BvhLevelNodeCounts;
        };

        /** @brief 表の差分更新: 表へ載らなかったインスタンスのメッシュ（準備前・クラスタ無し・引けない）を見たときの状態 */
        struct SceneSkippedMesh
        {
            MegaGeometry::MegaMeshHandle Handle;
            const MegaGeometry::MegaMeshGPUData *GpuData = nullptr;
            uint32_t ClusterCount = 0;
        };

        /**
         * @brief 前に作った表の、直したインスタンスの行だけを書き換える
         *
         * 先に全部を確かめ、表・メッシュ・予備の状態や割り当ての容量から作り直しが要るなら、何も変えずに false を返す。
         * VSM の無効化も、直した行とページの常駐が
         * 変わったメッシュの行だけを積む。
         */
        bool PatchSceneTables(const MegaGeometryPassCommand &command, SceneTables &tables, bool bResidencyFresh);

        /** @brief 表の差分更新: 表の行に効く中身（メッシュ・コンポーネント・影の設定・変換・境界）が変わった位置を覚える */
        void MarkSceneTableDirty(size_t position);

        /**
         * @brief 開発ビルドの検査: 一定の回数ごとに、全部を作り直した表と今の表（差分で直した表）を比べ、MEGA_TABLES に出す
         */
        void CheckSceneTables(const MegaGeometryPassCommand &command, const SceneTables &tables);

        /** @brief グループの BVH をたどれる状態か（設定・パイプライン・列のバッファ） */
        bool IsSceneBvhAvailable() const;
        /** @brief このメッシュをグループの BVH でたどれるか（IsSceneBvhAvailable と BVH の中身） */
        bool CanTraverseSceneBvh(const MegaGeometry::MegaMeshGPUData *data) const;
        /** @brief メッシュの表の 1 行を組む（区画のアドレスと基点は引けることを確かめた値） */
        static GPUMegaMesh MakeSceneMeshRow(const MegaGeometry::MegaMeshGPUData &gpuData, uint64_t clusterAddress,
                                            uint64_t vertexBase, uint64_t indexBase, uint32_t sectionIndex, bool bTraverseBvh);

        /**
         * @brief m_Instances の席の表の番号（TableSlot）を決める（m_bSceneSlotsDirty のときだけ）
         *
         * 0 番を空行に予約し、行の席（SceneSlot）をそのまま使う。席を持たないインスタンスと、同じ席を持つ
         * 2 つ目以降のインスタンスは、行の席の最大の次から順に割り当てる。
         */
        void AssignSceneTableSlots();

        /**
         * @brief 作った表と常駐の表の写しを比べ、変わった行だけを staging から常駐の表へ散在書き込みする
         *
         * 3 つの更新の条件を分ける: 席の表は変わった席（Setup が記録したインスタンスの位置、作り直したフレームは全部）の
         * 行のうち中身が違うもの、描く順の表はその並びの出所が変わった行と席の行が変わった行、メッシュの表は中身が変わったとき。
         * 常駐（ページの表）はフレームスロットの版で別に揃える（SyncPageTable）。書くものが無ければ何も記録しない。
         * commandList へ記録するので、表を読むどのパスよりも前に呼ぶこと。
         * @param pageTableBytes このフレームにページの表へ書いたバイト数（統計 MEGA_SCENE_UPLOAD に足す）
         * @return false なら表を書けなかった（このフレームは描けない）
         */
        bool UpdateResidentSceneTables(FrameSlot &slot, const SceneTables &tables, RHI::ICommandList *commandList,
                                       uint64_t pageTableBytes);

        /**
         * @brief 開発ビルドの検査: 表を広げた更新の後の常駐の表を、この組の読み戻しのバッファへコピーする記録を積む
         *
         * そのときの写し（GPU の表にあるはずの中身）を組に覚え、組を次に使うときに ResolveSceneTableCheck が照合する。
         * 表ごとに先頭の SceneCheckMaxRows 行までを見る。
         */
        void RecordSceneTableCheck(FrameSlot &slot, RHI::ICommandList *commandList, uint32_t entryCount, uint32_t meshCount);

        /** @brief 開発ビルドの検査: この組の読み戻しを覚えた写しと照合し、食い違った行を MEGA_SCENE_GROW_CHECK に出す */
        void ResolveSceneTableCheck(FrameSlot &slot);

        /**
         * @brief インスタンスの無いフレームで、GPU の仕事が終わった組のメッシュの区画の持ち主を手放す
         *
         * シーンを出た後も、組が最後の記録のメッシュの区画を持ち続けないようにする（組を次に使うまで区画が空きへ戻らない）。
         */
        void ReleaseIdleMeshRegionOwners(uint32_t inFlightIndex, uint64_t frameSerial);

        /** @brief VSM の投影物のカリングの入力を、常駐の表とこのフレームのページの表から作る */
        void PublishShadowCasterInputs(const FrameSlot &slot, SceneTables &tables);

        /** @brief VSM のキャッシュの無効化: 影の表へ載せたインスタンス 1 つの記録（m_Instances と同じ並び） */
        struct ShadowInstanceRecord
        {
            // ワールドの境界の球（中心 xyz + 半径）。bHasBounds が false なら使えない
            float Sphere[4] = {};
            // 影を落とすインスタンスとして影の表へ載せたか（メッシュが描けない・影を落とさないなら false）
            bool bCaster = false;
            bool bHasBounds = false;
        };

        /** @brief VSM のキャッシュの無効化: 影の表へ載せたメッシュの、影に効く中身（区画・境界・ページの常駐） */
        struct ShadowMeshState
        {
            const void *VertexBuffer = nullptr;
            const void *IndexBuffer = nullptr;
            const void *ClusterBuffer = nullptr;
            uint64_t VertexBufferOffsetBytes = 0;
            uint64_t IndexBufferOffsetBytes = 0;
            uint64_t ClusterBufferOffsetBytes = 0;
            uint32_t ClusterCount = 0;
            uint32_t PageTableBase = 0;
            uint32_t PageCount = 0;
            float TotalBounds[4] = {};
            // ページの範囲の常駐の区画の署名（ページを読み込むメッシュだけ。常駐が変わると、カリングが選ぶクラスタが変わる）
            uint64_t Residency = 0;
            // 最後に表へ載せた回（載らなかったメッシュは記録から外し、次に載ったときに変わったものとして扱う）
            uint64_t Stamp = 0;

            bool operator==(const ShadowMeshState &other) const;
        };

        /**
         * @brief m_Instances を作り直す（並びが変わる）前に呼ぶ。今の並びと記録を、次に表を作るときの比べる元として残す
         *
         * 表を作る前に何度作り直しても、残すのは最後に表を作ったときの並び（VSM が知っている境界）。
         * @param bKeepInstances 今の並びを写して残す（m_Instances を空にせず足していくとき）。false なら移す
         */
        void BeginShadowRebuild(bool bKeepInstances);
        /** @brief 変わった席のインスタンスを直したとき、影に効く中身（変換・メッシュ・影の設定・境界）が変わった位置を覚える */
        void NoteShadowPatched(size_t position, const MegaMeshInstance &before, const MegaMeshInstance &after);
        /** @brief インスタンスが無くなって表を作らないとき、残した並びの投影物の境界を無効化へ積む */
        void FlushShadowRebuildWithoutInstances();
        /** @brief インスタンス 1 つの前と今の記録から、描き直す範囲を積む（どちらかが投影物でなければ、もう片方だけ） */
        void PushShadowInvalidation(const ShadowInstanceRecord &before, const ShadowInstanceRecord &after);
        /**
         * @brief 作り直しを待っていない表で、インスタンス 1 つの影の記録を今のものにする
         *
         * メッシュの中身・常駐が変わった、影に効く中身が変わった位置（NoteShadowPatched）、投影物かどうかが変わったときだけ、
         * 前と今の境界を無効化へ積む。
         */
        void NoteShadowRecord(size_t instanceIndex, const ShadowInstanceRecord &now, bool bMeshChanged);
        /**
         * @brief m_Instances の全部から表を作る
         *
         * @param bCommit true なら差分更新の出所（席の行・メッシュ・載らなかったメッシュ）を覚える。
         * false なら何も変えずに作る（開発ビルドの照合）
         * @param outRows インスタンスの位置ごとの表の行（載らなければ NoSceneRow）
         * @param outRecords m_Instances と同じ並びの、影の表へ載せた記録（載らなかったインスタンスは投影物でない記録）
         */
        void ComposeSceneTables(const MegaGeometryPassCommand &command, SceneTables &tables, bool bCommit,
                                Container::VariableArray<ShadowInstanceRecord> &outRecords,
                                Container::VariableArray<uint32_t> &outRows);
        /**
         * @brief インスタンス 1 つの影の表の行（FirstGroup 以外）と記録を組む
         * @param ioEntry FirstGroup はそのまま残し、ほかを書く
         */
        void FillSceneShadowRow(const MegaMeshInstance &instance, const MegaGeometry::MegaMeshGPUData &gpuData,
                                MegaGeometryShadowInstance &ioEntry, ShadowInstanceRecord &outRecord) const;
        /**
         * @brief このフレームに表へ載せるメッシュの中身を覚えたものと比べる
         * @param bResidencyFresh ページの表の写しが前に比べてから変わった（常駐の署名を作り直す）
         * @return 前に載せたときから中身・常駐が変わった（初めて載せたときも true）
         */
        bool UpdateShadowMeshState(MegaGeometry::MegaMeshHandle handle, const MegaGeometry::MegaMeshGPUData &data,
                                   bool bResidencyFresh);

        /** @brief 常駐の表へ書いたバイト数を数え、120 回ごとに MEGA_SCENE_UPLOAD を出す */
        void NoteSceneUpload(uint64_t bytes, uint32_t rows, uint32_t entries, uint32_t shadows, uint32_t meshes,
                             uint64_t pageTableBytes, const SceneTables &tables);

        /**
         * @brief 表を書く共通の前段。読み手のどのパスよりも前（MegaGeometryPass::Execute）で 1 回呼ぶ
         *
         * フレームスロットを 1 組取り、表を作り、ページの表をそろえ、常駐の表へ差分を書く（commandList へ記録する）。
         * 同じフレームの記録はこのスロットとページの表の版を使う。
         */
        void PrepareSceneTables(const MegaGeometryPassCommand &command, RHI::ICommandList *commandList);

        /**
         * @brief フレームスロットのページの表を、今の常駐の状態に合わせる
         *
         * 表の版がスロットの写しと違うときだけ、バッファを（足りなければ作り直して）書き直す。
         * @param outBytes 書いたバイト数（書かなければ 0）を受け取る。null なら受け取らない
         * @return false ならバッファを作れなかった（このフレームは描けない）
         */
        bool SyncPageTable(FrameSlot &slot, MegaGeometryResources &resources, uint64_t *outBytes = nullptr);

        /**
         * @brief IndirectDraw コマンド・区間のカウンタ・描画情報のバッファを、必要な数に収まる大きさにする
         *
         * 足りなければ作り直し、古いバッファは、GPUが使い終わった後に破棄する。
         */
        bool EnsureBatchBuffers(uint32_t commandCapacity, uint32_t counterCapacity, uint32_t growthCeiling = 0);

        /** @brief BVH のたどりのカウンタ（間接 dispatch の引数を含む）を作る。平らな判定のディスクリプタセットも結ぶので初期化で作る */
        bool EnsureBvhCounterBuffer();

        /**
         * @brief BVH のたどりの固定容量の列と 2 パス目の根の入力を作る（作り直さない）
         *
         * 割り当てに失敗したら例外を投げずに false を返し、最初の 1 回だけ ERROR を出す（呼ぶ側はそのフレームの描画を見送る）。
         */
        bool EnsureBvhQueueBuffers();

        /**
         * @brief ソフトウェアラスタの一覧のバッファを、パスごとに capacity 個のクラスタを収められる大きさで用意する
         *
         * 足りないときだけ作り直し、古いものは m_RetiredBuffers へ回す。
         */
        bool EnsureSwRasterBuffer(uint32_t capacity, uint32_t growthCeiling = 0);

        /**
         * @brief 可視クラスタの binning のバッファ（全体の固定容量の列・区画の数え・開発ビルドの照合の印）を用意する
         *
         * 数えは区間の表の要素の数 slotCount（パスの数 × 区間の数）を収める大きさで、足りないときだけ作り直す。
         * 全体の列と照合の印は固定容量で作り直さない。列を作れなかったら（VRAM の不足）最初の 1 回だけ ERROR を出し、
         * 代わりのバッファを結ぶ（判定は列の容量 0 として溢れを数え、描画は区間ごとのコマンドで続ける）。
         * @return false なら数えのバッファを作れなかった（呼ぶ側はそのフレームの描画を見送る）
         */
        bool EnsureBinBuffers(uint32_t slotCount);

        /** @brief フレームスロットに、BVH のたどりの段 stageCount 個ぶんの UBO・ディスクリプタセットを用意する */
        bool EnsureBvhStageResources(FrameSlot &slot, uint32_t passIndex, uint32_t stageCount);

        /**
         * @brief 2パスの遮蔽カリングで描けるか。描けるときはHZBを深度の大きさに合わせる
         *
         * 無効（--mega-occlusion=off）・HZBが作れない・描く範囲が深度の全体と一致しない（遮蔽の判定は深度の
         * 全体を画面と見る）ときは false で、従来の1回の判定で描く。
         */
        bool CanUseTwoPassOcclusion(const MegaGeometryPassCommand &command);

        /**
         * @brief 前のフレームの HZB の判定（m_PrevHiZStatus）を MEGA_PREV_HIZ の行に出す（開発ビルドだけ）
         *
         * 判定が変わったフレームと、判定 300 回ごとに出す（毎フレームは出さない）。
         */
        void LogPreviousHiZStatus(const MegaHiZContract &previous, const MegaHiZContract &current, int64_t epochFrame);

        /** @brief GBuffer の描画を止めるか（止める設定で、解決が実際に使えるとき） */
        bool ShouldSkipGBufferDraw(DebugViewMode mode) const;

        /** @brief Execute が積むもの（準備段が決める） */
        enum class PreparedFrameAction : uint8_t
        {
            None,
            // GBuffer の添付の状態遷移だけ（描かない・記録を ID のラスタへ移した）
            EmptyTransition,
            // 記録を保留のコマンドとして積む
            Record,
        };

        /** @brief グラフの GBuffer の Texture を引いて、添付に使うかを決める */
        void ResolveGraphGBufferTextures(RenderGraphResources &resources);
        /** @brief 表の準備（PrepareSceneTables）と、記録を ID のラスタへ移すかを決め、Execute が積むものを返す */
        PreparedFrameAction PrepareFrame(ViewRenderContext &context);
        /** @brief 記録（ID のラスタのワーカー）が引く材質の定数の 1x1 テクスチャ（作成と転送を伴う）を先に作る */
        void PrepareConstantMaterialTextures();

        /** @brief 手放した古いバッファを、GPUが使い終わった後に破棄する */
        void ReleaseStaleBuffers();

        /** @brief 統計（MEGA_OCCLUSION）の読み戻しのスロットを用意する。作れなければ統計は取らない */
        /** @brief 統計（MEGA_OCCLUSION）の読み戻しの結果を受け取り、ログへ出す（提出の完了の後に RenderThread で呼ばれる） */
        void OnFrameReadback(const FrameReadbackResult &result) override;

        // 設定
        MegaGeometryPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // カリングコンピュートパイプライン（m_CullPipeline = 平らなクラスタの列、m_BvhCullPipeline = グループの BVH をたどる）
        RHI::PipelinePtr m_CullPipeline;
        RHI::ShaderPtr m_CullShader;
        RHI::PipelinePtr m_BvhCullPipeline;
        RHI::ShaderPtr m_BvhCullShader;

        // BVH のたどりの列（uvec2: インスタンスの番号・節の番号。固定容量の節の列 2 本と葉の列）・カウンタと間接 dispatch の引数・
        // 2 パス目の根の入力。全インスタンス・全パスで共用する。列と根の入力は BVH のインスタンスが初めて現れたときに作る
        RHI::BufferPtr m_BvhQueueBuffer;
        RHI::BufferPtr m_BvhCounterBuffer;
        RHI::BufferPtr m_BvhPass2Buffer;
        // 列を作れなかったことを記録したか（作れるまで描画を見送り、ERROR は続けて出さない）
        bool m_bBvhQueueAllocFailedLogged = false;
        // 最後に記録した BVH のたどりの規模（変わったときだけ記録する）
        uint32_t m_LoggedBvhInstances = 0xFFFFFFFFu;
        uint32_t m_LoggedBvhLevels = 0xFFFFFFFFu;
        uint32_t m_LoggedFlatInstances = 0xFFFFFFFFu;

        // 可視クラスタの binning: 判定が積む全体の固定容量の列（binding 23。uvec4: 席・クラスタ・
        // 区画・区画の中の位置）、区画の数え・接頭和・照合の引数（binding 24）、開発ビルドの照合の印（binding 25）。
        // 接頭和は各パスの判定の後に mega_bin_prefix.comp、詰めた経路の書き出しはその後に mega_bin_scatter.comp、
        // 旧の経路（--mega-binning=off）の新旧の照合は 2 パス目の後に mega_bin_check.comp が作る
        RHI::PipelinePtr m_BinPrefixPipeline;
        RHI::ShaderPtr m_BinPrefixShader;
        RHI::PipelinePtr m_BinScatterPipeline;
        RHI::ShaderPtr m_BinScatterShader;
        RHI::PipelinePtr m_BinCheckPipeline;
        RHI::ShaderPtr m_BinCheckShader;
        RHI::BufferPtr m_VisibleListBuffer;
        RHI::BufferPtr m_BinCounterBuffer;
        uint32_t m_BinCounterCapacity = 0; // m_BinCounterBuffer の語数
        RHI::BufferPtr m_BinCheckMarkBuffer;
        bool m_bBinCheckMarksCleared = false; // 照合の印を作った後に 0 で埋めたか
        bool m_bVisibleListAllocFailedLogged = false;
        uint32_t m_BinCheckStamp = 0;     // 照合ごとに進める印（0 は使わない）
        uint32_t m_VisibleListPeak = 0;   // 全体の列へ積もうとした数の最大（読み戻した統計から）
        bool m_bBinCheckLoggedOnce = false; // MEGA_BIN（一致）を出したか
        bool m_bBinMismatchLogged = false;  // MEGA_BIN の不一致を出したか

        // カリング・描画用GPUバッファ（全インスタンス・全パスで1組。コマンドは区間ごとの連続した範囲で、
        // パスごとに別の範囲を使う。範囲の配置は区間の表が持つ）
        RHI::BufferPtr m_IndirectDrawBuffer; // DrawIndexedIndirectCommand[]
        RHI::BufferPtr m_DrawCountBuffer;    // uint32_t[] 区間ごとの可視クラスタ数（詰めた経路は後ろに記録の区間の表）
        RHI::BufferPtr m_DrawInfoBuffer;     // uint32_t[2] × コマンド（インスタンスの番号・payload）
        uint32_t m_CommandCapacity = 0;      // m_IndirectDrawBuffer・m_DrawInfoBuffer のコマンド数
        uint32_t m_CounterCapacity = 0;      // m_DrawCountBuffer のカウンタ数
        RGResourceHandle m_IndirectDrawBufferHandle;
        RGResourceHandle m_DrawCountBufferHandle;
        RGResourceHandle m_MegaGeometryCompleteHandle;

        // ホストが毎フレーム書く資源。同じパスが1フレームに何回記録されても（複数のビューポート）、提出前・GPU が読み終わる前の
        // 組を上書きしないよう、記録の回数ではなくフレームの通し番号で使用済みの位置を戻す（FrameUseRing）
        FrameUseRing<FrameSlot> m_FrameSlots;
        // 組のどれかがメッシュの区画の持ち主を持っているか（インスタンスの無いフレームで手放す）
        bool m_bFrameSlotMeshOwnersHeld = false;
        // 記録したフレームの数え（フレームの通し番号が変わるたびに 1 進む）。退避したバッファの寿命と統計の枠の判定に使う
        // （m_OcclusionFrameCount は記録ごとに進むので、1フレームに複数回記録すると実時間が短くなる）
        uint64_t m_RenderFrameCount = 0;
        uint64_t m_LastRenderFrameSerial = 0;

        // GBuffer描画用グラフィックスパイプライン
        RHI::PipelinePtr m_DrawPipeline;
        RHI::PipelinePtr m_DrawWireframePipeline;
        RHI::ShaderPtr m_DrawVertexShader;
        RHI::ShaderPtr m_DrawFragmentShader;

        // GBuffer描画用レンダーパス・フレームバッファ（GBufferPassから共有）
        RHI::RenderPassPtr m_GBufferRenderPass;
        RHI::FramebufferPtr m_GBufferFramebuffer;

        // 2パス目のGBuffer描画用レンダーパス・フレームバッファ。1パス目の描画の後に続けて開くので、
        // 全てのアタッチメントが ShaderResource の状態から始まる（1パス目の終わりの状態）。
        RHI::RenderPassPtr m_SecondGBufferRenderPass;
        RHI::FramebufferPtr m_SecondGBufferFramebuffer;

        // 遮蔽カリング（2パス）。HZB は 2 枚を交互に作り、それぞれが作ったときの契約を持つ。m_HiZIndex は最後に作った方
        // （今のフレームは もう一方へ作り、前のフレームの HZB を残す）
        HiZPyramid m_HiZ[2];
        MegaHiZContract m_HiZContracts[2];
        uint32_t m_HiZIndex = 0;
        bool m_bHiZReady = false;
        // 前のフレームの HZB が使えるかの最後の判定（判定にはまだ使わない）と、ログの変化の検出・数え
        MegaPrevHiZStatus m_PrevHiZStatus = MegaPrevHiZStatus::Missing;
        bool m_bPrevHiZLogged = false;
        MegaPrevHiZStatus m_PrevHiZLoggedStatus = MegaPrevHiZStatus::Missing;
        uint64_t m_PrevHiZReadyCount = 0;
        uint64_t m_PrevHiZNotReadyCount = 0;
        // 今のシーンの世代（Setup が写しから受け取る）と、インスタンスを全部作り直した回数
        uint64_t m_SceneGeneration = 0;
        uint64_t m_SceneResyncCount = 0;
        // 従来の経路が binding 6 に結ぶ代わりのバッファ（シェーダーは触らない）
        RHI::BufferPtr m_DummyStatsBuffer;

        // ソフトウェアラスタの振り分け: パスごとの一覧と間接 dispatch の引数（binding 13）。振り分けないフレームは触らず、
        // 代わりのバッファ（m_DummyStatsBuffer）を結ぶ
        bool m_bSwRasterRequested = false;
        float m_SwRasterMaxPixels = DefaultSwRasterMaxPixels;
        RHI::BufferPtr m_SwRasterBuffer;
        uint32_t m_SwRasterBufferCapacity = 0; // m_SwRasterBuffer がパスごとに収められるクラスタ数
        uint32_t m_SwRasterCapacity = 0;       // 最後に振り分けたフレームの、パスごとの一覧の容量
        bool m_bSwRasterBinned = false;
        bool m_bSwRasterFallbackLogged = false;

        struct RetiredBuffer
        {
            RHI::BufferPtr Buffer;
            uint64_t RetiredFrame = 0; // 退避したときの m_RenderFrameCount
        };
        Container::VariableArray<RetiredBuffer> m_RetiredBuffers;
        // GPU Scene のバッファを置き換えるたびに進む版。各セットが結んだ版と照合する
        uint64_t m_DescriptorBufferRevision = 1;
#if NORVES_BUILD_DEVELOPMENT
        struct GpuSceneBufferAllocation
        {
            Container::TWeakPtr<RHI::IBuffer> Buffer;
            uint64_t Bytes = 0;
            uint64_t ExpiredFrame = ~0ull;
            Container::AnsiString Name;
            bool bCPUAccessible = false;
            bool bExcludeDeviceLocal = false;
            bool bDevelopmentOnly = false;
            bool bPending = false;
        };
        struct GpuSceneVramUsage
        {
            uint64_t LiveBytes = 0;
            uint64_t PendingBytes = 0;
            uint64_t PeakBytes = 0;
            uint64_t PeakLiveBytes = 0;
            uint64_t PeakPendingBytes = 0;
            uint64_t PeakFrame = 0;
        };
        struct GpuSceneVramStats
        {
            GpuSceneVramUsage All;
            GpuSceneVramUsage Device;
            GpuSceneVramUsage AllDevice;
            GpuSceneVramUsage DevelopmentOnlyDevice;
            GpuSceneVramUsage Host;
            GpuSceneVramUsage HostMaybeDevice;
        };
        void UpdateGpuSceneVram();
        Thread::Mutex m_GpuSceneVramMutex;
        Container::VariableArray<GpuSceneBufferAllocation> m_GpuSceneBuffers;
        GpuSceneVramStats m_GpuSceneVramStats;
        Container::AnsiString m_GpuSceneDevicePeakBuffers;
        uint64_t m_LastLoggedGpuSceneDevicePeakBytes = 0;
#endif
        uint64_t m_OcclusionFrameCount = 0;

        // 統計（MEGA_OCCLUSION）。フレームの持ち主の読み戻しの口で借りた先へ GPU が数え、提出の完了の後に受け取る（GPUを待たない）。
        // 読み戻しの要求ごとに、ログに付ける記録の情報を持つ
        struct PendingStats
        {
            uint64_t RequestId = 0;
            uint64_t Frame = 0;
            uint64_t RenderFrame = 0;  // 描画のフレームの番号（ViewRenderContext::FrameNumber）
            int64_t EpochFrame = -1;   // 決定的な撮影のエポックからの相対フレーム。エポック前は -1
            bool bSwRasterBin = false; // このフレームがソフトウェアラスタの振り分けを行い、統計の 4〜7 が有効
            uint32_t SwDispatches = 0; // このフレームで sink が記録したソフトの dispatch の数（GPU の統計ではなく CPU の記録の数）
            bool bBinCheck = false;    // このフレームが binning の新旧の照合を記録し、統計の 17〜30 が有効
            uint32_t BinCount = 0;     // 区画の数（パスの数 × 区間の数 × 2）
            uint32_t VisibleListCapacity = 0; // このフレームの全体の列の要素の数（列を確保できなかったときは 0）。統計の 16 は照合によらず有効
            bool bPrevHiZUsed = false;    // このフレームの 1 パス目が前の HZB で判定したか（使わないフレームは統計の 31〜41 が 0）
            MegaPrevHiZStatus PrevHiZStatus = MegaPrevHiZStatus::Missing; // 前の HZB が使えるかの判定
        };
        bool m_bStatsEpochActive = false;
        bool m_bDropOcclusionContinuity = false; // 決定的な撮影のエポックの最初のフレーム: 前のフレームの HZB を使わない
        FrameReadbackReceiver m_StatsReadback{this};
        Container::VariableArray<PendingStats> m_PendingStats;
        bool m_bStatsLoggedOnce = false;
        bool m_bOcclusionFallbackLogged = false;
        bool m_bBatchUnsupportedLogged = false;

        // GBufferテクスチャ参照（GBufferPassが作成したものをSharedResourcesから取得）
        RHI::TexturePtr m_AlbedoTexture;
        RHI::TexturePtr m_NormalTexture;
        RHI::TexturePtr m_MaterialTexture;
        RHI::TexturePtr m_EmissiveTexture;
        RHI::TexturePtr m_DepthTexture;
        // velocity（currentUV - previousUV）。GBufferPass と同じテクスチャへ書く。
        RHI::TexturePtr m_VelocityTexture;
        RGResourceHandle m_GBufferAlbedoHandle;
        RGResourceHandle m_GBufferNormalHandle;
        RGResourceHandle m_GBufferMaterialHandle;
        RGResourceHandle m_GBufferEmissiveHandle;
        RGResourceHandle m_GBufferDepthHandle;
        RGResourceHandle m_GBufferVelocityHandle;

        // デフォルトPBRテクスチャ（マテリアル未設定時のフォールバック）
        Container::TUniquePtr<ConstantMaterialTextureCache> m_ConstantMaterialTextures;
        RHI::TexturePtr m_DefaultWhiteTexture;      // 1x1 白 — Albedo/AO/Roughnessデフォルト
        RHI::TexturePtr m_DefaultFlatNormalTexture; // 1x1 フラット法線 (128,128,255) — Normalデフォルト
        RHI::TexturePtr m_DefaultBlackTexture;      // 1x1 黒 — Metallic/Heightデフォルト
        RHI::SamplerPtr m_DefaultLinearSampler;     // Linear/Wrapサンプラー
        RHI::SamplerPtr m_TerrainSampler;           // 地形の混ぜ合わせの材質のサンプラー（異方性 16、Wrap。地面を斜めに見てもぼけない）

        // フレーム単位のインスタンスリスト
        Container::VariableArray<MegaMeshInstance> m_Instances;
        // 解放の前の位置と表の行、移動の前後の位置、追加の位置。表を作るまで順に積む
        Container::VariableArray<SceneInstanceDelta> m_SceneInstanceDeltas;
#if NORVES_BUILD_DEVELOPMENT
        // 出し入れと全量同期の回数・件数（影の統計を出す間の合計）
        struct SceneInstanceDeltaStats
        {
            uint64_t Frames = 0;
            uint64_t Patched = 0;
            uint64_t Moved = 0;
            uint64_t Removed = 0;
            uint64_t Added = 0;
            uint64_t Resync = 0;
            uint64_t Full = 0;
        };
        SceneInstanceDeltaStats m_SceneInstanceDeltaStats;
#endif
        // m_Instances を作った時点の RenderThread の写しの版と配列。差分の同期で写しが変わらないフレームは作り直さない
        // （0 / nullptr は、写しから作ったのではない）
        uint64_t m_InstanceSourceRevision = 0;
        const void *m_InstanceSourceProxies = nullptr;

        // 常駐の表（device-local。フレームをまたいで残り、前段が変わった行だけを staging から散在書き込みする）。
        // 席の表（席ごと 128 B）が変換の元で、描く順の表（MegaInstanceRow）は GPU が席の表と描く順の記録から組む。
        // 影の表は描く順の表と同じ並び・同じ容量。転送は RenderResources のリング（ジオメトリ・テクスチャのタイル）とは
        // 別の道（フレームスロットの staging）で、予算もリングの 1 フレームの上限とは別（書く量だけ staging を広げる）
        RHI::ShaderPtr m_SceneScatterShader;
        RHI::PipelinePtr m_SceneScatterPipeline;
        RHI::BufferPtr m_SlotTableBuffer;
        uint32_t m_SlotTableCapacity = 0;
        RHI::BufferPtr m_InstanceTableBuffer;
        RHI::BufferPtr m_ShadowTableBuffer;
        uint32_t m_InstanceTableCapacity = 0;
        RHI::BufferPtr m_MeshTableBuffer;
        uint32_t m_MeshTableCapacity = 0;
        // 範囲の境界（描く順の行を 64 個ずつ区切った範囲の AABB。範囲ごとに vec4 × 2）。行の変わった範囲だけを GPU が作り直す
        // （mega_scene_scatter.comp の段 2）。m_RangeBoundsRowCount は境界を作ったときの描く順の行の数（減ったら末尾の範囲を作り直す）
        RHI::BufferPtr m_RangeBoundsBuffer;
        uint32_t m_RangeBoundsCapacity = 0;
        uint32_t m_RangeBoundsRowCount = 0;
        // 常駐の表の中身の写し（CPU）。散在書き込みは写しと違う行だけを送る。席の行の状態は 0 = 中身なし、
        // 1 = GPU の表にある、2 = 中身は覚えているが GPU の表に無い（更新に失敗した後。送り直すが変わったとは見なさない）。
        // 表を広げるときは古い表の中身を GPU でコピーするので、状態はそのまま引き継ぐ
        Container::VariableArray<GPUMegaSlotRow> m_SlotRowCopies;
        Container::VariableArray<uint8_t> m_SlotRowStates;
        // 席の行の中身が変わった更新の番号（その更新で、席を指す描く順の行を組み直す）
        Container::VariableArray<uint32_t> m_SlotRowChangedStamps;
        uint32_t m_SceneUpdateStamp = 0;
        Container::VariableArray<GPUMegaEntry> m_EntryCopies;
        uint32_t m_EntryCopyCount = 0;
        Container::VariableArray<MegaGeometryShadowInstance> m_ShadowRowCopies;
        uint32_t m_ShadowRowCopyCount = 0;
        // 最後の書き込みから表で直した行。全部を比べるときは行の一覧を使わない
        Container::VariableArray<uint32_t> m_SceneUploadRows;
        bool m_bSceneUploadAllRows = true;
        // 別の表を書いた後は、元の表に戻したときも全行を比べる
        bool m_bSceneUploadCopiesAreSceneTables = false;
        Container::VariableArray<GPUMegaMesh> m_MeshCopies;
        bool m_bMeshCopiesValid = false;
        // 席の表を確かめる行: Setup が m_Instances を作り直した（全部）か、直したインスタンスの位置。差分を書いた更新が消す
        bool m_bSceneRowsAllDirty = true;
        Container::VariableArray<uint32_t> m_SceneDirtyPositions;
        // m_Instances の TableSlot を決め直すか（作り直し・手で足した・消したとき）
        bool m_bSceneSlotsDirty = true;
        // 行の席が全て一意で、TableSlot が SceneSlot のときだけ差分で席を保てる
        bool m_bSceneSlotsPatchable = false;
        Container::VariableArray<uint8_t> m_SceneSlotSeen;
        // 散在書き込みの記録を組む作業領域（フレームをまたいで使い回す）
        Container::VariableArray<uint32_t> m_SceneScatterWords;
        // 統計 MEGA_SCENE_UPLOAD（120 回の更新ごと）
        struct SceneUploadStats
        {
            uint32_t Updates = 0;
            uint32_t ZeroUpdates = 0;
            uint64_t WindowBytes = 0;
            uint32_t Mismatches = 0;
            // 表を広げた更新の、GPU の表の読み戻しの照合（窓の間の合計）
            uint32_t Grows = 0;
            uint32_t GpuChecks = 0;
            uint32_t GpuMismatches = 0;
            bool bLoggedOnce = false;
        };
        SceneUploadStats m_SceneUploadStats;

        // VSM の投影物のカリングの入力（Setup で無効に戻り、前段か記録が表を書けたときに有効になる）
        MegaGeometryShadowCasterInputs m_ShadowCasterInputs;
        // VSM のキャッシュの無効化: 影の表へ載せた記録（m_Instances と同じ並び）と、影に効く中身が変わった位置（印と一覧）
        Container::VariableArray<ShadowInstanceRecord> m_ShadowRecords;
        Container::VariableArray<uint8_t> m_ShadowDirtyMarks;
        Container::VariableArray<uint32_t> m_ShadowDirtyPositions;
        // m_Instances を作り直した後、まだ表を作っていない（前に表を作ったときの並びと記録を、鍵で引いて比べる）
        bool m_bShadowRebuildPending = false;
        Container::VariableArray<MegaMeshInstance> m_ShadowPreviousInstances;
        Container::VariableArray<ShadowInstanceRecord> m_ShadowPreviousRecords;
        // 表へ載せたメッシュ（ハンドルの番号で引く）と、常駐の署名を作るページの表の写し
        Container::UnorderedMap<uint64_t, ShadowMeshState> m_ShadowMeshes;
        uint64_t m_ShadowMeshStamp = 0;
        uint64_t m_ShadowPageTableVersion = ~0ull;
        Container::VariableArray<MegaGeometry::GeometryPageTable::Entry> m_ShadowPageEntries;
        // VSM が取り出すまで積む無効化
        MegaGeometryShadowInvalidation m_ShadowInvalidation;
        // 統計 MEGA_SHADOW_DIRTY（表を作った 120 回ごと。窓の間の合計）
        struct ShadowDirtyStats
        {
            uint32_t Builds = 0;
            uint32_t Rebuilds = 0;
            uint64_t DirtySlots = 0;
            uint64_t MeshChanges = 0;
            // そのうちページの常駐だけが変わった回数
            uint64_t ResidencyChanges = 0;
            uint64_t Boxes = 0;
            uint32_t InvalidateAll = 0;
            bool bLoggedOnce = false;
        };
        ShadowDirtyStats m_ShadowDirtyStats;
        // このフレームの表（前段が作り、同じフレームの記録が使う。フレームをまたいで持ち続け、差分で直す）
        SceneTables m_SceneTables;
        // 表の差分更新: false なら次の表は全部を作る（インスタンスの作り直し・席の表の番号の決め直し・持ち主の手放しなど）
        bool m_bSceneTablesValid = false;
        // 表を全部作ったときに BVH をたどれる状態だったか（設定・パイプライン・列のバッファ）
        bool m_bSceneTablesBvhAvailable = false;
        // m_Instances の位置ごとの、表の行（NoSceneRow は載っていない）と、表を作ったときのメッシュのハンドルの番号
        Container::VariableArray<uint32_t> m_SceneInstanceRows;
        Container::VariableArray<uint64_t> m_SceneInstanceHandles;
        // メッシュの表の行ごとの出所と、表へ載らなかったインスタンスのメッシュ（ハンドルごとに 1 つ）
        Container::VariableArray<SceneMeshSource> m_SceneMeshSources;
        Container::VariableArray<SceneSkippedMesh> m_SceneSkippedMeshes;
        // 平らな空行は主の割り当ての大きさで引き、影の割り当ても足りるものを使う
        struct SceneFlatHole
        {
            uint32_t Row = 0;
            uint32_t ShadowGroups = 0;
        };
        Container::Map<uint32_t, Container::VariableArray<SceneFlatHole>> m_SceneFlatHoles;
        uint32_t m_SceneFlatHoleCount = 0;
        Container::VariableArray<uint32_t> m_SceneMeshLiveCounts;
        enum class SceneFullReason : uint8_t
        {
            State, ShadowRebuild, MeshChanged, MissingMesh, Spare, BvhGroups, Holes, Overflow, FlatCapacity, MeshUnused, Count
        };
        SceneFullReason m_SceneFullReason = SceneFullReason::State;
        // 表の行に効く中身が変わった位置（印と一覧。PatchInstancesFromChangedSlots が積み、表を作ると消える）
        Container::VariableArray<uint8_t> m_SceneTableDirtyMarks;
        Container::VariableArray<uint32_t> m_SceneTableDirtyPositions;
        // 表を作る作業領域（フレームをまたいで使い回す）
        Container::VariableArray<size_t> m_SceneInstanceOrder;
        Container::VariableArray<ShadowInstanceRecord> m_SceneComposeRecords;
        Container::VariableArray<uint8_t> m_SceneMeshShadowChanged;
        // 開発ビルドの照合 MEGA_TABLES: 全部を作り直した表の作業領域と、統計（mismatch・checks は通算、ほかは出す間隔の間）
        SceneTables m_SceneTablesCheck;
        Container::VariableArray<ShadowInstanceRecord> m_SceneTablesCheckRecords;
        Container::VariableArray<uint32_t> m_SceneTablesCheckRows;
        struct SceneTablesStats
        {
            uint32_t SinceCheck = 0;
            uint32_t FullBuilds = 0;
            uint32_t DeltaBuilds = 0;
            uint64_t DeltaRows = 0;
            uint64_t Moved = 0;
            uint64_t Removed = 0;
            uint64_t Added = 0;
            uint32_t FullReasons[static_cast<size_t>(SceneFullReason::Count)] = {};
            uint32_t Checks = 0;
            uint32_t Mismatches = 0;
        };
        SceneTablesStats m_SceneTablesStats;
        PreparedSceneTables m_PreparedSceneTables;

        // ビジビリティバッファのラスタへ渡す描画の写し（TakeVisibilityDrawPlan が取り出す）
        bool m_bVisibilityPlanEnabled = false;
        // GBuffer への描画を止めるか（ビジビリティバッファの解決が GBuffer を書くとき）
        bool m_bSkipGBufferDraw = false;
        const VisibilityResolvePass* m_ResolvePass = nullptr;
        const VisibilityRasterPass* m_VisibilityRaster = nullptr;
        // このフレームの記録を ID のラスタへ移したか（Execute(context) が決め、ラスタが TakeFrameRecordDeferred で取り出す）
        bool m_bFrameRecordDeferred = false;
        // 準備段が PrepareFrame を済ませたフレーム（次の Execute が 1 回だけ読む）と、そのとき決めた積むもの
        bool m_bFramePrepared = false;
        PreparedFrameAction m_PreparedAction = PreparedFrameAction::None;
        // Declare の時点の表示（記録を ID のラスタへ移すかの判定に使う）
        DebugViewMode m_DeclaredDebugMode = DebugViewMode::Normal;
        VisibilityDrawPlan m_VisibilityPlan;
        // 描画の写しを作ったフレームの IndirectDraw・カウンタ・描画情報のバッファが、Common へ戻されないまま残っているか
        bool m_bVisibilityBuffersHeld = false;
        RHI::BufferPtr m_HeldIndirectDrawBuffer;
        RHI::BufferPtr m_HeldDrawCountBuffer;
        RHI::BufferPtr m_HeldDrawInfoBuffer;

        // 現在のGBufferサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bPreferRenderGraphGBufferResources = false;
        bool m_bGBufferRenderPassUsesRenderGraphAttachmentStates = false;
        bool m_bMegaGeometryDebugPayloadUnsupportedWarned = false;
        // フレームの通し番号が無い記録の通知を一度だけ出すための印
        bool m_bUnsetFrameSerialWarned = false;

        // メッシュ共通のLOD球を持つメッシュ（全クラスタが同じ段を選ぶ）の、最後に記録した段。
        // 段が変わったときだけ、選んだ段と三角形数を記録する。
        struct LoggedUniformLOD
        {
            uint64_t MegaMeshId = 0;
            uint32_t Level = 0;
        };
        Container::VariableArray<LoggedUniformLOD> m_LoggedUniformLODs;
    };

} // namespace NorvesLib::Core::Rendering
