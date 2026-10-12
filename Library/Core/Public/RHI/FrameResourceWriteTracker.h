#pragma once

#include "Container/Containers.h"
#include "Debug/DebugConfig.h"
#include "Logging/LogMacros.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

#include <cstdint>

/**
 * @file FrameResourceWriteTracker.h
 * @brief 守られていない上書きを開発ビルドで見つける（フレームをまたいで持ち続ける資源の CPU からの書き換えの見張り）
 *
 * 見張るのはバッファと記述子セットで、数える書き込みは IBuffer::Update・IBuffer::NoteExplicitWrite（明示した書き込みの借り）・
 * 書き込みのある IDescriptorSet::Update だけ。Map で得たポインタへの任意の書き込みは捕まえられない
 * （読み戻しの Map は書き込みとみなさない）。テクスチャの書き換えは見ない。
 *
 * 出すものは 2 種類:
 * - 一覧（合否に使わない）: 2 つ以上のフレームで CPU から書き換えた、守られていない資源を
 *   `FRAME_RESOURCE_REWRITE resource=<名前> pass=<パス> frames=<n> instances=<n>` で名前とパスの組ごとに 1 回（終わりに出す）。
 * - 守られていない上書き（合否に使う）: 飛行中のフレームを 2 と仮定して前のフレームの記録が参照した範囲への書き込み、
 *   同じフレームで記録済みのコマンドが参照する範囲への書き込み、束縛した後の記述子セットの Update を
 *   `FRAME_RESOURCE_UNPROTECTED resource=<名前> pass=<パス> kind=<種類>` で名前・パス・種類の組ごとに初めて見つけたときに出し、
 *   終わりに `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=<組の数>` を出す。
 *
 * 作成の直後の 1 回（大きさの変更は作り直しなので、その直後の 1 回も同じ）と、初期化で 1 回だけ書く物は数えない。
 * 「守られている」資源（フレームの持ち主から借りた物・飛行中のスロットで引くリングの資源）は、作るときに仕組みの側が登録する
 * （FrameResourceProtectedCreationScope・MarkFrameUseProtectedCreation）。守られている資源は前のフレームとの重なりを見ず、
 * 一覧にも載せないが、同じフレームの中の重なり（記録済みのコマンドが参照する範囲への書き込み・束縛した後の Update）は見る。
 * Release では何もしない。
 */

#if NORVES_BUILD_DEVELOPMENT
#define NORVES_FRAME_RESOURCE_WRITE_TRACKING 1
#else
#define NORVES_FRAME_RESOURCE_WRITE_TRACKING 0
#endif

namespace NorvesLib::RHI
{
    class FrameResourceWriteTracker;

    /** @brief 見張る資源の種類 */
    enum class FrameResourceTrackKind : uint8_t
    {
        Buffer,
        DescriptorSet
    };

    /** @brief 守られていない上書きの種類（kind=） */
    enum class FrameResourceHazard : uint8_t
    {
        /** 前のフレームの記録が参照した範囲へのバッファの書き込み（飛行中のフレームを 2 と仮定） */
        BufferPreviousFrame,
        /** 同じフレームで記録済みのコマンドが参照する範囲へのバッファの書き込み */
        BufferRecordedSameFrame,
        /** 前のフレームで束縛した記述子セットの Update（飛行中のフレームを 2 と仮定） */
        DescriptorBoundPreviousFrame,
        /** 同じフレームで束縛した後の記述子セットの Update */
        DescriptorBoundSameFrame,
        Count
    };

    inline const char* GetFrameResourceHazardName(FrameResourceHazard hazard)
    {
        switch (hazard)
        {
        case FrameResourceHazard::BufferPreviousFrame:
            return "buffer_previous_frame";
        case FrameResourceHazard::BufferRecordedSameFrame:
            return "buffer_recorded_same_frame";
        case FrameResourceHazard::DescriptorBoundPreviousFrame:
            return "descriptor_bound_previous_frame";
        case FrameResourceHazard::DescriptorBoundSameFrame:
            return "descriptor_bound_same_frame";
        default:
            return "unknown";
        }
    }

