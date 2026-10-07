#pragma once

// ビジビリティバッファの幾何の解決: VisBuffer.Id の画素から三角形を引き、GBuffer の Albedo・Normal・Material・Velocity・Emissive を書く。
//
// 計算シェーダー（visbuffer_resolve.comp。本体は Common/VisibilityResolve.glsl）が、1 スレッド 1 画素で次を行う。
//   - ID から描画の記録（VisibilityBuffer.h）と三角形を引き、頂点・インデックスのデバイスアドレスから 3 頂点を読む。
//   - 画素の中心を通るカメラの光線と三角形の平面の交点で、透視の補正つきの重心座標を求める。
//     隣の画素（x+1、y+1）の光線でも交わらせて、位置・UV の解析的な微分（前進差分）を得る。
//   - 法線（補間して正規化）・接線の基底・前のフレームの頂点から求めた速度を作り、GBuffer へ書く。
//   - 材質ごとの形では、その材質のテクスチャ（アルベド・法線・金属度・粗さ・AO・高さ。ORM の 1 枚も）を dispatch ごとの
//     ディスクリプタセットへ束ね、ラスタの材質シェーダーと同じ標本・復号・POM の関数を、画面微分の代わりに三角形から求めた
//     解析的な微分（textureGrad）で呼んで Albedo（インスタンスの色 × アルベド、α はテクスチャの α）・Normal（法線マップ適用後）・
//     Material（金属度・粗さ・AO）を書く。VT（sparse）の材質は非常駐のタイルを読まず粗いミップへ逃げる。
//     VT の要求（フィードバック）も、ラスタの材質シェーダーと同じ規則で要求のバッファへ書く（4×4 の巡回の 1 画素と、
//     非常駐へ逃げた画素。欲しいミップは textureQueryLOD の代わりに解析的な微分から求める）。
//     画面全体の直接 dispatch はテクスチャを束ねられないので、材質の定数（基本色・スカラー値）だけで書き、要求も書かない。
//   - 発光（Emissive）は、材質の表の件の色度 × 輝度 × フレームのプリエクスポージャ（65504 で頭打ち）を、どちらの形でも書く。
// GBuffer の形式・意味（Albedo.a、法線の格納、Velocity の式）は、ラスタの経路（gbuffer.frag）と同じ。
//
// 解決は 2 つの形で記録できる（VisibilityResolve::Record が dispatch の入力で選ぶ）。
//   - 画面全体の直接 dispatch（既定）: 画面のタイル（8x8）を 1 回の dispatch で処理する。
//   - 材質ごとのタイルの一覧から走る形: 材質ごとのタイルの分類（MaterialTileClassifyPass）の引数と一覧で、材質ごとに 1 回ずつ
//     間接 dispatch する（visbuffer_resolve_tiles.comp）。どの材質の一覧にも入らない画素（分類の上限以上の材質）は解決されない。
// 2 つの形は同じ本体（Common/VisibilityResolve.glsl）を取り込み、同じ式で画素を解決する。結果が画素ごとにビット単位で
// 一致することは、別々のパイプラインについて Vulkan が保証するものではない。確かめたのは VisibilityResolveVulkanTest（この GPU・
// このドライバ）の比較で、別の装置・ドライバでは一致を前提にしない（撮影の比較で一致の度合いを測る）。

#include "Container/Containers.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/RenderTypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    struct DeviceCapabilities;
} // namespace NorvesLib::RHI

namespace NorvesLib::Core::Rendering
{
    class MaterialTileClassifyPass;
    class ShaderManager;
    class SkinningComputePass;
    class VisibilityRasterPass;
    struct CameraViewConstants;
    struct ViewRenderContext;

