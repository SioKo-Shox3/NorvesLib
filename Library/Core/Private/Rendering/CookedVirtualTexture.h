#pragma once

// クック済みの NVTEX v0.2 の材質のテクスチャを VT（sparse）として扱う本番の部品。
// - CookedVirtualTextureSource: タイル 1 枚をジョブシステムの範囲読みで読む（VirtualTextureStreamer の読み込みの窓口）
// - DeviceVirtualTextureGpu: 結び付け（IDevice::BindSparse）とコピー（TileUploader）の窓口
// - PrepareCookedVirtualTexture: ファイルのメタデータとミップテイルだけを読み、作成情報と読み込みの窓口を用意する

#include "Asset/AssetFileReader.h"
#include "Asset/AssetReadRequest.h"
#include "Asset/CookedTextureFormat.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/CookedTextureUpload.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/TileUploader.h"
#include "Rendering/VirtualTextureStreamer.h"
#include "RHI/IDevice.h"
#include "Thread/JobSystem.h"
#include "Thread/Mutex.h"
#include "Thread/Task.h"

#include <cstdint>
#include <exception>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief クック済みのテクスチャのタイルを、ジョブシステムの範囲読みで読む窓口（テクスチャ 1 枚に 1 つ）
     *
     * BeginRead はジョブを積むだけで待たない。ジョブは窓口より長く生きてもよい（結果の置き場は共有）。
     */
    class CookedVirtualTextureSource final : public IVirtualTextureTileSource
    {
    public:
        /**
         * @param reader パッケージ（または単体の .nvtex）を読むファイル読み込み
         * @param request ファイルを指す読み込みの要求
         * @param baseOffset .nvtex の先頭のファイル内の位置
         * @param layout ReadCookedTextureLayout の結果（v0.2 のタイルの表を持つこと）
         */
        CookedVirtualTextureSource(Asset::AssetFileReader reader,
                                   Asset::AssetReadRequest request,
                                   uint64_t baseOffset,
                                   Asset::CookedTextureData layout);

        bool BeginRead(const VirtualTextureTileKey &key) override;
        void CollectCompleted(Container::VariableArray<VirtualTextureTileReadResult> &out) override;

    private:
        struct Shared;
        Container::TSharedPtr<Shared> m_Shared;
    };

    /**
     * @brief 結び付けとコピーの本番の窓口。BindSparse はデバイスへ、コピーは TileUploader のリングへ送る。
     */
    class DeviceVirtualTextureGpu final : public IVirtualTextureGpu
    {
    public:
        DeviceVirtualTextureGpu(Container::TSharedPtr<RHI::IDevice> device, TileUploader &uploader)
            : m_Device(std::move(device)), m_Uploader(uploader)
        {
        }

        bool BindSparse(const RHI::SparseBindRequest &request) override
        {
            return m_Device != nullptr && m_Device->BindSparse(request);
        }

        bool EnqueueInitialize(const RHI::TexturePtr &texture) override { return m_Uploader.EnqueueInitialize(texture); }

        bool EnqueueTile(const RHI::TexturePtr &texture,
                         const RHI::TextureRegionCopy &region,
                         const void *data,
                         uint64_t bytes) override
        {
            return m_Uploader.EnqueueTile(texture, region, data, bytes);
        }

        uint64_t GetCopyBytesAvailable() const override { return m_Uploader.GetRecordableCopyBytes(); }

        void GetPendingCopies(uint32_t &outCount, uint64_t &outBytes) const override
        {
            m_Uploader.GetPendingCopyLoad(outCount, outBytes);
        }

        void DiscardEnqueued(uint32_t count) override { m_Uploader.DiscardLastEnqueued(count); }

        void AbandonTexture(const RHI::TexturePtr &texture) override { m_Uploader.AbandonTexture(texture); }

        void AbandonRegion(const RHI::TexturePtr &texture, const RHI::TextureRegionCopy &region) override
        {
            m_Uploader.AbandonRegion(texture, region);
        }

    private:
        Container::TSharedPtr<RHI::IDevice> m_Device;
        TileUploader &m_Uploader;
    };

    /** @brief クック済みのテクスチャを VT として作るための下ごしらえ */
    struct CookedVirtualTexturePlan
    {
        /** @brief sparse のテクスチャを作る情報（bSparse が立っている） */
        TextureCreateInfo CreateInfo;
        Asset::CookedTexturePixelFormat Format = Asset::CookedTexturePixelFormat::R8UNorm;
        uint32_t Width = 0;
        uint32_t Height = 0;
        /** @brief ミップテイル（FirstTailMip 以降の段を行優先で詰めたもの） */
        Container::VariableArray<uint8_t> TailData;
        Container::TSharedPtr<IVirtualTextureTileSource> Source;
    };

    /**
     * @brief ファイルのメタデータとミップテイルだけを範囲読みして、VT の作成の下ごしらえをする
     * @param reader クック済みのパッケージを読むファイル読み込み
     * @param request ファイルを指す読み込みの要求
     * @param baseOffset .nvtex の先頭のファイル内の位置
     * @param nvtexSize .nvtex のバイト数
     * @param pOutReason 失敗の理由（ログ用。null でもよい）
     * @param bTreatSrgbAsLinear AssetSystem が sRGB を UNORM として上げる互換設定のとき true。クック済みの sRGB の
     *        色空間を Linear に読み替え（BC7 sRGB は BC7 UNORM）、全常駐で読むテクスチャと同じ標本値にする
     * @return 作れたら true。v0.2 でない・レイヤーが複数・形式に対応しないときは false
     */
    [[nodiscard]] bool PrepareCookedVirtualTexture(const Asset::AssetFileReader &reader,
                                                   const Asset::AssetReadRequest &request,
                                                   uint64_t baseOffset,
                                                   uint64_t nvtexSize,
                                                   const Container::String &debugName,
                                                   CookedVirtualTexturePlan &outPlan,
                                                   Container::String *pOutReason = nullptr,
                                                   bool bTreatSrgbAsLinear = false);

    struct CookedVirtualTextureSource::Shared
    {
        Asset::AssetFileReader Reader;
        Asset::AssetReadRequest Request;
        uint64_t BaseOffset = 0;
        Asset::CookedTextureData Layout;

        Thread::Mutex Mutex;
        Container::VariableArray<VirtualTextureTileReadResult> Completed;
    };

    inline CookedVirtualTextureSource::CookedVirtualTextureSource(Asset::AssetFileReader reader,
                                                           Asset::AssetReadRequest request,
                                                           uint64_t baseOffset,
                                                           Asset::CookedTextureData layout)
        : m_Shared(Container::MakeShared<Shared>())
    {
        m_Shared->Reader = std::move(reader);
        m_Shared->Request = std::move(request);
        m_Shared->BaseOffset = baseOffset;
        m_Shared->Layout = std::move(layout);
    }

    inline bool CookedVirtualTextureSource::BeginRead(const VirtualTextureTileKey &key)
    {
        Container::TSharedPtr<Shared> shared = m_Shared;
        auto readTile = [shared, key]()
        {
            VirtualTextureTileReadResult result;
            result.Key = key;
            try
            {
                const Asset::AssetReadResult read = Asset::ReadCookedTextureTile(
                    shared->Reader, shared->Request, shared->BaseOffset, shared->Layout, key.Mip, 0, key.X, key.Y);
                if (read.Succeeded())
                {
                    const auto bytes = read.Blob.GetSpan();
                    result.Data.assign(bytes.data(), bytes.data() + bytes.size());
                    result.bSucceeded = true;
                }
            }
            catch (const std::exception &)
            {
                result.bSucceeded = false;
                result.Data.clear();
            }

            Thread::ScopedLock lock(shared->Mutex);
            shared->Completed.push_back(std::move(result));
        };

        // ジョブは窓口より長く生きてもよいので、結果の置き場（Shared）を共有して持たせる
        Thread::TaskPtr task = Thread::Task::Create(readTile, Thread::TaskPriority::NORMAL);
        return Thread::JobSystem::Get().SubmitTask(task);
    }

    inline void CookedVirtualTextureSource::CollectCompleted(Container::VariableArray<VirtualTextureTileReadResult> &out)
    {
        Thread::ScopedLock lock(m_Shared->Mutex);
        for (VirtualTextureTileReadResult &result : m_Shared->Completed)
        {
            out.push_back(std::move(result));
        }
        m_Shared->Completed.clear();
    }

    inline bool PrepareCookedVirtualTexture(const Asset::AssetFileReader &reader,
                                     const Asset::AssetReadRequest &request,
                                     uint64_t baseOffset,
                                     uint64_t nvtexSize,
                                     const Container::String &debugName,
                                     CookedVirtualTexturePlan &outPlan,
                                     Container::String *pOutReason,
                                     bool bTreatSrgbAsLinear)
    {
        auto fail = [pOutReason](const char *reason)
        {
            if (pOutReason != nullptr)
            {
                *pOutReason = Container::String(reason);
            }
            return false;
        };

        Asset::CookedTextureParseResult layout = Asset::ReadCookedTextureLayout(reader, request, baseOffset, nvtexSize);
        if (!layout.Succeeded())
        {
            return fail("クック済みテクスチャのレイアウトを読めなかった");
        }
        Asset::CookedTextureData &texture = layout.Texture;
        if (!texture.bTiled)
        {
            return fail("クック済みテクスチャが NVTEX v0.2（タイル分割）ではない");
        }
        if (texture.LayerCount != 1)
        {
            return fail("VT は単一レイヤーのテクスチャにだけ対応する");
        }
        if (bTreatSrgbAsLinear && texture.ColorSpace == Asset::CookedTextureColorSpace::SRGB)
        {
            texture.ColorSpace = Asset::CookedTextureColorSpace::Linear;
        }

        TextureCreateInfo createInfo;
        if (BuildCookedTextureCreateInfo(texture, debugName, createInfo, false) != CookedTextureUploadStatus::Success)
        {
            return fail("クック済みテクスチャの形式に対応していない");
        }
        createInfo.bSparse = true;

        // ミップテイルは小さいので、作成時に読んでしまう（タイルより小さい段が無いテクスチャは空）
        Container::VariableArray<uint8_t> tailData;
        if (texture.Tiling.TailSize > 0)
        {
            const Asset::AssetReadResult tail =
                Asset::ReadCookedTextureMipTail(reader, request, baseOffset, texture);
            if (!tail.Succeeded() || tail.Blob.GetSize() != texture.Tiling.TailSize)
            {
                return fail("クック済みテクスチャのミップテイルを読めなかった");
            }
            const auto bytes = tail.Blob.GetSpan();
            tailData.assign(bytes.data(), bytes.data() + bytes.size());
        }

        outPlan = CookedVirtualTexturePlan();
        outPlan.CreateInfo = std::move(createInfo);
        outPlan.Format = texture.PixelFormat;
        outPlan.Width = texture.Width;
        outPlan.Height = texture.Height;
        outPlan.TailData = std::move(tailData);
        outPlan.Source = Container::MakeShared<CookedVirtualTextureSource>(reader, request, baseOffset, std::move(texture));
        return true;
    }
} // namespace NorvesLib::Core::Rendering
