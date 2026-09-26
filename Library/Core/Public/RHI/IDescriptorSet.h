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
    };

} // namespace NorvesLib::RHI
