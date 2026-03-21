#pragma once

#include "NeuralMaterialResource.h"
#include "DrawCommand.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;

    /**
     * @brief ニューラルマテリアルデコーダー
     *
     * NeuralMaterialResourceの重みデータをコンピュートシェーダーで推論し、
     * 複数の出力テクスチャ（Albedo, Normal, ARM等）にデコード結果を書き込みます。
     *
     * 1つのMLPが全PBRプロパティを一括生成し、
     * 出力スロット構成はNeuralMaterialDescによって外部から設定可能です。
     *
     * DrawCommand::CreateDispatch() を通じてコマンドを生成し、
     * SceneRendererに渡して実行します。
     *
     * 使用フロー:
     * 1. Initialize() でコンピュートパイプラインを作成
     * 2. RegisterResource() でデコード対象のNeuralMaterialResourceを登録
     * 3. GenerateDecodeCommands() でDispatchコマンドを生成
     * 4. パスがSceneRenderer経由でコマンドを実行
     */
    class NeuralMaterialDecoder
    {
    public:
        NeuralMaterialDecoder() = default;
        ~NeuralMaterialDecoder();

        /**
         * @brief 初期化
         * @param device RHIデバイス
         * @param shaderManager シェーダーマネージャー
         * @return 成功時true
         */
        bool Initialize(RHI::IDevice *device, ShaderManager *shaderManager);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief デコード対象のNeuralMaterialResourceを登録
         * @param resource 登録するリソース
         */
        void RegisterResource(NeuralMaterialResource *resource);

        /**
         * @brief 登録済みリソースをクリア
         */
        void ClearResources();

        /**
         * @brief 登録されたリソースに対するDispatch DrawCommandを生成
         * @param outCommands 出力先のDrawCommandリスト
         *
         * 各NeuralMaterialResourceにつき1つのDispatchコマンドを生成。
         * DrawCommand::CreateDispatch()を使用し、Pipeline/DescriptorSetを設定します。
         * 1回のDispatchで全出力スロット（Albedo, Normal, ARM等）を同時にデコードします。
         */
        void GenerateDecodeCommands(Container::VariableArray<DrawCommand> &outCommands);

        /**
         * @brief 初期化済みか
         */
        bool IsInitialized() const { return m_bInitialized; }

        /**
         * @brief コンピュートシェーダーのスレッドグループサイズ
         */
        static constexpr uint32_t ThreadGroupSizeX = 16;
        static constexpr uint32_t ThreadGroupSizeY = 16;

        /**
         * @brief 出力テクスチャスロットの最大数
         *
         * DescriptorSetのバインディング数に影響。
         * binding 0 = 重みバッファ, binding 1～MaxOutputSlots = 出力テクスチャ
         */
        static constexpr uint32_t MaxOutputSlots = 8;

    private:
        /**
         * @brief 単一リソースに対するDescriptorSetを作成・更新
         *
         * binding構成:
         *   binding 0: StorageBuffer (MLP重みデータ) - readonly
         *   binding 1～N: StorageTexture (出力テクスチャ) - writeonly
         */
        RHI::DescriptorSetPtr CreateDescriptorSetForResource(NeuralMaterialResource *resource);

    private:
        RHI::IDevice *m_Device = nullptr;
        ShaderManager *m_ShaderManager = nullptr;

        // コンピュートパイプライン
        RHI::PipelinePtr m_ComputePipeline;

        // デコード対象リソース
        Container::VariableArray<NeuralMaterialResource *> m_Resources;

        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
