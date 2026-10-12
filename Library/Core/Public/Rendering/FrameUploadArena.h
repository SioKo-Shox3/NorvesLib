#pragma once

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>

namespace NorvesLib::Core::Rendering
{
    /** @brief フレームの持ち主から借りる、CPU が書いて GPU が読むデータの使い道（整列と範囲の上限が決まる） */
    enum class FrameUploadUsage : uint8_t
    {
        /** 定数（uniform buffer の束縛。minUniformBufferOffsetAlignment と maxUniformBufferRange を守る） */
        Constants,
        /** 頂点（SetVertexBuffer のオフセット） */
        Vertex,
        /** インデックス（SetIndexBuffer のオフセット。uint32 の大きさの倍数にそろえる） */
        Index,
        /** ストレージ（storage buffer の束縛。minStorageBufferOffsetAlignment と maxStorageBufferRange を守る） */
        Storage,
    };

    inline const char* GetFrameUploadUsageName(FrameUploadUsage usage)
    {
        switch (usage)
        {
        case FrameUploadUsage::Constants:
            return "Constants";
        case FrameUploadUsage::Vertex:
            return "Vertex";
        case FrameUploadUsage::Index:
            return "Index";
        case FrameUploadUsage::Storage:
            return "Storage";
        }
        return "Unknown";
    }

    /**
     * @brief 使い道ごとの開始オフセットの整列と、1 つの切れ端の大きさの上限
     *
     * デバイスの能力から作る（FromCapabilities）。能力が 0（不明）のときは Vulkan の仕様が許す最も厳しい値を使う。
     */
    struct FrameUploadLimits
    {
        uint64_t ConstantsAlignment = 256;
        uint64_t StorageAlignment = 256;
        uint64_t VertexAlignment = 16;
        uint64_t IndexAlignment = 16;
        /** 定数の切れ端の大きさの上限（記述子の範囲の上限） */
        uint64_t MaxConstantsSize = 16384;
        /** ストレージの切れ端の大きさの上限（記述子の範囲の上限） */
        uint64_t MaxStorageSize = 1ull << 27;

        static FrameUploadLimits FromCapabilities(const RHI::DeviceCapabilities& capabilities)
        {
            FrameUploadLimits limits;
            // 頂点・インデックスは属性の vec4 と uint32 の両方を満たす 16 にそろえる。定数・ストレージも 16 を下限にする
            if (capabilities.MinUniformBufferOffsetAlignment != 0)
            {
                limits.ConstantsAlignment = std::max<uint64_t>(capabilities.MinUniformBufferOffsetAlignment, 16);
            }
            if (capabilities.MinStorageBufferOffsetAlignment != 0)
            {
                limits.StorageAlignment = std::max<uint64_t>(capabilities.MinStorageBufferOffsetAlignment, 16);
            }
            if (capabilities.MaxUniformBufferRange != 0)
            {
                limits.MaxConstantsSize = capabilities.MaxUniformBufferRange;
            }
            if (capabilities.MaxStorageBufferRange != 0)
            {
                limits.MaxStorageSize = capabilities.MaxStorageBufferRange;
            }
            return limits;
        }

        uint64_t GetAlignment(FrameUploadUsage usage) const
        {
            switch (usage)
            {
            case FrameUploadUsage::Constants:
                return ConstantsAlignment;
            case FrameUploadUsage::Vertex:
                return VertexAlignment;
            case FrameUploadUsage::Index:
                return IndexAlignment;
            case FrameUploadUsage::Storage:
                return StorageAlignment;
            }
            return ConstantsAlignment;
        }

        /** @brief 1 つの切れ端の大きさの上限（0 は上限なし） */
        uint64_t GetMaxSize(FrameUploadUsage usage) const
        {
            switch (usage)
            {
            case FrameUploadUsage::Constants:
                return MaxConstantsSize;
            case FrameUploadUsage::Storage:
                return MaxStorageSize;
            case FrameUploadUsage::Vertex:
            case FrameUploadUsage::Index:
                return 0;
            }
            return 0;
        }

        /** @brief 記述子の束縛（uint32 のオフセットと大きさ）に渡す使い道か */
        static bool UsesDescriptorRange(FrameUploadUsage usage)
        {
            return usage == FrameUploadUsage::Constants || usage == FrameUploadUsage::Storage;
        }
    };

    /** @brief 記述子の束縛に渡せるオフセットと大きさの上限（BindConstantBuffer / BindStorageBuffer の引数が uint32） */
    inline constexpr uint64_t FrameUploadMaxDescriptorValue = 0xFFFFFFFFull;

