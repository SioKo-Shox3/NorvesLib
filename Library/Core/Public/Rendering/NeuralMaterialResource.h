#pragma once

#include "RHI/RHITypes.h"
#include "RenderTypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class RenderResourceManager;

    /**
     * @brief ニューラルマテリアルの出力スロット定義
     *
     * MLPの出力チャンネルをどのテクスチャスロットにマッピングするかを定義します。
     * 外部から構成を変更可能にするため、コンフィグとして分離しています。
     */
    struct NeuralMaterialOutputSlot
    {
        Container::String Name;                                  ///< スロット名（"Albedo", "Normal", "ARM" 等）
        RHI::Format TextureFormat = RHI::Format::R8G8B8A8_UNORM; ///< 出力テクスチャフォーマット
        uint32_t Channels = 4;                                   ///< このスロットが使うMLPの出力チャンネル数
    };

    /**
     * @brief ニューラルマテリアルのネットワーク＋出力記述
     *
     * MLPの層構成と出力スロットの構成を定義します。
     * OutputSlotsを外部から設定することで、出力テクスチャの構成を柔軟に変更できます。
     */
    struct NeuralMaterialDesc
    {
        // MLP構成
        uint32_t InputChannels = 2; ///< 入力チャネル数（通常UV = 2）
        uint32_t HiddenLayers = 4;  ///< 隠れ層の数
        uint32_t HiddenWidth = 64;  ///< 隠れ層のニューロン数

        // 出力解像度
        uint32_t OutputWidth = 0;  ///< 出力テクスチャ幅
        uint32_t OutputHeight = 0; ///< 出力テクスチャ高さ

        // 出力スロット構成（外部設定可能）
        Container::VariableArray<NeuralMaterialOutputSlot> OutputSlots;

        Container::String DebugName; ///< デバッグ名

        /**
         * @brief MLPの総出力チャンネル数を計算
         * @return 全スロットのChannelsの合計
         */
        uint32_t GetTotalOutputChannels() const
        {
            uint32_t total = 0;
            for (const auto &slot : OutputSlots)
            {
                total += slot.Channels;
            }
            return total;
        }

        /**
         * @brief デフォルトPBR構成を生成
         *
         * Albedo(RGBA8, 4ch) + Normal(RG16F, 2ch) + ARM(RGBA8, 3ch: AO/Roughness/Metallic)
         *
         * @param width 出力テクスチャ幅
         * @param height 出力テクスチャ高さ
         * @return デフォルトPBR構成のNeuralMaterialDesc
         */
        static NeuralMaterialDesc DefaultPBR(uint32_t width, uint32_t height)
        {
            NeuralMaterialDesc desc;
            desc.OutputWidth = width;
            desc.OutputHeight = height;

            // Albedo: RGBA8 (RGB=color, A=opacity)
            NeuralMaterialOutputSlot albedoSlot;
            albedoSlot.Name = "Albedo";
            albedoSlot.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            albedoSlot.Channels = 4;
            desc.OutputSlots.push_back(albedoSlot);

            // Normal: RG16F (XY, Zは再構築)
            NeuralMaterialOutputSlot normalSlot;
            normalSlot.Name = "Normal";
            normalSlot.TextureFormat = RHI::Format::R16G16_FLOAT;
            normalSlot.Channels = 2;
            desc.OutputSlots.push_back(normalSlot);

            // ARM: RGBA8 (R=AO, G=Roughness, B=Metallic, A=reserved)
            NeuralMaterialOutputSlot armSlot;
            armSlot.Name = "ARM";
            armSlot.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            armSlot.Channels = 3;
            desc.OutputSlots.push_back(armSlot);

            return desc;
        }
    };

    /**
     * @brief ニューラルマテリアルリソース
     *
     * Cooperative Vector (VK_NV_cooperative_vector) を使用した
     * ニューラルネットワークベースのマテリアルデコードに必要な
     * GPU側リソース（重みバッファ、出力テクスチャ群）を管理します。
     *
     * 1つのMLPで複数のPBRプロパティ（Albedo, Normal, ARM等）を一括デコードし、
     * 各出力テクスチャに分配書き込みします。
     *
     * 出力テクスチャはRenderResourceManagerにTextureHandleとして登録され、
     * GBufferPassが通常のテクスチャと同じ経路で参照できます。
     *
     * 責務:
     * - MLPの重み/バイアスデータをStorageBufferとして保持
     * - 出力テクスチャ群（スロット別）を生成・管理
     * - 出力テクスチャのTextureHandle登録
     * - リソースの生成・破棄のライフサイクル管理
     */
    class NeuralMaterialResource
    {
    public:
        NeuralMaterialResource() = default;
        ~NeuralMaterialResource();

        /**
         * @brief リソースを初期化
         * @param device RHIデバイス
         * @param desc ネットワーク＋出力記述
         * @return 成功時true
         */
        bool Initialize(RHI::IDevice *device, const NeuralMaterialDesc &desc);

        /**
         * @brief 出力テクスチャをRenderResourceManagerにTextureHandleとして登録
         * @param resourceManager テクスチャハンドル登録先
         * @return 成功時true
         */
        bool RegisterOutputTextures(RenderResourceManager &resourceManager);

        /**
         * @brief 重みデータをアップロード
         * @param weightData 重みデータ（バイト列）
         * @param dataSize データサイズ
         * @return 成功時true
         */
        bool UploadWeights(const void *weightData, size_t dataSize);

        /**
         * @brief リソースを解放
         */
        void Shutdown();

        // ========================================
        // アクセサ
        // ========================================

        /** @brief 重み/バイアスStorageBufferを取得 */
        RHI::BufferPtr GetWeightBuffer() const { return m_WeightBuffer; }

        /** @brief 指定スロットの出力テクスチャを取得 */
        RHI::TexturePtr GetOutputTexture(uint32_t slotIndex) const;

        /** @brief 指定スロットのTextureHandleを取得（RegisterOutputTextures後に有効） */
        TextureHandle GetOutputTextureHandle(uint32_t slotIndex) const;

        /** @brief 出力スロット数を取得 */
        uint32_t GetOutputSlotCount() const { return static_cast<uint32_t>(m_Desc.OutputSlots.size()); }

        /** @brief ネットワーク記述を取得 */
        const NeuralMaterialDesc &GetDesc() const { return m_Desc; }

        /** @brief 初期化済みか */
        bool IsInitialized() const { return m_bInitialized; }

        /** @brief 重みデータのバイトサイズを計算 */
        size_t CalculateWeightBufferSize() const;

    private:
        RHI::IDevice *m_Device = nullptr;
        NeuralMaterialDesc m_Desc;

        RHI::BufferPtr m_WeightBuffer;                              ///< MLP重み/バイアスのStorageBuffer
        Container::VariableArray<RHI::TexturePtr> m_OutputTextures; ///< スロット別出力テクスチャ
        Container::VariableArray<TextureHandle> m_OutputHandles;    ///< スロット別TextureHandle

        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