    namespace VisibilityResolveGeometry
    {
        /** @brief ResolveParams::Screen[2] のビット（シェーダーの RESOLVE_FLAG_*
         * と同じ） */
        constexpr uint32_t FLAG_PREVIOUS_VALID = 1u;
        /** @brief 材質ごとの形の dispatch
         * の定数（ResolveTileParams::tile.z）のビット（シェーダーの VIS_TILE_FLAG_*
         * と同じ） */
        constexpr uint32_t TILE_FLAG_ORM = 1u;
        constexpr uint32_t TILE_FLAG_SPARSE = 2u;
        /** @brief
         * 材質ごとの形が束ねる材質のテクスチャの数（アルベド・法線・金属度・粗さ・AO・高さ）
         */
        constexpr uint32_t MATERIAL_TEXTURE_COUNT = 6;
        /** @brief ワークグループ（画面のタイル）の一辺の画素数（シェーダーの local_size
         * と同じ） */
        constexpr uint32_t TILE_SIZE = 8;
        /** @brief 検証用の書き出しの、画素あたりの vec4 の数（シェーダーの
         * VIS_RESOLVE_DUMP_STRIDE と同じ） */
        constexpr uint32_t DUMP_STRIDE_VEC4 = 12;
        constexpr uint32_t DUMP_STRIDE_BYTES = DUMP_STRIDE_VEC4 * 4u * sizeof(float);

        /**
         * @brief 検証用の書き出しの vec4 の添字（シェーダーの dump[] の並びと同じ）
         *
         * Barycentric: xyz = 重心座標、w = 描画の種類。 Uv: xy = UV、zw = 画素の x
         * 方向の UV の微分。 UvDy: xy = y 方向の UV の微分。 HitPosition: xyz =
         * カメラ相対のワールド位置。 DposDx・DposDy: 位置の微分。
         * TangentT・TangentB・TangentN: 接線の基底。 PreviousClip:
         * 前のフレームのクリップ座標。 Misc: xy = 速度、z = 材質の番号、w =
         * 三角形の番号。
         */
        enum DumpIndex : uint32_t
        {
            DumpBarycentric = 0,
            DumpUv = 1,
            DumpUvDy = 2,
            DumpHitPosition = 3,
            DumpDposDx = 4,
            DumpDposDy = 5,
            DumpTangentT = 6,
            DumpTangentB = 7,
            DumpTangentN = 8,
            DumpPreviousClip = 9,
            DumpMisc = 10,
        };

        /** @brief シェーダーの ResolveParams（std140）と同じ 256 バイトの定数 */
        struct alignas(16) ResolveParams
        {
            /** @brief ビュー空間 → ワールド（シェーダーの並び。列ごと） */
            float InvView[16] = {};
            /** @brief クリップ → ビュー空間（ラスタと同じ投影。ジッターを含む） */
            float InvProj[16] = {};
            /** @brief 前のフレームのカメラの投影 * ビュー */
            float PreviousViewProj[16] = {};
            float CameraPosition[4] = {};
            /** @brief x, y, 幅, 高さ（画素。ラスタの描画範囲） */
            float Viewport[4] = {};
            /** @brief x = 画面の幅、y = 画面の高さ、z = FLAG_*、w = 材質の表の件数 */
            uint32_t Screen[4] = {};
            /** @brief x = 発光に掛けるプリエクスポージャ（ラスタの frameParams.y
             * と同じ値。既定は 1）、 y = MegaGeometry
             * のデバッグの表示（ResolveDebugViewCode の値。0 は通常の解決） */
            float Frame[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        };
        static_assert(sizeof(ResolveParams) == 256, "visbuffer_resolve の ResolveParams（std140）と一致しなければならない");

        /**
         * @brief ラスタと同じカメラの定数から ResolveParams を作る
         * @param current 今のフレームのカメラ（GBufferPass と同じ
         * CameraViewConstants::BuildForDevice の結果）
         * @param previous 前のフレームのカメラ。null なら前のカメラは無い（速度は 0）
         */
        ResolveParams BuildParams(const CameraViewConstants& current,
                                  const CameraViewConstants* previous,
                                  const RHI::Viewport& viewport,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t materialCount);