    /** @brief 常にマップしたままの 1 ページ（HOST_VISIBLE | HOST_COHERENT のバッファ） */
    struct FrameUploadPage
    {
        RHI::BufferPtr Buffer;
        uint8_t* CPUAddress = nullptr;
        uint64_t Size = 0;
    };

    /**
     * @brief ページを作る口
     *
     * 本番は DeviceFrameUploadPageSource（RHI のデバイスで CPU から書けるバッファを作り、マップしたままにする）。
     * 検査は GPU を使わない偽物を渡す。RenderThread（または記録中のワーカー。領域の排他の下）から呼ぶ。
     */
    class IFrameUploadPageSource
    {
    public:
        virtual ~IFrameUploadPageSource() = default;

        /** @brief sizeBytes のページを作り、マップした先頭を outPage に入れる。作れなければ false */
        virtual bool CreatePage(uint64_t sizeBytes, FrameUploadPage& outPage) = 0;

        /** @brief 使い道ごとの整列と範囲の上限 */
        virtual FrameUploadLimits GetLimits() const = 0;
    };

    /**
     * @brief RHI のデバイスでページを作る口
     *
     * ページは定数・頂点・インデックス・ストレージのどれにも束縛できる使い道で作り、CPUAccessible
     * （HOST_VISIBLE | HOST_COHERENT。フラッシュは要らない）で 1 回だけマップしたまま持つ。
     * 書き込みは返したポインタへの memcpy で、IBuffer::Update（中でマップし直す）は通さない。
     * アンマップとハンドルの破棄はバッファの最後の参照が手放されたとき（デバイスの破棄の列が提出の完了まで遅らせる）。
     */
    class DeviceFrameUploadPageSource final : public IFrameUploadPageSource
    {
    public:
        void SetDevice(const Container::TSharedPtr<RHI::IDevice>& device)
        {
            m_Device = device;
            m_Limits = device ? FrameUploadLimits::FromCapabilities(device->GetCapabilities()) : FrameUploadLimits{};
        }

        bool HasDevice() const { return static_cast<bool>(m_Device); }

        bool CreatePage(uint64_t sizeBytes, FrameUploadPage& outPage) override
        {
            if (!m_Device || sizeBytes == 0)
            {
                return false;
            }
            RHI::BufferDesc desc;
            desc.Size = sizeBytes;
            desc.Usage = RHI::ResourceUsage::ConstantBuffer | RHI::ResourceUsage::VertexBuffer |
                         RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::StorageBuffer;
            desc.CPUAccessible = true;
            desc.DebugName = "FrameResource.UploadPage";
            try
            {
                RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
                if (!buffer)
                {
                    return false;
                }
                void* const mapped = buffer->Map(0, 0);
                if (!mapped)
                {
                    return false;
                }
                outPage.Buffer = buffer;
                outPage.CPUAddress = static_cast<uint8_t*>(mapped);
                outPage.Size = sizeBytes;
                return true;
            }
            catch (const std::exception& error)
            {
                NORVES_LOG_ERROR("FrameResource", "FRAME_RESOURCE_UPLOAD_PAGE_FAILED bytes=%llu reason=%s",
                                 static_cast<unsigned long long>(sizeBytes), error.what());
                return false;
            }
        }

        FrameUploadLimits GetLimits() const override { return m_Limits; }

    private:
        Container::TSharedPtr<RHI::IDevice> m_Device;
        FrameUploadLimits m_Limits;
    };

    /**
     * @brief フレームの持ち主から借りた、CPU が書いて GPU が読むデータの切れ端
     *
     * Buffer + Offset + Size をそのまま束縛に渡せる（SetVertexBuffer / SetIndexBuffer のオフセット、
     * BindConstantBuffer / BindStorageBuffer のオフセットと大きさ）。CPUAddress へはこのフレームの記録中だけ書いてよい。
     * 借りたフレームの提出の完了（または提出しなかったフレームの次の開始）で戻るので、メンバに持って次のフレームで使わない
     * （使う前に FrameResourceOwner::ValidateBorrowed(Generation, ...) で確かめられる）。
     */
    struct FrameUploadAllocation
    {
        RHI::BufferPtr Buffer;
        uint64_t Offset = 0;
        uint64_t Size = 0;
        void* CPUAddress = nullptr;
        FrameUploadUsage Usage = FrameUploadUsage::Constants;
        /** 借りた持ち主の世代（FrameResourceOwner.h の FrameResourceGeneration と同じ値。SlotIndex と Generation） */
        uint32_t GenerationSlotIndex = ~0u;
        uint64_t Generation = 0;

        bool IsValid() const { return Buffer && CPUAddress && Size != 0; }

