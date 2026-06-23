#pragma once

#include "RenderTypes.h"
#include "MaterialTypes.h"
#include "MeshTypes.h"
#include "Container/Containers.h"
#include "RHI/RHITypes.h"
#include "Math/Matrix4x4.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    // ========================================
    // DrawCommandType
    // ========================================

    /**
     * @brief GPUコマンドの種別
     */
    enum class DrawCommandType : uint8_t
    {
        DrawIndexed,          // インデックス付き描画
        Draw,                 // インデックスなし描画
        DrawIndexedInstanced, // インスタンシングインデックス描画
        DrawInstanced,        // インスタンシング描画
        Dispatch,             // コンピュートディスパッチ
    };

    enum class DrawPayloadKind : uint8_t
    {
        Mesh,
        Board
    };

    // ========================================
    // DrawParams
    // ========================================

    /**
     * @brief グラフィックス描画パラメータ
     *
     * Draw/DrawIndexed系コマンドで使用されるパラメータ群。
     */
    struct DrawParams
    {
        // メッシュデータ
        DrawPayloadKind PayloadKind = DrawPayloadKind::Mesh;
        BoardRenderSubtype BoardSubtype = BoardRenderSubtype::Standard;
        MeshDataHandle MeshHandle; // メッシュハンドル
        uint32_t SubMeshIndex = 0; // サブメッシュインデックス
        uint32_t IndexOffset = 0;  // インデックスオフセット
        uint32_t IndexCount = 0;   // インデックス数
        uint32_t VertexOffset = 0; // 頂点オフセット

        // マテリアル
        MaterialHandle MaterialHandle; // マテリアルハンドル
        uint32_t MaterialIndex = 0;    // マテリアルインデックス
        BlendMode MaterialBlendMode = BlendMode::Opaque;
        TextureHandle Texture = TextureHandle::Invalid();
        float SortDepth = 0.0f;
        uint64_t ObjectId = 0;
        uint64_t SourceMeshComponentId = 0;

        // インスタンシング
        uint32_t InstanceCount = 1;      // インスタンス数
        uint32_t FirstInstance = 0;      // 最初のインスタンスID
        uint32_t InstanceDataOffset = 0; // インスタンスデータオフセット

        // トランスフォーム（非インスタンシング時）
        Math::Matrix4x4 WorldMatrix;  // ワールド行列
        Math::Matrix4x4 NormalMatrix; // 法線行列（転置逆行列）

        // フラグ
        bool bCastShadow = true; // シャドウを落とすか
        bool bInstanced = false; // インスタンシング描画か

        // カスタムデータ
        float CustomData[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // シェーダーに渡すカスタムデータ
    };

    // ========================================
    // DispatchParams
    // ========================================

    /**
     * @brief コンピュートディスパッチパラメータ
     *
     * Dispatchコマンドで使用されるスレッドグループ数。
     */
    struct DispatchParams
    {
        uint32_t ThreadGroupCountX = 1; // Xスレッドグループ数
        uint32_t ThreadGroupCountY = 1; // Yスレッドグループ数
        uint32_t ThreadGroupCountZ = 1; // Zスレッドグループ数
    };

    // ========================================
    // DrawCommand
    // ========================================

    /**
     * @brief GPUコマンド抽象
     *
     * GPUに発行される描画命令/コンピュート命令の統一的な表現。
     * Draw/DrawIndexed/Dispatch等のコマンドを共通の構造体で扱い、
     * RenderThread上でコマンドリストに記録されます。
     */
    struct DrawCommand
    {
        // ========================================
        // コマンド種別
        // ========================================

        DrawCommandType Type = DrawCommandType::DrawIndexed; // コマンド種別

        // ========================================
        // パイプライン・ディスクリプタ（共通）
        // ========================================

        RHI::PipelinePtr Pipeline;           // 使用するパイプライン（nullならデフォルト）
        RHI::DescriptorSetPtr DescriptorSet; // ディスクリプタセット（nullならパス側で管理）
        uint32_t DescriptorSetSlot = 0;      // ディスクリプタセットスロット

        // ========================================
        // コマンドパラメータ（種別に応じて使い分ける）
        // ========================================

        DrawParams Draw;        // グラフィックス描画パラメータ
        DispatchParams Compute; // コンピュートパラメータ

        // ========================================
        // ソートキー
        // ========================================

        uint64_t SortKey = 0; // ソートキー

        // ========================================
        // ファクトリメソッド
        // ========================================

        /**
         * @brief インデックス付き描画コマンドを生成
         */
        static DrawCommand CreateDrawIndexed()
        {
            DrawCommand cmd;
            cmd.Type = DrawCommandType::DrawIndexed;
            return cmd;
        }

        /**
         * @brief インデックスなし描画コマンドを生成
         */
        static DrawCommand CreateDraw()
        {
            DrawCommand cmd;
            cmd.Type = DrawCommandType::Draw;
            return cmd;
        }

        /**
         * @brief コンピュートディスパッチコマンドを生成
         * @param groupX Xスレッドグループ数
         * @param groupY Yスレッドグループ数
         * @param groupZ Zスレッドグループ数
         */
        static DrawCommand CreateDispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ)
        {
            DrawCommand cmd;
            cmd.Type = DrawCommandType::Dispatch;
            cmd.Compute.ThreadGroupCountX = groupX;
            cmd.Compute.ThreadGroupCountY = groupY;
            cmd.Compute.ThreadGroupCountZ = groupZ;
            return cmd;
        }

        // ========================================
        // ユーティリティ
        // ========================================

        /**
         * @brief グラフィックス描画コマンドか判定
         */
        bool IsGraphicsCommand() const
        {
            return Type != DrawCommandType::Dispatch;
        }

        /**
         * @brief コンピュートコマンドか判定
         */
        bool IsComputeCommand() const
        {
            return Type == DrawCommandType::Dispatch;
        }

        /**
         * @brief ソートキーを計算
         * @param depth カメラからの深度
         * @param blendMode ブレンドモード
         */
        void CalculateSortKey(float depth, BlendMode blendMode);
    };

    struct CommandRange
    {
        uint32_t First = 0;
        uint32_t Count = 0;

        bool IsEmpty() const
        {
            return Count == 0;
        }

        uint32_t End() const
        {
            return First + Count;
        }
    };

    struct DrawCommandView
    {
        const DrawCommand *Data = nullptr;
        uint32_t Count = 0;

        static DrawCommandView FromArray(const Container::VariableArray<DrawCommand> &commands)
        {
            if (commands.empty())
            {
                return {};
            }

            return {commands.data(), static_cast<uint32_t>(commands.size())};
        }

        static DrawCommandView FromRange(const Container::VariableArray<DrawCommand> &commands,
                                         const CommandRange &range)
        {
            if (range.Count == 0 || range.First >= commands.size())
            {
                return {};
            }

            const uint32_t availableCount = static_cast<uint32_t>(commands.size()) - range.First;
            const uint32_t count = range.Count < availableCount ? range.Count : availableCount;
            return {commands.data() + range.First, count};
        }

        bool empty() const
        {
            return !Data || Count == 0;
        }

        uint32_t size() const
        {
            return empty() ? 0u : Count;
        }

        const DrawCommand *begin() const
        {
            return empty() ? nullptr : Data;
        }

        const DrawCommand *end() const
        {
            return empty() ? nullptr : Data + Count;
        }

        const DrawCommand &operator[](uint32_t index) const
        {
            return Data[index];
        }
    };

    /**
     * @brief DrawCommandのインスタンス範囲をpacket内の絶対indexへ付け替えます。
     * @param commands 対象DrawCommand配列
     * @param baseInstance 加算するInstanceData先頭index
     */
    void RebaseDrawCommandInstanceRange(Container::VariableArray<DrawCommand> &commands,
                                        uint32_t baseInstance);

    // ========================================
    // MeshBatch
    // ========================================

    /**
     * @brief メッシュバッチ
     *
     * 同じメッシュ・マテリアルを持つProxyをグループ化。
     * インスタンシング描画の最適化に使用されます。
     */
    struct MeshBatch
    {
        // ========================================
        // 識別
        // ========================================

        MeshDataHandle MeshHandle;     // メッシュハンドル
        MaterialHandle MaterialHandle; // マテリアルハンドル
        uint32_t SubMeshIndex = 0;     // サブメッシュインデックス
        BlendMode MaterialBlendMode = BlendMode::Opaque;
        float SortDepth = 0.0f;
        bool bCastShadow = true;       // シャドウを落とすか

        // ========================================
        // バッチキー
        // ========================================

        /**
         * @brief バッチキーを計算
         *
         * 同じバッチにまとめられるかの判定に使用。
         */
        uint64_t GetBatchKey() const
        {
            // メッシュID + マテリアルID + サブメッシュ + シャドウ有無をキーにする
            return (static_cast<uint64_t>(MeshHandle.Id) << 32) |
                   (static_cast<uint64_t>(MaterialHandle.Id) << 8) |
                   (static_cast<uint64_t>(SubMeshIndex) << 1) |
                   (bCastShadow ? 1ull : 0ull);
        }

        // ========================================
        // インスタンスデータ
        // ========================================

        Container::VariableArray<Math::Matrix4x4> InstanceTransforms;
        Container::VariableArray<uint64_t> InstanceObjectIds; // 元のObjectID（デバッグ用）

        /**
         * @brief インスタンスごとの追加データ
         */
        struct PerInstanceExtraData
        {
            float CustomData[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float SortDepth = 0.0f;
            bool bCastShadow = true;
        };
        Container::VariableArray<PerInstanceExtraData> InstanceExtraData;

        // ========================================
        // ユーティリティ
        // ========================================

        /**
         * @brief インスタンスを追加
         * @param worldTransform ワールド変換行列
         * @param objectId オブジェクトID
         */
        void AddInstance(const Math::Matrix4x4 &worldTransform, uint64_t objectId = 0,
                         const float *customData = nullptr, bool bCastShadow = true,
                         float sortDepth = 0.0f)
        {
            InstanceTransforms.push_back(worldTransform);
            InstanceObjectIds.push_back(objectId);
            PerInstanceExtraData extra;
            if (customData)
            {
                extra.CustomData[0] = customData[0];
                extra.CustomData[1] = customData[1];
                extra.CustomData[2] = customData[2];
                extra.CustomData[3] = customData[3];
            }
            extra.SortDepth = sortDepth;
            extra.bCastShadow = bCastShadow;
            InstanceExtraData.push_back(extra);

            if (InstanceTransforms.size() == 1 || sortDepth < SortDepth)
            {
                SortDepth = sortDepth;
            }
        }

        /**
         * @brief インスタンス数を取得
         */
        uint32_t GetInstanceCount() const
        {
            return static_cast<uint32_t>(InstanceTransforms.size());
        }

        /**
         * @brief バッチをクリア
         */
        void Clear()
        {
            InstanceTransforms.clear();
            InstanceObjectIds.clear();
            InstanceExtraData.clear();
        }

        /**
         * @brief インスタンシング描画が有効か
         *
         * @param minInstanceCount インスタンシング描画へ切り替える最小インスタンス数
         * @param bAllowInstancing インスタンシング描画を許可するか
         */
        bool IsInstanced(uint32_t minInstanceCount, bool bAllowInstancing) const
        {
            return bAllowInstancing &&
                   GetInstanceCount() >= minInstanceCount &&
                   (MaterialBlendMode == BlendMode::Opaque || MaterialBlendMode == BlendMode::Masked);
        }
    };

    // ========================================
    // MeshBatcher
    // ========================================

    /**
     * @brief メッシュバッチャー
     *
     * MeshProxyからMeshBatchを生成し、DrawCommandに変換します。
     */
    class MeshBatcher
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        MeshBatcher() = default;

        /**
         * @brief バッチングを開始
         */
        void BeginBatching();

        /**
         * @brief MeshProxyをバッチに追加
         * @param proxy 追加するMeshProxy
         */
        void AddMeshProxy(const struct MeshProxy &proxy);

        /**
         * @brief バッチングを終了し、バッチを確定
         */
        void EndBatching();

        /**
         * @brief DrawCommandを生成
         * @param outCommands 出力先のDrawCommandリスト
         * @param outInstanceData 出力先のGPUインスタンスデータリスト
         */
        void GenerateDrawCommands(Container::VariableArray<DrawCommand> &outCommands,
                                  Container::VariableArray<GPUSceneInstanceData> &outInstanceData,
                                  bool bAllowInstancing,
                                  uint32_t minInstanceCount);

        /**
         * @brief バッチをクリア
         */
        void Clear();

        // ========================================
        // 統計
        // ========================================

        struct BatchStats
        {
            uint32_t TotalProxies = 0;       // 入力Proxy総数
            uint32_t TotalBatches = 0;       // 生成されたバッチ数
            uint32_t TotalDrawCommands = 0;  // 生成されたDrawCommand数
            uint32_t InstancedDrawCalls = 0; // インスタンシング描画数
            uint32_t SavedDrawCalls = 0;     // 削減されたドローコール数
        };

        const BatchStats &GetStats() const { return m_Stats; }

    private:
        /**
         * @brief バッチキーからバッチを検索または作成
         * @param key バッチキー
         * @return バッチへの参照
         */
        MeshBatch &FindOrCreateBatch(uint64_t key);

    private:
        // バッチマップ（キー -> バッチインデックス）
        Container::UnorderedMap<uint64_t, uint32_t> m_BatchMap;

        // バッチリスト
        Container::VariableArray<MeshBatch> m_Batches;

        // 統計
        BatchStats m_Stats;
    };

    // ========================================
    // DrawCommandSorter
    // ========================================

    /**
     * @brief DrawCommandソーター
     *
     * DrawCommandをソートキーに基づいて並べ替えます。
     * 不透明→透明の順にソートし、状態変更を最小化します。
     */
    class DrawCommandSorter
    {
    public:
        /**
         * @brief ソートモード
         */
        enum class SortMode : uint8_t
        {
            FrontToBack, // 手前から奥（不透明オブジェクト用）
            BackToFront, // 奥から手前（透明オブジェクト用）
            ByMaterial,  // マテリアル順（状態変更最小化）
            None         // ソートなし
        };

        /**
         * @brief DrawCommandをソート
         * @param commands ソート対象のDrawCommandリスト
         * @param mode ソートモード
         */
        static void Sort(Container::VariableArray<DrawCommand> &commands, SortMode mode);

        /**
         * @brief 不透明と透明を分離してソート
         * @param commands ソート対象のDrawCommandリスト
         * @param outOpaqueCommands 不透明コマンド出力先
         * @param outTransparentCommands 透明コマンド出力先
         */
        static void SortAndSeparate(const Container::VariableArray<DrawCommand> &commands,
                                    Container::VariableArray<DrawCommand> &outOpaqueCommands,
                                    Container::VariableArray<DrawCommand> &outTransparentCommands);
    };

} // namespace NorvesLib::Core::Rendering
