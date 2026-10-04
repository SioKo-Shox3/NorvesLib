#pragma once

#include "RHITypes.h"

namespace NorvesLib::RHI 
{

/**
 * @brief sparse（部分常駐）テクスチャのタイルとミップテイルの情報
 *
 * 物理メモリを結ばずに作った 2D テクスチャについて、結び付け単位（タイル）の大きさ、
 * ミップごとのタイルの数、ミップテイルの位置を返す（vkGetImageSparseMemoryRequirements 相当）。
 * ミップテイルより前のミップはタイル単位で結び、ミップテイル以降は全体を1つの塊として結ぶ。
 */
struct SparseTextureInfo
{
    /** @brief 持てるミップ数の上限（16384x16384 の全ミップが 15 段） */
    static constexpr uint32_t MaxMipLevels = 16;

    /** @brief タイルの幅（texel） */
    uint32_t TileWidth = 0;

    /** @brief タイルの高さ（texel） */
    uint32_t TileHeight = 0;

    /** @brief 1タイルのバイト数（標準のブロック形状では 64 KiB） */
    uint32_t TileSizeBytes = 0;

    /** @brief ミップ数 */
    uint32_t MipLevels = 0;

    /** @brief ミップテイルが始まるミップ（MipLevels と等しければミップテイルは無い） */
    uint32_t MipTailFirstLevel = 0;

    /** @brief ミップテイルの大きさ（バイト。結び付け単位は 64 KiB の倍数） */
    uint64_t MipTailSize = 0;

    /** @brief ミップテイルの、イメージのメモリ上のオフセット（バイト） */
    uint64_t MipTailOffset = 0;

    /** @brief 配列レイヤー間のミップテイルの間隔（バイト。配列 1 枚では使わない） */
    uint64_t MipTailStride = 0;

    /** @brief ミップごとのタイルの数（x）。ミップテイル以降は 0 */
    uint32_t TilesX[MaxMipLevels] = {};

    /** @brief ミップごとのタイルの数（y）。ミップテイル以降は 0 */
    uint32_t TilesY[MaxMipLevels] = {};
};

/** @brief sparse の結び付け単位（ページ）の大きさ。標準のブロック形状のタイルは 1 ページ */
constexpr uint64_t SparsePageSizeBytes = 64 * 1024;

/**
 * @brief sparse テクスチャへ結ぶ物理メモリの塊（DeviceLocal）
 *
 * SparsePageSizeBytes のページに分けて使い、ページ単位でタイルやミップテイルへ結ぶ。
 * 結んだテクスチャを GPU が使い終わるまで破棄してはならない（ページの回収は GpuRetireQueue が担う）。
 */
class ISparseMemoryBlock
{
public:
    virtual ~ISparseMemoryBlock() = default;

    /** @brief 塊の大きさ（バイト。SparsePageSizeBytes の倍数） */
    virtual uint64_t GetSizeBytes() const = 0;
};

using SparseMemoryBlockPtr = TSharedPtr<ISparseMemoryBlock>;

/** @brief 塊の中の 1 ページ。Block が null なら「結ばない（外す）」を表す */
struct SparsePageRef
{
    ISparseMemoryBlock* Block = nullptr;

    /** @brief 塊の先頭からのオフセット（バイト。SparsePageSizeBytes の倍数） */
    uint64_t OffsetBytes = 0;

    bool IsValid() const { return Block != nullptr; }
};

/** @brief タイル（ミップ・x・y）1つへの結び付け。Page が無効なら外す */
struct SparseTileBind
{
    /** @brief 対象の sparse テクスチャ（要求を出している間と、結び付けの提出が済むまで生かしておく） */
    ITexture* Texture = nullptr;
    uint32_t MipLevel = 0;
    uint32_t TileX = 0;
    uint32_t TileY = 0;
    SparsePageRef Page;
};

/** @brief ミップテイルの 1 ページ分への結び付け。Page が無効なら外す */
struct SparseMipTailBind
{
    ITexture* Texture = nullptr;

