#pragma once

#include "Container/Containers.h"
#include "Container/Deque.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief タイル・ミップテイル・バッファの区画のデータを、GPU を待たずに書く共通のアップロードのリング
     *
     * 毎フレームのステージングのリング（既定 32 MiB、host-visible で DeviceLocal でない）へ CPU がデータを
     * 置き、描画のコマンドの先頭（RecordCopies）でバッファからイメージの領域（EnqueueTile）、または
     * バッファの区画（EnqueueBufferCopy。ジオメトリのプールなど）へコピーする。
     * `VulkanTexture::Update` の WaitIdle の経路は使わない。リングとフレームのコピー量の上限は
     * テクスチャとバッファで共有する。
     *
     * - リングの区画は、それをコピーしたフレームの提出 serial が完了するまで再利用しない
     *   （BeginFrame に完了済みの serial を渡すと、期限の来た区画が空く）。
     * - 1フレームにコピーする量には上限（既定 24 MiB）がある。超える分は次のフレームへ持ち越す。
     *   上限より大きい1件は Enqueue が拒否するので、どのフレームも上限を超えない。
     * - 同じテクスチャの同じミップ・配列要素で矩形が重なるコピー、同じバッファでバイト範囲が重なるコピーは、
     *   同じフレームに積まず次のフレームへ送る（コピー同士の書込み順をバリアなしに保証できないため。
     *   フレームをまたげば遷移のバリアが順序を作る）。
     * - コピーは Enqueue した順に記録する。フレームを提出できなかったとき（AbortFrame）は、記録した分を
     *   未記録へ戻して次のフレームで出し直す。
     * - 対象のテクスチャは、記録の前後とも ShaderResource の状態にあるものとして扱う。最初に
     *   EnqueueInitialize で一度だけ Undefined から ShaderResource へ遷移する（コピーより前に並べること）。
     *   コピーはテクスチャ全体を ShaderResource→CopyDest→ShaderResource へ遷移して挟む。
     *   RenderGraph が管理する資源ではない（材質のテクスチャなど）ことが前提。
     * - バッファの区画は、コピーの前後とも GenericRead（頂点・インデックス・storage・間接引数などの読み取り）の
     *   状態にあるものとして扱い、バッファ全体を GenericRead→CopyDest→GenericRead のバリアで挟む。
     *   コピー先は TransferDst の用途で作ること。区画を GPU が書く用途（UAV の書き込み）には使わない。
     * - 対象のテクスチャ・バッファは、そのコピーを含む提出が完了するまでここが参照を持つ。
     *
     * 状態はすべて内部のミューテックスで守るので、Enqueue はどのスレッドからでも呼べる。
     * RecordCopies・BeginFrame・CommitFrame・AbortFrame は RenderThread から呼ぶ。
     * リングのバッファは GPU が使い終わった後（WaitIdle の後）に破棄すること。
     */
    class TileUploader final
    {
    public:
        static constexpr uint64_t DefaultRingBytes = 32ull * 1024ull * 1024ull;
        static constexpr uint64_t DefaultFrameCopyLimitBytes = 24ull * 1024ull * 1024ull;
        // リング内の区画の位置・大きさの単位（ブロック圧縮のブロックや optimalBufferCopyOffsetAlignment を余裕で満たす）
        static constexpr uint64_t RingAlignment = 256;

        struct Config
        {
            uint64_t RingBytes = DefaultRingBytes;
            uint64_t FrameCopyLimitBytes = DefaultFrameCopyLimitBytes;
        };

        struct Stats
        {
            uint64_t RingBytes = 0;
            // 区画が使われている量（記録待ち・提出中・位置合わせの余白を含む）
            uint64_t RingUsedBytes = 0;
            // まだ記録していないコピーの数
            uint32_t PendingCopies = 0;
            // 記録済みで、提出の完了を待っているコピーの数
            uint32_t InFlightCopies = 0;
            // 今のフレームで記録したコピーの量（データの量）と数
            uint64_t FrameCopiedBytes = 0;
            uint32_t FrameCopyCount = 0;
        };

        explicit TileUploader(Container::TSharedPtr<RHI::IDevice> device, const Config &config = Config())
            : m_Device(std::move(device)), m_Config(config)
        {
            m_Config.RingBytes = AlignDown(m_Config.RingBytes, RingAlignment);
        }

        ~TileUploader() { Clear(); }

        TileUploader(const TileUploader &) = delete;
        TileUploader &operator=(const TileUploader &) = delete;

        /**
         * @brief テクスチャを最初の ShaderResource の状態へ遷移する依頼を積む（作成直後に一度だけ）
         *
         * 次の RecordCopies で Undefined→ShaderResource の遷移を記録する。そのテクスチャへの EnqueueTile より
         * 先に呼ぶこと（コピーより前に記録される）。
         */
        bool EnqueueInitialize(RHI::TexturePtr texture)
        {
            if (!texture)
            {
                return false;
            }
            Op op;
            op.Texture = std::move(texture);
            op.bCopy = false;
            Thread::ScopedLock lock(m_Mutex);
            m_Ops.push_back(std::move(op));
            return true;
        }

        /**
         * @brief タイル（またはミップテイル）のデータをリングへ置き、テクスチャの領域へのコピーを積む
         *
         * データはここでリングへ複写するので、呼び出しの後で元のバッファは手放してよい。
         * region の BufferOffset は無視する（リング内の位置をこちらで決める）。
         * @return 積めたら true。リングに空きが無い（GPU が使っているコピーの完了待ち）、バッファを作れない、
         *         引数が不正（大きさ 0・リングまたはフレームのコピー量の上限より大きい・矩形が空）のときは
         *         false（何も積まない）。
         *         空きが無いときは、フレームが進んだあとにもう一度呼ぶ。
         */
        bool EnqueueTile(RHI::TexturePtr texture, const RHI::TextureRegionCopy &region, const void *data, uint64_t bytes)
        {
            if (!texture || data == nullptr || bytes == 0 || region.Width == 0 || region.Height == 0)
            {
                return false;
            }

            Op op;
            op.Texture = std::move(texture);
            op.Region = region;
            return EnqueueCopyOp(std::move(op), data, bytes);
        }

        /**
         * @brief バッファの区画（dstOffset から bytes バイト）へのコピーを、データをリングへ置いて積む
         *
         * データはここでリングへ複写するので、呼び出しの後で元のメモリは手放してよい。
         * 記録は RecordCopies（描画のコマンドの先頭）で、GPU を待たない。
         * @return 積めたら true。リングに空きが無い、バッファを作れない、引数が不正（バッファが null・
         *         大きさ 0・範囲がバッファの外・TransferDst の用途が無い・リングまたはフレームのコピー量の上限より
         *         大きい）のときは false（何も積まない）。空きが無いときは、フレームが進んだあとにもう一度呼ぶ。
         */
        bool EnqueueBufferCopy(RHI::BufferPtr buffer, uint64_t dstOffset, const void *data, uint64_t bytes)
        {
            if (!buffer || data == nullptr || bytes == 0)
            {
                return false;
            }
            if (dstOffset > buffer->GetSize() || bytes > buffer->GetSize() - dstOffset)
            {
                LOG_ERROR("TileUploader: バッファの区画が範囲外 offset=%llu bytes=%llu size=%llu",
                          static_cast<unsigned long long>(dstOffset), static_cast<unsigned long long>(bytes),
                          static_cast<unsigned long long>(buffer->GetSize()));
                return false;
            }
            if ((buffer->GetUsage() & RHI::ResourceUsage::TransferDst) == RHI::ResourceUsage::None)
            {
                LOG_ERROR("TileUploader: コピー先のバッファに TransferDst の用途が無い");
                return false;
            }

            Op op;
            op.Buffer = std::move(buffer);
            op.DstOffset = dstOffset;
            return EnqueueCopyOp(std::move(op), data, bytes);
        }

        /**
         * @brief 次の RecordCopies で確実に記録できるコピー量の残り（バイト）
         *
         * フレームのコピー量の上限から、記録済みの量と、まだ記録していないコピーの量を引いた値。
         * 描画から読まれる前にコピーを済ませたい呼び出し側（VT のストリーマ）は、この範囲でだけ積む。
         */
        uint64_t GetRecordableCopyBytes() const
        {
            Thread::ScopedLock lock(m_Mutex);
            uint64_t used = m_FrameCopiedBytes;
            for (const Op &op : m_Ops)
            {
                if (op.bCopy && !op.bCancelled && op.State == OpState::Pending)
                {
                    used += op.DataBytes;
                }
            }
            return used >= m_Config.FrameCopyLimitBytes ? 0 : m_Config.FrameCopyLimitBytes - used;
        }

        /**
         * @brief 積んだがまだ記録していないコピーの数と量（バイト）
         *
         * AbortFrame で未記録へ戻ったコピーも含む。呼び出し側（VT のストリーマ）が、次の記録で出るコピーを
         * 自分のフレームの上限に算入するために使う。
         */
        void GetPendingCopyLoad(uint32_t &outCount, uint64_t &outBytes) const
        {
            Thread::ScopedLock lock(m_Mutex);
            outCount = 0;
            outBytes = 0;
            for (const Op &op : m_Ops)
            {
                if (op.bCopy && !op.bCancelled && op.State == OpState::Pending)
                {
                    ++outCount;
                    outBytes += op.DataBytes;
                }
            }
        }

        /**
         * @brief 最後に積んだ count 件（遷移の依頼を含む）を、記録する前に取り消す
         *
         * 結び付けに失敗したときなど、積んだコピーを無かったことにする。リングの区画も元へ戻す。
         * 後ろから順に取り消し、記録を始めた依頼に当たったらそこで止める。
         * @return 取り消した件数
         */
        uint32_t DiscardLastEnqueued(uint32_t count)
        {
            Container::VariableArray<RHI::TexturePtr> released;
            Container::VariableArray<RHI::BufferPtr> releasedBuffers;
            uint32_t discarded = 0;
            {
                Thread::ScopedLock lock(m_Mutex);
                while (discarded < count && !m_Ops.empty() && m_Ops.back().State == OpState::Pending &&
                       !m_Ops.back().bCancelled)
                {
                    Op &back = m_Ops.back();
                    if (back.bCopy)
                    {
                        m_UsedBytes -= back.RingBytes;
                        m_Head = back.HeadBefore;
                    }
                    released.push_back(std::move(back.Texture));
                    releasedBuffers.push_back(std::move(back.Buffer));
                    m_Ops.pop_back();
                    ++discarded;
                }
            }
            return discarded;
        }

        /**
         * @brief あるテクスチャ宛ての、まだ GPU へ出していない依頼を無効にする（登録の解除）
         *
         * 解除したテクスチャのページは別のテクスチャへ使い回されるので、古いコピーが後から書き込まないようにする。
         * 未記録の依頼はその場で無効にし、記録中のフレームの依頼は、そのフレームを提出できなかったとき
         * （AbortFrame）に無効にする。提出済みの依頼は、ページの返却が提出の完了まで待つので何もしない。
         */
        void AbandonTexture(const RHI::TexturePtr &texture)
        {
            if (!texture)
            {
                return;
            }
            Thread::ScopedLock lock(m_Mutex);
            for (Op &op : m_Ops)
            {
                if (op.Texture.get() != texture.get() || op.bCancelled)
                {
                    continue;
                }
                if (op.State == OpState::Pending)
                {
                    CancelLocked(op);
                }
                else if (op.State == OpState::Recorded)
                {
                    op.bAbandoned = true;
                }
            }
        }

        /**
         * @brief あるテクスチャの領域宛ての、まだ GPU へ出していないコピーを無効にする（タイルの追い出し）
         *
         * 外したタイルのページは別のタイルへ使い回されるので、古いコピーが後から書き込まないようにする。
         * 未記録のコピーはその場で無効にし、記録中のフレームのコピーは、そのフレームを提出できなかったとき
         * （AbortFrame）に無効にする。提出済みのコピーは、ページの返却が提出の完了まで待つので何もしない。
         * @return 無効にした（または記録中のため印を付けた）コピーの数
         */
        uint32_t AbandonRegion(const RHI::TexturePtr &texture, const RHI::TextureRegionCopy &region)
        {
            if (!texture)
            {
                return 0;
            }
            Thread::ScopedLock lock(m_Mutex);
            uint32_t count = 0;
            for (Op &op : m_Ops)
            {
                if (!op.bCopy || op.bCancelled || op.Texture.get() != texture.get() ||
                    !OverlapsRegion(op, region))
                {
                    continue;
                }
                if (op.State == OpState::Pending)
                {
                    CancelLocked(op);
                    ++count;
                }
                else if (op.State == OpState::Recorded)
                {
                    op.bAbandoned = true;
                    ++count;
                }
            }
            return count;
        }

        /**
         * @brief あるバッファの範囲宛ての、まだ GPU へ出していないコピーを無効にする（区画の解放）
         *
         * 解放した区画は別の用途へ使い回されるので、古いコピーが後から書き込まないようにする。
         * 未記録のコピーはその場で無効にし、記録中のフレームのコピーは、そのフレームを提出できなかったとき
         * （AbortFrame）に無効にする。提出済みのコピーは、区画の返却が提出の完了まで待つので何もしない。
         * @return 無効にした（または記録中のため印を付けた）コピーの数
         */
        uint32_t AbandonBufferRange(const RHI::BufferPtr &buffer, uint64_t offset, uint64_t bytes)
        {
            if (!buffer || bytes == 0)
            {
                return 0;
            }
            Thread::ScopedLock lock(m_Mutex);
            uint32_t count = 0;
            for (Op &op : m_Ops)
            {
                if (!op.bCopy || op.bCancelled || op.Buffer.get() != buffer.get() ||
                    !(op.DstOffset < offset + bytes && offset < op.DstOffset + op.DataBytes))
                {
                    continue;
                }
                if (op.State == OpState::Pending)
                {
                    CancelLocked(op);
                    ++count;
                }
                else if (op.State == OpState::Recorded)
                {
                    op.bAbandoned = true;
                    ++count;
                }
            }
            return count;
        }

        /**
         * @brief フレームの記録を始める（RenderThread。完了済みの serial を渡す）
         *
         * 提出が完了したコピーの区画とテクスチャの参照を手放し、フレームのコピー量を 0 に戻す。
         */
        void BeginFrame(uint64_t completedSerial)
        {
            Container::VariableArray<RHI::TexturePtr> released;
            Container::VariableArray<RHI::BufferPtr> releasedBuffers;
            {
                Thread::ScopedLock lock(m_Mutex);
                m_CompletedSerial = std::max(m_CompletedSerial, completedSerial);
                while (!m_Ops.empty())
                {
                    Op &front = m_Ops.front();
                    if (front.State != OpState::InFlight || front.Serial > m_CompletedSerial)
                    {
                        break;
                    }
                    m_UsedBytes -= front.RingBytes;
                    // テクスチャの破棄はロックの外で行う（RHI のデストラクタが他のロックを取っても詰まらないように）
                    released.push_back(std::move(front.Texture));
                    releasedBuffers.push_back(std::move(front.Buffer));
                    m_Ops.pop_front();
                }
                m_FrameCopiedBytes = 0;
                m_FrameCopyCount = 0;
            }
        }

        /**
         * @brief 積んであるコピーを、上限の範囲で描画のコマンドの先頭へ記録する（RenderThread）
         *
         * render pass の外で、フレームのコマンドを開いた直後に呼ぶ。記録した順は Enqueue の順。
         * @return このフレームで記録したコピーの数（遷移だけの依頼は数えない）
         */
        uint32_t RecordCopies(RHI::ICommandList &commandList)
        {
            Thread::ScopedLock lock(m_Mutex);

            // 先頭の未記録から、上限に収まる分までを選ぶ（順序は保つ）。
            size_t firstPending = 0;
            while (firstPending < m_Ops.size() && m_Ops[firstPending].State != OpState::Pending)
            {
                ++firstPending;
            }
            size_t endPending = firstPending;
            uint64_t copiedBytes = m_FrameCopiedBytes;
            Container::VariableArray<const Op *> selectedCopies;
            while (endPending < m_Ops.size())
            {
                const Op &op = m_Ops[endPending];
                if (op.bCancelled)
                {
                    ++endPending;
                    continue;
                }
                if (op.bCopy)
                {
                    if (copiedBytes + op.DataBytes > m_Config.FrameCopyLimitBytes)
                    {
                        break;
                    }
                    // 先に選んだコピーと書込み先が重なるものは次のフレームへ送る（後続も順序を保つため一緒に送る）
                    bool bOverlaps = false;
                    for (const Op *selected : selectedCopies)
                    {
                        if (OverlapsOp(*selected, op))
                        {
                            bOverlaps = true;
                            break;
                        }
                    }
                    if (bOverlaps)
                    {
                        break;
                    }
                    selectedCopies.push_back(&op);
                    copiedBytes += op.DataBytes;
                }
                ++endPending;
            }
            if (firstPending == endPending)
            {
                return 0;
            }

            // 1) 初期化の遷移 2) コピー先のテクスチャを CopyDest へ・バッファを GenericRead→CopyDest へ
            // 3) コピー 4) ShaderResource・GenericRead へ戻す
            Container::VariableArray<RHI::TexturePtr> copyTargets;
            Container::VariableArray<RHI::BufferPtr> bufferTargets;
            for (size_t index = firstPending; index < endPending; ++index)
            {
                const Op &op = m_Ops[index];
                if (op.bCancelled)
                {
                    continue;
                }
                if (!op.bCopy)
                {
                    commandList.TextureBarrier(op.Texture, RHI::ResourceState::Undefined, RHI::ResourceState::ShaderResource);
                    continue;
                }
                if (op.Buffer)
                {
                    bool bKnownBuffer = false;
                    for (const RHI::BufferPtr &target : bufferTargets)
                    {
                        if (target.get() == op.Buffer.get())
                        {
                            bKnownBuffer = true;
                            break;
                        }
                    }
                    if (!bKnownBuffer)
                    {
                        bufferTargets.push_back(op.Buffer);
                    }
                    continue;
                }
                bool bKnown = false;
                for (const RHI::TexturePtr &target : copyTargets)
                {
                    if (target.get() == op.Texture.get())
                    {
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    copyTargets.push_back(op.Texture);
                }
            }
            for (const RHI::TexturePtr &target : copyTargets)
            {
                commandList.TextureBarrier(target, RHI::ResourceState::ShaderResource, RHI::ResourceState::CopyDest);
            }
            for (const RHI::BufferPtr &target : bufferTargets)
            {
                commandList.BufferBarrier(target, RHI::ResourceState::GenericRead, RHI::ResourceState::CopyDest);
            }

            uint32_t recordedCopies = 0;
            for (size_t index = firstPending; index < endPending; ++index)
            {
                Op &op = m_Ops[index];
                if (op.bCancelled)
                {
                    continue;
                }
                op.State = OpState::Recorded;
                if (!op.bCopy)
                {
                    continue;
                }
                if (op.Buffer)
                {
                    commandList.CopyBuffer(m_Ring, op.Buffer, op.DataBytes, op.Region.BufferOffset, op.DstOffset);
                    ++recordedCopies;
                }
                else if (commandList.CopyBufferToTextureRegion(m_Ring, op.Texture, op.Region))
                {
                    ++recordedCopies;
                }
                else
                {
                    LOG_ERROR("TileUploader: 領域コピーを記録できなかった mip=%u x=%u y=%u", op.Region.MipLevel,
                              op.Region.OffsetX, op.Region.OffsetY);
                }
            }

            for (const RHI::TexturePtr &target : copyTargets)
            {
                commandList.TextureBarrier(target, RHI::ResourceState::CopyDest, RHI::ResourceState::ShaderResource);
            }
            for (const RHI::BufferPtr &target : bufferTargets)
            {
                commandList.BufferBarrier(target, RHI::ResourceState::CopyDest, RHI::ResourceState::GenericRead);
            }

            m_FrameCopiedBytes = copiedBytes;
            m_FrameCopyCount += recordedCopies;
            return recordedCopies;
        }

        /** @brief フレームを serial で提出した。このフレームで記録した分は、その serial の完了まで区画を保持する。 */
        void CommitFrame(uint64_t submissionSerial)
        {
            Thread::ScopedLock lock(m_Mutex);
            for (Op &op : m_Ops)
            {
                if (op.State == OpState::Recorded)
                {
                    op.State = OpState::InFlight;
                    op.Serial = submissionSerial;
                }
            }
        }

        /**
         * @brief フレームを提出せずに捨てた。記録した分は未記録へ戻し、次のフレームで出し直す。
         *
         * 記録中に AbandonTexture で解除されたテクスチャの依頼は、出し直さず無効にする。
         */
        void AbortFrame()
        {
            Thread::ScopedLock lock(m_Mutex);
            for (Op &op : m_Ops)
            {
                if (op.State != OpState::Recorded)
                {
                    continue;
                }
                if (op.bAbandoned)
                {
                    CancelLocked(op);
                }
                else
                {
                    op.State = OpState::Pending;
                }
            }
            m_FrameCopiedBytes = 0;
            m_FrameCopyCount = 0;
        }

        /**
         * @brief GPU が完全に止まった後（WaitIdle の後）に、積んだ分を全部捨てて初期状態へ戻す。
         *
         * リングのバッファも手放す（次の Enqueue で作り直す）。serial も振り出しに戻す。
         */
        void Clear()
        {
            Container::VariableArray<RHI::TexturePtr> releasedTextures;
            Container::VariableArray<RHI::BufferPtr> releasedBuffers;
            RHI::BufferPtr releasedRing;
            {
                Thread::ScopedLock lock(m_Mutex);
                for (Op &op : m_Ops)
                {
                    releasedTextures.push_back(std::move(op.Texture));
                    releasedBuffers.push_back(std::move(op.Buffer));
                }
                m_Ops.clear();
                if (m_Ring && m_Mapped != nullptr)
                {
                    m_Ring->Unmap();
                }
                m_Mapped = nullptr;
                releasedRing = std::move(m_Ring);
                m_Head = 0;
                m_UsedBytes = 0;
                m_CompletedSerial = 0;
                m_FrameCopiedBytes = 0;
                m_FrameCopyCount = 0;
            }
        }

        Stats GetStats() const
        {
            Thread::ScopedLock lock(m_Mutex);
            Stats stats;
            stats.RingBytes = m_Ring ? m_Config.RingBytes : 0;
            stats.RingUsedBytes = m_UsedBytes;
            for (const Op &op : m_Ops)
            {
                if (!op.bCopy || op.bCancelled)
                {
                    continue;
                }
                if (op.State == OpState::InFlight)
                {
                    ++stats.InFlightCopies;
                }
                else
                {
                    ++stats.PendingCopies;
                }
            }
            stats.FrameCopiedBytes = m_FrameCopiedBytes;
            stats.FrameCopyCount = m_FrameCopyCount;
            return stats;
        }

        /**
         * @brief あるバッファの範囲宛てのコピーが、GPU で完了していないものを持っているか
         *
         * 積んだがまだ記録していない・記録した・提出して完了を待っているコピーのどれかが範囲に重なれば true。
         * 無効にしたコピー（AbandonBufferRange）は数えない。範囲の中身が GPU から読める状態かを、
         * BeginFrame に完了済みの serial を渡した後に確かめるのに使う（コピーを積んだ後 false になれば書き終わっている）。
         */
        bool HasUnfinishedBufferCopies(const RHI::BufferPtr &buffer, uint64_t offset, uint64_t bytes) const
        {
            if (!buffer || bytes == 0)
            {
                return false;
            }
            Thread::ScopedLock lock(m_Mutex);
            for (const Op &op : m_Ops)
            {
                if (op.bCopy && !op.bCancelled && !op.bAbandoned && op.Buffer.get() == buffer.get() &&
                    op.DstOffset < offset + bytes && offset < op.DstOffset + op.DataBytes)
                {
                    return true;
                }
            }
            return false;
        }

        uint64_t GetFrameCopyLimitBytes() const { return m_Config.FrameCopyLimitBytes; }
        uint64_t GetRingBytes() const { return m_Config.RingBytes; }

        /** @brief リングのバッファ（まだ作っていなければ null）。メモリ属性の検査などに使う。 */
        RHI::BufferPtr GetRingBuffer() const
        {
            Thread::ScopedLock lock(m_Mutex);
            return m_Ring;
        }

    private:
        enum class OpState : uint8_t
        {
            Pending,  // 積んだだけで、まだコマンドへ記録していない
            Recorded, // 記録中のフレームのコマンドへ記録した（提出の serial は未確定）
            InFlight, // 提出した。serial の完了で区画を手放す
        };

        struct Op
        {
            // コピー先がテクスチャの依頼ではこちら。初期化の遷移だけの依頼もこちらを使う
            RHI::TexturePtr Texture;
            // コピー先がバッファの区画の依頼ではこちら（このとき Texture は null）
            RHI::BufferPtr Buffer;
            uint64_t DstOffset = 0;
            // false は初期化の遷移だけの依頼（リングを使わない）
            bool bCopy = false;
            // テクスチャ宛てでは書込み先の矩形。BufferOffset はどちらの宛先でもリング内の位置
            RHI::TextureRegionCopy Region;
            uint64_t DataBytes = 0;
            // 位置合わせの余白と末尾の回り込みを含む、リングで占める量
            uint64_t RingBytes = 0;
            uint64_t Serial = 0;
            OpState State = OpState::Pending;
            // 積む前のリングの先頭の位置（DiscardLastEnqueued が戻す）
            uint64_t HeadBefore = 0;
            // 解除されたテクスチャ宛てで、記録中のフレームが捨てられたら無効にする
            bool bAbandoned = false;
            // 無効にした依頼。コマンドへ記録せず、区画だけを順番どおりに手放す（提出済みの完了済みとして扱う）
            bool bCancelled = false;
        };

        // 依頼を無効にする。区画は先頭から順に手放す決まりなので、完了済みの提出として並びに残す。ロックを持って呼ぶ。
        static void CancelLocked(Op &op)
        {
            op.bCancelled = true;
            op.bAbandoned = false;
            op.State = OpState::InFlight;
            op.Serial = 0;
        }

        // 書込み先が重なるか（テクスチャは同じミップ・配列要素で矩形が重なる、バッファは同じバッファでバイト範囲が重なる）
        static bool OverlapsOp(const Op &a, const Op &b)
        {
            if (a.Buffer || b.Buffer)
            {
                return a.Buffer.get() == b.Buffer.get() && a.DstOffset < b.DstOffset + b.DataBytes &&
                       b.DstOffset < a.DstOffset + a.DataBytes;
            }
            return OverlapsRegion(a, b);
        }

        // 同じテクスチャの同じミップ・配列要素で、書込み先の矩形が重なるか
        static bool OverlapsRegion(const Op &a, const Op &b)
        {
            if (a.Texture.get() != b.Texture.get() || a.Region.MipLevel != b.Region.MipLevel ||
                a.Region.ArrayIndex != b.Region.ArrayIndex)
            {
                return false;
            }
            const uint64_t aRight = static_cast<uint64_t>(a.Region.OffsetX) + a.Region.Width;
            const uint64_t bRight = static_cast<uint64_t>(b.Region.OffsetX) + b.Region.Width;
            const uint64_t aBottom = static_cast<uint64_t>(a.Region.OffsetY) + a.Region.Height;
            const uint64_t bBottom = static_cast<uint64_t>(b.Region.OffsetY) + b.Region.Height;
            return a.Region.OffsetX < bRight && b.Region.OffsetX < aRight && a.Region.OffsetY < bBottom &&
                   b.Region.OffsetY < aBottom;
        }

        // 依頼の書込み先が、同じテクスチャの領域 region（同じミップ・配列要素）と重なるか。テクスチャの一致は呼び出し側が確かめる
        static bool OverlapsRegion(const Op &op, const RHI::TextureRegionCopy &region)
        {
            if (op.Region.MipLevel != region.MipLevel || op.Region.ArrayIndex != region.ArrayIndex)
            {
                return false;
            }
            const uint64_t opRight = static_cast<uint64_t>(op.Region.OffsetX) + op.Region.Width;
            const uint64_t right = static_cast<uint64_t>(region.OffsetX) + region.Width;
            const uint64_t opBottom = static_cast<uint64_t>(op.Region.OffsetY) + op.Region.Height;
            const uint64_t bottom = static_cast<uint64_t>(region.OffsetY) + region.Height;
            return op.Region.OffsetX < right && region.OffsetX < opRight && op.Region.OffsetY < bottom &&
                   region.OffsetY < opBottom;
        }

        static constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        static constexpr uint64_t AlignDown(uint64_t value, uint64_t alignment)
        {
            return value / alignment * alignment;
        }

        // リングへデータを置き、依頼を積む共通の処理。op は宛先（Texture+Region か Buffer+DstOffset）を埋めて渡す。
        bool EnqueueCopyOp(Op op, const void *data, uint64_t bytes)
        {
            Thread::ScopedLock lock(m_Mutex);
            if (bytes > m_Config.RingBytes)
            {
                LOG_ERROR("TileUploader: データがリングより大きい bytes=%llu ring=%llu",
                          static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(m_Config.RingBytes));
                return false;
            }
            if (bytes > m_Config.FrameCopyLimitBytes)
            {
                LOG_ERROR("TileUploader: データがフレームのコピー量の上限より大きい bytes=%llu limit=%llu",
                          static_cast<unsigned long long>(bytes),
                          static_cast<unsigned long long>(m_Config.FrameCopyLimitBytes));
                return false;
            }
            if (!EnsureRingLocked())
            {
                return false;
            }

            const uint64_t alignedBytes = AlignUp(bytes, RingAlignment);
            if (m_UsedBytes == 0)
            {
                m_Head = 0;
            }
            uint64_t offset = AlignUp(m_Head, RingAlignment);
            uint64_t skipBytes = offset - m_Head;
            if (offset + alignedBytes > m_Config.RingBytes)
            {
                // 末尾に収まらないので、末尾の余りを使わずに先頭へ回す（余りはこの区画が持つ）
                skipBytes = m_Config.RingBytes - m_Head;
                offset = 0;
            }
            const uint64_t ringBytes = skipBytes + alignedBytes;
            if (m_UsedBytes + ringBytes > m_Config.RingBytes)
            {
                return false;
            }

            std::memcpy(static_cast<uint8_t *>(m_Mapped) + offset, data, static_cast<size_t>(bytes));

            op.bCopy = true;
            op.Region.BufferOffset = offset;
            op.DataBytes = bytes;
            op.RingBytes = ringBytes;
            op.HeadBefore = m_Head;
            m_Ops.push_back(std::move(op));
            m_Head = offset + alignedBytes;
            m_UsedBytes += ringBytes;
            return true;
        }

        // 最初の Enqueue でリングを作る（使われない起動では 32 MiB を確保しない）。ロックを持って呼ぶ。
        bool EnsureRingLocked()
        {
            if (m_Ring)
            {
                return true;
            }
            if (!m_Device || m_Config.RingBytes == 0)
            {
                return false;
            }
            // DeviceLocal でない host-visible のメモリを要求する（選べなければ作成が失敗する）
            RHI::BufferDesc ringDesc(m_Config.RingBytes, RHI::ResourceUsage::TransferSrc, true, "TileUploadRing");
            ringDesc.bExcludeDeviceLocal = true;
            try
            {
                m_Ring = m_Device->CreateBuffer(ringDesc);
            }
            catch (const std::exception &exception)
            {
                LOG_ERROR("TileUploader: ステージングのリングの作成が例外で失敗した: %s", exception.what());
                m_Ring.reset();
            }
            if (!m_Ring)
            {
                LOG_ERROR("TileUploader: ステージングのリングを作れなかった bytes=%llu",
                          static_cast<unsigned long long>(m_Config.RingBytes));
                return false;
            }
            // 常時写像する（host-coherent なので flush は要らない。区画ごとの Map/Unmap を避ける）
            m_Mapped = m_Ring->Map(0, 0);
            if (m_Mapped == nullptr)
            {
                LOG_ERROR("TileUploader: ステージングのリングを写像できなかった");
                m_Ring.reset();
                return false;
            }
            return true;
        }

        Container::TSharedPtr<RHI::IDevice> m_Device;
        Config m_Config;
        mutable Thread::Mutex m_Mutex;
        RHI::BufferPtr m_Ring;
        void *m_Mapped = nullptr;
        // Enqueue の順（= 記録・提出の順）に並ぶ。先頭から、完了した提出の分だけ手放す。
        Container::Deque<Op> m_Ops;
        uint64_t m_Head = 0;
        uint64_t m_UsedBytes = 0;
        uint64_t m_CompletedSerial = 0;
        uint64_t m_FrameCopiedBytes = 0;
        uint32_t m_FrameCopyCount = 0;
    };
} // namespace NorvesLib::Core::Rendering