        /**
         * @brief 表示の選択を、解決のシェーダーが読む MegaGeometry
         * のデバッグの表示の値へ直す
         *
         * 1 = クラスタの色（MegaGeometryClusters）、2 = LOD
         * の段（LODLevel）、それ以外は 0（通常の解決）。 値は
         * Common/VisibilityResolve.glsl の RESOLVE_DEBUG_*（MegaGeometryPass の
         * DEBUG_PAYLOAD_MODE_*）と同じ。 描画の記録の payload がクラスタの番号・LOD
         * の段になるのは、MegaGeometryPass が同じ表示の選択で カリングの payload
         * の種別を切り替えるため。
         */
        constexpr float ResolveDebugViewCode(DebugViewMode mode)
        {
            return mode == DebugViewMode::MegaGeometryClusters ? 1.0f : (mode == DebugViewMode::LODLevel ? 2.0f : 0.0f);
        }

        /**
         * @brief この装置でビジビリティバッファの GBuffer の解決を使えるか
         *
         * ID
         * の書き込み（geometryShader・drawIndirectFirstInstance）、頂点のデバイスアドレス（bufferDeviceAddress）、
         * RG16F などの storage
         * image（shaderStorageImageExtendedFormats）が全部そろっているとき true。 false
         * の装置では、従来の GBuffer の描画のまま動かす。
         */
        bool IsSupported(const RHI::DeviceCapabilities& capabilities);

        /** @brief 解決を使えない理由。None のとき使える（GBuffer の描画を止めてよい） */
        enum class FallbackReason : uint8_t
        {
            None = 0,
            /** @brief 解決のパスが無い、または無効（グラフに載らない） */
            PassUnavailable,
            /** @brief 装置の機能が足りない（IsSupported が false） */
            DeviceUnsupported,
            /** @brief ID のラスタが描けない（パスが無い・シェーダーやパイプラインが作れていない） */
            RasterUnavailable,
            /** @brief 解決の計算パイプラインが作れていない */
            ResolveUnavailable,
            /**
             * @brief
             * 計算スキニングのパスが有効なのにパイプラインが作れていない（スキニングの頂点が作られず、ID
             * のラスタが
             *        スキニングの塊を描けないので、解決へ進むとスキニングの物が消える）
             */
            SkinningComputeUnavailable,
        };

        /** @brief ログ（VISBUFFER_FALLBACK reason=...）に出す、機械が照合する理由の名前 */
        const char* GetFallbackReasonName(FallbackReason reason);
    } // namespace VisibilityResolveGeometry

    /**
     * @brief 材質ごとの形の 1 回の dispatch
     * が束ねる、その材質のテクスチャ（材質の表の 1 件ぶん）
     *
     * 解決できなかった（指定が無い・RHI のテクスチャが引けない）ものは
     * null。束ねるときに、GBufferPass の材質の descriptor
     * と同じ規則で既定のテクスチャ（白・平坦な法線・黒・中間灰・スカラー値の
     * 1x1）へ置き換える。
     */
    struct VisibilityResolveMaterial
    {
        RHI::TexturePtr Albedo;
        RHI::TexturePtr Normal;
        RHI::TexturePtr Metallic;
        RHI::TexturePtr Roughness;
        RHI::TexturePtr AO;
        /** @brief ORM の 1 枚。あれば金属度・粗さ・AO
         * の枠をすべてこれで埋める（シェーダーは金属度の枠だけを読む） */
        RHI::TexturePtr ORM;
        RHI::TexturePtr Height;
        /** @brief 金属度・粗さのテクスチャの指定が無いときに使うスカラー値（1x1
         * のテクスチャにする）。負は未指定（既定の黒・中間灰） */
        float MetallicConstant = -1.0f;
        float RoughnessConstant = -1.0f;
        float AOConstant = 1.0f;
        /**
         * @brief MegaGeometry の区間の材質（材質の表の
         * MATERIAL_FLAG_MEGA_GEOMETRY）。ラスタの MegaGeometryPass と同じ 等方の
         * Linear のサンプラーを束ね、粗さのテクスチャも ORM
         * も無いときの粗さは白（1）にする。
         *        シェーダーも同じ印（材質の表の件）で等方の LOD の式を使う
         */
        bool bMegaGeometry = false;
        /**
         * @brief VT のフィードバックのパラメータ（ResolveVirtualTextureFeedbackParam
         * の結果。0 は要求を書かない）。 そのテクスチャが
         * VT（sparse）で、このフレームの要求のバッファがあるときだけ 0 でない。ORM
         * は金属度の枠
         */
        uint32_t FeedbackAlbedo = 0;
        uint32_t FeedbackNormal = 0;
        uint32_t FeedbackORM = 0;
        uint32_t FeedbackHeight = 0;
    };

