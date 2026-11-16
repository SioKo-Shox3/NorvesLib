#pragma once

#include "RenderTypes.h"
#include "VertexLayout.h"
#include "Container/Containers.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // サブメッシュ情報
    // ========================================

    /**
     * @brief サブメッシュ定義
     *
     * 1つのメッシュ内で異なるマテリアルを使用する部分を定義します。
     */
    struct SubMesh
    {
        uint32_t IndexStart = 0;    // インデックスバッファ内の開始位置
        uint32_t IndexCount = 0;    // インデックス数
        uint32_t VertexStart = 0;   // ベース頂点オフセット
        uint32_t MaterialIndex = 0; // 使用するマテリアルスロットのインデックス
        BoundingBox Bounds;         // サブメッシュのローカルバウンディングボックス

        SubMesh() = default;

        SubMesh(uint32_t indexStart, uint32_t indexCount,
                uint32_t vertexStart = 0, uint32_t materialIndex = 0)
            : IndexStart(indexStart), IndexCount(indexCount),
              VertexStart(vertexStart), MaterialIndex(materialIndex)
        {
        }
    };

    // ========================================
    // マテリアルスロット
    // ========================================

    /**
     * @brief マテリアルスロット定義
     *
     * メッシュが使用するマテリアルのスロット情報を定義します。
     * 実際のマテリアルはMeshComponentでオーバーライド可能です。
     */
    struct MaterialSlot
    {
        Container::String Name;         // スロット名（例: "Body", "Face"）
        MaterialHandle DefaultMaterial; // デフォルトマテリアル

        MaterialSlot() = default;

        explicit MaterialSlot(const Container::String &name)
            : Name(name)
        {
        }

        MaterialSlot(const Container::String &name, MaterialHandle material)
            : Name(name), DefaultMaterial(material)
        {
        }
    };

    // ========================================
    // メッシュ作成情報
    // ========================================

    /**
     * @brief メッシュ作成情報
     *
     * MeshResources経由でGPUバッファを作成するための情報。
     * Game側からはこの構造体を通じてのみメッシュを作成できます。
     */
    struct MeshCreateInfo
    {
        // 頂点データ（不透明なポインタ - 内部でコピーされる）
        const void *VertexData = nullptr;
        size_t VertexDataSize = 0;
        uint32_t VertexCount = 0;

        // インデックスデータ（不透明なポインタ - 内部でコピーされる）
        const void *IndexData = nullptr;
        size_t IndexDataSize = 0;
        uint32_t IndexCount = 0;

        // メッシュ設定
        VertexLayout Layout;
        IndexFormat IndexType = IndexFormat::UInt32;
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;

        // サブメッシュ情報
        Container::VariableArray<SubMesh> SubMeshes;

        // マテリアルスロット
        Container::VariableArray<MaterialSlot> MaterialSlots;

        // バウンディング情報
        BoundingBox Bounds;

        // オプション
        bool bKeepCPUData = false;   // CPU側にデータを保持するか
        Container::String DebugName; // デバッグ用名前
    };

    // ========================================
    // メッシュGPUデータ（内部用）
    // ========================================

    /**
     * @brief GPU上のメッシュデータ
     *
     * RenderingシステムがGPUリソースを管理するための内部構造体。
     * Game側からは直接アクセスできません。
     */
    struct MeshGPUData
    {
        BufferHandle VertexBuffer;
        BufferHandle IndexBuffer;

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;

        VertexLayoutHandle LayoutHandle;
        IndexFormat IndexType = IndexFormat::UInt32;
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;

        // サブメッシュ情報のコピー
        Container::VariableArray<SubMesh> SubMeshes;

        // バウンディング情報
        BoundingBox Bounds;
        BoundingSphere BoundingSphere;

        // 参照カウント（リソース管理用）
        uint32_t RefCount = 0;

        // デバッグ情報
        Container::String DebugName;
    };

    // ========================================
    // メッシュ記述子（Game側向け）
    // ========================================

    /**
     * @brief メッシュ記述子
     *
     * Game側がメッシュの情報を参照するための読み取り専用構造体。
     * 実際のGPUリソースへのアクセスは提供しません。
     */
    struct MeshDescriptor
    {
        MeshDataHandle Handle;

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t SubMeshCount = 0;
        uint32_t MaterialSlotCount = 0;

        VertexLayout Layout;
        PrimitiveTopology Topology = PrimitiveTopology::TriangleList;

        BoundingBox Bounds;
        BoundingSphere BoundSphere;

        Container::String Name;

        bool IsValid() const { return Handle.IsValid(); }
    };

    // ========================================
    // LOD情報
    // ========================================

    constexpr uint32_t MAX_MESH_LOD_LEVELS = 4;
    constexpr float DEFAULT_LOD_HYSTERESIS = 0.1f;

    /**
     * @brief 透視投影での境界球の画面の大きさを求める。
     * @return 半径を距離と視野角の半角の正接の積で割った値。
     */
    inline float ComputePerspectiveScreenSize(float radius, float distance, float tanHalfFovY)
    {
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            return 0.0f;
        }
        if (!std::isfinite(distance) || distance <= 0.0f ||
            !std::isfinite(tanHalfFovY) || tanHalfFovY <= 0.0f)
        {
            return std::numeric_limits<float>::max();
        }
        return radius / (distance * tanHalfFovY);
    }

    /**
     * @brief 正射影での境界球の画面の大きさを求める。
     * @return 半径を正射影の高さの半分で割った値。
     */
    inline float ComputeOrthographicScreenSize(float radius, float orthoHeight)
    {
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            return 0.0f;
        }
        if (!std::isfinite(orthoHeight) || orthoHeight <= 0.0f)
        {
            return std::numeric_limits<float>::max();
        }
        return radius / (orthoHeight * 0.5f);
    }

    /**
     * @brief 段の順にしきい値を比べ、画面の大きさに合う最初の段を返す。
     */
    inline uint32_t SelectLODByScreenSize(float screenSize, const float* thresholds, uint32_t count)
    {
        if (count == 0 || thresholds == nullptr)
        {
            return 0;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            if (screenSize >= thresholds[i])
            {
                return i;
            }
        }
        return count - 1;
    }

    /**
     * @brief 前の段を保持する帯を設けて画面の大きさからLODを選ぶ。
     */
    inline uint32_t SelectLODByScreenSizeWithHysteresis(float screenSize, const float* thresholds,
                                                        uint32_t count, uint32_t previousLevel,
                                                        float hysteresis = DEFAULT_LOD_HYSTERESIS)
    {
        if (count <= 1 || thresholds == nullptr)
        {
            return 0;
        }
        // 非数も範囲外の値として扱い、帯の幅を0にする。
        if (!(hysteresis >= 0.0f))
        {
            hysteresis = 0.0f;
        }
        else if (hysteresis > 0.5f)
        {
            hysteresis = 0.5f;
        }
        if (previousLevel >= count)
        {
            previousLevel = count - 1;
        }

        uint32_t lowered = count - 1;
        uint32_t raised = count - 1;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (screenSize >= thresholds[i] * (1.0f - hysteresis))
            {
                lowered = i;
                break;
            }
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            if (screenSize >= thresholds[i] * (1.0f + hysteresis))
            {
                raised = i;
                break;
            }
        }
        if (previousLevel < lowered)
        {
            return lowered;
        }
        if (previousLevel > raised)
        {
            return raised;
        }
        return previousLevel;
    }

    /**
     * @brief 固定段を優先し、指定がなければヒステリシスつきでLODを選ぶ。
     */
    inline uint32_t ResolveMeshLODLevel(float screenSize, const float* thresholds, uint32_t count,
                                        int32_t forcedLevel, uint32_t previousLevel,
                                        float hysteresis = DEFAULT_LOD_HYSTERESIS)
    {
        if (count == 0)
        {
            return 0;
        }
        if (forcedLevel >= 0)
        {
            const uint32_t level = static_cast<uint32_t>(forcedLevel);
            return level < count ? level : count - 1;
        }
        return SelectLODByScreenSizeWithHysteresis(screenSize, thresholds, count, previousLevel, hysteresis);
    }

    /**
     * @brief LODレベル定義
     */
    struct LODLevel
    {
        MeshDataHandle MeshHandle; // このLODレベルのメッシュ
        float ScreenSizeThreshold; // 境界球の半径を画面の縦半分に当たる世界の長さで割ったしきい値
        float TransitionWidth;     // トランジション幅（ディザリング用）
    };

    /**
     * @brief LODグループ
     *
     * 複数のLODレベルを持つメッシュグループ
     */
    struct LODGroup
    {
        Container::VariableArray<LODLevel> Levels;

        /**
         * @brief 画面サイズに基づいて適切なLODレベルを取得
         * @param screenSize 境界球の半径 ÷ (距離 × tan(fov/2))。正射影では半径 ÷ (OrthoHeight/2)
         * @return LODレベルインデックス
         */
        uint32_t GetLODLevel(float screenSize) const
        {
            if (Levels.empty())
            {
                return 0;
            }
            for (uint32_t i = 0; i < static_cast<uint32_t>(Levels.size()); ++i)
            {
                if (screenSize >= Levels[i].ScreenSizeThreshold)
                {
                    return i;
                }
            }
            return static_cast<uint32_t>(Levels.size()) - 1;
        }
    };

    // ========================================
    // メッシュインスタンシング情報
    // ========================================

    /**
     * @brief インスタンスデータレイアウト
     *
     * GPUインスタンシング用のインスタンスごとのデータレイアウト
     */
    struct InstanceDataLayout
    {
        Container::FixedArray<VertexElement, 8> Elements;
        uint32_t ElementCount = 0;
        uint32_t Stride = 0;

        /**
         * @brief 標準インスタンスレイアウト（Transform行列）
         */
        static InstanceDataLayout CreateStandard()
        {
            InstanceDataLayout layout;
            uint32_t offset = 0;

            // ワールド行列（4xFloat4）
            layout.Elements[0] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 0);
            offset += 16;
            layout.Elements[1] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 1);
            offset += 16;
            layout.Elements[2] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 2);
            offset += 16;
            layout.Elements[3] = VertexElement(VertexSemantic::Custom0, VertexFormat::Float4, offset, 1, 3);
            offset += 16;

            layout.ElementCount = 4;
            layout.Stride = offset;
            return layout;
        }
    };

    /**
     * @brief GPUへ送るシーンインスタンスデータ
     *
     * std430のvec4単位で扱えるよう16バイト境界に揃えます。
     * ShadingParams の並びは P0・P1・予約・被覆率の乗数（w）。負は未指定で材質の値を使います。
     */
    struct alignas(16) GPUSceneInstanceData
    {
        float World[16];
        float PreviousWorld[16];
        float NormalMatrix[12];
        float ObjectColor[4];
        float CustomData[4];
        float ShadingParams[4];
    };

    static_assert(alignof(GPUSceneInstanceData) == 16);
    static_assert(sizeof(GPUSceneInstanceData) == sizeof(float) * 56);
    static_assert(sizeof(GPUSceneInstanceData) % 16 == 0);
    static_assert(offsetof(GPUSceneInstanceData, World) == 0);
    static_assert(offsetof(GPUSceneInstanceData, PreviousWorld) == 64);
    static_assert(offsetof(GPUSceneInstanceData, NormalMatrix) == 128);
    static_assert(offsetof(GPUSceneInstanceData, ObjectColor) == 176);
    static_assert(offsetof(GPUSceneInstanceData, CustomData) == 192);
    static_assert(offsetof(GPUSceneInstanceData, ShadingParams) == 208);

} // namespace NorvesLib::Core::Rendering
