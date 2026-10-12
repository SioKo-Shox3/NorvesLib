#pragma once

#include "RHITypes.h"
#include "Container/Containers.h"

namespace NorvesLib::RHI
{

    /**
     * @brief リソースバインドの種類
     */
    enum class ResourceBindType
    {
        ConstantBuffer,
        Texture,
        Sampler,
        CombinedImageSampler, ///< テクスチャ+サンプラーの結合バインド（uniform sampler2D用）
        RWTexture,
        RWBuffer,
        StructuredBuffer,
        AccelerationStructure
    };

    /**
     * @brief デスクリプターセットのバインディング定義
     */
    struct DescriptorBinding
    {
        uint32_t binding = 0;
        ResourceBindType type = ResourceBindType::ConstantBuffer;
        ShaderStage stages = ShaderStage::All;
        /** @brief 配列要素数。1は非配列binding。2以上はCombinedImageSamplerだけが対応する。 */
        uint32_t count = 1;
    };

    /**
     * @brief ディスクリプタセット作成情報
     */
    struct DescriptorSetDesc
    {
        NorvesLib::Core::Container::VariableArray<DescriptorBinding> bindings;
    };

    /**
     * @brief ディスクリプタセットインターフェース
     * ディスクリプタセットはシェーダーリソースのバインディングを管理するためのオブジェクトです。
     */
    class IDescriptorSet
    {
    public:
        virtual ~IDescriptorSet() = default;

        /**
         * @brief 定数バッファをバインドする
         * @param binding バインディングポイント
         * @param buffer バインドするバッファ
         * @param offset バインドするバッファのオフセット
         * @param size バインドするバッファのサイズ
         */
        virtual void BindConstantBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size) = 0;

        /**
         * @brief テクスチャをバインドする
         * @param binding バインディングポイント
         * @param texture バインドするテクスチャ
         */
        virtual void BindTexture(uint32_t binding, TexturePtr texture) = 0;

        /**
         * @brief サンプラーをバインドする
         * @param binding バインディングポイント
         * @param sampler バインドするサンプラー
         */
        virtual void BindSampler(uint32_t binding, SamplerPtr sampler) = 0;

