#pragma once

#include "Rendering/GpuResourceTypes.h"
#include "Rendering/ITextureHandleRegistrar.h"
#include "Rendering/MaterialTypes.h"
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
    class SparsePagePool;
    class TileUploader;
    class VirtualTextureFeedbackRing;
    class VirtualTextureRequestSet;

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

    private:
        friend class RenderResources;
        explicit SkinnedMeshResources(RenderResources* pOwner);

        RenderResources* m_pOwner = nullptr;
    };

    class MegaGeometryResources
    {
    public:
        MegaGeometry::MegaMeshHandle CreateMegaMesh(const MegaGeometry::MegaMeshCreateInfo &createInfo);
        const MegaGeometry::MegaMeshGPUData *GetMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;
        void ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle);

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

    private:
        friend class RenderResources;
        friend class ModelAssetRuntime;

        explicit MegaGeometryResources(RenderResources *pOwner);
        void ReleaseModelUnmanaged(ModelHandle handle);

        RenderResources *m_pOwner = nullptr;
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

        // 積んであるタイル・ミップテイルのコピーを、フレームのコマンドの先頭へ記録する（RenderThread。
        // render pass の外で、BeginRetireFrame の後・コマンドを開いた直後に呼ぶ）。記録したコピーの数を返す。
        // sparse に対応しないデバイス・未初期化では何もせず 0。
        uint32_t RecordTileUploads(RHI::ICommandList &commandList);
        // ステージングのリング経由でテクスチャの領域へ書く経路。sparse に対応しないデバイス・未初期化では nullptr。
        TileUploader *GetTileUploader() const;

        // VT の要求（材質のシェーダーが書くタイルの要求）を、3つのバッファのリングで数フレーム遅れて読み戻して集計する仕組み。
        // 提出が完了したバッファだけを GPU を待たずに読むので、RenderThread は止まらない。BeginRetireFrame・CommitRetireFrame・
        // AbortRetireFrame で一緒に進む。sparse に対応しないデバイス・未初期化では nullptr。初期状態は無効（SetEnabled で有効にする）。
        VirtualTextureFeedbackRing *GetVirtualTextureFeedback() const;
        // 溜まった要求の集計を out へ渡し、こちらは空に戻す（VT のストリーマが毎フレーム呼ぶ。どのスレッドからでもよい）。
        // 要求も溢れた件数も無いとき・リングが無いときは false で、out は変えない。
        bool TakeVirtualTextureRequests(VirtualTextureRequestSet &out);

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