    /**
     * @brief 書き込みを起こしたパスの名前と、守られている資源を作る区間（スレッドごと）
     *
     * RenderGraph はパスの Declare・PrepareParallelRecord・Execute をパスの名前の区間で包む。パスの外で書く所は
     * 呼び出し元の名前で包む。名前は区間の間ずっと有効な文字列（文字列の定数かパスの GetName()）を渡す。
     */
    namespace FrameResourceWriteScopeDetail
    {
        struct ThreadState
        {
            const char* Name = nullptr;
            /** 今の区間の番号（0 は区間の外） */
            uint64_t ScopeSerial = 0;
            uint64_t NextScopeSerial = 1;
            /** FrameUseRing の Acquire が開いた「守られている資源を作る」窓の区間の番号 */
            uint64_t FrameUseCreationScope = 0;
            /** FrameResourceProtectedCreationScope の深さ */
            uint32_t ProtectedCreationDepth = 0;
        };

        inline thread_local ThreadState t_State;
    } // namespace FrameResourceWriteScopeDetail

    /** @brief 書き込みを起こしたパスの名前の区間（入れ子にしてよい。抜けると外の名前へ戻る） */
    class FrameResourceWriteScope
    {
    public:
        explicit FrameResourceWriteScope(const char* name)
        {
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
            auto& state = FrameResourceWriteScopeDetail::t_State;
            m_PreviousName = state.Name;
            m_PreviousSerial = state.ScopeSerial;
            state.Name = name;
            state.ScopeSerial = state.NextScopeSerial++;
#else
            (void)name;
#endif
        }

        ~FrameResourceWriteScope()
        {
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
            auto& state = FrameResourceWriteScopeDetail::t_State;
            state.Name = m_PreviousName;
            state.ScopeSerial = m_PreviousSerial;
#endif
        }

        FrameResourceWriteScope(const FrameResourceWriteScope&) = delete;
        FrameResourceWriteScope& operator=(const FrameResourceWriteScope&) = delete;

    private:
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
        const char* m_PreviousName = nullptr;
        uint64_t m_PreviousSerial = 0;
#endif
    };

    /**
     * @brief この区間で作る資源を「守られている」と登録する（フレームの持ち主の領域のページ・飛行中のスロットで引くリングの資源）
     *
     * 区間で作った資源は前のフレームとの重なりを見ない。同じフレームの中の重なりは見る。
     */
    class FrameResourceProtectedCreationScope
    {
    public:
        FrameResourceProtectedCreationScope()
        {
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
            ++FrameResourceWriteScopeDetail::t_State.ProtectedCreationDepth;
#endif
        }

        ~FrameResourceProtectedCreationScope()
        {
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
            --FrameResourceWriteScopeDetail::t_State.ProtectedCreationDepth;
#endif
        }

        FrameResourceProtectedCreationScope(const FrameResourceProtectedCreationScope&) = delete;
        FrameResourceProtectedCreationScope& operator=(const FrameResourceProtectedCreationScope&) = delete;
    };

    /**
     * @brief FrameUseRing の Acquire が呼ぶ。今のパスの区間の残りで作る資源を「守られている」と登録する
     *
     * リングの Use の資源は Acquire の後に呼び出し側が作る（作り直す）ので、Acquire から今の区間の終わりまでを窓にする。
     * 区間の外（パスの区間を持たない所）の Acquire は窓を開かない。窓で作ったパスのメンバの資源も守られている扱いになるが、
     * 同じフレームの中の重なりは引き続き見る。
     */
    inline void MarkFrameUseProtectedCreation()
    {
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
        auto& state = FrameResourceWriteScopeDetail::t_State;
        state.FrameUseCreationScope = state.ScopeSerial;
#endif
    }

    /** @brief 今作る資源を「守られている」と登録するか */
    inline bool IsFrameResourceProtectedCreation()
    {
#if NORVES_FRAME_RESOURCE_WRITE_TRACKING
        const auto& state = FrameResourceWriteScopeDetail::t_State;
        return state.ProtectedCreationDepth > 0 ||
               (state.ScopeSerial != 0 && state.FrameUseCreationScope == state.ScopeSerial);
#else
        return false;
#endif
    }

    /** @brief 今の書き込みを起こしたパスの名前（区間の外なら Unscoped） */
    inline const char* GetFrameResourceWriteScopeName()
    {
        const char* name = FrameResourceWriteScopeDetail::t_State.Name;
        return name ? name : "Unscoped";
    }