    /** @brief ミップテイルの先頭から数えたページの番号（ミップテイルの大きさ / SparsePageSizeBytes 未満） */
    uint32_t PageIndex = 0;
    SparsePageRef Page;
};

/**
 * @brief 1回の vkQueueBindSparse にまとめる結び付け・外しの集合（1フレーム分）
 *
 * 同じタイル・ページを1つの要求の中で二度指定しない。
 */
struct SparseBindRequest
{
    Core::Container::VariableArray<SparseTileBind> Tiles;
    Core::Container::VariableArray<SparseMipTailBind> MipTails;

    bool IsEmpty() const { return Tiles.empty() && MipTails.empty(); }
};

/**
 * @brief テクスチャインターフェース
 * テクスチャは画像データを格納するリソースです。
 * レンダーターゲット、デプスステンシルバッファ、サンプリングテクスチャなどに使用されます。
 */
class ITexture 
{
public:
    virtual ~ITexture() = default;

    /**
     * @brief テクスチャの幅を取得
     * @return テクスチャの幅（ピクセル）
     */
    virtual uint32_t GetWidth() const = 0;

    /**
     * @brief テクスチャの高さを取得
     * @return テクスチャの高さ（ピクセル）
     */
    virtual uint32_t GetHeight() const = 0;

    /**
     * @brief テクスチャの深さを取得
     * @return テクスチャの深さ（3Dテクスチャの場合）
     */
    virtual uint32_t GetDepth() const = 0;

    /**
     * @brief テクスチャのミップレベル数を取得
     * @return ミップレベル数
     */
    virtual uint32_t GetMipLevels() const = 0;

    /**
     * @brief テクスチャの配列サイズを取得
     * @return 配列サイズ
     */
    virtual uint32_t GetArraySize() const = 0;

    /**
     * @brief テクスチャのフォーマットを取得
     * @return テクスチャのフォーマット
     */
    virtual Format GetFormat() const = 0;

    /**
     * @brief テクスチャの使用用途を取得
     * @return テクスチャの使用用途
     */
    virtual ResourceUsage GetUsage() const = 0;

    /**
     * @brief キューブマップかどうかを取得
     * @return キューブマップの場合true
     */
    virtual bool IsCubemap() const = 0;

    /**
     * @brief テクスチャデータを更新
     * @param data 更新するデータへのポインタ
     * @param rowPitch 1行あたりのバイト数
     * @param slicePitch 1スライスあたりのバイト数
     * @param mipLevel 更新するミップレベル
     * @param arrayIndex 更新する配列インデックス
     */
    virtual void Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, uint32_t mipLevel = 0, uint32_t arrayIndex = 0) = 0;

    /**
     * @brief 特定ミップレベル用のImageViewハンドルを取得
     *
     * コンピュートシェーダーで個々のミップレベルにimageStoreする場合に使用。
     * @param mipLevel 対象のミップレベル
     * @return プラットフォーム固有のハンドル（未サポートの場合は0）
     */
    virtual uint64_t GetMipImageViewHandle(uint32_t mipLevel) const { (void)mipLevel; return 0; }

    /**
     * @brief sparse（部分常駐）テクスチャか
     *
     * sparse のテクスチャは作成時に物理メモリを結ばない。結んだタイルだけがサンプルできる。
     * Update は使えない（タイル単位の書き込みで中身を作る）。
     */
    virtual bool IsSparse() const { return false; }

    /**
     * @brief sparse テクスチャのタイルとミップテイルの情報を取得
     * @param outInfo 結果の書き込み先
     * @return sparse テクスチャなら true。そうでなければ false（outInfo は変えない）
     */
    virtual bool GetSparseInfo(SparseTextureInfo& outInfo) const { (void)outInfo; return false; }

    /**
     * @brief sparse テクスチャに現在結んでいる物理メモリの量（バイト）
     *
     * VRAM の台帳は sparse のテクスチャをこの量で数える。sparse でなければ 0。
     */
    virtual uint64_t GetSparseBoundBytes() const { return 0; }
};

} // namespace NorvesLib::RHI
