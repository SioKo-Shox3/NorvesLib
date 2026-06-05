#include "Rendering/DrawCommand.h"
#include "Rendering/SceneProxy.h"
#include <algorithm>
#include <cstring>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // DrawCommand
    // ========================================

    void DrawCommand::CalculateSortKey(float depth, BlendMode blendMode)
    {
        // コンピュートコマンドはソート不要
        if (IsComputeCommand())
        {
            SortKey = 0;
            return;
        }

        // ソートキーの構成:
        // [63-56] ブレンドモード（不透明=0が先）
        // [55-32] マテリアルID（状態変更最小化）
        // [31-0]  深度（不透明は手前から、透明は奥から）

        uint64_t blendPart = static_cast<uint64_t>(blendMode) << 56;
        uint64_t materialPart = static_cast<uint64_t>(Draw.MaterialHandle.Id) << 32;

        // 深度を正規化（0.0〜1.0を32bit整数に）
        uint32_t depthInt = static_cast<uint32_t>(depth * 4294967295.0f);

        // 透明オブジェクトは深度を反転（奥から手前）
        if (blendMode != BlendMode::Opaque)
        {
            depthInt = 0xFFFFFFFF - depthInt;
        }

        SortKey = blendPart | materialPart | depthInt;
    }

    // ========================================
    // MeshBatcher
    // ========================================

    void MeshBatcher::BeginBatching()
    {
        Clear();
    }

    void MeshBatcher::AddMeshProxy(const MeshProxy &proxy)
    {
        if (!proxy.IsValid())
        {
            return;
        }

        m_Stats.TotalProxies++;

        // 各マテリアルスロットごとにバッチを作成
        for (uint32_t i = 0; i < proxy.MaterialCount; ++i)
        {
            // バッチキーを計算
            MeshBatch tempBatch;
            tempBatch.MeshHandle = proxy.MeshHandle;
            tempBatch.MaterialHandle = proxy.Materials[i];
            tempBatch.SubMeshIndex = i;

            uint64_t key = tempBatch.GetBatchKey();

            // バッチを検索または作成
            MeshBatch &batch = FindOrCreateBatch(key);
            batch.MeshHandle = proxy.MeshHandle;
            batch.MaterialHandle = proxy.Materials[i];
            batch.SubMeshIndex = i;

            // インスタンスを追加（カスタムデータとシャドウフラグも含む）
            batch.AddInstance(proxy.WorldTransform, proxy.ObjectId, proxy.CustomData, proxy.bCastShadow);
        }
    }

    void MeshBatcher::EndBatching()
    {
        m_Stats.TotalBatches = static_cast<uint32_t>(m_Batches.size());

        // インスタンシング統計を計算
        for (const MeshBatch &batch : m_Batches)
        {
            if (batch.IsInstanced())
            {
                m_Stats.InstancedDrawCalls++;
                m_Stats.SavedDrawCalls += batch.GetInstanceCount() - 1;
            }
        }
    }

    void MeshBatcher::GenerateDrawCommands(Container::VariableArray<DrawCommand> &outCommands)
    {
        outCommands.clear();

        for (const MeshBatch &batch : m_Batches)
        {
            if (batch.IsInstanced())
            {
                DrawCommand cmd;
                cmd.Type = DrawCommandType::DrawIndexed;
                cmd.Draw.MeshHandle = batch.MeshHandle;
                cmd.Draw.MaterialHandle = batch.MaterialHandle;
                cmd.Draw.SubMeshIndex = batch.SubMeshIndex;

                // インスタンシング描画
                cmd.Type = DrawCommandType::DrawIndexedInstanced;
                cmd.Draw.bInstanced = true;
                cmd.Draw.InstanceCount = batch.GetInstanceCount();
                cmd.Draw.FirstInstance = 0;
                // カスタムデータとフラグをコピー（最初のインスタンスの値を使用）
                if (!batch.InstanceExtraData.empty())
                {
                    const auto &extra = batch.InstanceExtraData[0];
                    std::memcpy(cmd.Draw.CustomData, extra.CustomData, sizeof(cmd.Draw.CustomData));
                    cmd.Draw.bCastShadow = extra.bCastShadow;
                }
                // TODO: インスタンスデータバッファへのオフセット設定
                outCommands.push_back(cmd);
                continue;
            }

            for (uint32_t instanceIndex = 0; instanceIndex < batch.GetInstanceCount(); ++instanceIndex)
            {
                DrawCommand cmd;
                cmd.Type = DrawCommandType::DrawIndexed;
                cmd.Draw.MeshHandle = batch.MeshHandle;
                cmd.Draw.MaterialHandle = batch.MaterialHandle;
                cmd.Draw.SubMeshIndex = batch.SubMeshIndex;
                cmd.Draw.bInstanced = false;
                cmd.Draw.InstanceCount = 1;
                cmd.Draw.WorldMatrix = batch.InstanceTransforms[instanceIndex];
                // カスタムデータとフラグをコピー
                if (instanceIndex < batch.InstanceExtraData.size())
                {
                    const auto &extra = batch.InstanceExtraData[instanceIndex];
                    std::memcpy(cmd.Draw.CustomData, extra.CustomData, sizeof(cmd.Draw.CustomData));
                    cmd.Draw.bCastShadow = extra.bCastShadow;
                }
                // TODO: 法線行列の計算
                outCommands.push_back(cmd);
            }
        }

        m_Stats.TotalDrawCommands = static_cast<uint32_t>(outCommands.size());
    }

    void MeshBatcher::Clear()
    {
        m_BatchMap.clear();
        m_Batches.clear();
        m_Stats = BatchStats{};
    }

    MeshBatch &MeshBatcher::FindOrCreateBatch(uint64_t key)
    {
        auto it = m_BatchMap.find(key);
        if (it != m_BatchMap.end())
        {
            return m_Batches[it->second];
        }

        uint32_t index = static_cast<uint32_t>(m_Batches.size());
        m_Batches.emplace_back();
        m_BatchMap[key] = index;
        return m_Batches[index];
    }

    // ========================================
    // DrawCommandSorter
    // ========================================

    void DrawCommandSorter::Sort(Container::VariableArray<DrawCommand> &commands, SortMode mode)
    {
        if (mode == SortMode::None || commands.empty())
        {
            return;
        }

        switch (mode)
        {
        case SortMode::FrontToBack:
            std::sort(commands.begin(), commands.end(),
                      [](const DrawCommand &a, const DrawCommand &b)
                      {
                          return a.SortKey < b.SortKey;
                      });
            break;

        case SortMode::BackToFront:
            std::sort(commands.begin(), commands.end(),
                      [](const DrawCommand &a, const DrawCommand &b)
                      {
                          return a.SortKey > b.SortKey;
                      });
            break;

        case SortMode::ByMaterial:
            std::sort(commands.begin(), commands.end(),
                      [](const DrawCommand &a, const DrawCommand &b)
                      {
                          return a.Draw.MaterialHandle.Id < b.Draw.MaterialHandle.Id;
                      });
            break;

        default:
            break;
        }
    }

    void DrawCommandSorter::SortAndSeparate(const Container::VariableArray<DrawCommand> &commands,
                                            Container::VariableArray<DrawCommand> &outOpaqueCommands,
                                            Container::VariableArray<DrawCommand> &outTransparentCommands)
    {
        outOpaqueCommands.clear();
        outTransparentCommands.clear();

        for (const DrawCommand &cmd : commands)
        {
            // TODO: マテリアルからブレンドモードを取得
            // 現在は単純にすべて不透明として扱う
            bool bIsTransparent = false; // MaterialManager::IsTransparent(cmd.Draw.MaterialHandle);

            if (bIsTransparent)
            {
                outTransparentCommands.push_back(cmd);
            }
            else
            {
                outOpaqueCommands.push_back(cmd);
            }
        }
    }

} // namespace NorvesLib::Core::Rendering