    /** @brief バッファの参照の範囲 [Begin, End) */
    struct FrameResourceRange
    {
        uint64_t Begin = 0;
        uint64_t End = 0;
    };

    /** @brief 一覧の名前とパスの組ごとの集計 */
    struct FrameResourceRewriteEntry
    {
        Core::Container::String Resource;
        Core::Container::String Pass;
        Thread::Atomic<uint64_t> MaxFrames{0};
        uint64_t Instances = 0;
    };

    /**
     * @brief 資源 1 つの見張りの状態（バッファ・記述子セットのオブジェクトが持つ）
     *
     * 識別は ID（作るたびに増える。使い回さない）と世代（借りた記述子セットは pool の世代、ほかは 1）。
     * バッファの参照の範囲は、書き換えを一度見たバッファだけ記録する（作成の直後に 1 回だけ書く頂点などは記録しない）。
     * そのため書き換えの最初の 1 回はまだ参照を持たず、重なりは 2 回目の書き換えから見つかる。
     */
    class FrameResourceTrackState
    {
    public:
        FrameResourceTrackState() = default;
        FrameResourceTrackState(const FrameResourceTrackState&) = delete;
        FrameResourceTrackState& operator=(const FrameResourceTrackState&) = delete;

        bool IsRegistered() const { return Tracker != nullptr; }

        FrameResourceWriteTracker* Tracker = nullptr;
        uint64_t Id = 0;
        uint64_t Generation = 1;
        uint64_t Size = 0;
        FrameResourceTrackKind Kind = FrameResourceTrackKind::Buffer;
        bool bProtected = false;
        Core::Container::String Name;

        Thread::Mutex Mutex;
        /** 数えた書き込みの回数（作成の直後の 1 回を含む） */
        uint64_t WriteCount = 0;
        uint64_t LastWriteFrame = 0;
        /** 作成の直後の 1 回を除いて書き換えたフレームの数 */
        uint64_t RewriteFrames = 0;
        FrameResourceRewriteEntry* RewriteEntry = nullptr;
        /** 種類ごとに 1 度だけ報告する */
        uint32_t ReportedMask = 0;

        /** バッファ: 参照の範囲を記録するか（書き換えを一度見たら true） */
        Thread::Atomic<bool> bTrackReferences{false};
        /** バッファ: フレームの番号の偶奇ごとの参照の範囲 */
        uint64_t ReferenceFrame[2] = {0, 0};
        Core::Container::VariableArray<FrameResourceRange> References[2];

        /** 記述子セット: 最後に束縛したフレームと、その前に束縛したフレーム */
        Thread::Atomic<uint64_t> LastBoundFrame{0};
        uint64_t PreviousBoundFrame = 0;
    };

    /**
     * @brief 守られていない上書きの見張り（デバイスが 1 つ持つ。開発ビルドだけ）
     *
     * フレームの番号は RenderingCoordinator がフレームの持ち主の開始のたびに BeginFrame で進める。
     * どのスレッドから呼んでもよい（資源ごとの排他と、報告の排他を持つ。順序は資源 → 報告）。
     */
    class FrameResourceWriteTracker
    {
    public:
        /** 1 つのフレームで 1 つのバッファに残す参照の範囲の数の上限（超えたら最後の範囲へまとめる） */
        static constexpr uint32_t MaxReferenceRanges = 32;

        void BeginFrame() { m_Frame.FetchAdd(1); }
        uint64_t GetFrame() const { return m_Frame.Load(std::memory_order_relaxed); }