        /**
         * @brief ストレージバッファ（RWBuffer）をバインドする
         * @param binding バインディングポイント
         * @param buffer バインドするバッファ
         * @param offset バインドするバッファのオフセット
         * @param size バインドするバッファのサイズ
         */
        virtual void BindStorageBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size) = 0;

        /**
         * @brief 加速構造をバインドする
         * @param binding バインディングポイント
         * @param accelerationStructure バインドする加速構造
         * @return 対応する加速構造descriptorへのbindingが成立した場合はtrue
         */
        virtual bool BindAccelerationStructure(uint32_t binding,
                                               AccelerationStructurePtr accelerationStructure)
        {
            (void)binding;
            (void)accelerationStructure;
            return false;
        }

        /**
         * @brief 配列CombinedImageSamplerの1要素へテクスチャとサンプラーをバインドする
         *
         * 配列要素は動的添字で参照されうるため、呼び出し側はUpdate前に全要素を埋める。
         * @param binding バインディングポイント（count>=2のCombinedImageSampler）
         * @param arrayElement 配列内の要素番号
         * @param texture バインドするテクスチャ
         * @param sampler バインドするサンプラー
         * @return 配列bindingの範囲内で受理した場合はtrue
         */
        virtual bool BindTextureArrayElement(uint32_t binding,
                                             uint32_t arrayElement,
                                             TexturePtr texture,
                                             SamplerPtr sampler)
        {
            (void)binding;
            (void)arrayElement;
            (void)texture;
            (void)sampler;
            return false;
        }

        /**
         * @brief ストレージテクスチャ（RWTexture）をバインドする
         * @param binding バインディングポイント
         * @param texture バインドするテクスチャ
         */
        virtual void BindStorageTexture(uint32_t binding, TexturePtr texture) = 0;

        /**
         * @brief ストレージテクスチャの特定ミップレベルをバインドする
         * @param binding バインディングポイント
         * @param texture バインドするテクスチャ
         * @param mipLevel バインドするミップレベル
         */
        virtual void BindStorageTexture(uint32_t binding, TexturePtr texture, uint32_t mipLevel) = 0;

        /**
         * @brief ディスクリプタセットをアップデートする
         * バインド設定をGPUに反映する
         */
        virtual void Update() = 0;

        /**
         * @brief フレームごとの pool から借りたセットで、その pool が回収された後か
         *
         * 回収された後のセットは書き込みも束縛もできない（Update と SetDescriptorSet は拒む）。
         * CreateDescriptorSet で作った専用のセットは常に false。
         */
        virtual bool IsExpired() const { return false; }
    };

    /**
     * @brief 共有のディスクリプタセットレイアウト
     *
     * IDevice::GetOrCreateDescriptorSetLayout が返す。同じ束縛の形（binding・型・ステージ・要素数の並び）には
     * デバイスの中で 1 つだけ作り、デバイスが生きている間は使い回す。フレームごとの pool から借りるときに渡す。
     */
    class IDescriptorSetLayout
    {
    public:
        virtual ~IDescriptorSetLayout() = default;

        /** @brief 作ったときの束縛の形 */
        virtual const DescriptorSetDesc& GetDesc() const = 0;
    };

    using DescriptorSetLayoutPtr = TSharedPtr<IDescriptorSetLayout>;

    /**
     * @brief フレームごとの記述子の pool 1 つの容量（型ごとの記述子の数と、セットの数）
     *
     * 足りなくなったら同じ容量の pool を足す。1 つのレイアウトがこの容量を超える型を持つときは、
     * そのレイアウトが入る大きさの pool を足す。
     */
    struct FrameDescriptorPoolCapacity
    {
        uint32_t MaxSets = 256;
        uint32_t UniformBuffers = 512;
        uint32_t SampledImages = 512;
        uint32_t Samplers = 256;
        uint32_t StorageBuffers = 512;
        uint32_t StorageImages = 256;
        uint32_t CombinedImageSamplers = 1024;
        uint32_t AccelerationStructures = 32;
    };

    /** @brief フレームごとの記述子の pool の統計（前の回収から今までの分） */
    struct FrameDescriptorStats
    {
        /** 借りたセットの数 */
        uint64_t Allocations = 0;
        /** 借りたセットへ書き込んだ記述子の数（Update が書いた束縛・配列の要素の数） */
        uint64_t Writes = 0;
        /** pool を足した回数 */
        uint64_t PoolsAdded = 0;
        /** 借りられなかった回数（レイアウトが無効・pool を作れない） */
        uint64_t Failures = 0;
        /** 回収された後のセットへの書き込みを拒んだ回数 */
        uint64_t ExpiredRejects = 0;
        /** 借りにかかった CPU の時間（ナノ秒。開発ビルドだけ測る） */
        uint64_t AllocateNanoseconds = 0;
        /** 書き込み（Update）にかかった CPU の時間（ナノ秒。開発ビルドだけ測る） */
        uint64_t WriteNanoseconds = 0;
    };

    /**
     * @brief フレームごとにまとめた記述子の pool（飛行中のスロット 1 つ分）
     *
     * Allocate は共有のレイアウトからセットを 1 つ借りる（レイアウト・pool・パイプラインレイアウトは作らない）。
     * pool が足りなければ pool を足す。Reset でこのフレームに借りたセットを全部まとめて戻す（pool は使い回す）。
     * Reset の後、前に借りたセットは IsExpired() が true になり、書き込みも束縛もできない。
     * Reset は GPU がこのフレームのセットを使い終えた後（フレームの持ち主の回収）にだけ呼ぶ。
     * Allocate と借りたセットの Update は排他の下で行うので、並列の記録のワーカーから呼んでよい。
     */
    class IFrameDescriptorAllocator
    {
    public:
        virtual ~IFrameDescriptorAllocator() = default;

        /** @brief layout のセットを 1 つ借りる。借りられなければ nullptr */
        virtual DescriptorSetPtr Allocate(const DescriptorSetLayoutPtr& layout) = 0;

        /** @brief 借りたセットを全部戻す（pool は手放さずに使い回す） */
        virtual void Reset() = 0;

        /** @brief pool を全部手放す（デバイスを手放す前に呼ぶ。借りたセットも使えなくなる） */
        virtual void Release() = 0;

        /** @brief 前の Reset から今までの統計 */
        virtual FrameDescriptorStats GetFrameStats() const = 0;

        /** @brief 持っている pool の数 */
        virtual uint32_t GetPoolCount() const = 0;

        /** @brief 作ってから pool を足した回数の合計 */
        virtual uint64_t GetTotalPoolsAdded() const = 0;
    };

    using FrameDescriptorAllocatorPtr = TSharedPtr<IFrameDescriptorAllocator>;

} // namespace NorvesLib::RHI