        /**
         * @brief 記述子の束縛（uint32 の引数）に渡すオフセット
         *
         * 定数・ストレージの切れ端は、領域がオフセットと大きさを uint32 に収まる位置でだけ返すので切り詰めは起きない。
         * 頂点・インデックスは SetVertexBuffer / SetIndexBuffer の uint64 のオフセットに Offset をそのまま渡す。
         */
        uint32_t GetOffset32() const { return static_cast<uint32_t>(Offset); }
        /** @brief 記述子の束縛（uint32 の引数）に渡す大きさ */
        uint32_t GetSize32() const { return static_cast<uint32_t>(Size); }
    };

    /** @brief 領域の統計（1 フレーム分と、作ったページの合計） */
    struct FrameUploadStats
    {
        /** 借りた数 */
        uint64_t Allocations = 0;
        /** 借りたバイト数（求められた大きさの合計。整列の詰め物は含めない） */
        uint64_t Bytes = 0;
        /** ページを足した回数 */
        uint64_t PagesAdded = 0;
        /** 借りられなかった数（大きさの上限を超えた・ページを作れなかった・記録中でない） */
        uint64_t Failures = 0;
    };

    /**
     * @brief 飛行中のスロット 1 つ分の、CPU から書ける領域（常にマップしたままのページの列）
     *
     * 1 フレームの間は前へ進むだけで、足りなければページを足す。既に返した切れ端は動かさず、上書きもしない
     * （ページは回収まで手放さず、カーソルは戻らない）。回収（Reset）で先頭へ戻り、ページは次のフレームで使い回す。
     * 寿命は持ち主（FrameResourceOwner）の段で決まり、Upload の回数などで古さを判定しない。
     * 並列の記録では区間ごとに別の領域（FrameResourceLane の口）を使うので、ふつうは 1 つの領域を同時に使うのは
     * 1 つのスレッドだけ。それでも Allocate は排他の下で行うので、同時に呼んでも壊れない。Reset・Release は RenderThread だけ。
     */
    class FrameUploadArena
    {
    public:
        /** @brief 既定の最初のページの大きさ */
        static constexpr uint64_t DefaultInitialPageBytes = 1024ull * 1024ull;

        FrameUploadArena() = default;
        FrameUploadArena(const FrameUploadArena&) = delete;
        FrameUploadArena& operator=(const FrameUploadArena&) = delete;

        /**
         * @brief ページを作る口と、ページの大きさを決める（RenderThread。借りる前に 1 回）
         * @param pageSource ページを作る口（nullptr なら借りられない）
         * @param pageBytes 1 ページの大きさ。これより大きい切れ端はその大きさのページを足す
         */
        void Configure(IFrameUploadPageSource* pageSource, uint64_t pageBytes)
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            if (pageSource != m_PageSource || pageBytes != m_PageBytes)
            {
                // 作る口や大きさが替わったら、作り済みのページは次から使わない（手放すのは参照が無くなったとき）
                ReleasePagesLocked();
            }
            m_PageSource = pageSource;
            m_PageBytes = std::max<uint64_t>(pageBytes, 256);
            m_Limits = pageSource ? pageSource->GetLimits() : FrameUploadLimits{};
        }

        bool IsConfigured() const { return m_PageSource != nullptr; }
        IFrameUploadPageSource* GetPageSource() const { return m_PageSource; }
        uint64_t GetPageBytes() const { return m_PageBytes; }
        const FrameUploadLimits& GetLimits() const { return m_Limits; }

        /**
         * @brief size バイトの切れ端を借りる（中身は未初期化）
         * @return 借りた切れ端。借りられなければ IsValid() が false
         */
        FrameUploadAllocation Allocate(uint64_t size, FrameUploadUsage usage)
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            FrameUploadAllocation allocation;
            allocation.Usage = usage;
            if (!m_PageSource || size == 0)
            {
                ++m_FrameStats.Failures;
                return allocation;
            }
            const bool usesDescriptorRange = FrameUploadLimits::UsesDescriptorRange(usage);
            uint64_t maxSize = m_Limits.GetMaxSize(usage);
            if (usesDescriptorRange && (maxSize == 0 || maxSize > FrameUploadMaxDescriptorValue))
            {
                maxSize = FrameUploadMaxDescriptorValue;
            }
            if (maxSize != 0 && size > maxSize)
            {
                ++m_FrameStats.Failures;
                NORVES_LOG_ERROR("FrameResource",
                                 "FRAME_RESOURCE_UPLOAD_TOO_LARGE usage=%s bytes=%llu max=%llu",
                                 GetFrameUploadUsageName(usage), static_cast<unsigned long long>(size),
                                 static_cast<unsigned long long>(maxSize));
                return allocation;
            }
            const uint64_t alignment = std::max<uint64_t>(m_Limits.GetAlignment(usage), 1);

