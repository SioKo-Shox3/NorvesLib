#include "Rendering/DrawCommand.h"
#include "Rendering/SceneProxy.h"
#include "Math/MatrixUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float SORT_DEPTH_SCALE = 1024.0f;

        bool IsTransparentBlendMode(BlendMode blendMode)
        {
            return blendMode != BlendMode::Opaque && blendMode != BlendMode::Masked;
        }

        uint32_t QuantizeSortDepth(float depth)
        {
            if (depth <= 0.0f)
            {
                return 0;
            }

            const float scaledDepth = depth * SORT_DEPTH_SCALE;
            if (scaledDepth >= 4294967295.0f)
            {
                return 0xFFFFFFFFu;
            }

            return static_cast<uint32_t>(scaledDepth);
        }

        bool AreSortDepthsEquivalent(float lhs, float rhs)
        {
            return std::fabs(lhs - rhs) <= 0.0001f;
        }

        void FillGPUSceneInstanceData(const Math::Matrix4x4 &world,
                                      const Math::Matrix4x4 &normalMatrix,
                                      const float *customData,
                                      GPUSceneInstanceData &outData)
        {
            Math::MatrixUtils::CopyToShaderData(world, outData.World);

            Math::MatrixUtils::CopyUpper3x3ToShaderData(normalMatrix, outData.NormalMatrix);

            if (customData)
            {
                outData.ObjectColor[0] = customData[0] != 0.0f ? customData[0] : 1.0f;
                outData.ObjectColor[1] = customData[1] != 0.0f ? customData[1] : 1.0f;
                outData.ObjectColor[2] = customData[2] != 0.0f ? customData[2] : 1.0f;
                outData.ObjectColor[3] = customData[3] != 0.0f ? customData[3] : 1.0f;
                std::memcpy(outData.CustomData, customData, sizeof(outData.CustomData));
            }
            else
            {
                outData.ObjectColor[0] = 1.0f;
                outData.ObjectColor[1] = 1.0f;
                outData.ObjectColor[2] = 1.0f;
                outData.ObjectColor[3] = 1.0f;
                outData.CustomData[0] = 0.0f;
                outData.CustomData[1] = 0.0f;
                outData.CustomData[2] = 0.0f;
                outData.CustomData[3] = 0.0f;
            }
        }
    } // namespace

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

        Draw.SortDepth = depth;
        Draw.MaterialBlendMode = blendMode;

        const uint64_t blendPart = static_cast<uint64_t>(IsTransparentBlendMode(blendMode) ? 1ull : 0ull) << 63;
        const uint64_t depthPart = static_cast<uint64_t>(QuantizeSortDepth(depth)) << 31;
        const uint64_t materialPart = (Draw.MaterialHandle.Id & 0x7FFFu) << 16;
        const uint64_t objectPart = Draw.ObjectId & 0xFFFFu;

        SortKey = blendPart | depthPart | materialPart | objectPart;
    }

    void RebaseDrawCommandInstanceRange(Container::VariableArray<DrawCommand> &commands,
                                        uint32_t baseInstance)
    {
        if (baseInstance == 0)
        {
            return;
        }

        for (DrawCommand &cmd : commands)
        {
            if (!cmd.IsGraphicsCommand())
            {
                continue;
            }

            cmd.Draw.FirstInstance += baseInstance;
            cmd.Draw.InstanceDataOffset += baseInstance;
        }
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

        if (proxy.SubMeshCount == 0)
        {
            // 各マテリアルスロットごとにバッチを作成
            const uint32_t materialCount = std::min(proxy.MaterialCount, MAX_MATERIAL_SLOTS);
            for (uint32_t i = 0; i < materialCount; ++i)
            {
                // バッチキーを計算
                MeshBatch tempBatch;
                tempBatch.MeshHandle = proxy.MeshHandle;
                tempBatch.MaterialHandle = proxy.Materials[i];
                tempBatch.SubMeshIndex = i;
                tempBatch.IndexOffset = 0;
                tempBatch.IndexCount = 0;
                tempBatch.VertexOffset = 0;
                tempBatch.MaterialBlendMode = proxy.MaterialBlendModes[i];
                tempBatch.SortDepth = proxy.SortDepth;
                tempBatch.bCastShadow = proxy.bCastShadow;

                uint64_t key = tempBatch.GetBatchKey();

                // バッチを検索または作成
                MeshBatch &batch = FindOrCreateBatch(key);
                batch.MeshHandle = proxy.MeshHandle;
                batch.MaterialHandle = proxy.Materials[i];
                batch.SubMeshIndex = i;
                batch.IndexOffset = 0;
                batch.IndexCount = 0;
                batch.VertexOffset = 0;
                batch.MaterialBlendMode = proxy.MaterialBlendModes[i];
                batch.bCastShadow = proxy.bCastShadow;

                // インスタンスを追加（カスタムデータとシャドウフラグも含む）
                batch.AddInstance(proxy.WorldTransform,
                                  proxy.ObjectId,
                                  proxy.CustomData,
                                  proxy.bCastShadow,
                                  proxy.SortDepth);
            }
            return;
        }

        const uint32_t subMeshCount = std::min(proxy.SubMeshCount, MAX_MATERIAL_SLOTS);
        for (uint32_t i = 0; i < subMeshCount; ++i)
        {
            const SubMeshRange& subMesh = proxy.SubMeshes[i];
            const uint32_t materialIndex = (subMesh.MaterialIndex < proxy.MaterialCount &&
                                            subMesh.MaterialIndex < MAX_MATERIAL_SLOTS)
                                               ? subMesh.MaterialIndex
                                               : 0u;

            // バッチキーを計算
            MeshBatch tempBatch;
            tempBatch.MeshHandle = proxy.MeshHandle;
            tempBatch.MaterialHandle = proxy.Materials[materialIndex];
            tempBatch.SubMeshIndex = i;
            tempBatch.IndexOffset = subMesh.IndexStart;
            tempBatch.IndexCount = subMesh.IndexCount;
            tempBatch.VertexOffset = subMesh.VertexStart;
            tempBatch.MaterialBlendMode = proxy.MaterialBlendModes[materialIndex];
            tempBatch.SortDepth = proxy.SortDepth;
            tempBatch.bCastShadow = proxy.bCastShadow;

            uint64_t key = tempBatch.GetBatchKey();

            // バッチを検索または作成
            MeshBatch &batch = FindOrCreateBatch(key);
            batch.MeshHandle = proxy.MeshHandle;
            batch.MaterialHandle = proxy.Materials[materialIndex];
            batch.SubMeshIndex = i;
            batch.IndexOffset = subMesh.IndexStart;
            batch.IndexCount = subMesh.IndexCount;
            batch.VertexOffset = subMesh.VertexStart;
            batch.MaterialBlendMode = proxy.MaterialBlendModes[materialIndex];
            batch.bCastShadow = proxy.bCastShadow;

            // インスタンスを追加（カスタムデータとシャドウフラグも含む）
            batch.AddInstance(proxy.WorldTransform,
                              proxy.ObjectId,
                              proxy.CustomData,
                              proxy.bCastShadow,
                              proxy.SortDepth);
        }
    }

    void MeshBatcher::EndBatching()
    {
        m_Stats.TotalBatches = static_cast<uint32_t>(m_Batches.size());

    }

    void MeshBatcher::GenerateDrawCommands(Container::VariableArray<DrawCommand> &outCommands,
                                           Container::VariableArray<GPUSceneInstanceData> &outInstanceData,
                                           bool bAllowInstancing,
                                           uint32_t minInstanceCount)
    {
        outCommands.clear();
        outInstanceData.clear();
        m_Stats.TotalBatches = static_cast<uint32_t>(m_Batches.size());
        m_Stats.TotalDrawCommands = 0;
        m_Stats.InstancedDrawCalls = 0;
        m_Stats.SavedDrawCalls = 0;

        for (const MeshBatch &batch : m_Batches)
        {
            if (batch.IsInstanced(minInstanceCount, bAllowInstancing))
            {
                DrawCommand cmd;
                cmd.Type = DrawCommandType::DrawIndexed;
                cmd.Draw.MeshHandle = batch.MeshHandle;
                cmd.Draw.MaterialHandle = batch.MaterialHandle;
                cmd.Draw.SubMeshIndex = batch.SubMeshIndex;
                cmd.Draw.IndexOffset = batch.IndexOffset;
                cmd.Draw.IndexCount = batch.IndexCount;
                cmd.Draw.VertexOffset = batch.VertexOffset;
                cmd.Draw.MaterialBlendMode = batch.MaterialBlendMode;
                cmd.Draw.SortDepth = batch.SortDepth;
                cmd.Draw.ObjectId = batch.InstanceObjectIds.empty() ? 0 : batch.InstanceObjectIds[0];

                // インスタンシング描画
                cmd.Type = DrawCommandType::DrawIndexedInstanced;
                cmd.Draw.bInstanced = true;
                cmd.Draw.InstanceCount = batch.GetInstanceCount();
                cmd.Draw.FirstInstance = static_cast<uint32_t>(outInstanceData.size());
                cmd.Draw.InstanceDataOffset = cmd.Draw.FirstInstance;
                cmd.Draw.bCastShadow = batch.bCastShadow;
                // カスタムデータとフラグをコピー（最初のインスタンスの値を使用）
                if (!batch.InstanceExtraData.empty())
                {
                    const auto &extra = batch.InstanceExtraData[0];
                    std::memcpy(cmd.Draw.CustomData, extra.CustomData, sizeof(cmd.Draw.CustomData));
                }

                for (uint32_t instanceIndex = 0; instanceIndex < batch.GetInstanceCount(); ++instanceIndex)
                {
                    const float *customData = nullptr;
                    if (instanceIndex < batch.InstanceExtraData.size())
                    {
                        customData = batch.InstanceExtraData[instanceIndex].CustomData;
                    }

                    const Math::Matrix4x4 normalMatrix =
                        Math::MatrixUtils::CreateNormalMatrix(batch.InstanceTransforms[instanceIndex]);
                    GPUSceneInstanceData instanceData;
                    FillGPUSceneInstanceData(batch.InstanceTransforms[instanceIndex],
                                             normalMatrix,
                                             customData,
                                             instanceData);
                    outInstanceData.push_back(instanceData);

                    if (instanceIndex == 0)
                    {
                        cmd.Draw.WorldMatrix = batch.InstanceTransforms[instanceIndex];
                        cmd.Draw.NormalMatrix = normalMatrix;
                    }
                }

                outCommands.push_back(cmd);
                m_Stats.InstancedDrawCalls++;
                m_Stats.SavedDrawCalls += batch.GetInstanceCount() - 1;
                continue;
            }

            for (uint32_t instanceIndex = 0; instanceIndex < batch.GetInstanceCount(); ++instanceIndex)
            {
                DrawCommand cmd;
                cmd.Type = DrawCommandType::DrawIndexed;
                cmd.Draw.MeshHandle = batch.MeshHandle;
                cmd.Draw.MaterialHandle = batch.MaterialHandle;
                cmd.Draw.SubMeshIndex = batch.SubMeshIndex;
                cmd.Draw.IndexOffset = batch.IndexOffset;
                cmd.Draw.IndexCount = batch.IndexCount;
                cmd.Draw.VertexOffset = batch.VertexOffset;
                cmd.Draw.MaterialBlendMode = batch.MaterialBlendMode;
                cmd.Draw.bInstanced = false;
                cmd.Draw.InstanceCount = 1;
                cmd.Draw.WorldMatrix = batch.InstanceTransforms[instanceIndex];
                cmd.Draw.ObjectId = instanceIndex < batch.InstanceObjectIds.size()
                                        ? batch.InstanceObjectIds[instanceIndex]
                                        : 0;
                cmd.Draw.FirstInstance = static_cast<uint32_t>(outInstanceData.size());
                cmd.Draw.InstanceDataOffset = cmd.Draw.FirstInstance;
                // カスタムデータとフラグをコピー
                if (instanceIndex < batch.InstanceExtraData.size())
                {
                    const auto &extra = batch.InstanceExtraData[instanceIndex];
                    std::memcpy(cmd.Draw.CustomData, extra.CustomData, sizeof(cmd.Draw.CustomData));
                    cmd.Draw.SortDepth = extra.SortDepth;
                    cmd.Draw.bCastShadow = extra.bCastShadow;
                }
                cmd.Draw.NormalMatrix = Math::MatrixUtils::CreateNormalMatrix(cmd.Draw.WorldMatrix);

                GPUSceneInstanceData instanceData;
                FillGPUSceneInstanceData(cmd.Draw.WorldMatrix,
                                         cmd.Draw.NormalMatrix,
                                         cmd.Draw.CustomData,
                                         instanceData);
                outInstanceData.push_back(instanceData);

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
                          if (!AreSortDepthsEquivalent(a.Draw.SortDepth, b.Draw.SortDepth))
                          {
                              return a.Draw.SortDepth < b.Draw.SortDepth;
                          }

                          if (a.Draw.MaterialHandle.Id != b.Draw.MaterialHandle.Id)
                          {
                              return a.Draw.MaterialHandle.Id < b.Draw.MaterialHandle.Id;
                          }

                          if (a.Draw.ObjectId != b.Draw.ObjectId)
                          {
                              return a.Draw.ObjectId < b.Draw.ObjectId;
                          }

                          return a.SortKey < b.SortKey;
                      });
            break;

        case SortMode::BackToFront:
            std::sort(commands.begin(), commands.end(),
                      [](const DrawCommand &a, const DrawCommand &b)
                      {
                          if (!AreSortDepthsEquivalent(a.Draw.SortDepth, b.Draw.SortDepth))
                          {
                              return a.Draw.SortDepth > b.Draw.SortDepth;
                          }

                          if (a.Draw.MaterialHandle.Id != b.Draw.MaterialHandle.Id)
                          {
                              return a.Draw.MaterialHandle.Id < b.Draw.MaterialHandle.Id;
                          }

                          if (a.Draw.ObjectId != b.Draw.ObjectId)
                          {
                              return a.Draw.ObjectId < b.Draw.ObjectId;
                          }

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
            const bool bIsTransparent = cmd.Draw.PayloadKind == DrawPayloadKind::Board ||
                                        IsTransparentBlendMode(cmd.Draw.MaterialBlendMode);

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