        /** @brief 資源を作ったときに登録する（名前が空なら作ったパスの名前と種類から決める） */
        void Register(FrameResourceTrackState& state,
                      FrameResourceTrackKind kind,
                      const char* debugName,
                      uint64_t size,
                      uint64_t generation,
                      bool bProtected)
        {
            state.Tracker = this;
            state.Id = m_NextId.FetchAdd(1);
            state.Generation = generation;
            state.Size = size;
            state.Kind = kind;
            state.bProtected = bProtected;
            if (debugName && debugName[0] != '\0')
            {
                state.Name = debugName;
            }
            else
            {
                state.Name = GetFrameResourceWriteScopeName();
                state.Name += kind == FrameResourceTrackKind::Buffer ? ".Buffer" : ".DescriptorSet";
            }
            for (char& c : state.Name)
            {
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                {
                    c = '_';
                }
            }
            // 末尾の番号（DynUBO_Slot12 など、同じ種類の資源を番号で分けた名前）は # にまとめ、組を種類ごとにする
            size_t digitBegin = state.Name.size();
            while (digitBegin > 0 && state.Name[digitBegin - 1] >= '0' && state.Name[digitBegin - 1] <= '9')
            {
                --digitBegin;
            }
            if (digitBegin > 0 && digitBegin < state.Name.size())
            {
                Core::Container::String normalized;
                for (size_t index = 0; index < digitBegin; ++index)
                {
                    normalized += state.Name[index];
                }
                normalized += '#';
                state.Name = normalized;
            }
        }

        /** @brief バッファへの書き込み（Update・明示した書き込みの借り） */
        void OnBufferWrite(FrameResourceTrackState& state, uint64_t offset, uint64_t size)
        {
            const uint64_t frame = GetFrame();
            const char* scope = GetFrameResourceWriteScopeName();
            NorvesLib::Thread::ScopedLock lock(state.Mutex);
            if (!CountWriteLocked(state, frame, scope))
            {
                return;
            }
            state.bTrackReferences.Store(true, std::memory_order_relaxed);
            const FrameResourceRange written{offset, offset + size};
            if (OverlapsLocked(state, frame, written))
            {
                ReportLocked(state, FrameResourceHazard::BufferRecordedSameFrame, scope, frame, written);
            }
            else if (!state.bProtected && frame > 1 && OverlapsLocked(state, frame - 1, written))
            {
                ReportLocked(state, FrameResourceHazard::BufferPreviousFrame, scope, frame, written);
            }
        }

        /** @brief 記録したコマンドがバッファの範囲を参照した（束縛・コピー・間接引数） */
        void OnBufferReference(FrameResourceTrackState& state, uint64_t offset, uint64_t size)
        {
            if (!state.bTrackReferences.Load(std::memory_order_relaxed))
            {
                return;
            }
            const uint64_t frame = GetFrame();
            if (offset >= state.Size)
            {
                return;
            }
            const uint64_t end = (size == ~0ull || size > state.Size - offset) ? state.Size : offset + size;
            NorvesLib::Thread::ScopedLock lock(state.Mutex);
            const uint32_t slot = static_cast<uint32_t>(frame & 1u);
            if (state.ReferenceFrame[slot] != frame)
            {
                state.ReferenceFrame[slot] = frame;
                state.References[slot].clear();
            }
            AddRange(state.References[slot], FrameResourceRange{offset, end});
        }

        /** @brief 記述子セットへの書き込み（実際に書いた Update） */
        void OnDescriptorWrite(FrameResourceTrackState& state)
        {
            const uint64_t frame = GetFrame();
            const char* scope = GetFrameResourceWriteScopeName();
            NorvesLib::Thread::ScopedLock lock(state.Mutex);
            if (!CountWriteLocked(state, frame, scope))
            {
                return;
            }
            const uint64_t lastBound = state.LastBoundFrame.Load(std::memory_order_relaxed);
            if (lastBound == frame)
            {
                ReportLocked(state, FrameResourceHazard::DescriptorBoundSameFrame, scope, frame, FrameResourceRange{});
            }
            else if (!state.bProtected && frame > 1 &&
                     (lastBound == frame - 1 || state.PreviousBoundFrame == frame - 1))
            {
                ReportLocked(state, FrameResourceHazard::DescriptorBoundPreviousFrame, scope, frame, FrameResourceRange{});
            }
        }

        /** @brief 記録したコマンドが記述子セットを束縛した（セットが参照するバッファの範囲は呼び出し側が OnBufferReference で足す） */
        void OnDescriptorBound(FrameResourceTrackState& state)
        {
            const uint64_t frame = GetFrame();
            if (state.LastBoundFrame.Load(std::memory_order_relaxed) == frame)
            {
                return;
            }
            NorvesLib::Thread::ScopedLock lock(state.Mutex);
            const uint64_t lastBound = state.LastBoundFrame.Load(std::memory_order_relaxed);
            if (lastBound != frame)
            {
                state.PreviousBoundFrame = lastBound;
                state.LastBoundFrame.Store(frame, std::memory_order_relaxed);
            }
        }