            // 今のページの残りに入らなければ、次のページ（前のフレームで作った物）へ進み、無ければ足す。
            // 戻らないので、既に返した切れ端の範囲には二度と書かない。
            // 定数・ストレージは記述子の束縛に渡すオフセットが uint32 に収まらない位置（4 GiB を超える大きなページの奥）を飛ばす
            while (m_CurrentPage < m_Pages.size())
            {
                const FrameUploadPage& page = m_Pages[m_CurrentPage];
                const uint64_t offset = AlignUp(m_Cursor, alignment);
                const bool offsetBindable = !usesDescriptorRange || offset <= FrameUploadMaxDescriptorValue;
                if (offsetBindable && offset <= page.Size && size <= page.Size - offset)
                {
                    return TakeLocked(page, offset, size, usage);
                }
                ++m_CurrentPage;
                m_Cursor = 0;
            }

            FrameUploadPage page;
            // ページの大きさは揃え、収まらない大きな切れ端だけその大きさ（64 KiB 単位）のページにする
            const uint64_t pageBytes = size <= m_PageBytes ? m_PageBytes : AlignUp(size, 64ull * 1024ull);
            if (!m_PageSource->CreatePage(pageBytes, page) || !page.Buffer || !page.CPUAddress || page.Size < size)
            {
                ++m_FrameStats.Failures;
                NORVES_LOG_ERROR("FrameResource", "FRAME_RESOURCE_UPLOAD_PAGE_FAILED usage=%s bytes=%llu page=%llu",
                                 GetFrameUploadUsageName(usage), static_cast<unsigned long long>(size),
                                 static_cast<unsigned long long>(pageBytes));
                return allocation;
            }
            m_Pages.push_back(page);
            m_TotalPageBytes += page.Size;
            ++m_FrameStats.PagesAdded;
            ++m_TotalPagesAdded;
            m_CurrentPage = m_Pages.size() - 1;
            m_Cursor = 0;
            return TakeLocked(m_Pages[m_CurrentPage], 0, size, usage);
        }

        /** @brief 今のフレームの統計（RenderThread） */
        FrameUploadStats GetFrameStats() const
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            return m_FrameStats;
        }

        /** @brief 作ったページの数（RenderThread） */
        size_t GetPageCount() const
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            return m_Pages.size();
        }

        /** @brief 作ったページの合計バイト数 */
        uint64_t GetTotalPageBytes() const
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            return m_TotalPageBytes;
        }

        /** @brief これまでにページを足した回数 */
        uint64_t GetTotalPagesAdded() const
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            return m_TotalPagesAdded;
        }

        /**
         * @brief 回収。GPU がこのスロットの切れ端を使い終えた後に呼び、先頭へ戻す（ページは使い回す。RenderThread）
         * @return 戻したフレームの統計
         */
        FrameUploadStats Reset()
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            const FrameUploadStats stats = m_FrameStats;
            m_FrameStats = {};
            m_CurrentPage = 0;
            m_Cursor = 0;
            return stats;
        }

        /** @brief ページを全部手放す（GPU を待ち終えた後の終了処理。RenderThread） */
        void Release()
        {
            NorvesLib::Thread::ScopedLock lock(m_Mutex);
            ReleasePagesLocked();
        }

    private:
        static uint64_t AlignUp(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        FrameUploadAllocation TakeLocked(const FrameUploadPage& page, uint64_t offset, uint64_t size,
                                         FrameUploadUsage usage)
        {
            FrameUploadAllocation allocation;
            allocation.Buffer = page.Buffer;
            allocation.Offset = offset;
            allocation.Size = size;
            allocation.CPUAddress = page.CPUAddress + offset;
            allocation.Usage = usage;
            m_Cursor = offset + size;
            ++m_FrameStats.Allocations;
            m_FrameStats.Bytes += size;
            return allocation;
        }

        void ReleasePagesLocked()
        {
            m_Pages.clear();
            m_TotalPageBytes = 0;
            m_CurrentPage = 0;
            m_Cursor = 0;
        }

        mutable NorvesLib::Thread::Mutex m_Mutex;
        IFrameUploadPageSource* m_PageSource = nullptr;
        uint64_t m_PageBytes = DefaultInitialPageBytes;
        FrameUploadLimits m_Limits;
        Container::VariableArray<FrameUploadPage> m_Pages;
        size_t m_CurrentPage = 0;
        uint64_t m_Cursor = 0;
        uint64_t m_TotalPageBytes = 0;
        uint64_t m_TotalPagesAdded = 0;
        FrameUploadStats m_FrameStats;
    };
} // namespace NorvesLib::Core::Rendering
