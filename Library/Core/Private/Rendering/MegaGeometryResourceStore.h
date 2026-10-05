#pragma once

#include "Rendering/GeometryPool.h"
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
     */
    class MegaGeometryResourceStore final
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

        void Clear();

    private:
        struct MegaMeshEntry
        {
            MegaGeometry::MegaMeshGPUData Data;
            GeometryPool::RegionLease Lease;
            // 区画の中身（クラスタ・頂点・インデックスを並べたもの）。リングへ積み終えるまで持つ
            Container::VariableArray<uint8_t> StagedBytes;
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

        // 区画を GpuRetireQueue へ渡す（無ければすぐ返す）。未記録のコピーは無効にする。m_Mutex を持って呼ぶ
        void RetireEntryLocked(MegaMeshEntry &entry);
        bool IsEntryGpuReadyLocked(const MegaMeshEntry &entry) const;

        Container::TSharedPtr<RHI::IDevice> m_Device;
        Thread::Atomic<uint64_t> &m_NextHandleId;
        GeometryPool *m_Pool = nullptr;
        TileUploader *m_Uploader = nullptr;
        GpuRetireQueue *m_RetireQueue = nullptr;
        Container::Map<uint64_t, MegaMeshEntry> m_MegaMeshes;
        // 中身をまだリングへ積み終えていないメッシュ（古い順）
        Container::VariableArray<uint64_t> m_PendingUploadIds;
        Container::Map<uint64_t, ModelResourceData> m_Models;
        mutable Thread::Mutex m_Mutex;
    };

} // namespace NorvesLib::Core::Rendering
