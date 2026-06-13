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

        Math::Matrix4x4 CalculateNormalMatrix(const Math::Matrix4x4 &world)
        {
            const float a00 = world.m00;
            const float a01 = world.m01;
            const float a02 = world.m02;
            const float a10 = world.m10;
            const float a11 = world.m11;
            const float a12 = world.m12;
            const float a20 = world.m20;
            const float a21 = world.m21;
            const float a22 = world.m22;

            const float determinant =
                a00 * (a11 * a22 - a12 * a21) -
                a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);

            Math::Matrix4x4 normalMatrix = Math::Matrix4x4::Identity;
            if (std::abs(determinant) < Math::Constants::EPSILON)
            {
                return normalMatrix;
            }

            const float invDeterminant = 1.0f / determinant;

            const float inv00 = (a11 * a22 - a12 * a21) * invDeterminant;
            const float inv01 = (a02 * a21 - a01 * a22) * invDeterminant;
            const float inv02 = (a01 * a12 - a02 * a11) * invDeterminant;
            const float inv10 = (a12 * a20 - a10 * a22) * invDeterminant;
            const float inv11 = (a00 * a22 - a02 * a20) * invDeterminant;
            const float inv12 = (a02 * a10 - a00 * a12) * invDeterminant;
            const float inv20 = (a10 * a21 - a11 * a20) * invDeterminant;
            const float inv21 = (a01 * a20 - a00 * a21) * invDeterminant;
            const float inv22 = (a00 * a11 - a01 * a10) * invDeterminant;

            normalMatrix.m00 = inv00;
            normalMatrix.m01 = inv10;
            normalMatrix.m02 = inv20;
            normalMatrix.m10 = inv01;
            normalMatrix.m11 = inv11;
            normalMatrix.m12 = inv21;
            normalMatrix.m20 = inv02;
            normalMatrix.m21 = inv12;
            normalMatrix.m22 = inv22;

            return normalMatrix;
        }

        void FillGPUSceneInstanceData(const Math::Matrix4x4 &world,
                                      const Math::Matrix4x4 &normalMatrix,
                                      const float *customData,
                                      GPUSceneInstanceData &outData)
        {
            Math::MatrixUtils::CopyToShaderData(world, outData.World);

            outData.NormalMatrix[0] = normalMatrix.m00;
            outData.NormalMatrix[1] = normalMatrix.m01;
            outData.NormalMatrix[2] = normalMatrix.m02;
            outData.NormalMatrix[3] = 0.0f;
            outData.NormalMatrix[4] = normalMatrix.m10;
            outData.NormalMatrix[5] = normalMatrix.m11;
            outData.NormalMatrix[6] = normalMatrix.m12;
            outData.NormalMatrix[7] = 0.0f;
            outData.NormalMatrix[8] = normalMatrix.m20;
            outData.NormalMatrix[9] = normalMatrix.m21;
            outData.NormalMatrix[10] = normalMatrix.m22;
            outData.NormalMatrix[11] = 0.0f;

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

        // 各マテリアルスロットごとにバッチを作成
        const uint32_t materialCount = std::min(proxy.MaterialCount, MAX_MATERIAL_SLOTS);
        for (uint32_t i = 0; i < materialCount; ++i)
        {
            // バッチキーを計算
            MeshBatch tempBatch;
            tempBatch.MeshHandle = proxy.MeshHandle;
            tempBatch.MaterialHandle = proxy.Materials[i];
            tempBatch.SubMeshIndex = i;
            tempBatch.MaterialBlendMode = proxy.MaterialBlendModes[i];
            tempBatch.SortDepth = proxy.SortDepth;
            tempBatch.bCastShadow = proxy.bCastShadow;

            uint64_t key = tempBatch.GetBatchKey();

            // バッチを検索または作成
            MeshBatch &batch = FindOrCreateBatch(key);
            batch.MeshHandle = proxy.MeshHandle;
            batch.MaterialHandle = proxy.Materials[i];
            batch.SubMeshIndex = i;
            batch.MaterialBlendMode = proxy.MaterialBlendModes[i];
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
                        CalculateNormalMatrix(batch.InstanceTransforms[instanceIndex]);
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
                cmd.Draw.NormalMatrix = CalculateNormalMatrix(cmd.Draw.WorldMatrix);

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
            const bool bIsTransparent = IsTransparentBlendMode(cmd.Draw.MaterialBlendMode);

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
