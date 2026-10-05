#pragma once

// ビジビリティバッファの幾何の解決: VisBuffer.Id の画素から三角形を引き、GBuffer の Albedo・Normal・Velocity を書く。
//
// 計算シェーダー（visbuffer_resolve.comp。本体は Common/VisibilityResolve.glsl）が、1 スレッド 1 画素で次を行う。
//   - ID から描画の記録（VisibilityBuffer.h）と三角形を引き、頂点・インデックスのデバイスアドレスから 3 頂点を読む。
//   - 画素の中心を通るカメラの光線と三角形の平面の交点で、透視の補正つきの重心座標を求める。
//     隣の画素（x+1、y+1）の光線でも交わらせて、位置・UV の解析的な微分（前進差分）を得る。
//   - 法線（補間して正規化）・接線の基底・前のフレームの頂点から求めた速度を作り、GBuffer へ書く。
//     Albedo は材質の基本色（テクスチャなし。α = 1）。
// GBuffer の形式・意味（Albedo.a、法線の格納、Velocity の式）は、ラスタの経路（gbuffer.frag）と同じ。
//
// 材質ごとのタイルの分類（MaterialTileClassifyPass）の引数・一覧は、材質ごとに間接 dispatch する材質の解決が使う。
// この解決は材質に依らない値（基本色の定数）だけを書くので、画面のタイル（8x8）を 1 回の直接 dispatch で処理する。
// RHI の ICommandList に間接 dispatch が無い間は、材質ごとの dispatch はできない（材質のテクスチャを引く解決で扱う）。

