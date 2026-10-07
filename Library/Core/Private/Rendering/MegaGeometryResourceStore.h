#pragma once

#include "Rendering/GeometryPageStreamer.h"
#include "Rendering/GeometryPool.h"
#include "Rendering/MegaGeometry/GeometryPageLayout.h"
#include "Rendering/MegaGeometry/GeometryPageTable.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class GpuRetireQueue;
    class TileUploader;
    class MegaGeometryRegionHolder;
    struct MegaGeometryRetireSink;

    struct ModelResourceData
    {
        MegaGeometry::MegaMeshHandle MegaMesh;
        Container::String DebugName;
        Container::String SourcePath;
    };

    /**
     * @brief MegaMesh（頂点・インデックス・クラスタ）の GPU 資源の置き場
     *
     * メッシュごとのバッファは作らず、ジオメトリの共有プール（GeometryPool）の DeviceLocal の区画へ、
     * クラスタ・頂点・インデックスを1つの区画に並べて置く。中身はステージングのリング（TileUploader）経由で
     * フレームのコマンドの先頭に書くので、GPU を待たない。CreateMegaMesh は区画を確保して CPU 側の写しを積むだけで返し、
     * 書き込みは PumpUploads が毎フレーム上限の範囲でリングへ送る。GPU が読めるのは、全てのコピーの完了後
     * （IsMegaMeshGpuReady）で、描画・影・レイトレーシングはその状態になったメッシュだけを使う。
     * メッシュの解放は区画を GpuRetireQueue へ渡し、最後に使った提出の完了まで空きへ戻さない。
     *
     * ページを持つメッシュ（NVMESH v1.1）は、根のページの頂点・インデックスだけを自分の区画へ書いて常駐させ、残りのページは
     * ストリーマ（GeometryPageStreamer。このストアがその窓口 IGeometryPageBackend を務める）が要求から読んで、ページごとの区画へ
     * 書く。ページの区画はメッシュの区画と同じ塊の中にあり、書き終えたページのクラスタの記録（頂点・インデックスの位置）を
     * ページの区画を指すように書き換えてから、ページの表へ公開する。外したページの区画は GpuRetireQueue へ渡す。
     */
    class MegaGeometryResourceStore final : public IGeometryPageBackend
    {
    public:
        /**
         * @param pool 区画を借りるプール
         * @param uploader 区画へ書くステージングのリング
         * @param retireQueue 解放した区画の返却を GPU の完了まで遅らせる待ち行列（null なら解放はすぐ空きへ戻す）
         *
         * pool・uploader・retireQueue は呼び出し側（RenderResources）が持ち、このストアの Clear の後まで生かす
         * （デストラクタは触らない）。pool か uploader が null なら CreateMegaMesh は失敗する。
         */
        MegaGeometryResourceStore(Container::TSharedPtr<RHI::IDevice> device,
                                  Thread::Atomic<uint64_t> &nextHandleId,
                                  GeometryPool *pool,
                                  TileUploader *uploader,
                                  GpuRetireQueue *retireQueue);
        ~MegaGeometryResourceStore();

        MegaGeometryResourceStore(const MegaGeometryResourceStore &) = delete;
        MegaGeometryResourceStore &operator=(const MegaGeometryResourceStore &) = delete;

        /**
         * @brief ページを持つメッシュを、根のページだけ常駐させてストリーミングするか（既定は有効）
         *
         * 無効にすると、ページを持つメッシュも全てのページを区画へ書いて常駐させる（--geometry-streaming=off）。
         * メッシュを作る前に決める。
         */
        void SetPageStreamingEnabled(bool bEnabled);
        bool IsPageStreamingEnabled() const;

        /** @brief メッシュの区画の大きさの合計（バイト）。ページの区画は含まない。ストリーミングの目標から引く外せない分 */
        uint64_t GetMeshRegionBytes() const;

        MegaGeometry::MegaMeshHandle CreateMegaMesh(const MegaGeometry::MegaMeshCreateInfo &createInfo);
        /** @brief 登録済みのメッシュの GPU データ（書き込みの完了は問わない。GPU で読むときは GetReadyMegaMeshGPUData） */
        const MegaGeometry::MegaMeshGPUData *GetMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;
        /** @brief 全てのコピーが GPU で完了しているメッシュだけの GPU データ。未完了・未登録は null */
        const MegaGeometry::MegaMeshGPUData *GetReadyMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;
        bool IsMegaMeshGpuReady(MegaGeometry::MegaMeshHandle handle) const;
        void ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle);

        ModelHandle RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                  const Container::String &debugName = "",
                                  const Container::String &sourcePath = "");
        MegaGeometry::MegaMeshHandle GetModelMegaMeshHandle(ModelHandle handle) const;
        void ReleaseModel(ModelHandle handle);

        /**
         * @brief 書き込み待ちのメッシュの中身を、上限の範囲でステージングのリングへ積む（RenderThread。RecordCopies の直前）
         *
         * 古い依頼から順に、チャンク（4 MiB）単位で積む。リングの空きとフレームのコピー量の残りも超えない。
         * @param maxBytes このフレームで積む量の上限（バイト）
         * @return 積んだ量（バイト）
         */
        uint64_t PumpUploads(uint64_t maxBytes);

        /** @brief 登録済みで、まだ GPU で読める状態になっていないメッシュがあるか */
        bool HasPendingGpuUploads() const;

        /**
         * @brief ページの表の写しを取る（表の版が inOutVersion と違うときだけ。GPU の表を書き直す判断に使う）
         * @param inOutVersion 前に写した版。写したときは今の版に更新する
         * @return 版が変わっていて out へ複製したら true
         */
        bool CopyPageTableIfChanged(uint64_t &inOutVersion,
                                    Container::VariableArray<MegaGeometry::GeometryPageTable::Entry> &out) const;

        /**
         * @brief ページの表のグローバルな位置から、メッシュ（ハンドルの番号）とメッシュの中のページの番号を引く
         * @param tableVersion 要求を書いたフレームのシェーダーが見た表の版。その版より後に割り当てられた範囲
         *        （要求の後に解放されて別のメッシュが再利用した範囲）は、要求の持ち主ではないので false
         */
        bool ResolvePageTableIndex(uint32_t globalIndex, uint64_t tableVersion, uint64_t &outMeshId,
                                   uint32_t &outPageId) const;

        /**
         * @brief メッシュのページを区画 region に常駐させる（PAGE_NON_RESIDENT なら非常駐にする）。ストリーマが使う
         * @return 未登録のメッシュ・範囲外のページ・固定したページ（生成元を決められない子のページ）を非常駐にする指定なら false
         */
        bool SetMegaMeshPageRegion(MegaGeometry::MegaMeshHandle handle, uint32_t pageId, uint32_t region);

        // ---- IGeometryPageBackend（ストリーマの窓口） ----
        bool ResolveRequest(uint32_t tableIndex, uint64_t tableVersion, uint64_t &outMeshId,
                            uint32_t &outPageId) const override;
        bool GetPageDescriptor(uint64_t meshId, uint32_t pageId, GeometryPageDescriptor &out) const override;
        bool IsMeshStreamed(uint64_t meshId) const override;
        bool BeginRead(uint64_t meshId, uint32_t pageId) override;
        void CollectReads(Container::VariableArray<GeometryPageReadCompletion> &out) override;
        GeometryPageUploadStart BeginUpload(uint64_t meshId, uint32_t pageId,
                                            const Container::VariableArray<uint8_t> &data) override;
        GeometryPageUploadState PollUpload(uint64_t meshId, uint32_t pageId) const override;
        bool Publish(uint64_t meshId, uint32_t pageId) override;
        bool Evict(uint64_t meshId, uint32_t pageId) override;
        uint64_t GetCopyBytesAvailable() const override;

        void Clear();

    private:
        // ストリーミングするメッシュの、ページごとの状態
        struct StreamedPageState
        {
            enum class Phase : uint8_t
            {
                NonResident,
                Uploading,
                Resident
            };
            Phase Value = Phase::NonResident;
            // ページの区画の持ち主（NonResident では空）。手放すと GpuRetireQueue へ渡る
            Container::TSharedPtr<MegaGeometryRegionHolder> Region;
            // 区画の塊の先頭からのバイト位置と、書き込み先のクラスタの記録の範囲（書き終えたかの問い合わせに使う）
            uint64_t RegionOffsetBytes = 0;
            uint64_t RegionBytes = 0;
            uint64_t ClusterPatchOffsetBytes = 0;
            uint64_t ClusterPatchBytes = 0;
        };

        struct StreamedMesh
        {
            Container::TSharedPtr<MegaGeometry::IGeometryPageSource> Source;
            Container::VariableArray<MegaGeometry::MeshPageInfo> Pages;
            // クラスタの記録（頂点・インデックスの位置は全体の配列の位置のまま）。ページを区画へ置くときに書き換えて写す
            Container::VariableArray<MegaGeometry::GPUClusterData> Clusters;
            MegaGeometry::GeometryPageLayout::PageRelations Relations;
            Container::VariableArray<uint32_t> PageLevels;
            Container::VariableArray<StreamedPageState> PageStates;
            // ページの区画を借りる塊（メッシュの区画と同じ塊でなければ、同じバッファから引けない）
            uint32_t BlockIndex = 0;
            // メッシュの区画の基点（描画がインスタンスの基点として足す値）
            MegaGeometry::GeometryPageLayout::RegionBases MeshBases;
            uint32_t RootPageCount = 0;
        };

        struct MegaMeshEntry
        {
            MegaGeometry::MegaMeshGPUData Data;
            // ページをストリーミングするメッシュだけが持つ
            Container::TUniquePtr<StreamedMesh> Streamed;
            // 区画の持ち主。ストアとレイトレーシングのスナップショットが共有し、最後の参照が消えたときに区画を返却の待ち行列へ渡す
            Container::TSharedPtr<MegaGeometryRegionHolder> Region;
            // 区画の中身（クラスタ・頂点・インデックスを並べたもの）。リングへ積み終えるまで持つ
            Container::VariableArray<uint8_t> StagedBytes;
            // 常駐のまま固定するページ（ページの番号で引く。1 が固定。ComputeGeometryPageLinks の PinnedPages）
            Container::VariableArray<uint8_t> PinnedPages;
            uint64_t EnqueuedBytes = 0;
            bool bFullyEnqueued = false;
            // 全てのコピーが完了したと確かめた後は、問い合わせを省く
            mutable bool bGpuReady = false;
        };

        template <typename HandleType>
        HandleType AllocateHandle()
        {
            HandleType handle;
            handle.Id = m_NextHandleId.FetchAdd(1, std::memory_order_relaxed);
            return handle;
        }

        // 未記録のコピーを無効にして、ストアの持つ区画の参照を手放す。区画の返却は、スナップショットなどの参照も
        // 全部消えたときに GpuRetireQueue へ渡る（無ければすぐ返す）。m_Mutex を持って呼ぶ
        void RetireEntryLocked(MegaMeshEntry &entry);
        bool IsEntryGpuReadyLocked(const MegaMeshEntry &entry) const;

        Container::TSharedPtr<RHI::IDevice> m_Device;
        Thread::Atomic<uint64_t> &m_NextHandleId;
        GeometryPool *m_Pool = nullptr;
        TileUploader *m_Uploader = nullptr;
        // ページをストリーミングするか。メッシュの区画の大きさの合計（ページの区画は含まない）
        bool m_bPageStreamingEnabled = true;
        uint64_t m_MeshRegionBytes = 0;
        GpuRetireQueue *m_RetireQueue = nullptr;
        // 区画の持ち主が、ストアより長く生きても返却先の待ち行列を触らないようにする窓口（デストラクタで閉じる）
        Container::TSharedPtr<MegaGeometryRetireSink> m_RetireSink;
        Container::Map<uint64_t, MegaMeshEntry> m_MegaMeshes;
        // メッシュごとのページの範囲と常駐の状態（m_Mutex で守る）
        MegaGeometry::GeometryPageTable m_PageTable;
        // 中身をまだリングへ積み終えていないメッシュ（古い順）
        Container::VariableArray<uint64_t> m_PendingUploadIds;
        Container::Map<uint64_t, ModelResourceData> m_Models;
        mutable Thread::Mutex m_Mutex;
    };

} // namespace NorvesLib::Core::Rendering
