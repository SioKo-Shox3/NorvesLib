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
        StructuredBuffer
    };

    /**
     * @brief デスクリプターセットのバインディング定義
     */
    struct DescriptorBinding
    {
        uint32_t binding = 0;
        ResourceBindType type = ResourceBindType::ConstantBuffer;
        ShaderStage stages = ShaderStage::All;
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
