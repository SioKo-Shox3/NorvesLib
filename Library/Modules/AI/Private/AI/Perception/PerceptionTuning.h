// 知覚モデルの調整値と、JSON から読み込むための窓口。
#pragma once

#include "AI/Perception/AwarenessIntegrator.h"
#include "AI/Perception/HearingModel.h"
#include "AI/Perception/SmellModel.h"
#include "AI/Perception/VisionModel.h"
#include "Container/String.h"
#include "Container/StringView.h"

namespace NorvesLib::Modules::AI::Perception
{
    struct PerceptionTuning
    {
        VisionTuning Vision;
        HearingTuning Hearing;
        SmellTuning Smell;
        AwarenessTuning Awareness;
    };

    struct PerceptionTuningLoadResult
    {
        bool bSucceeded = false;
        Core::Container::String Detail; ///< 失敗の理由と対象のキー。
        bool Succeeded() const { return bSucceeded; }
    };

    /// JSON を既定値に重ねる。失敗したら outTuning を保持し、理由を返す。
    PerceptionTuningLoadResult ParsePerceptionTuningJson(const Core::Container::String& json, PerceptionTuning& outTuning);

    /// アセット相対パスから読み込む。失敗したら outTuning を保持する。
    PerceptionTuningLoadResult LoadPerceptionTuningFromAssetFile(Core::Container::AnsiStringView path, PerceptionTuning& outTuning);

    /// 知覚モデルの既定の設定ファイルを読み込む。
    inline PerceptionTuningLoadResult LoadPerceptionTuningFromAssetFile(PerceptionTuning& outTuning)
    {
        return LoadPerceptionTuningFromAssetFile("AI/PerceptionTuning.json", outTuning);
    }
}