    /** @brief 1 回の解決の入力と出力 */
    struct VisibilityResolveDispatch
    {
        /** @brief VisBuffer.Id（R32_UINT。ShaderResource の状態で読む） */
        RHI::TexturePtr IdTexture;
        /** @brief 描画の記録の表（storage buffer）と、使っているバイト数 */
        RHI::BufferPtr RecordTable;
        uint64_t RecordTableBytes = 0;
        /** @brief フレームの材質の表（MaterialEntry の並び）と、使っているバイト数 */
        RHI::BufferPtr MaterialTable;
        uint64_t MaterialTableBytes = 0;
        /** @brief MegaGeometry のインスタンスの表。無ければ null（空の表を束ねる） */
        RHI::BufferPtr MegaInstances;
        uint64_t MegaInstancesBytes = 0;
        /** @brief 描画のインスタンスの表（手続きメッシュ）。無ければ
         * null（空の表を束ねる） */
        RHI::BufferPtr DrawInstances;
        uint64_t DrawInstancesBytes = 0;
        /** @brief 出力（GBuffer の Albedo・Normal・Material・Velocity）。呼び出し前に
         * UnorderedAccess の状態で、終わっても UnorderedAccess のまま */
        RHI::TexturePtr Albedo;
        RHI::TexturePtr Normal;
        RHI::TexturePtr Material;
        RHI::TexturePtr Velocity;
        /** @brief 発光の出力（GBuffer.Emissive。RGBA16F。ほかの出力と同じく
         * UnorderedAccess の状態で渡す） */
        RHI::TexturePtr Emissive;
        /** @brief 検証用の版（Initialize の bDump）の出力。画素あたり
         * DUMP_STRIDE_BYTES。製品の版では使わない */
        RHI::BufferPtr Dump;
        /**
         * @brief VT
         * の要求のバッファ（TextureResources::GetVirtualTextureFeedbackTarget の
         * Buffer）と、束ねるバイト数（0 なら全体）。
         *        材質ごとの形が書く。無い・対応しないデバイスでは、書かれない小さな代替を束ねる（パラメータが
         * 0 の材質は書かない）
         */
        RHI::BufferPtr Feedback;
        uint64_t FeedbackBytes = 0;
        VisibilityResolveGeometry::ResolveParams Params;

        /**
         * @brief 材質ごとのタイルの一覧から走る形の入力（MaterialTileClassify
         * の出力）。TileArgs と TileList の両方があれば、 材質ごとの間接 dispatch
         * の形で記録する（Initialize の bTiles
         * が必要）。どちらかが無ければ画面全体の直接 dispatch
         *
         * 引数は IndirectBuffer の用途で作り、一覧と一緒に GenericRead
         * の状態にしてから渡す（Args の並びは MaterialTiles::ARGS_STRIDE_BYTES
         * ごと）。
         */
        RHI::BufferPtr TileArgs;
        RHI::BufferPtr TileList;
        /** @brief 間接 dispatch する材質の数（材質の番号 0..N-1）。0
         * なら引数の表の件数（TileArgs の大きさ / 引数 1 つの大きさ）全部 */
        uint32_t TileMaterialCount = 0;
        /**
         * @brief
         * 材質ごとの形が束ねる材質のテクスチャ（添え字が材質の表の番号）。短いとき・空のときは、足りない材質を
         *        すべて既定のテクスチャで束ねる。画面全体の直接 dispatch では使わない
         */
        Container::VariableArray<VisibilityResolveMaterial> Materials;
    };

