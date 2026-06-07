#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/RenderResourceRegistryFwd.h"
#include "Rendering/FrameCommand.h"
#include "RHI/RHITypes.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
    class IGPUResourceAllocator;
    class TransientResourcePool;
}

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    class PersistentResourceCache;

    /**
     * @brief SceneRenderer統計情報
     */
    struct SceneRendererStats
    {
        uint32_t DrawCallCount = 0; ///< ドローコール数
        uint32_t DispatchCount = 0; ///< ディスパッチ数
        uint32_t TriangleCount = 0; ///< 三角形数
        float RenderTimeMs = 0.0f;  ///< レンダリング時間（ミリ秒）
        size_t GPUMemoryUsed = 0;   ///< GPU使用メモリ
    };

    /**
     * @brief RHIを使用して実際の描画を行うクラス
     *
     * SceneViewからDrawCommandを受け取り、
     * GPUリソースを管理しながら描画を実行します。
     *
     * 責務:
     * - DrawCommandの実行
     * - シェーダーバインド
     * - レンダーパス管理
     * - PersistentResourceCache/TransientResourcePoolの利用
     *
     * ※ GPUリソースの直接管理はPersistentResourceCacheに委譲
     *
     * 使用例:
     * ```cpp
     * renderer.BeginFrame();
     * renderer.Render(sceneView, commandList);
     * renderer.EndFrame();
     * ```
     */
    class SceneRenderer
    {
    public:
        /**
         * @brief コンストラクタ
         */
        SceneRenderer() = default;

        /**
         * @brief デストラクタ
         */
        ~SceneRenderer();

        /**
         * @brief 初期化します
         * @param device RHIデバイス
         * @param resourceCache 永続リソースキャッシュ
         * @param transientPool 一時リソースプール（オプション）
         * @return 初期化成功時true
         */
        bool Initialize(RHI::IDevice *device,
                        PersistentResourceCache *resourceCache,
                        RHI::TransientResourcePool *transientPool = nullptr);

        /**
         * @brief デフォルトパイプラインを設定します
         * @param pipeline パイプライン
         *
         * マテリアルシステムが未完成の間、全DrawCommandはこのパイプラインで描画されます。
         */
        void SetDefaultPipeline(RHI::PipelinePtr pipeline) { m_DefaultPipeline = pipeline; }

        /**
         * @brief デフォルトパイプラインを取得します
         * @return デフォルトパイプライン
         */
        RHI::PipelinePtr GetDefaultPipeline() const { return m_DefaultPipeline; }

        /**
         * @brief 終了処理を行います
         */
        void Shutdown();

        // ========================================
        // フレーム管理
        // ========================================

        /**
         * @brief フレーム開始処理
         */
        void BeginFrame();

        /**
         * @brief フレーム終了処理
         */
        void EndFrame();

        // ========================================
        // 描画
        // ========================================

        /**
         * @brief SceneViewをレンダリングします
         * @param sceneView シーンビュー
         * @param commandList コマンドリスト
         */
        void Render(SceneView *sceneView, RHI::ICommandList *commandList);

        /**
         * @brief DrawCommandを直接実行します
         * @param commands DrawCommand配列
         * @param commandList コマンドリスト
         */
        void ExecuteDrawCommands(const Container::VariableArray<DrawCommand> &commands,
                                 RHI::ICommandList *commandList);

        /**
         * @brief FrameCommandを実行します
         * @param commands FrameCommand配列
         * @param commandList コマンドリスト
         */
        void ExecuteFrameCommands(const Container::VariableArray<FrameCommand> &commands,
                                  RHI::ICommandList *commandList);

        /**
         * @brief 単一DrawCommandのメッシュ描画をコマンドリストに記録します
         *
         * パスがUBOとディスクリプタセットを準備した後、
         * メッシュのGPUデータ解決・頂点/インデックスバッファ設定・DrawIndexedを実行します。
         *
         * @param command 描画コマンド
         * @param commandList コマンドリスト
         * @param resourceManager レンダリングリソースマネージャ
         * @param descriptorSet UBOバインド済みディスクリプタセット
         * @param descriptorSetSlot ディスクリプタセットスロット
         * @return 描画に成功した場合true
         */
        bool RecordMeshDrawCall(const DrawCommand &command,
                                RHI::ICommandList *commandList,
                                RenderResourceRegistry *resourceManager,
                                RHI::DescriptorSetPtr descriptorSet,
                                uint32_t descriptorSetSlot = 0);

        // ========================================
        // 統計情報
        // ========================================

        /**
         * @brief 統計情報を取得します
         * @return 統計情報
         */
        const SceneRendererStats &GetStats() const { return m_Stats; }

        /**
         * @brief 統計情報をリセットします
         */
        void ResetStats();

    private:
        /**
         * @brief 単一のDrawCommandを実行します
         * @param command DrawCommand
         * @param commandList コマンドリスト
         */
        void ExecuteDrawCommand(const DrawCommand &command, RHI::ICommandList *commandList);

        /**
         * @brief 単一のFrameCommandを実行します
         * @param command FrameCommand
         * @param commandList コマンドリスト
         */
        void ExecuteFrameCommand(const FrameCommand &command, RHI::ICommandList *commandList);

        /**
         * @brief バッチされたDrawCommandを実行します
         * @param commands DrawCommand配列
         * @param startIndex 開始インデックス
         * @param count コマンド数
         * @param commandList コマンドリスト
         */
        void ExecuteBatch(const Container::VariableArray<DrawCommand> &commands,
                          size_t startIndex, size_t count,
                          RHI::ICommandList *commandList);

        RHI::IDevice *m_Device = nullptr;
        PersistentResourceCache *m_ResourceCache = nullptr;
        RHI::TransientResourcePool *m_TransientPool = nullptr;

        // デフォルトパイプライン（マテリアルシステム完成まで使用）
        RHI::PipelinePtr m_DefaultPipeline;

        // 前回バインドしたパイプライン（冗長なバインドを避けるため）
        RHI::PipelinePtr m_BoundPipeline;

        SceneRendererStats m_Stats;
        uint64_t m_CurrentFrame = 0;

        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
