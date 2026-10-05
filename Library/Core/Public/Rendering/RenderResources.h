#pragma once

#include "Rendering/GpuResourceTypes.h"
#include "Rendering/ITextureHandleRegistrar.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/MeshIndexChunks.h"
#include "Rendering/MegaGeometry/GeometryPageRequestSet.h"
#include "Rendering/MegaGeometry/GeometryPageTable.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/NeuralMaterialResource.h"
#include "Rendering/ProceduralMeshGPUData.h"
#include "Rendering/SkinnedMeshTypes.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/TextureAssetTypes.h"
#include "Rendering/TextureAsyncTypes.h"
#include "Rendering/VideoMemoryBudgetManager.h"
#include "Rendering/VertexLayout.h"
#include "Container/PointerTypes.h"
#include "Delegate/Delegate.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IBuffer;
    class ICommandList;
    class IDevice;
    class IShader;
    class ITexture;
}

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Core::Rendering
{
    struct SubMesh;
    struct AssetRuntimeSnapshotReloadTestAccess;
    struct TextureAssetRuntimeShutdownTestAccess;
    class ModelAssetRuntime;
    class TextureAssetRuntime;
    class RenderWorld;
    class GeometryPool;
    class SparsePagePool;
    class TileUploader;
    class VirtualTextureFeedbackRing;
    class VirtualTextureRequestSet;
    class VirtualTextureStreamer;

    class GpuResources
    {
    public:
        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo);
        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo,
                                  const void *data,
                                  size_t dataSize);
        bool UpdateBuffer(BufferHandle handle,
                          const void *data,
                          size_t dataSize,
                          size_t offset = 0);
        void ReleaseBuffer(BufferHandle handle);

        SamplerHandle GetDefaultSampler();
        SamplerHandle GetPointSampler();
        void ReleaseSampler(SamplerHandle handle);

        ShaderHandle CreateShader(const ShaderCreateInfo &createInfo);
        ShaderHandle LoadShader(const Container::String &path, ShaderStage stage);
        void ReleaseShader(ShaderHandle handle);

        VertexLayoutHandle RegisterVertexLayout(const VertexLayout &layout);
        const VertexLayout *GetVertexLayout(VertexLayoutHandle handle) const;

        RHI::IBuffer *GetRHIBuffer(BufferHandle handle) const;
        RHI::IShader *GetRHIShader(ShaderHandle handle) const;
        ResourceStats GetResourceStats() const;

    private:
        friend class RenderResources;

        explicit GpuResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class TextureResources : public ITextureHandleRegistrar
    {
    public:
        TextureHandle CreateTexture(const TextureCreateInfo &createInfo);
        TextureHandle CreateTexture(const TextureCreateInfo &createInfo,
                                    const void *data,
                                    size_t dataSize);
        TextureHandle LoadTexture(const Container::String &path);
        uint32_t LoadTextureAsync(const Container::String &path,
                                  NorvesLib::Core::Delegate<void, TextureHandle> callback = {});
        uint32_t FlushCompletedTextureLoads();
        uint32_t GetPendingAsyncLoadCount() const;

        bool SetTextureAssetRoot(const Container::String &assetRoot);
        bool LoadTextureAssetManifestFromJsonText(
            const Container::String &jsonText,
            const Container::String &sourceName = Container::String());
        bool ResetTextureAssetManifest();
        bool SetTextureAssetFallbackMode(TextureAssetFallbackMode mode);

        [[nodiscard]] PreparedTextureAsset PrepareTextureAssetForWorker(
            const Container::String &requestPath,
            const Container::String &resolvedFallbackPath = {},
            const char *role = "worker",
            uint32_t requestId = 0);
        [[nodiscard]] bool IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const;
        [[nodiscard]] TextureHandle FinalizePreparedTextureAsset(
            const PreparedTextureAsset &prepared,
            const char *role = "main_render",
            uint32_t requestId = 0);
        [[nodiscard]] bool TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
            const PreparedTextureAsset &prepared,
            PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
            Container::String *pOutReason = nullptr,
            const char *role = "worker",
            uint32_t requestId = 0) const;

        TextureHandle RegisterExternalTexture(
            Container::TSharedPtr<RHI::ITexture> rhiTexture,
            const Container::String &debugName = Container::String()) override;
        void ReleaseTexture(TextureHandle handle) override;

        RHI::ITexture *GetRHITexture(TextureHandle handle) const;
        Container::TSharedPtr<RHI::ITexture> GetRHITexturePtr(TextureHandle handle) const;

        // クック済み（NVTEX v0.2）の材質のテクスチャを、sparse の VT として作る。作成時にファイルのメタデータと
        // ミップテイルだけを範囲読みし、タイルは要求に応じてストリーマが読む。
        // ミップテイルは次の UpdateVirtualTextureStreaming で結ぶので、IsVirtualTextureReady が true になるまで
        // 材質でサンプルしてはいけない。sparse に対応しないデバイス・v0.2 でないテクスチャ・マニフェストに無いパスは
        // 無効なハンドルを返す。フィードバックを有効にできないときも、VT を解放して無効なハンドルを返す。
        // pOutVirtualTextureIndex には VT の表の添字（フィードバックの要求が指す番号）を返す。
        TextureHandle CreateVirtualTexture(const Container::String &path, uint32_t *pOutVirtualTextureIndex = nullptr);
        // VT のミップテイルが常駐してサンプルできるか。VT でないハンドルは false
        bool IsVirtualTextureReady(TextureHandle handle) const;
        // デバイスが sparse の VT に対応していて、CreateVirtualTexture を試せるか
        // （材質のシェーダーが要求を書けるフィードバックの対応も含む。false のデバイスは全常駐で読む）
        bool SupportsVirtualTexture() const;
        // CreateVirtualTexture で作り、ミップテイルが常駐してサンプルできるようになったら callback をメインスレッドで呼ぶ
        // （FlushCompletedTextureLoads で呼ばれる。読み込み中は GetPendingAsyncLoadCount に数える）。
        // 作れなかったときは false を返し、callback は呼ばない（呼び出し側が全常駐の LoadTextureAsync へ戻す）。
        // 一定のフレームの間にミップテイルが常駐しなければ VT を解放し、無効なハンドルで callback を呼ぶ。
        bool CreateVirtualTextureAsync(const Container::String &path,
                                       NorvesLib::Core::Delegate<void, TextureHandle> callback);
        // VT のストリーマが落ち着いているか（要求済み・読み込み中・結び待ちのタイルが無い）。VT が無いときは true。
        // 起動画面の決定的な撮影が、タイルがそろってから撮るために使う。
        bool IsVirtualTextureStreamingIdle() const;
        // VT の表の添字を取る。VT でないハンドルは false
        bool TryGetVirtualTextureIndex(TextureHandle handle, uint32_t &outIndex) const;

        // 材質のシェーダーが VT の要求（フィードバック）を書く先。このフレームの descriptor に束ねる（RenderThread）。
        struct VirtualTextureFeedbackTarget
        {
            // 束ねるバッファ。デバイスがフィードバックに対応していれば常に有効（今のフレームのバッファが無いときは、書かれない
            // 小さな代替）。対応しないデバイス（材質のシェーダーに binding が入らない）では null
            Container::TSharedPtr<RHI::IBuffer> Buffer;
            uint64_t Bytes = 0;
            // このフレームの要求のバッファを獲得できている（材質が要求を書いてよい）か
            bool bWriting = false;
            // 材質のパラメータの巡回の位相を決めるフレームの番号
            uint64_t Frame = 0;
        };
        VirtualTextureFeedbackTarget GetVirtualTextureFeedbackTarget() const;
        // 対応するデバイスなら、要求のバッファのリングを有効にする（最初の VT の作成で呼ばれる。何度呼んでもよい）。有効にできたら true
        bool EnableVirtualTextureFeedback();

    private:
        friend class RenderResources;

        explicit TextureResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class MaterialResources
    {
    public:
        MaterialHandle Create(const MaterialCreateData &createInfo);
        const MaterialResourceData *GetData(MaterialHandle handle) const;
        bool Update(MaterialHandle handle, const MaterialCreateData &createInfo);
        void Release(MaterialHandle handle);

        MaterialHandle CreateNeural(const NeuralMaterialDesc &desc);
        Container::VariableArray<NeuralMaterialResource *> GetNeuralResources() const;

    private:
        friend class RenderResources;

        explicit MaterialResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class MeshResources
    {
    public:
        using MeshGPUData = ProceduralMeshGPUData;

        bool Register(MeshDataHandle handle,
                      const void *vertices,
                      size_t vertexSize,
                      const uint32_t *indices,
                      uint32_t indexCount);
        bool Register(MeshDataHandle handle,
                      const void *vertices,
                      size_t vertexSize,
                      const uint32_t *indices,
                      uint32_t indexCount,
                      const SubMesh* subMeshes,
                      uint32_t subMeshCount);
        const MeshGPUData *GetGPUData(MeshDataHandle handle) const;
        bool TryGetSubMeshRanges(MeshDataHandle handle,
                                 Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS>& out,
                                 uint32_t& outCount) const;
        /**
         * @brief 登録した頂点の位置から求めたローカル空間のAABBを取得する
         * @return 登録済みで、頂点が Mesh3DVertex の並びだったメッシュなら true
         */
        bool TryGetLocalBounds(MeshDataHandle handle, BoundingBox &outBounds) const;
        void Unregister(MeshDataHandle handle);

    private:
        friend class RenderResources;

        explicit MeshResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class SkinnedMeshResources
    {
    public:
        void BeginFrame(uint64_t completedSubmissionSerial);
        bool PrepareDraw(const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease,
                         const Container::VariableArray<Math::Matrix4x4>& bonePalette,
                         const Math::Matrix4x4& worldTransform,
                         SkinnedMeshPreparedDraw& outPrepared,
                         const Container::VariableArray<Math::Matrix4x4>* previousBonePalette = nullptr,
                         const Math::Matrix4x4* previousWorldTransform = nullptr);
        bool MarkLastUse(const SkinnedMeshPreparedDraw& prepared,
                         const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease);
        bool CommitSubmittedFrame(uint64_t submissionSerial);
        void AbortFrame();
        bool GetLifetimeSnapshot(SkinnedMeshHandle handle, SkinnedMeshGpuLifetimeSnapshot& outSnapshot) const;
        bool IsResident(SkinnedMeshHandle handle) const;
        // 登録時に分けた128三角形以下の塊（未登録は false）
        bool TryGetChunks(SkinnedMeshHandle handle, Container::VariableArray<MeshIndexChunk>& out) const;

    private:
        friend class RenderResources;
        explicit SkinnedMeshResources(RenderResources* pOwner);

        RenderResources* m_pOwner = nullptr;
    };

    class MegaGeometryResources
    {
    public:
        MegaGeometry::MegaMeshHandle CreateMegaMesh(const MegaGeometry::MegaMeshCreateInfo &createInfo);
        // 登録済みのメッシュの GPU データ。頂点・インデックス・クラスタはジオメトリのプールの区画にあり、
        // 書き込みが GPU で完了したとは限らない（描画・影・レイトレーシングは GetReadyMegaMeshGPUData を使う）。
        const MegaGeometry::MegaMeshGPUData *GetMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;
        // 区画へのコピーが全て GPU で完了し、GPU から読めるメッシュだけの GPU データ。未完了・未登録は nullptr。
        const MegaGeometry::MegaMeshGPUData *GetReadyMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;
        // 登録済みで、まだ GPU から読める状態になっていないメッシュがあるか（読み込みの落ち着きの判定に使う）。
        bool HasPendingGpuUploads() const;
        void ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle);

        // ---- ジオメトリのページ（常駐の表と、カリングが書く要求） ----
        // ページの表の写し。表の版が inOutVersion と違うときだけ out へ複製して true を返し、版を更新する
        // （描画のパスが、フレームの前に GPU の表を書き直すのに使う）。
        bool CopyPageTableIfChanged(uint64_t &inOutVersion,
                                    Container::VariableArray<MegaGeometry::GeometryPageTable::Entry> &out) const;
        // ページの表のグローバルな位置から、メッシュ（ハンドルの番号）とメッシュの中のページの番号を引く。
        // tableVersion は要求を書いたフレームのシェーダーが見た表の版（GeometryPageRequestSet::Request::TableVersion）。
        // その版より後に割り当てられた範囲（解放の後に別のメッシュが再利用したもの）は、要求の持ち主ではないので false。
        bool ResolvePageTableIndex(uint32_t globalIndex, uint64_t tableVersion, uint64_t &outMeshId,
                                   uint32_t &outPageId) const;
        // メッシュのページを区画 region に常駐させる（PAGE_NON_RESIDENT なら非常駐にする）。ストリーマと試験が使う。
        bool SetMegaMeshPageRegion(MegaGeometry::MegaMeshHandle handle, uint32_t pageId, uint32_t region);
        // このフレームのカリングが要求を書くバッファと容量。獲得できなかったフレーム（無効・空きなし）は null と 0。
        RHI::BufferPtr GetCurrentPageRequestBuffer() const;
        uint32_t GetCurrentPageRequestCapacity() const;
        // このフレームのシェーダーが見るページの表の版を、要求のバッファへ結び付ける（SyncPageTable の後に1回）。
        void SetCurrentPageTableVersion(uint64_t tableVersion);
        // 要求のバッファへのシェーダーの書き込みを、ホストの読み取りへ見せるバリアを記録する（最後の書き込みの後に1回）。
        bool RecordPageRequestHostBarrier(RHI::ICommandList &commandList);
        // 数フレーム遅れで読み戻して溜めた要求を out へ渡す（ストリーマが読む）。無ければ false。
        bool TakePageRequests(MegaGeometry::GeometryPageRequestSet &out);

        // ページを持つメッシュ（NVMESH v1.1）を、根のページだけ常駐させて、残りを要求から読み込むか。既定は有効。
        // 起動引数 --geometry-streaming=off で無効にすると、全てのページを常駐させる（見た目・VRAM の比較用）。
        // 起動時（メッシュを作る前）に設定する。
        void SetPageStreamingEnabled(bool bEnabled);
        bool IsPageStreamingEnabled() const;
        // ページの読み込み・書き込みが進行中、または要求されたまま読み込んでいないページがあるか（読み込みの落ち着きの判定に使う）。
        bool HasPendingPageStreaming() const;

        ModelHandle RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                  const Container::String &debugName = "",
                                  const Container::String &sourcePath = "");
        ModelHandle LoadModel(const Asset::AssetSystem& assetSystem,
                              const Container::String& logicalPath);
        bool SetModelAssetSystem(Container::TSharedPtr<const Asset::AssetSystem> assetSystem);
        uint32_t LoadModelAsync(const Container::String& logicalPath,
                                Delegate<void, ModelHandle> callback = {});
        uint32_t FlushCompletedModelLoads(uint32_t maxLoadsPerFrame = 1);
        void CancelModelLoad(uint32_t requestId);
        // Blocking cancellation and RenderResources clear/shutdown must not be called by a completion callback.
        bool CancelPendingModelLoadsAndWait();
        uint32_t GetPendingAsyncModelLoadCount() const;
        MegaGeometry::MegaMeshHandle GetModelMegaMeshHandle(ModelHandle handle) const;
        void ReleaseModel(ModelHandle handle);

        // MegaGeometry の遮蔽カリング（2パス）を使うか。既定は有効。起動引数 --mega-occlusion=off で無効にすると、
        // 遮蔽の判定なしの従来の1回の判定で描く。起動時（最初のフレームの前）に設定する。
        void SetOcclusionCullingEnabled(bool bEnabled) { m_bOcclusionCullingEnabled = bEnabled; }
        bool IsOcclusionCullingEnabled() const { return m_bOcclusionCullingEnabled; }

    private:
        friend class RenderResources;
        friend class ModelAssetRuntime;

        explicit MegaGeometryResources(RenderResources *pOwner);
        void ReleaseModelUnmanaged(ModelHandle handle);

        RenderResources *m_pOwner = nullptr;
        bool m_bOcclusionCullingEnabled = true;
    };

    class RenderResources
    {
    public:
        RenderResources();
        ~RenderResources();

        RenderResources(const RenderResources &) = delete;
        RenderResources &operator=(const RenderResources &) = delete;

        bool Initialize(Container::TSharedPtr<RHI::IDevice> device);
        void Shutdown();
        bool IsInitialized() const;

        // VRAM の上限（MB。0 は上限なし）。起動引数 --vram-budget-mb から渡される。
        void SetVideoMemoryCapMb(uint64_t capMb);
        uint64_t GetVideoMemoryCapMb() const;

        // GameThread から毎フレーム呼ぶ。初回と、その後は約1秒ごとに予算を取得して、
        // プールへ割り振る量（VideoMemoryBudgetManager）を計算し、VT のプールの上限へ反映する。
        // 予算か使用量が 1% 以上変わったときだけ VRAM_BUDGET を、割り振りが変わったときだけ VRAM_POOLS をログへ出す。
        void PollVideoMemoryBudget();

        // 直近の PollVideoMemoryBudget の割り振り結果（GameThread から読む。まだ計算していなければ全て 0・上限なし）。
        // 後の段のジオメトリ・VSM のプールも、ここからそれぞれの目標の大きさを受け取る。
        const VideoMemoryBudgetResult &GetVideoMemoryBudgetResult() const;
        // プールの取り分の重みを決める（既定は VT が全て）。
        void SetVideoMemoryPoolShare(VideoMemoryPool pool, uint32_t weight);

        // GPU が使い終わるまで RHI 資源の破棄を待つ仕組み（ReleaseTexture・ReleaseBuffer が使う）。
        // RenderThread が、フレームの記録の開始（完了済みの提出 serial を渡す）・提出・中止の
        // それぞれで呼ぶ。呼ばれない環境（ヘッドレスのテスト等）では、解放は即座に破棄される。
        void BeginRetireFrame(uint64_t completedSubmissionSerial);
        void CommitRetireFrame(uint64_t submissionSerial);
        void AbortRetireFrame();
        // 破棄を待っている RHI 資源の数（観測用）。
        size_t GetPendingRetireCount() const;

        // sparse テクスチャへ結ぶ物理メモリのページのプール。sparse に対応しないデバイス・未初期化では nullptr。
        SparsePagePool *GetSparsePagePool() const;

        // ジオメトリ（頂点・インデックス・クラスタ）が共有する DeviceLocal の大きなバッファのプール。未初期化では nullptr。
        // 塊のバッファは最初の確保で作るので、使わなければ VRAM を取らない。
        GeometryPool *GetGeometryPool() const;
        // プールの1つの塊の大きさ（バイト。既定 256 MiB）。Initialize の前に呼ぶ（デバイスを持たない試験が小さな塊で動かすため）。
        void SetGeometryPoolBlockBytes(uint64_t blockBytes);

        // 積んであるタイル・ミップテイル・ジオメトリの区画のコピーを、フレームのコマンドの先頭へ記録する（RenderThread。
        // render pass の外で、BeginRetireFrame の後・コマンドを開いた直後に呼ぶ）。記録したコピーの数を返す。
        // 書き込み待ちのジオメトリの中身は、フレームごとの上限の範囲でここでリングへ積んでから記録する。未初期化では何もせず 0。
        uint32_t RecordTileUploads(RHI::ICommandList &commandList);
        // ステージングのリング経由でテクスチャの領域・バッファの区画へ書く経路。未初期化では nullptr。
        TileUploader *GetTileUploader() const;

        // VT の要求（材質のシェーダーが書くタイルの要求）を、3つのバッファのリングで数フレーム遅れて読み戻して集計する仕組み。
        // 提出が完了したバッファだけを GPU を待たずに読むので、RenderThread は止まらない。BeginRetireFrame・CommitRetireFrame・
        // AbortRetireFrame で一緒に進む。sparse に対応しないデバイス・未初期化では nullptr。初期状態は無効（SetEnabled で有効にする）。
        VirtualTextureFeedbackRing *GetVirtualTextureFeedback() const;
        // 溜まった要求の集計を out へ渡し、こちらは空に戻す（VT のストリーマが毎フレーム呼ぶ。どのスレッドからでもよい）。
        // 要求も溢れた件数も無いとき・リングが無いときは false で、out は変えない。
        bool TakeVirtualTextureRequests(VirtualTextureRequestSet &out);
        // このフレームの要求のバッファへの書き込みを、ホストの読み取りへ見せるバリアを記録する。最後の書き込みの後・
        // render pass の外・コマンドの終了より前に RenderThread が呼ぶ。バッファが無い（無効・空き無し）フレームでは何もしない。
        void RecordVirtualTextureFeedbackBarrier(RHI::ICommandList &commandList);

        // VT のストリーマ。タイルの読み込み・ページの結び付け・コピーの積み込みを進める。sparse に対応しないデバイス・
        // 未初期化では nullptr。
        VirtualTextureStreamer *GetVirtualTextureStreamer() const;
        // 溜まった要求を取り出して、ストリーマを1フレーム進める（RenderThread。BeginRetireFrame の後・コマンドを開く前に呼ぶ。
        // BindSparse はコマンドの送信と同じ直列化の下で呼ぶ必要がある）。VT が1枚も無いときは何もしない。
        void UpdateVirtualTextureStreaming();

        // ジオメトリのページのストリーマを1フレーム進める（RenderThread。BeginRetireFrame の後・コマンドを開く前に呼ぶ）。
        // カリングが書いて読み戻した要求を取り込み、ページの読み込み・区画への書き込み・ページの表への公開・追い出しを行う。
        void UpdateGeometryPageStreaming();

        bool ReloadAssetRuntimeSnapshot(
            const Container::String& assetRoot,
            Container::TSharedPtr<const Asset::AssetSystem> candidate);

        void ClearAllResources();
        void CleanupUnusedResources();
        ResourceStats GetResourceStats() const;

        GpuResources &Gpu() { return m_Gpu; }
        const GpuResources &Gpu() const { return m_Gpu; }

        TextureResources &Textures() { return m_Textures; }
        const TextureResources &Textures() const { return m_Textures; }

        MaterialResources &Materials() { return m_Materials; }
        const MaterialResources &Materials() const { return m_Materials; }

        MeshResources &Meshes() { return m_Meshes; }
        SkinnedMeshResources& SkinnedMeshes() { return m_SkinnedMeshes; }
        const SkinnedMeshResources& SkinnedMeshes() const { return m_SkinnedMeshes; }

        const MeshResources &Meshes() const { return m_Meshes; }

        MegaGeometryResources &MegaGeometry() { return m_MegaGeometry; }
        const MegaGeometryResources &MegaGeometry() const { return m_MegaGeometry; }

    private:
        friend class GpuResources;
        friend class TextureResources;
        friend class SkinnedMeshResources;
        friend class MaterialResources;
        friend class MeshResources;
        friend class MegaGeometryResources;
        friend class RenderWorld;
        friend struct AssetRuntimeSnapshotReloadTestAccess;
        friend struct TextureAssetRuntimeShutdownTestAccess;

        struct Impl;

        TextureAssetRuntime* GetTextureAssetRuntimeForTesting();
        ModelAssetRuntime* GetModelAssetRuntimeForTesting();
        void CloseAsyncAssetLoadAdmissionAndWait();

        Container::TUniquePtr<Impl> m_Impl;
        GpuResources m_Gpu;
        TextureResources m_Textures;
        SkinnedMeshResources m_SkinnedMeshes;
        MaterialResources m_Materials;
        MeshResources m_Meshes;
        MegaGeometryResources m_MegaGeometry;
    };

} // namespace NorvesLib::Core::Rendering