    /**
     * @brief 幾何の解決を計算シェーダーで記録する本体（RenderGraph にも GPU
     * のテストにも使う）
     *
     * 資源（定数・ディスクリプタセット）は FrameUseRing が持つ。Record
     * のたびに枠の次の 1 組を使うので、 1 フレームに何回 Record
     * しても提出前の資源を上書きしない。
     */
    class VisibilityResolve final
    {
    public:
        VisibilityResolve();
        ~VisibilityResolve();

        VisibilityResolve(const VisibilityResolve&) = delete;
        VisibilityResolve& operator=(const VisibilityResolve&) = delete;

        /**
         * @brief シェーダーとパイプラインを作る。失敗したら false（以降の Record
         * は何もしない）
         * @param bDump true なら画素ごとの中間の値を書き出す検証用の版（GPU
         * のテスト用）
         * @param bTiles true
         * なら、材質ごとのタイルの一覧から走る形（visbuffer_resolve_tiles.comp）のパイプラインも作る。
         *               作れなければ false（画面全体の直接 dispatch の分も使えない）
         */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager, bool bDump = false, bool bTiles = false);
        void Shutdown();
        bool IsReady() const
        {
            return m_Pipeline != nullptr;
        }
        /** @brief 材質ごとのタイルの一覧から走る形（TileArgs・TileList つきの
         * Record）を使えるか */
        bool IsTileReady() const
        {
            return m_TilePipeline != nullptr;
        }
        /** @brief
         * 材質ごとの形の記録が使い回す、資源の並べ替え用の作業配列の容量（検査用。Record
         * のたびに作り直さないことの確認） */
        size_t GetTileUseScratchCapacity() const
        {
            return m_TileUseScratch.capacity();
        }

        /** @brief 飛行中のフレームの番号の枠を選ぶ（FrameUseRing の BeginFrame
         * と同じ） */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /**
         * @brief 解決を記録する
         *
         * dispatch の TileArgs・TileList が無ければ、画面のタイルごとに 1
         * グループの直接 dispatch を 1 回。 あれば、材質 0..N-1 について 1 回ずつ
         * DispatchIndirect する（N は TileMaterialCount か引数の表の件数。 1 回の間接
         * dispatch が 1 組の資源を使うので、N が大きいと資源も N 組要る）。 間接
         * dispatch の間には UAV のバリアを入れない（各 dispatch
         * は別の画素にだけ書き、出力を読まない）。
         * タイルの形では、コマンドリストへ何かを記録する前に、入力・出力・引数のバッファの用途・全部の資源の作成を確かめる。
         * @return 記録できたら
         * true。入力・出力が足りない、出力が画面より小さい、タイルの形を使えない（IsTileReady
         * が false）、 引数に 1 件も収まらない、引数が IndirectBuffer
         * の用途を持たない、資源を作れないときは、false で何も記録しない。
         *         ただし最初の間接 dispatch をコマンドリストが断ったとき（既定の
         * ICommandList::DispatchIndirect）は、
         *         パイプラインとディスクリプタセットの設定だけが残り、dispatch は 1
         * 回も記録されない
         */
        bool Record(RHI::ICommandList* commandList, const VisibilityResolveDispatch& dispatch);

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
            // 材質ごとの形の、1 回の dispatch の定数（横のタイル数・材質の番号）とディスクリプタセット
            RHI::BufferPtr TileUniform;
            RHI::DescriptorSetPtr TileDescriptorSet;
        };

