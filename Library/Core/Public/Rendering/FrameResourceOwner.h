#pragma once

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/FrameUploadArena.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "Thread/Atomic.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    /** @brief フレームの持ち主が区別できる飛行中のフレームの数の上限（スワップチェーンの飛行中のフレーム数はこれ以下でなければならない） */
    inline constexpr uint32_t FrameResourceMaxInFlightSlots = 4;

    /**
     * @brief フレームの持ち主から借りた物に付ける世代
     *
     * どの飛行中のスロットの、どの開始で借りたかを表す。世代は開始のたびに全部のスロットを通して 1 ずつ増えるので、
     * 同じスロットでも前のフレームで借りた物とは必ず違う値になる。0 は未設定。
     */
    struct FrameResourceGeneration
    {
        static constexpr uint32_t InvalidSlot = ~0u;

        uint32_t SlotIndex = InvalidSlot;
        uint64_t Generation = 0;

        bool IsSet() const { return SlotIndex != InvalidSlot && Generation != 0; }
    };

    /** @brief フレームの持ち主の寿命の段 */
    enum class FrameResourceOwnerState : uint8_t
    {
        /** 借りた物は無い（回収済み、またはまだ使っていない） */
        Idle,
        /** 開始から提出・中止まで。借りられるのはこの間だけ */
        Recording,
        /** 提出した。結び付けた提出の通し番号の完了で回収する */
        Submitted,
        /** 提出しなかった。GPU は使っていないので次の開始で回収する */
        Aborted,
    };

    inline const char* GetFrameResourceOwnerStateName(FrameResourceOwnerState state)
    {
        switch (state)
        {
        case FrameResourceOwnerState::Idle:
            return "Idle";
        case FrameResourceOwnerState::Recording:
            return "Recording";
        case FrameResourceOwnerState::Submitted:
            return "Submitted";
        case FrameResourceOwnerState::Aborted:
            return "Aborted";
        }
        return "Unknown";
    }

    /** @brief 世代のフレームを提出したか（記録したコマンドが GPU で実行されるか） */
    enum class FrameSubmissionOutcome : uint8_t
    {
        /** まだ記録中（今のフレーム）。同じフレームで先に記録したコマンドは、後に記録したコマンドより先に実行される */
        Recording,
        /** 提出した。GPU はそのフレームのコマンドを、後のフレームのコマンドより先に実行する */
        Submitted,
        /** 提出しなかった。そのフレームに記録したコマンドは実行されない */
        Aborted,
        /** 記録が残っていない（古すぎる・未設定・持ち主の外の世代）。呼び出し側は提出しなかったものとして扱う */
        Unknown,
    };

    /**
     * @brief 終わったフレームの世代ごとの提出の結果（直近の Capacity 個）
     *
     * 世代は全部のスロットを通して 1 ずつ増えるので、世代を Capacity で割った余りの位置に置く。RenderThread が提出・中止の
     * ときに書き、パスが記録中に読む（書くのは記録の外なので排他は持たない）。
     */
    struct FrameSubmissionHistory
    {
        static constexpr uint32_t Capacity = 16;

        uint64_t Generations[Capacity] = {};
        uint8_t Submitted[Capacity] = {};

        void Record(uint64_t generation, bool bSubmitted)
        {
            if (generation == 0)
            {
                return;
            }
            const uint32_t index = static_cast<uint32_t>(generation % Capacity);
            Generations[index] = generation;
            Submitted[index] = bSubmitted ? 1u : 0u;
        }

        FrameSubmissionOutcome Find(uint64_t generation) const
        {
            if (generation == 0)
            {
                return FrameSubmissionOutcome::Unknown;
            }
            const uint32_t index = static_cast<uint32_t>(generation % Capacity);
            if (Generations[index] != generation)
            {
                return FrameSubmissionOutcome::Unknown;
            }
            return Submitted[index] != 0 ? FrameSubmissionOutcome::Submitted : FrameSubmissionOutcome::Aborted;
        }
    };

    /**
     * @brief フレームの持ち主から借りた記述子セット
     *
     * Set は共有のレイアウトから借りたセットで、借りたフレームの回収まで使える。Bind* と Update で中身を書き、
     * SetDescriptorSet に渡す。回収の後は Set が IsExpired() になり、書き込みも束縛も拒まれる。
     */
    struct FrameDescriptorAllocation
    {
        RHI::DescriptorSetPtr Set;
        uint32_t GenerationSlotIndex = FrameResourceGeneration::InvalidSlot;
        uint64_t Generation = 0;

        bool IsValid() const { return static_cast<bool>(Set); }
    };

    /**
     * @brief フレームの持ち主から借りる口（リング・記述子の pool など）が実装する回収の受け口
     *
     * 持ち主はフレームの分を回収するとき（結び付けた提出の完了、または提出しなかったフレームの次の開始）に呼ぶ。
     * 呼ばれた時点で、そのフレームに借りた物を GPU は使っていないので、全部を戻して次のフレームに貸してよい。
     * RenderThread から呼ぶ。
     */
    class IFrameResourceAllocator
    {
    public:
        virtual ~IFrameResourceAllocator() = default;

        /** @brief generation の世代で借りた物を全部戻す */
        virtual void OnFrameResourcesReclaimed(const FrameResourceGeneration& generation) = 0;
    };

    class FrameResourceOwner;
    class FrameResourceOwners;

    /**
     * @brief フレームの持ち主から借りる口（記録先のコマンドリスト 1 本分）
     *
     * CPU から書ける領域（FrameUploadArena）と記述子の pool を 1 組ずつ持つ。持ち主自身が既定の口（番号 0）で、
     * RenderThread の直列の記録はこれから借りる。RenderGraph が Parallel の区間を記録するときは、区間（コマンドリスト）
     * ごとに持ち主から別の口を借りて（FrameResourceOwner::AcquireLane）写した文脈の FrameResources に入れるので、
     * 同時に記録するワーカーが 1 つのカーソル・pool を共有しない（Vulkan は同じ pool からの確保に外部の同期を求める）。
     * 口の裏のページと pool は持ち主に束ねられ、戻す時期は持ち主の段（回収）だけで決まる。パスを書く人は口の違いを
     * 気にせず、文脈の FrameResources から借りればよい。
     *
     * ワーカーが記録中にページ・pool を足してよい条件（IRenderGraphPass::PrepareParallelRecord の約束と対になる）:
     * - 足すのは自分の区間の口だけ。1 つの口を同時に使うのは、1 本のコマンドリストを記録する 1 つのワーカーだけ。
     * - 足す処理はデバイスでの作成（vkCreateBuffer・vkAllocateMemory・vkMapMemory・vkCreateDescriptorPool）だけで、
     *   キューへの提出・待ち・バリアを伴わない。これらはデバイスに外部の同期を求めないので、ワーカーから呼んでよい。
     * - 足したページ・pool は持ち主が持ち続け、先頭へ戻す（回収）のも手放す（Release）のも RenderThread だけ。
     * 提出・待ちを伴う準備（テクスチャの作成・転送、render pass・framebuffer・パイプラインの作り直し）は
     * PrepareParallelRecord で済ませる。
     */
    class FrameResourceLane
    {
    public:
        FrameResourceLane(const FrameResourceLane&) = delete;
        FrameResourceLane& operator=(const FrameResourceLane&) = delete;
        ~FrameResourceLane() = default;

        /** @brief この口を束ねる持ち主 */
        FrameResourceOwner& GetOwner() { return *m_Owner; }
        const FrameResourceOwner& GetOwner() const { return *m_Owner; }

        /** @brief 持ち主の中の口の番号（0 は持ち主自身の既定の口） */
        uint32_t GetLaneIndex() const { return m_LaneIndex; }

        uint32_t GetSlotIndex() const;
        FrameResourceOwnerState GetState() const;
        bool IsRecording() const;

        /** @brief 今のフレームの世代。借りた物に付ける（記録中でなければ未設定を返す。持ち主の全部の口で同じ） */
        FrameResourceGeneration GetGeneration() const;

        /** @brief generation が持ち主の記録中のフレームで借りた物か */
        bool IsCurrent(const FrameResourceGeneration& generation) const;

        /**
         * @brief generation のフレームを提出したか
         *
         * GPU だけが使う資源へコピーを記録したパスが、その内容が GPU に届いたか（次のフレームでコピーを省いてよいか）を
         * 決めるのに使う。記録中のフレームなら Recording。終わったフレームは直近の FrameSubmissionHistory::Capacity 個まで
         * 答え、それより古い・未設定の世代は Unknown（提出しなかったものとして扱う）。
         */
        FrameSubmissionOutcome GetSubmissionOutcome(const FrameResourceGeneration& generation) const;

        /**
         * @brief 借りた物を使ってよいか確かめる
         *
         * 開発ビルドでは、記録中のフレームで借りた物でなければ FRAME_RESOURCE_STALE をログに出し、false を返す
         * （呼び出し側は使わずに捨てる）。開発ビルドでなければ確かめずに true を返す。どの口で借りた物でも確かめられる。
         */
        bool ValidateBorrowed(const FrameResourceGeneration& generation, const char* resourceName) const;

        /**
         * @brief CPU が書いて GPU が読むデータの切れ端を借りる（中身は未初期化。記録中だけ）
         *
         * 返す Buffer + Offset + Size は usage の束縛にそのまま渡せる（整列と記述子の範囲の上限は領域が守る）。
         * CPUAddress へ書いたものは HOST_COHERENT なのでフラッシュせずに GPU から見える。戻るのはこのフレームの回収で、
         * それまで同じフレームで何回借りても前の切れ端は動かず、上書きもされない（ほかの口の切れ端とも重ならない）。
         * 記録中でない・大きさの上限を超えた・ページを作れなかったときは IsValid() が false の切れ端を返す。
         */
        FrameUploadAllocation AllocateUpload(uint64_t size, FrameUploadUsage usage);

        /** @brief 切れ端を借りて data の size バイトを書き込む（Buffer::Update は通さず、マップしたままのページへ写す） */
        FrameUploadAllocation AllocateUploadData(const void* data, uint64_t size, FrameUploadUsage usage)
        {
            FrameUploadAllocation allocation = AllocateUpload(size, usage);
            if (allocation.IsValid() && data)
            {
                std::memcpy(allocation.CPUAddress, data, static_cast<size_t>(size));
            }
            return allocation;
        }

        /** @brief 定数の切れ端を借りて value を書き込む（BindConstantBuffer にそのまま渡せる） */
        template <typename T>
        FrameUploadAllocation AllocateConstants(const T& value)
        {
            return AllocateUploadData(&value, sizeof(T), FrameUploadUsage::Constants);
        }

        /** @brief 切れ端の世代（FrameResourceGeneration の形） */
        static FrameResourceGeneration GetAllocationGeneration(const FrameUploadAllocation& allocation)
        {
            return {allocation.GenerationSlotIndex, allocation.Generation};
        }

        /** @brief 借りた切れ端を使ってよいか確かめる（世代で確かめる ValidateBorrowed と同じ。開発ビルドだけ） */
        bool ValidateBorrowed(const FrameUploadAllocation& allocation, const char* resourceName) const
        {
            return ValidateBorrowed(GetAllocationGeneration(allocation), resourceName);
        }

        /** @brief この口の CPU から書ける領域（統計と検査のため） */
        const FrameUploadArena& GetUploadArena() const { return m_Upload; }

        /**
         * @brief 記述子セットを 1 つ借りる（記録中だけ）
         *
         * layout は IDevice::GetOrCreateDescriptorSetLayout で引いた共有のレイアウト。レイアウト・pool・
         * パイプラインレイアウトは作らず、この口の pool から確保するだけなので毎フレーム呼んでよい。
         * 戻るのはこのフレームの回収で、それまでは同じフレームの中で何度でも書き換え・束縛できる。
         * 記録中でない・pool を設定していない・借りられなかったときは IsValid() が false を返す。
         */
        FrameDescriptorAllocation AllocateDescriptorSet(const RHI::DescriptorSetLayoutPtr& layout);

        /** @brief 借りた記述子セットの世代（FrameResourceGeneration の形） */
        static FrameResourceGeneration GetAllocationGeneration(const FrameDescriptorAllocation& allocation)
        {
            return {allocation.GenerationSlotIndex, allocation.Generation};
        }

        /** @brief 借りた記述子セットを使ってよいか確かめる（世代で確かめる ValidateBorrowed と同じ。開発ビルドだけ） */
        bool ValidateBorrowed(const FrameDescriptorAllocation& allocation, const char* resourceName) const
        {
            return ValidateBorrowed(GetAllocationGeneration(allocation), resourceName);
        }

        /** @brief この口の記述子の pool（統計と検査のため。設定していなければ nullptr） */
        const RHI::IFrameDescriptorAllocator* GetDescriptorAllocator() const { return m_Descriptors.get(); }

    protected:
        FrameResourceLane(FrameResourceOwner* owner, uint32_t laneIndex) : m_Owner(owner), m_LaneIndex(laneIndex) {}

    private:
        friend class FrameResourceOwner;
        friend class FrameResourceOwners;

        // 回収。この口の領域を先頭へ戻し、pool をまとめて戻す（RenderThread）
        void ResetLane()
        {
            m_Upload.Reset();
            if (m_Descriptors)
            {
                m_Descriptors->Reset();
            }
        }

        // 記述子の pool を作り直す（device が nullptr なら外す。RenderThread）
        void RecreateDescriptors(RHI::IDevice* device, const RHI::FrameDescriptorPoolCapacity& capacity)
        {
            ReleaseDescriptors();
            m_Descriptors = device ? device->CreateFrameDescriptorAllocator(capacity) : nullptr;
        }

        void ReleaseDescriptors()
        {
            if (m_Descriptors)
            {
                m_Descriptors->Release();
                m_Descriptors.reset();
            }
        }

        FrameResourceOwner* m_Owner = nullptr;
        uint32_t m_LaneIndex = 0;
        NorvesLib::Thread::Atomic<uint64_t> m_UploadOutsideRecordingCount{0};
        NorvesLib::Thread::Atomic<uint64_t> m_DescriptorOutsideRecordingCount{0};
        FrameUploadArena m_Upload;
        RHI::FrameDescriptorAllocatorPtr m_Descriptors;
    };

    /**
     * @brief 飛行中のスロット 1 つ分の「フレームの持ち主」
     *
     * パスを書く人は、フレームごとに書くデータを持ち主の口から借りる（ViewRenderContext::FrameResources で引く）。
     * 飛行中のフレームの番号・提出の通し番号・FrameUseRing を扱わなくてよい。寿命は FrameResourceOwners が決める:
     * 開始（スワップチェーンの BeginFrame の成功の後）→ 提出（提出の通し番号を結び付ける）または中止 → 回収。
     * 1 フレームの間（開始から提出・中止まで）は、全ビュー・ビューポート・overlay が同じ持ち主を使い、
     * パスが何回 Execute しても位置は戻らない（戻すのは回収だけ）。
     *
     * 持ち主自身が既定の口で、並列の記録の区間には AcquireLane で別の口を貸す（FrameResourceLane を参照）。
     * 借りた物には GetGeneration() の世代を付け、使う前に ValidateBorrowed で確かめる。開発ビルドでは、
     * 借りたフレームより後で使うと FRAME_RESOURCE_STALE を出して拒む。
     * 状態を変えるのは RenderThread だけ。借りること・ValidateBorrowed は記録中に並列の記録のワーカーから呼んでもよい。
     */
    class FrameResourceOwner : public FrameResourceLane
    {
    public:
        FrameResourceOwner() : FrameResourceLane(this, 0) {}

        /** @brief 結び付けた提出の通し番号（提出する前は 0） */
        uint64_t GetSubmissionSerial() const { return m_SubmissionSerial; }

        /** @brief ValidateBorrowed が拒んだ回数（開発ビルドだけ数える） */
        uint64_t GetStaleRejectCount() const { return m_StaleRejectCount.Load(); }

        /** @brief 回収した回数（回収の段を確かめる検査と統計のため） */
        uint64_t GetReclaimCount() const { return m_ReclaimCount; }

        /**
         * @brief 並列の記録の区間 1 つ分の口を借りる（RenderThread。記録中だけ）
         *
         * 空いている口を返し、無ければ作る（ページ・pool は口から最初に借りたときに作る）。返した口は ReleaseLane まで
         * ほかの区間に渡さない。同じフレームの中で返された口を次の区間が使っても、領域は前へ進むだけで pool も
         * 戻さないので、前の区間の切れ端・セットとは重ならない。記録中でなければ nullptr（呼び出し側は既定の口を使う）。
         */
        FrameResourceLane* AcquireLane()
        {
            if (!IsRecording())
            {
                return nullptr;
            }
            for (size_t index = 0; index < m_Lanes.size(); ++index)
            {
                if (!m_LaneLeased[index])
                {
                    m_LaneLeased[index] = 1;
                    return m_Lanes[index].get();
                }
            }
            Container::TUniquePtr<FrameResourceLane> lane(
                new FrameResourceLane(this, static_cast<uint32_t>(m_Lanes.size()) + 1));
            lane->m_Upload.Configure(m_Upload.GetPageSource(), m_Upload.GetPageBytes());
            lane->RecreateDescriptors(m_DescriptorDevice, m_DescriptorCapacity);
            m_Lanes.push_back(std::move(lane));
            m_LaneLeased.push_back(1);
            return m_Lanes.back().get();
        }

        /**
         * @brief 区間の口を返す（RenderThread。その口で記録したワーカーが終わった後）
         *
         * 借りた切れ端・セットは返した後もこのフレームの回収まで有効のまま（戻すのは回収だけ）。
         */
        void ReleaseLane(FrameResourceLane* lane)
        {
            if (!lane || lane == this)
            {
                return;
            }
            for (size_t index = 0; index < m_Lanes.size(); ++index)
            {
                if (m_Lanes[index].get() == lane)
                {
                    m_LaneLeased[index] = 0;
                    return;
                }
            }
        }

        /** @brief 既定の口を除いた口の数（作った数。回収でも減らない） */
        uint32_t GetLaneCount() const { return static_cast<uint32_t>(m_Lanes.size()); }

        /** @brief 今、区間に貸している口の数 */
        uint32_t GetLeasedLaneCount() const
        {
            uint32_t count = 0;
            for (const uint8_t leased : m_LaneLeased)
            {
                count += leased ? 1u : 0u;
            }
            return count;
        }

        /** @brief 既定の口を除いた index 番目の口（検査と統計のため） */
        const FrameResourceLane& GetLane(uint32_t index) const
        {
            assert(index < m_Lanes.size());
            return *m_Lanes[index];
        }

        /** @brief 回収の時点で区間に貸したままだった口の数（区間の後始末の漏れ） */
        uint64_t GetLeakedLaneLeaseCount() const { return m_LeakedLaneLeaseCount; }

        /** @brief 今のフレームの領域の統計（全部の口の合計。RenderThread） */
        FrameUploadStats GetFrameUploadStats() const
        {
            FrameUploadStats total = m_Upload.GetFrameStats();
            for (const auto& lane : m_Lanes)
            {
                const FrameUploadStats stats = lane->m_Upload.GetFrameStats();
                total.Allocations += stats.Allocations;
                total.Bytes += stats.Bytes;
                total.PagesAdded += stats.PagesAdded;
                total.Failures += stats.Failures;
            }
            return total;
        }

        /** @brief 今のフレームの記述子の統計（全部の口の合計。RenderThread） */
        RHI::FrameDescriptorStats GetFrameDescriptorStats() const
        {
            RHI::FrameDescriptorStats total = m_Descriptors ? m_Descriptors->GetFrameStats() : RHI::FrameDescriptorStats{};
            for (const auto& lane : m_Lanes)
            {
                if (!lane->m_Descriptors)
                {
                    continue;
                }
                const RHI::FrameDescriptorStats stats = lane->m_Descriptors->GetFrameStats();
                total.Allocations += stats.Allocations;
                total.Writes += stats.Writes;
                total.PoolsAdded += stats.PoolsAdded;
                total.Failures += stats.Failures;
                total.ExpiredRejects += stats.ExpiredRejects;
                total.AllocateNanoseconds += stats.AllocateNanoseconds;
                total.WriteNanoseconds += stats.WriteNanoseconds;
            }
            return total;
        }

        /** @brief 全部の口で作ったページの数・合計バイト数・足した回数 */
        uint64_t GetUploadPageCount() const
        {
            uint64_t total = m_Upload.GetPageCount();
            for (const auto& lane : m_Lanes)
            {
                total += lane->m_Upload.GetPageCount();
            }
            return total;
        }

        uint64_t GetUploadPageBytes() const
        {
            uint64_t total = m_Upload.GetTotalPageBytes();
            for (const auto& lane : m_Lanes)
            {
                total += lane->m_Upload.GetTotalPageBytes();
            }
            return total;
        }

        uint64_t GetUploadTotalPagesAdded() const
        {
            uint64_t total = m_Upload.GetTotalPagesAdded();
            for (const auto& lane : m_Lanes)
            {
                total += lane->m_Upload.GetTotalPagesAdded();
            }
            return total;
        }

        /** @brief 全部の口の記述子の pool の数・足した回数 */
        uint64_t GetDescriptorPoolCount() const
        {
            uint64_t total = m_Descriptors ? m_Descriptors->GetPoolCount() : 0;
            for (const auto& lane : m_Lanes)
            {
                total += lane->m_Descriptors ? lane->m_Descriptors->GetPoolCount() : 0;
            }
            return total;
        }

        uint64_t GetDescriptorTotalPoolsAdded() const
        {
            uint64_t total = m_Descriptors ? m_Descriptors->GetTotalPoolsAdded() : 0;
            for (const auto& lane : m_Lanes)
            {
                total += lane->m_Descriptors ? lane->m_Descriptors->GetTotalPoolsAdded() : 0;
            }
            return total;
        }

        /** @brief 借りる口を登録する。回収のたびに OnFrameResourcesReclaimed が呼ばれる（RenderThread） */
        void AttachAllocator(IFrameResourceAllocator* allocator)
        {
            if (allocator && std::find(m_Allocators.begin(), m_Allocators.end(), allocator) == m_Allocators.end())
            {
                m_Allocators.push_back(allocator);
            }
        }

        /** @brief 登録を外す（RenderThread） */
        void DetachAllocator(IFrameResourceAllocator* allocator)
        {
            const auto it = std::find(m_Allocators.begin(), m_Allocators.end(), allocator);
            if (it != m_Allocators.end())
            {
                m_Allocators.erase(it);
            }
        }

    private:
        friend class FrameResourceLane;
        friend class FrameResourceOwners;

        void Begin(uint32_t slotIndex, uint64_t generation)
        {
            assert(m_State == FrameResourceOwnerState::Idle && "回収していない持ち主を開始した");
            m_SlotIndex = slotIndex;
            m_Generation = generation;
            m_SubmissionSerial = 0;
            m_State = FrameResourceOwnerState::Recording;
        }

        void MarkSubmitted(uint64_t submissionSerial)
        {
            assert(IsRecording());
            m_SubmissionSerial = submissionSerial;
            m_State = FrameResourceOwnerState::Submitted;
            if (m_SubmissionHistory)
            {
                m_SubmissionHistory->Record(m_Generation, true);
            }
        }

        void MarkAborted()
        {
            assert(IsRecording());
            m_SubmissionSerial = 0;
            m_State = FrameResourceOwnerState::Aborted;
            if (m_SubmissionHistory)
            {
                m_SubmissionHistory->Record(m_Generation, false);
            }
        }

        void Reclaim()
        {
            if (m_State == FrameResourceOwnerState::Idle)
            {
                return;
            }
            const FrameResourceGeneration generation{m_SlotIndex, m_Generation};
            for (IFrameResourceAllocator* allocator : m_Allocators)
            {
                allocator->OnFrameResourcesReclaimed(generation);
            }
            // GPU はこのフレームの切れ端・セットを使い終えたので、全部の口の領域を先頭へ戻し、pool をまとめて戻す
            // （ページ・pool は使い回す。前に借りたセットは使えなくなる）
            ResetLane();
            for (size_t index = 0; index < m_Lanes.size(); ++index)
            {
                m_Lanes[index]->ResetLane();
                if (m_LaneLeased[index])
                {
                    // 区間の後で返されなかった口。記録は終わっているので返したものとして扱う
                    ++m_LeakedLaneLeaseCount;
                    m_LaneLeased[index] = 0;
                    NORVES_LOG_WARNING("FrameResource", "FRAME_RESOURCE_LANE_LEAKED slot=%u lane=%u",
                                       m_SlotIndex, m_Lanes[index]->GetLaneIndex());
                }
            }
            m_SubmissionSerial = 0;
            m_State = FrameResourceOwnerState::Idle;
            ++m_ReclaimCount;
        }

        // 全部の口の領域のページを作る口と大きさを設定する（RenderThread）
        void ConfigureUploadLanes(IFrameUploadPageSource* pageSource, uint64_t pageBytes)
        {
            m_Upload.Configure(pageSource, pageBytes);
            for (const auto& lane : m_Lanes)
            {
                lane->m_Upload.Configure(pageSource, pageBytes);
            }
        }

        void ReleaseUploadLanes()
        {
            m_Upload.Release();
            for (const auto& lane : m_Lanes)
            {
                lane->m_Upload.Release();
            }
        }

        // 全部の口の記述子の pool を作り直す（device が nullptr なら外す）。全部作れたら true
        bool ConfigureDescriptorLanes(RHI::IDevice* device, const RHI::FrameDescriptorPoolCapacity& capacity)
        {
            m_DescriptorDevice = device;
            m_DescriptorCapacity = capacity;
            RecreateDescriptors(device, capacity);
            bool bAllCreated = m_Descriptors != nullptr;
            for (const auto& lane : m_Lanes)
            {
                lane->RecreateDescriptors(device, capacity);
                bAllCreated = bAllCreated && lane->m_Descriptors != nullptr;
            }
            return bAllCreated;
        }

        void ReleaseDescriptorLanes()
        {
            ReleaseDescriptors();
            for (const auto& lane : m_Lanes)
            {
                lane->ReleaseDescriptors();
            }
            m_DescriptorDevice = nullptr;
        }

        uint32_t m_SlotIndex = FrameResourceGeneration::InvalidSlot;
        uint64_t m_Generation = 0;
        uint64_t m_SubmissionSerial = 0;
        FrameResourceOwnerState m_State = FrameResourceOwnerState::Idle;
        // 全部の持ち主で共有する提出の結果（FrameResourceOwners が持つ。単独の持ち主では null で、終わった世代は Unknown）
        FrameSubmissionHistory* m_SubmissionHistory = nullptr;
        uint64_t m_ReclaimCount = 0;
        uint64_t m_LeakedLaneLeaseCount = 0;
        mutable NorvesLib::Thread::Atomic<uint64_t> m_StaleRejectCount{0};
        Container::VariableArray<IFrameResourceAllocator*> m_Allocators;
        // 区間に貸す口（既定の口を除く）。アドレスは回収・追加でも変わらない
        Container::VariableArray<Container::TUniquePtr<FrameResourceLane>> m_Lanes;
        Container::VariableArray<uint8_t> m_LaneLeased;
        // 後から作る口の記述子の pool を作るデバイスと容量（ConfigureDescriptors で決める）
        RHI::IDevice* m_DescriptorDevice = nullptr;
        RHI::FrameDescriptorPoolCapacity m_DescriptorCapacity;
    };

    inline uint32_t FrameResourceLane::GetSlotIndex() const { return m_Owner->m_SlotIndex; }

    inline FrameResourceOwnerState FrameResourceLane::GetState() const { return m_Owner->m_State; }

    inline bool FrameResourceLane::IsRecording() const
    {
        return m_Owner->m_State == FrameResourceOwnerState::Recording;
    }

    inline FrameResourceGeneration FrameResourceLane::GetGeneration() const
    {
        if (!IsRecording())
        {
            return {};
        }
        return {m_Owner->m_SlotIndex, m_Owner->m_Generation};
    }

    inline bool FrameResourceLane::IsCurrent(const FrameResourceGeneration& generation) const
    {
        return IsRecording() && generation.SlotIndex == m_Owner->m_SlotIndex &&
               generation.Generation == m_Owner->m_Generation;
    }

    inline FrameSubmissionOutcome FrameResourceLane::GetSubmissionOutcome(const FrameResourceGeneration& generation) const
    {
        if (!generation.IsSet())
        {
            return FrameSubmissionOutcome::Unknown;
        }
        if (IsCurrent(generation))
        {
            return FrameSubmissionOutcome::Recording;
        }
        return m_Owner->m_SubmissionHistory ? m_Owner->m_SubmissionHistory->Find(generation.Generation)
                                            : FrameSubmissionOutcome::Unknown;
    }

    inline bool FrameResourceLane::ValidateBorrowed(const FrameResourceGeneration& generation,
                                                    const char* resourceName) const
    {
#if NORVES_BUILD_DEVELOPMENT
        if (IsCurrent(generation))
        {
            return true;
        }
        const uint64_t rejectCount = m_Owner->m_StaleRejectCount.FetchAdd(1) + 1;
        // 同じ誤りが毎フレーム続くとログが埋まるので、最初の 8 回と、その後は 2 の冪の回だけ出す
        if (rejectCount <= 8 || (rejectCount & (rejectCount - 1)) == 0)
        {
            NORVES_LOG_ERROR("FrameResource",
                             "FRAME_RESOURCE_STALE resource=%s slot=%u borrowed_slot=%u borrowed_generation=%llu "
                             "current_generation=%llu state=%s rejects=%llu",
                             resourceName ? resourceName : "(unnamed)",
                             m_Owner->m_SlotIndex,
                             generation.SlotIndex,
                             static_cast<unsigned long long>(generation.Generation),
                             static_cast<unsigned long long>(m_Owner->m_Generation),
                             GetFrameResourceOwnerStateName(m_Owner->m_State),
                             static_cast<unsigned long long>(rejectCount));
        }
        return false;
#else
        (void)generation;
        (void)resourceName;
        return true;
#endif
    }

    inline FrameUploadAllocation FrameResourceLane::AllocateUpload(uint64_t size, FrameUploadUsage usage)
    {
        if (!IsRecording())
        {
            const uint64_t rejectCount = m_UploadOutsideRecordingCount.FetchAdd(1) + 1;
            if (rejectCount <= 8 || (rejectCount & (rejectCount - 1)) == 0)
            {
                NORVES_LOG_ERROR("FrameResource",
                                 "FRAME_RESOURCE_UPLOAD_NOT_RECORDING slot=%u lane=%u usage=%s bytes=%llu state=%s "
                                 "rejects=%llu",
                                 GetSlotIndex(), m_LaneIndex, GetFrameUploadUsageName(usage),
                                 static_cast<unsigned long long>(size), GetFrameResourceOwnerStateName(GetState()),
                                 static_cast<unsigned long long>(rejectCount));
            }
            FrameUploadAllocation allocation;
            allocation.Usage = usage;
            return allocation;
        }
        FrameUploadAllocation allocation = m_Upload.Allocate(size, usage);
        allocation.GenerationSlotIndex = m_Owner->m_SlotIndex;
        allocation.Generation = m_Owner->m_Generation;
        return allocation;
    }

    inline FrameDescriptorAllocation FrameResourceLane::AllocateDescriptorSet(const RHI::DescriptorSetLayoutPtr& layout)
    {
        FrameDescriptorAllocation allocation;
        if (!IsRecording() || !m_Descriptors)
        {
            const uint64_t rejectCount = m_DescriptorOutsideRecordingCount.FetchAdd(1) + 1;
            if (rejectCount <= 8 || (rejectCount & (rejectCount - 1)) == 0)
            {
                NORVES_LOG_ERROR("FrameResource",
                                 "FRAME_RESOURCE_DESCRIPTOR_NOT_RECORDING slot=%u lane=%u state=%s configured=%d "
                                 "rejects=%llu",
                                 GetSlotIndex(), m_LaneIndex, GetFrameResourceOwnerStateName(GetState()),
                                 m_Descriptors ? 1 : 0, static_cast<unsigned long long>(rejectCount));
            }
            return allocation;
        }
        allocation.Set = m_Descriptors->Allocate(layout);
        if (allocation.Set)
        {
            allocation.GenerationSlotIndex = m_Owner->m_SlotIndex;
            allocation.Generation = m_Owner->m_Generation;
        }
        return allocation;
    }

    /**
     * @brief 飛行中のスロットごとのフレームの持ち主と、その寿命の 4 つの段
     *
     * - 開始（BeginFrame）: スワップチェーンの BeginFrame が成功した後に 1 回呼ぶ。先に完了した提出の分を回収し、
     *   提出も中止もしないまま残ったフレーム（例外などで抜けた）を中止として扱い、中止した分を回収してから、
     *   このスロットの持ち主を新しい世代で記録中にする。
     * - 提出（CommitSubmitted）: 提出が成功したら、その提出の通し番号を記録中の持ち主に結び付ける。
     * - 中止（AbortFrame）: 提出しなかったら呼ぶ。GPU は使っていないので次の開始で回収する。
     * - 回収（Retire）: 結び付けた提出の通し番号が完了したら戻す。スワップチェーンの作り直しで完了の通し番号が
     *   前より小さくなった（振り直した）ときは、作り直す前に GPU を待ち終えているので、提出済みの分を全部戻す。
     *
     * RenderThread だけから使う（排他は持たない）。
     */
    class FrameResourceOwners
    {
    public:
        FrameResourceOwners()
        {
            for (uint32_t index = 0; index < FrameResourceMaxInFlightSlots; ++index)
            {
                m_Owners[index].m_SlotIndex = index;
                m_Owners[index].m_SubmissionHistory = &m_SubmissionHistory;
            }
        }

        /**
         * @brief 開始。inFlightIndex のスロットの持ち主を記録中にして返す
         * @param inFlightIndex 飛行中のフレームの番号（スワップチェーンの現在のフレームの番号）
         * @param completedSubmissionSerial スワップチェーンの完了済みの提出の通し番号
         * @return 記録中の持ち主。番号が上限を超えていれば nullptr
         */
        FrameResourceOwner* BeginFrame(uint32_t inFlightIndex, uint64_t completedSubmissionSerial)
        {
            Retire(completedSubmissionSerial);
            if (m_Recording)
            {
                // 提出も中止もしないまま次のフレームが始まった。提出していないので GPU は使っていない
                ++m_AbandonedFrameCount;
                FinishRecording(*m_Recording);
                m_Recording->MarkAborted();
                m_Recording = nullptr;
            }
            for (FrameResourceOwner& owner : m_Owners)
            {
                if (owner.m_State == FrameResourceOwnerState::Aborted)
                {
                    owner.Reclaim();
                }
            }

            if (inFlightIndex >= FrameResourceMaxInFlightSlots)
            {
                NORVES_LOG_ERROR("FrameResource",
                                 "飛行中のフレームの番号 %u がフレームの持ち主の上限 %u を超えています",
                                 inFlightIndex, FrameResourceMaxInFlightSlots);
                return nullptr;
            }

            FrameResourceOwner& owner = m_Owners[inFlightIndex];
            if (owner.m_State == FrameResourceOwnerState::Submitted)
            {
                // スワップチェーンはこのスロットの前の提出のフェンスを待ってから BeginFrame を返すので、ここで
                // 完了していないのは完了の通し番号の渡し方の誤り。GPU は待ち終えているので回収して進める
                ++m_UnretiredBeginCount;
                NORVES_LOG_WARNING("FrameResource",
                                   "FRAME_RESOURCE_OWNER_UNRETIRED slot=%u submission=%llu completed=%llu",
                                   inFlightIndex,
                                   static_cast<unsigned long long>(owner.m_SubmissionSerial),
                                   static_cast<unsigned long long>(completedSubmissionSerial));
                owner.Reclaim();
            }
            owner.Begin(inFlightIndex, m_NextGeneration++);
            m_Recording = &owner;
            return &owner;
        }

        /** @brief 提出。記録中の持ち主に提出の通し番号（0 以外）を結び付ける。記録中の持ち主が無ければ false */
        bool CommitSubmitted(uint64_t submissionSerial)
        {
            if (!m_Recording)
            {
                return false;
            }
            if (submissionSerial == 0)
            {
                AbortFrame();
                return false;
            }
            FinishRecording(*m_Recording);
            m_Recording->MarkSubmitted(submissionSerial);
            m_Recording = nullptr;
            return true;
        }

        /** @brief 中止。記録中の持ち主を、次の開始で回収する印にする */
        void AbortFrame()
        {
            if (!m_Recording)
            {
                return;
            }
            FinishRecording(*m_Recording);
            m_Recording->MarkAborted();
            m_Recording = nullptr;
        }

        /** @brief 回収。completedSubmissionSerial までに完了した提出の持ち主を戻す（振り直しは全部戻す） */
        void Retire(uint64_t completedSubmissionSerial)
        {
            const bool bRestarted = completedSubmissionSerial < m_LastCompletedSubmissionSerial;
            m_LastCompletedSubmissionSerial = completedSubmissionSerial;
            if (bRestarted)
            {
                ++m_SerialRestartCount;
            }
            for (FrameResourceOwner& owner : m_Owners)
            {
                if (owner.m_State == FrameResourceOwnerState::Submitted &&
                    (bRestarted || owner.m_SubmissionSerial <= completedSubmissionSerial))
                {
                    owner.Reclaim();
                }
            }
        }

        /** @brief 全部の持ち主を回収して初期状態へ戻す（GPU を待ち終えた後の終了処理で呼ぶ） */
        void Reset()
        {
            m_Recording = nullptr;
            for (FrameResourceOwner& owner : m_Owners)
            {
                owner.Reclaim();
            }
            m_LastCompletedSubmissionSerial = 0;
        }

        /** @brief 記録中の持ち主（開始から提出・中止まで。それ以外は nullptr） */
        FrameResourceOwner* GetRecordingOwner() { return m_Recording; }
        const FrameResourceOwner* GetRecordingOwner() const { return m_Recording; }

        FrameResourceOwner& GetOwner(uint32_t slotIndex)
        {
            assert(slotIndex < FrameResourceMaxInFlightSlots);
            return m_Owners[slotIndex];
        }

        /** @brief 提出も中止もしないまま次の開始で中止として扱ったフレームの数 */
        uint64_t GetAbandonedFrameCount() const { return m_AbandonedFrameCount; }
        /** @brief 開始の時点で回収されていなかった提出の数（完了の通し番号の渡し方の誤り） */
        uint64_t GetUnretiredBeginCount() const { return m_UnretiredBeginCount; }
        /** @brief 完了の通し番号の振り直しを見つけた回数 */
        uint64_t GetSerialRestartCount() const { return m_SerialRestartCount; }

        /**
         * @brief CPU から書ける領域のページを作る口と、1 ページの大きさを全スロットの全部の口に設定する（RenderThread。借りる前に）
         * @param pageSource ページを作る口（持ち主より長く生きること。nullptr なら借りられない）
         * @param pageBytes 1 ページの大きさ（開発ビルドの --frame-arena-initial-kb で小さくできる）
         */
        void ConfigureUpload(IFrameUploadPageSource* pageSource, uint64_t pageBytes)
        {
            for (FrameResourceOwner& owner : m_Owners)
            {
                owner.ConfigureUploadLanes(pageSource, pageBytes);
            }
            m_bUploadConfigured = pageSource != nullptr;
        }

        bool IsUploadConfigured() const { return m_bUploadConfigured; }

        /** @brief 全スロットの全部の口の領域のページを手放す（Reset の後、デバイスを手放す前に呼ぶ） */
        void ReleaseUploadPages()
        {
            LogUploadStats("release");
            for (FrameResourceOwner& owner : m_Owners)
            {
                owner.ReleaseUploadLanes();
            }
        }

        /**
         * @brief 全スロットの全部の口に記述子の pool を作る（RenderThread。借りる前に 1 回。後から作る口も同じ容量で作る）
         * @param device pool を作るデバイス（nullptr なら pool を外す）
         * @param capacity pool 1 つの容量
         * @return 全スロットに pool を作れたら true
         */
        bool ConfigureDescriptors(RHI::IDevice* device,
                                  const RHI::FrameDescriptorPoolCapacity& capacity = RHI::FrameDescriptorPoolCapacity{})
        {
            bool bAllCreated = device != nullptr;
            for (FrameResourceOwner& owner : m_Owners)
            {
                bAllCreated = owner.ConfigureDescriptorLanes(device, capacity) && bAllCreated;
            }
            m_bDescriptorsConfigured = bAllCreated;
            return bAllCreated;
        }

        bool IsDescriptorsConfigured() const { return m_bDescriptorsConfigured; }

        /** @brief 全スロットの全部の口の記述子の pool を手放す（Reset の後、デバイスを手放す前に呼ぶ） */
        void ReleaseDescriptorPools()
        {
            LogDescriptorStats("release");
            for (FrameResourceOwner& owner : m_Owners)
            {
                owner.ReleaseDescriptorLanes();
            }
            m_bDescriptorsConfigured = false;
        }

        /** @brief 最後に記録を終えたフレーム（提出・中止）の記述子の統計（全部の口の合計） */
        const RHI::FrameDescriptorStats& GetLastFrameDescriptorStats() const { return m_LastFrameDescriptorStats; }

        /** @brief 全スロットで記述子の pool を足した回数の合計 */
        uint64_t GetTotalDescriptorPoolsAdded() const
        {
            uint64_t total = 0;
            for (const FrameResourceOwner& owner : m_Owners)
            {
                total += owner.GetDescriptorTotalPoolsAdded();
            }
            return total;
        }

        /** @brief 全スロットで作った区間の口の数の合計（既定の口を除く） */
        uint64_t GetTotalLaneCount() const
        {
            uint64_t total = 0;
            for (const FrameResourceOwner& owner : m_Owners)
            {
                total += owner.GetLaneCount();
            }
            return total;
        }

        /**
         * @brief 記述子の統計を 1 行でログに出す（開発ビルドだけ）
         *
         * 1 フレームの値は最後に記録を終えたフレームのもの。pool の数は全スロットの全部の口の合計。
         */
        void LogDescriptorStats(const char* reason) const
        {
#if NORVES_BUILD_DEVELOPMENT
            uint64_t poolCount = 0;
            for (const FrameResourceOwner& owner : m_Owners)
            {
                poolCount += owner.GetDescriptorPoolCount();
            }
            NORVES_LOG_INFO("FrameResource",
                            "FrameResource.Descriptor reason=%s frames=%llu FrameResource.Descriptor.Allocations=%llu "
                            "FrameResource.Descriptor.Writes=%llu FrameResource.Descriptor.PoolsAdded=%llu "
                            "FrameResource.Descriptor.Failures=%llu FrameResource.Descriptor.ExpiredRejects=%llu "
                            "FrameResource.Descriptor.AllocateUs=%.1f FrameResource.Descriptor.WriteUs=%.1f "
                            "FrameResource.Descriptor.TotalPoolsAdded=%llu FrameResource.Descriptor.Pools=%llu",
                            reason ? reason : "(none)",
                            static_cast<unsigned long long>(m_FinishedFrameCount),
                            static_cast<unsigned long long>(m_LastFrameDescriptorStats.Allocations),
                            static_cast<unsigned long long>(m_LastFrameDescriptorStats.Writes),
                            static_cast<unsigned long long>(m_LastFrameDescriptorStats.PoolsAdded),
                            static_cast<unsigned long long>(m_LastFrameDescriptorStats.Failures),
                            static_cast<unsigned long long>(m_LastFrameDescriptorStats.ExpiredRejects),
                            static_cast<double>(m_LastFrameDescriptorStats.AllocateNanoseconds) / 1000.0,
                            static_cast<double>(m_LastFrameDescriptorStats.WriteNanoseconds) / 1000.0,
                            static_cast<unsigned long long>(GetTotalDescriptorPoolsAdded()),
                            static_cast<unsigned long long>(poolCount));
#else
            (void)reason;
#endif
        }

        /** @brief 最後に記録を終えたフレーム（提出・中止）の領域の統計 */
        const FrameUploadStats& GetLastFrameUploadStats() const { return m_LastFrameUploadStats; }
        /** @brief 1 フレームで借りたバイト数の最大 */
        uint64_t GetMaxFrameUploadBytes() const { return m_MaxFrameUploadBytes; }
        /** @brief 全スロットでページを足した回数の合計 */
        uint64_t GetTotalUploadPagesAdded() const
        {
            uint64_t total = 0;
            for (const FrameResourceOwner& owner : m_Owners)
            {
                total += owner.GetUploadTotalPagesAdded();
            }
            return total;
        }

        /**
         * @brief 領域の統計を 1 行でログに出す（開発ビルドだけ）
         *
         * 1 フレームの値は最後に記録を終えたフレームのもの。ページの数・バイト数・口の数は全スロットの全部の口の合計。
         */
        void LogUploadStats(const char* reason) const
        {
#if NORVES_BUILD_DEVELOPMENT
            uint64_t pageCount = 0;
            uint64_t pageBytes = 0;
            for (const FrameResourceOwner& owner : m_Owners)
            {
                pageCount += owner.GetUploadPageCount();
                pageBytes += owner.GetUploadPageBytes();
            }
            NORVES_LOG_INFO("FrameResource",
                            "FrameResource.Upload reason=%s frames=%llu FrameResource.Upload.Allocations=%llu "
                            "FrameResource.Upload.Bytes=%llu FrameResource.Upload.PagesAdded=%llu "
                            "FrameResource.Upload.Failures=%llu FrameResource.Upload.MaxFrameBytes=%llu "
                            "FrameResource.Upload.TotalPagesAdded=%llu FrameResource.Upload.Pages=%llu "
                            "FrameResource.Upload.PageBytes=%llu FrameResource.Lanes=%llu",
                            reason ? reason : "(none)",
                            static_cast<unsigned long long>(m_FinishedFrameCount),
                            static_cast<unsigned long long>(m_LastFrameUploadStats.Allocations),
                            static_cast<unsigned long long>(m_LastFrameUploadStats.Bytes),
                            static_cast<unsigned long long>(m_LastFrameUploadStats.PagesAdded),
                            static_cast<unsigned long long>(m_LastFrameUploadStats.Failures),
                            static_cast<unsigned long long>(m_MaxFrameUploadBytes),
                            static_cast<unsigned long long>(GetTotalUploadPagesAdded()),
                            static_cast<unsigned long long>(pageCount),
                            static_cast<unsigned long long>(pageBytes),
                            static_cast<unsigned long long>(GetTotalLaneCount()));
#else
            (void)reason;
#endif
        }

    private:
        /** @brief 統計をログへ出す間隔（記録を終えたフレームの数） */
        static constexpr uint64_t UploadStatsLogIntervalFrames = 600;

        // 記録を終える（提出・中止）ときに、そのフレームの領域の統計（全部の口の合計）を残す
        void FinishRecording(const FrameResourceOwner& owner)
        {
            m_LastFrameUploadStats = owner.GetFrameUploadStats();
            m_MaxFrameUploadBytes = std::max(m_MaxFrameUploadBytes, m_LastFrameUploadStats.Bytes);
            m_LastFrameDescriptorStats = owner.GetFrameDescriptorStats();
            ++m_FinishedFrameCount;
            const bool bPeriodic =
                m_FinishedFrameCount == 1 || m_FinishedFrameCount % UploadStatsLogIntervalFrames == 0;
            if (bPeriodic || m_LastFrameUploadStats.PagesAdded != 0 || m_LastFrameUploadStats.Failures != 0)
            {
                LogUploadStats(m_LastFrameUploadStats.Failures != 0      ? "failure"
                               : m_LastFrameUploadStats.PagesAdded != 0 ? "pages_added"
                                                                        : "periodic");
            }
            const bool bDescriptorFailure =
                m_LastFrameDescriptorStats.Failures != 0 || m_LastFrameDescriptorStats.ExpiredRejects != 0;
            if (bPeriodic || bDescriptorFailure || m_LastFrameDescriptorStats.PoolsAdded != 0)
            {
                LogDescriptorStats(bDescriptorFailure                              ? "failure"
                                   : m_LastFrameDescriptorStats.PoolsAdded != 0 ? "pools_added"
                                                                                : "periodic");
            }
        }

        FrameResourceOwner m_Owners[FrameResourceMaxInFlightSlots];
        // 持ち主が提出・中止のときに書く、終わったフレームの提出の結果（持ち主はこれを指す。この型は写せず動かせない）
        FrameSubmissionHistory m_SubmissionHistory;
        FrameResourceOwner* m_Recording = nullptr;
        uint64_t m_NextGeneration = 1;
        uint64_t m_LastCompletedSubmissionSerial = 0;
        uint64_t m_AbandonedFrameCount = 0;
        uint64_t m_UnretiredBeginCount = 0;
        uint64_t m_SerialRestartCount = 0;
        bool m_bUploadConfigured = false;
        bool m_bDescriptorsConfigured = false;
        FrameUploadStats m_LastFrameUploadStats;
        RHI::FrameDescriptorStats m_LastFrameDescriptorStats;
        uint64_t m_MaxFrameUploadBytes = 0;
        uint64_t m_FinishedFrameCount = 0;
    };
} // namespace NorvesLib::Core::Rendering
