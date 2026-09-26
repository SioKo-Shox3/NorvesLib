#pragma once

#include "RHITypes.h"
#include "IDescriptorSet.h"
#include "IShader.h"

namespace NorvesLib::RHI
{

    /**
     * @brief レイトレーシングshader groupの種類
     */
    enum class RayTracingShaderGroupType : uint8_t
    {
        General,
        TrianglesHit,
        ProceduralHit
    };

    /**
     * @brief レイトレーシングpipeline内のshader group
     */
    struct RayTracingShaderGroupDesc
    {
        RayTracingShaderGroupType type = RayTracingShaderGroupType::General;
        ShaderPtr generalShader;
        ShaderPtr closestHitShader;
        ShaderPtr anyHitShader;
        ShaderPtr intersectionShader;
    };

    /**
     * @brief レイトレーシングpipeline作成情報
     */
    struct RayTracingPipelineDesc
    {
        NorvesLib::Core::Container::VariableArray<RayTracingShaderGroupDesc> shaderGroups;
        NorvesLib::Core::Container::VariableArray<DescriptorSetDesc> descriptorSetLayouts;
        uint32_t maxPipelineRayRecursionDepth = 1;
    };

    /**
     * @brief レイトレーシングpipelineのshader group構成を検証
     */
    inline bool IsValidRayTracingPipelineDesc(const RayTracingPipelineDesc& desc)
    {
        if (desc.maxPipelineRayRecursionDepth == 0 || desc.shaderGroups.empty() ||
            desc.shaderGroups.size() > UINT32_MAX)
        {
            return false;
        }

        uint32_t rayGenerationGroupCount = 0;
        uint32_t missGroupCount = 0;
        uint32_t closestHitGroupCount = 0;
        for (const RayTracingShaderGroupDesc& group : desc.shaderGroups)
        {
            switch (group.type)
            {
            case RayTracingShaderGroupType::General:
                if (!group.generalShader || group.closestHitShader || group.anyHitShader ||
                    group.intersectionShader)
                {
                    return false;
                }
                switch (group.generalShader->GetStage())
                {
                case ShaderStage::RayGen:
                    ++rayGenerationGroupCount;
                    break;
                case ShaderStage::Miss:
                    ++missGroupCount;
                    break;
                case ShaderStage::Callable:
                    break;
                default:
                    return false;
                }
                break;
            case RayTracingShaderGroupType::TrianglesHit:
                if (group.generalShader || group.intersectionShader ||
                    (!group.closestHitShader && !group.anyHitShader) ||
                    (group.closestHitShader && group.closestHitShader->GetStage() != ShaderStage::ClosestHit) ||
                    (group.anyHitShader && group.anyHitShader->GetStage() != ShaderStage::AnyHit))
                {
                    return false;
                }
                if (group.closestHitShader)
                {
                    ++closestHitGroupCount;
                }
                break;
            case RayTracingShaderGroupType::ProceduralHit:
                if (group.generalShader || !group.intersectionShader ||
                    group.intersectionShader->GetStage() != ShaderStage::Intersection ||
                    (group.closestHitShader && group.closestHitShader->GetStage() != ShaderStage::ClosestHit) ||
                    (group.anyHitShader && group.anyHitShader->GetStage() != ShaderStage::AnyHit))
                {
                    return false;
                }
                if (group.closestHitShader)
                {
                    ++closestHitGroupCount;
                }
                break;
            default:
                return false;
            }
        }

        return rayGenerationGroupCount == 1 && missGroupCount > 0 && closestHitGroupCount > 0;
    }

    /**
     * @brief パイプラインインターフェース
     * パイプラインはシェーダー、ステート、入出力レイアウト等を含むレンダリングの設定です。
     */
    class IPipeline
    {
    public:
        virtual ~IPipeline() = default;

        /**
         * @brief パイプラインタイプを取得
         * @return パイプラインタイプ
         */
        virtual PipelineType GetPipelineType() const = 0;

        /**
         * @brief パイプラインがコンピュートパイプラインかどうか
         * @return コンピュートパイプラインの場合true、グラフィックパイプラインの場合false
         */
        virtual bool IsComputePipeline() const
        {
            return GetPipelineType() == PipelineType::Compute;
        }

        /**
         * @brief パイプラインがレイトレーシングパイプラインかどうか
         */
        virtual bool IsRayTracingPipeline() const
        {
            return GetPipelineType() == PipelineType::RayTracing;
        }

        /**
         * @brief バインドポイント数の取得
         * @return バインドポイント（ディスクリプタセット）の数
         */
        virtual uint32_t GetBindPointCount() const = 0;
    };

} // namespace NorvesLib::RHI
