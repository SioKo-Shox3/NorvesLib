#pragma once

#include "Container/Span.h"
#include "Resource/SkeletalImportOptions.h"
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    struct SkinInfluence
    {
        uint32_t JointIndex = 0;
        double Weight = 0;
    };
    struct InfluenceReductionOptions
    {
        double MinimumWeight = 1e-6;
        double InputWeightSumTolerance = 0.001;
        double WarnDroppedWeight = DefaultWarnDroppedWeight;
        double FailDroppedWeight = DefaultFailDroppedWeight;
    };
    struct ReducedSkinInfluences
    {
        uint32_t Joints[4] = {};
        float Weights[4] = {};
    };
    enum class InfluenceReductionStatus
    {
        Success, InvalidInput, InvalidOptions, InvalidJoint, InvalidWeight, InvalidWeightSum,
        InsufficientWorkspace, OverlappingStorage, AllWeightsRemoved, DroppedWeightExceeded, Unrepresentable
    };
    struct InfluenceReductionOutcome
    {
        InfluenceReductionStatus Status = InfluenceReductionStatus::InvalidInput;
        uint32_t OriginalNonzeroCount = 0;
        uint32_t UniqueNonzeroCount = 0;
        uint32_t KeptCount = 0;
        double DroppedWeight = 0;
        bool bWarning = false;
        bool bReduced = false;
        bool bRenormalized = false;
    };
    // 正規化された数値影響を同一jointでまとめ、重み降順・同値joint番号順で4本へ縮約する。
    // glTF整数weightのraw総和/セット構造/形式の検証は呼出側で別途必須。
    // 入力と全workspace Spanと出力は非重複。設定も書換え領域と重複しないこと。入力は不変、失敗時outは保持する。
    // workspaceはinput数以上。検証後は変更する。脱落量は元の総和に対する比率で、
    // MinimumWeight以下と上位4本外の全量を含む。許容超過時も脱落量等を返す。
    [[nodiscard]] InfluenceReductionOutcome ReduceSkinInfluences(Container::Span<const SkinInfluence> input,
        uint32_t jointCount, const InfluenceReductionOptions& options, Container::Span<SkinInfluence> workspace,
        ReducedSkinInfluences& out) noexcept;
} // namespace NorvesLib::Core::Skeletal