        bool EnsurePlaceholder();
        bool EnsureMaterialDefaults();
        /** @brief 定数のスカラー値（8bit に丸めた値）の 1x1 のテクスチャ。GBufferPass
         * の GetOrCreateConstantGrayTexture と同じ */
        RHI::TexturePtr GetConstantGrayTexture(float value);
        void BindCommon(RHI::IDescriptorSet& set, const RHI::BufferPtr& paramsUniform, const VisibilityResolveDispatch& dispatch) const;
        bool RecordTiles(RHI::ICommandList* commandList, const VisibilityResolveDispatch& dispatch);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        // 材質ごとのタイルの一覧から走る形（Initialize の bTiles のときだけ）
        RHI::ShaderPtr m_TileShader;
        RHI::PipelinePtr m_TilePipeline;
        RHI::SamplerPtr m_Sampler;
        // 表を持たないフレームで束ねる、空の表（読まれない）
        RHI::BufferPtr m_Placeholder;
        // 材質ごとの形が、テクスチャの指定が無い枠に束ねる既定のテクスチャ（GBufferPass と同じ値）とサンプラー。
        // m_MaterialSampler は手続き・スキニング用（GBufferPass と同じ異方性 4、Wrap）、m_MegaMaterialSampler は
        // MegaGeometry 用（MegaGeometryPass と同じ等方の Linear、maxAnisotropy 指定なし、Wrap）
        RHI::TexturePtr m_DefaultWhite;
        RHI::TexturePtr m_DefaultFlatNormal;
        RHI::TexturePtr m_DefaultBlack;
        RHI::TexturePtr m_DefaultMidGray;
        RHI::SamplerPtr m_MaterialSampler;
        RHI::SamplerPtr m_MegaMaterialSampler;
        Container::UnorderedMap<uint32_t, RHI::TexturePtr> m_ConstantGrayTextures;
        bool m_bDump = false;
        // 材質ごとの形のシェーダーが VT の要求のバッファの binding を持つか（デバイスが VT のフィードバックに対応するとき）
        bool m_bFeedback = false;
        FrameUseRing<Use> m_Uses;
        // RecordTiles が 1 回の間接 dispatch ごとの資源（m_Uses の要素）を並べる作業用の配列。Record のたびに作らず容量を使い回す
        // （要素は m_Uses を指すだけ。次の Record の頭で空にする）
        Container::VariableArray<Use*> m_TileUseScratch;
    };

    /**
     * @brief VisBuffer.Id から GBuffer の Albedo・Normal・Velocity を解決する
     * RenderGraph のパス
     *
     * VisibilityRasterPass が書いた ID
     * と、同じパスが持つ記録の表・材質の表・インスタンスの表を読み、GBuffer の
     * Albedo・Normal・Material・Velocity・Emissive の 5 枚を storage image
     * として書く。材質ごとの形では VT の要求も書く。GBufferPass・MegaGeometryPass
     * はこのパスが有効なとき GBuffer の描画を止める （GBuffer
     * のクリアだけを行う）ので、描かれなかった画素はクリア値のまま。
     *
     * 有効なのは --visibility-buffer=on のときだけ（SceneView
     * が足す）。使えないとき（GetFallbackReason が None 以外。装置が 対応しない・ID
     * のラスタや解決のパイプラインが作れていない・渡された計算スキニングのパスが有効なのにパイプラインが作れていない）は、
     * 何も宣言せず、GBufferPass・MegaGeometryPass も描画を止めない（従来の GBuffer
     * の描画のまま動く）。 その旨を VISBUFFER_FALLBACK reason=<理由> で 1
     * 回ログへ出す。GBufferPass・MegaGeometryPass はこのパスへの参照
     * （SetVisibilityResolvePass）で同じ判定を問い合わせる。参照を渡さないと、装置の機能だけの判定になる。
     */
    class VisibilityResolvePass final : public IViewPass, public IRenderGraphPass
    {
    public:
        VisibilityResolvePass();
        ~VisibilityResolvePass() override;

        const char* GetName() const override { return "VisibilityResolvePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief 記録の表・材質の表・インスタンスの表の取り出し元（同じ View の
         * VisibilityRasterPass） */
        void SetRasterPass(const VisibilityRasterPass *pass)
        {
            m_RasterPass = pass;
        }
        const VisibilityRasterPass *GetRasterPass() const
        {
            return m_RasterPass;
        }
        /** @brief 変形した頂点（スキニング）の取り出し元（同じ View
         * のパス）。頂点はデバイスアドレスで読むので、グラフの読み取りに足して書き込みを見せる
         */
        void SetSkinningComputePass(const SkinningComputePass *pass)
        {
            m_SkinningComputePass = pass;
        }
        const SkinningComputePass *GetSkinningComputePass() const
        {
            return m_SkinningComputePass;
        }
        /**
         * @brief 材質ごとのタイルの分類のパス（同じ View
         * のパス。グラフでこのパスより前に足し、有効にしておく）
         *
         * 渡すと、分類の引数・一覧・統計を GenericRead で読み、材質ごとに 1
         * 回ずつ間接 dispatch する形で解決する （Initialize
         * が材質ごとの形のパイプラインも作る）。渡さない・分類が使えない・そのフレームの分類が食い違うときは、
         * 画面全体の直接 dispatch で解決する（画面を空にしない）。Initialize
         * の前に渡す
         */
        void SetClassifyPass(const MaterialTileClassifyPass* pass) { m_ClassifyPass = pass; }
        const MaterialTileClassifyPass* GetClassifyPass() const { return m_ClassifyPass; }
        /** @brief 材質ごとの形の記録が使い回す作業配列の容量（検査用） */
        size_t GetTileUseScratchCapacity() const { return m_Resolve.GetTileUseScratchCapacity(); }

        /** @brief 最後の Execute が解決を記録したか */
        bool WasResolved() const
        {
            return m_bResolved;
        }
        /** @brief 最後の Execute が、材質ごとの間接 dispatch の形で解決したか（false
         * なら画面全体の直接 dispatch、または解決していない） */
        bool WasResolvedWithTiles() const
        {
            return m_bResolvedWithTiles;
        }
        /** @brief 最後の Execute が記録した間接 dispatch
         * の数（材質の数。タイルの形で解決しなかったときは 0） */
        uint32_t GetLastTileDispatchCount() const
        {
            return m_LastTileDispatchCount;
        }

        /**
         * @brief この装置・今の初期化の状態で、解決が GBuffer
         * を書けない理由（書けるなら None）
         *
         * GBufferPass・MegaGeometryPass は、これが None のときだけ GBuffer
         * の描画を止める。ID のラスタのパイプラインや解決の
         * パイプライン、スキニングの頂点を作る計算パイプラインが作れていないのに描画を止めると、画面（スキニングの物）が空に
         * なるため。初期化（View が Declare
         * の前に行う）の後に使う。計算スキニングのパスは、渡されていて有効なときだけ見る。
         *
         * mode は今のビューポートの表示。Wireframe のときは、ID
         * のラスタが線のパイプラインを持たなければ RasterUnavailable になり （GBuffer
         * の描画がワイヤーフレームを描く）、持てば解決が線の画素を GBuffer へ書く。
         */
        VisibilityResolveGeometry::FallbackReason GetFallbackReason(const RHI::IDevice* device,
                                                                    DebugViewMode mode = DebugViewMode::Normal) const;
        bool CanResolve(const RHI::IDevice* device, DebugViewMode mode = DebugViewMode::Normal) const
        {
            return GetFallbackReason(device, mode) == VisibilityResolveGeometry::FallbackReason::None;
        }

    private:
        const VisibilityRasterPass* m_RasterPass = nullptr;
        const SkinningComputePass* m_SkinningComputePass = nullptr;
        const MaterialTileClassifyPass* m_ClassifyPass = nullptr;
        VisibilityResolve m_Resolve;
        RGTextureHandle m_IdHandle;
        RGResourceHandle m_AlbedoHandle;
        RGResourceHandle m_NormalHandle;
        RGResourceHandle m_MaterialHandle;
        RGResourceHandle m_VelocityHandle;
        RGResourceHandle m_EmissiveHandle;
        // 分類の出力の読み取り（Declare が読むと宣言したときだけ有効）
        RGResourceHandle m_TileArgsHandle;
        RGResourceHandle m_TileListHandle;
        RGResourceHandle m_TileStatsHandle;
        bool m_bResolved = false;
        bool m_bResolvedWithTiles = false;
        uint32_t m_LastTileDispatchCount = 0;
        bool m_bLoggedFallback = false;
        // 材質ごとの形の記録を、変わったときだけログへ出すための前回の値
        bool m_bLoggedTileState = false;
        bool m_bLoggedTileUsed = false;
        uint32_t m_LoggedTileMaterials = 0;
        const char* m_LoggedTileReason = nullptr;
    };

} // namespace NorvesLib::Core::Rendering