        /**
         * @brief 一覧と守られていない上書きの集計をログへ出す（終わりに 1 回。2 回目からは何もしない）
         */
        void EmitSummary(const char* reason)
        {
            NorvesLib::Thread::ScopedLock lock(m_ReportMutex);
            if (m_bSummaryEmitted)
            {
                return;
            }
            m_bSummaryEmitted = true;
            uint64_t rewriteCount = 0;
            for (const auto& [key, entry] : m_Rewrites)
            {
                (void)key;
                const uint64_t frames = entry->MaxFrames.Load(std::memory_order_relaxed);
                if (frames < 2)
                {
                    continue;
                }
                ++rewriteCount;
                NORVES_LOG_INFO("FrameResource",
                                "FRAME_RESOURCE_REWRITE resource=%s pass=%s frames=%llu instances=%llu",
                                entry->Resource.c_str(),
                                entry->Pass.c_str(),
                                static_cast<unsigned long long>(frames),
                                static_cast<unsigned long long>(entry->Instances));
            }
            uint64_t resources = 0;
            for (const auto& [key, entry] : m_Unprotected)
            {
                (void)key;
                resources += entry.Resources;
                NORVES_LOG_INFO("FrameResource",
                                "FRAME_RESOURCE_UNPROTECTED_TOTAL resource=%s pass=%s kind=%s resources=%llu first_frame=%llu",
                                entry.Resource.c_str(),
                                entry.Pass.c_str(),
                                entry.Kind,
                                static_cast<unsigned long long>(entry.Resources),
                                static_cast<unsigned long long>(entry.FirstFrame));
            }
            NORVES_LOG_INFO("FrameResource",
                            "FRAME_RESOURCE_UNPROTECTED_SUMMARY count=%llu resources=%llu events=%llu rewrites=%llu frames=%llu reason=%s",
                            static_cast<unsigned long long>(m_Unprotected.size()),
                            static_cast<unsigned long long>(resources),
                            static_cast<unsigned long long>(m_Events.Load(std::memory_order_relaxed)),
                            static_cast<unsigned long long>(rewriteCount),
                            static_cast<unsigned long long>(GetFrame()),
                            reason ? reason : "unknown");
        }

        /** @brief 守られていない上書きの組の数（名前・パス・種類の組） */
        uint64_t GetUnprotectedCount() const
        {
            NorvesLib::Thread::ScopedLock lock(m_ReportMutex);
            return m_Unprotected.size();
        }

        /** @brief 見つけた守られていない上書きの回数（報告を省いた分を含む） */
        uint64_t GetUnprotectedEventCount() const { return m_Events.Load(std::memory_order_relaxed); }

    private:
        struct UnprotectedEntry
        {
            Core::Container::String Resource;
            Core::Container::String Pass;
            const char* Kind = "";
            uint64_t Resources = 0;
            uint64_t FirstFrame = 0;
        };

        /** @brief 名前・パス・種類の組のキー（FNV-1a） */
        static uint64_t HashKey(const char* resource, const char* pass, const char* kind)
        {
            uint64_t hash = 14695981039346656037ull;
            const char* parts[3] = {resource, pass, kind};
            for (const char* part : parts)
            {
                for (const char* c = part; c && *c != '\0'; ++c)
                {
                    hash = (hash ^ static_cast<uint8_t>(*c)) * 1099511628211ull;
                }
                hash = (hash ^ static_cast<uint8_t>('|')) * 1099511628211ull;
            }
            return hash;
        }