#include "Container/Containers.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    struct DeviceCapabilities;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;
    class SkinningComputePass;
    class VisibilityRasterPass;
    struct CameraViewConstants;
    struct ViewRenderContext;

    namespace VisibilityResolveGeometry
    {
        /** @brief ResolveParams::Screen[2] のビット（シェーダーの RESOLVE_FLAG_* と同じ） */
        constexpr uint32_t FLAG_PREVIOUS_VALID = 1u;
        /** @brief ワークグループ（画面のタイル）の一辺の画素数（シェーダーの local_size と同じ） */
        constexpr uint32_t TILE_SIZE = 8;
        /** @brief 検証用の書き出しの、画素あたりの vec4 の数（シェーダーの VIS_RESOLVE_DUMP_STRIDE と同じ） */
        constexpr uint32_t DUMP_STRIDE_VEC4 = 12;
        constexpr uint32_t DUMP_STRIDE_BYTES = DUMP_STRIDE_VEC4 * 4u * sizeof(float);

        /**
         * @brief 検証用の書き出しの vec4 の添字（シェーダーの dump[] の並びと同じ）
         *
         * Barycentric: xyz = 重心座標、w = 描画の種類。 Uv: xy = UV、zw = 画素の x 方向の UV の微分。
         * UvDy: xy = y 方向の UV の微分。 HitPosition: xyz = カメラ相対のワールド位置。 DposDx・DposDy: 位置の微分。
         * TangentT・TangentB・TangentN: 接線の基底。 PreviousClip: 前のフレームのクリップ座標。
         * Misc: xy = 速度、z = 材質の番号、w = 三角形の番号。
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

        /** @brief シェーダーの ResolveParams（std140）と同じ 240 バイトの定数 */
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
        };
        static_assert(sizeof(ResolveParams) == 240, "visbuffer_resolve の ResolveParams（std140）と一致しなければならない");

        /**
         * @brief ラスタと同じカメラの定数から ResolveParams を作る
         * @param current 今のフレームのカメラ（GBufferPass と同じ CameraViewConstants::BuildForDevice の結果）
         * @param previous 前のフレームのカメラ。null なら前のカメラは無い（速度は 0）
         */
        ResolveParams BuildParams(const CameraViewConstants& current,
                                  const CameraViewConstants* previous,
                                  const RHI::Viewport& viewport,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t materialCount);

        /**
         * @brief この装置でビジビリティバッファの GBuffer の解決を使えるか
         *
         * ID の書き込み（geometryShader・drawIndirectFirstInstance）、頂点のデバイスアドレス（bufferDeviceAddress）、
         * RG16F などの storage image（shaderStorageImageExtendedFormats）が全部そろっているとき true。
         * false の装置では、従来の GBuffer の描画のまま動かす。
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
        };

        /** @brief ログ（VISBUFFER_FALLBACK reason=...）に出す、機械が照合する理由の名前 */
        const char* GetFallbackReasonName(FallbackReason reason);
    } // namespace VisibilityResolveGeometry

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
        /** @brief 描画のインスタンスの表（手続きメッシュ）。無ければ null（空の表を束ねる） */
        RHI::BufferPtr DrawInstances;
        uint64_t DrawInstancesBytes = 0;
        /** @brief 出力（GBuffer の Albedo・Normal・Velocity）。呼び出し前に UnorderedAccess の状態で、終わっても UnorderedAccess のまま */
        RHI::TexturePtr Albedo;
        RHI::TexturePtr Normal;
        RHI::TexturePtr Velocity;
        /** @brief 検証用の版（Initialize の bDump）の出力。画素あたり DUMP_STRIDE_BYTES。製品の版では使わない */
        RHI::BufferPtr Dump;
        VisibilityResolveGeometry::ResolveParams Params;
    };

    /**
     * @brief 幾何の解決を計算シェーダーで記録する本体（RenderGraph にも GPU のテストにも使う）
     *
     * 資源（定数・ディスクリプタセット）は FrameUseRing が持つ。Record のたびに枠の次の 1 組を使うので、
     * 1 フレームに何回 Record しても提出前の資源を上書きしない。
     */
    class VisibilityResolve final
    {
    public:
        VisibilityResolve();
        ~VisibilityResolve();

        VisibilityResolve(const VisibilityResolve&) = delete;
        VisibilityResolve& operator=(const VisibilityResolve&) = delete;

        /**
         * @brief シェーダーとパイプラインを作る。失敗したら false（以降の Record は何もしない）
         * @param bDump true なら画素ごとの中間の値を書き出す検証用の版（GPU のテスト用）
         */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager, bool bDump = false);
        void Shutdown();
        bool IsReady() const { return m_Pipeline != nullptr; }

        /** @brief 飛行中のフレームの番号の枠を選ぶ（FrameUseRing の BeginFrame と同じ） */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /**
         * @brief 解決を記録する（画面のタイルごとに 1 グループの dispatch）
         * @return 記録できたら true。入力・出力が足りない、出力が画面より小さいときは false で何も記録しない
         */
        bool Record(RHI::ICommandList* commandList, const VisibilityResolveDispatch& dispatch);

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        bool EnsurePlaceholder();

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        RHI::SamplerPtr m_Sampler;
        // 表を持たないフレームで束ねる、空の表（読まれない）
        RHI::BufferPtr m_Placeholder;
        bool m_bDump = false;
        FrameUseRing<Use> m_Uses;
    };

    /**
     * @brief VisBuffer.Id から GBuffer の Albedo・Normal・Velocity を解決する RenderGraph のパス
     *
     * VisibilityRasterPass が書いた ID と、同じパスが持つ記録の表・材質の表・インスタンスの表を読み、GBuffer の
     * 3 枚を storage image として書く。GBufferPass・MegaGeometryPass はこのパスが有効なとき GBuffer の描画を止める
     * （GBuffer のクリアだけを行う）ので、描かれなかった画素はクリア値のまま。
     *
     * 有効なのは --visibility-buffer=on のときだけ（SceneView が足す）。使えないとき（GetFallbackReason が None 以外。装置が
     * 対応しない・ID のラスタや解決のパイプラインが作れていない）は、何も宣言せず、GBufferPass・MegaGeometryPass も描画を止めない
     * （従来の GBuffer の描画のまま動く）。その旨を VISBUFFER_FALLBACK reason=<理由> で 1 回ログへ出す。
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

        /** @brief 記録の表・材質の表・インスタンスの表の取り出し元（同じ View の VisibilityRasterPass） */
        void SetRasterPass(const VisibilityRasterPass* pass) { m_RasterPass = pass; }
        /** @brief 変形した頂点（スキニング）の取り出し元（同じ View のパス）。頂点はデバイスアドレスで読むので、グラフの読み取りに足して書き込みを見せる */
        void SetSkinningComputePass(const SkinningComputePass* pass) { m_SkinningComputePass = pass; }

        /** @brief 最後の Execute が解決を記録したか */
        bool WasResolved() const { return m_bResolved; }

        /**
         * @brief この装置・今の初期化の状態で、解決が GBuffer を書けない理由（書けるなら None）
         *
         * GBufferPass・MegaGeometryPass は、これが None のときだけ GBuffer の描画を止める。ID のラスタのパイプラインや解決の
         * パイプラインが作れていないのに描画を止めると、画面が空になるため。初期化（View が Declare の前に行う）の後に使う。
         */
        VisibilityResolveGeometry::FallbackReason GetFallbackReason(const RHI::IDevice* device) const;
        bool CanResolve(const RHI::IDevice* device) const
        {
            return GetFallbackReason(device) == VisibilityResolveGeometry::FallbackReason::None;
        }

    private:
        const VisibilityRasterPass* m_RasterPass = nullptr;
        const SkinningComputePass* m_SkinningComputePass = nullptr;
        VisibilityResolve m_Resolve;
        RGTextureHandle m_IdHandle;
        RGResourceHandle m_AlbedoHandle;
        RGResourceHandle m_NormalHandle;
        RGResourceHandle m_VelocityHandle;
        bool m_bResolved = false;
        bool m_bLoggedFallback = false;
    };

} // namespace NorvesLib::Core::Rendering