        /** @brief 書き込みを数え、見張る書き換えなら true（作成の直後の 1 回は false） */
        bool CountWriteLocked(FrameResourceTrackState& state, uint64_t frame, const char* scope)
        {
            ++state.WriteCount;
            const bool bNewFrame = state.LastWriteFrame != frame;
            state.LastWriteFrame = frame;
            if (state.WriteCount == 1)
            {
                return false;
            }
            if (!state.bProtected && bNewFrame)
            {
                ++state.RewriteFrames;
                if (state.RewriteFrames >= 2)
                {
                    if (!state.RewriteEntry)
                    {
                        NorvesLib::Thread::ScopedLock reportLock(m_ReportMutex);
                        const uint64_t key = HashKey(state.Name.c_str(), scope, "");
                        auto& entry = m_Rewrites[key];
                        if (!entry)
                        {
                            entry = Core::Container::MakeUnique<FrameResourceRewriteEntry>();
                            entry->Resource = state.Name;
                            entry->Pass = scope;
                        }
                        ++entry->Instances;
                        state.RewriteEntry = entry.get();
                    }
                    uint64_t current = state.RewriteEntry->MaxFrames.Load(std::memory_order_relaxed);
                    while (current < state.RewriteFrames &&
                           !state.RewriteEntry->MaxFrames.CompareExchangeWeak(current, state.RewriteFrames))
                    {
                    }
                }
            }
            return true;
        }

        static bool OverlapsLocked(const FrameResourceTrackState& state, uint64_t frame, const FrameResourceRange& range)
        {
            const uint32_t slot = static_cast<uint32_t>(frame & 1u);
            if (state.ReferenceFrame[slot] != frame)
            {
                return false;
            }
            for (const FrameResourceRange& reference : state.References[slot])
            {
                if (reference.Begin < range.End && range.Begin < reference.End)
                {
                    return true;
                }
            }
            return false;
        }

        static void AddRange(Core::Container::VariableArray<FrameResourceRange>& ranges, const FrameResourceRange& range)
        {
            for (FrameResourceRange& existing : ranges)
            {
                if (existing.Begin <= range.End && range.Begin <= existing.End)
                {
                    existing.Begin = existing.Begin < range.Begin ? existing.Begin : range.Begin;
                    existing.End = existing.End > range.End ? existing.End : range.End;
                    return;
                }
            }
            if (ranges.size() >= MaxReferenceRanges)
            {
                FrameResourceRange& last = ranges.back();
                last.Begin = last.Begin < range.Begin ? last.Begin : range.Begin;
                last.End = last.End > range.End ? last.End : range.End;
                return;
            }
            ranges.push_back(range);
        }

        void ReportLocked(FrameResourceTrackState& state,
                          FrameResourceHazard hazard,
                          const char* scope,
                          uint64_t frame,
                          const FrameResourceRange& range)
        {
            m_Events.FetchAdd(1, std::memory_order_relaxed);
            const uint32_t bit = 1u << static_cast<uint32_t>(hazard);
            if ((state.ReportedMask & bit) != 0)
            {
                return;
            }
            state.ReportedMask |= bit;

            const char* kindName = GetFrameResourceHazardName(hazard);
            NorvesLib::Thread::ScopedLock reportLock(m_ReportMutex);
            UnprotectedEntry& entry = m_Unprotected[HashKey(state.Name.c_str(), scope, kindName)];
            ++entry.Resources;
            if (entry.Resources != 1)
            {
                return;
            }
            entry.Resource = state.Name;
            entry.Pass = scope;
            entry.Kind = kindName;
            entry.FirstFrame = frame;
            NORVES_LOG_WARNING("FrameResource",
                               "FRAME_RESOURCE_UNPROTECTED resource=%s pass=%s kind=%s id=%llu gen=%llu range=%llu+%llu frame=%llu",
                               state.Name.c_str(),
                               scope,
                               kindName,
                               static_cast<unsigned long long>(state.Id),
                               static_cast<unsigned long long>(state.Generation),
                               static_cast<unsigned long long>(range.Begin),
                               static_cast<unsigned long long>(range.End - range.Begin),
                               static_cast<unsigned long long>(frame));
        }

        Thread::Atomic<uint64_t> m_Frame{1};
        Thread::Atomic<uint64_t> m_NextId{1};
        Thread::Atomic<uint64_t> m_Events{0};
        mutable Thread::Mutex m_ReportMutex;
        // キーは名前・パス・種類の 64 bit のハッシュ（衝突は見込まない）
        Core::Container::UnorderedMap<uint64_t, UnprotectedEntry> m_Unprotected;
        Core::Container::UnorderedMap<uint64_t, Core::Container::TUniquePtr<FrameResourceRewriteEntry>> m_Rewrites;
        bool m_bSummaryEmitted = false;
    };
} // namespace NorvesLib::RHI
