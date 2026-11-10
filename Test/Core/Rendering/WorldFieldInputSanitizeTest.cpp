// 世界フィールド入力の不正値の除去と安全な範囲への丸めを確かめる。
#include "Rendering/WorldFieldTypes.h"

#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

int main()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const float invalidValues[] = {nan, infinity, -infinity};
    WorldFieldInteractor valid;
    valid.Radius = 1.0f;
    valid.PrevRadius = 2.0f;
    Container::VariableArray<WorldFieldInteractor> output;

    // 現在と前回の両端点の全軸が、非有限値を通さない契約を確かめる。
    for (float invalid : invalidValues)
    {
        for (uint32_t coordinate = 0; coordinate < 12; ++coordinate)
        {
            WorldFieldInteractor candidate = valid;
            float* points[] = {candidate.P0, candidate.P1, candidate.PrevP0, candidate.PrevP1};
            points[coordinate / 3][coordinate % 3] = invalid;
            output.push_back(valid);
            if (SanitizeWorldFieldInteractors({&candidate, 1}, output) != 0 || !output.empty())
            {
                std::cout << "失敗: 非有限の座標が残ったか、以前の出力が消えていません\n";
                return 1;
            }
        }
        for (uint32_t scalar = 0; scalar < 3; ++scalar)
        {
            WorldFieldInteractor candidate = valid;
            float* scalars[] = {&candidate.Radius, &candidate.PrevRadius, &candidate.Strength};
            *scalars[scalar] = invalid;
            if (SanitizeWorldFieldInteractors({&candidate, 1}, output) != 0)
            {
                std::cout << "失敗: 非有限の半径または強さが残っています\n";
                return 1;
            }
        }
        WorldFieldSettings settings;
        settings.DecayTimeConstantSeconds = invalid;
        settings.ForwardOffsetMeters = invalid;
        const auto sanitized = SanitizeWorldFieldSettings(settings);
        if (sanitized.DecayTimeConstantSeconds != 1.5f || sanitized.ForwardOffsetMeters != 10.0f)
        {
            std::cout << "失敗: 非有限の設定値が既定値に戻りません\n";
            return 1;
        }
    }

    WorldFieldInteractor candidates[6] = {valid, valid, valid, valid, valid, valid};
    candidates[0].Radius = -1.0f;
    candidates[0].Strength = 2.0f;
    candidates[0].ChannelMask = 0xffffffffu;
    candidates[1].Radius = 100.0f;
    candidates[1].PrevRadius = -1.0f;
    candidates[2].PrevRadius = 100.0f;
    candidates[3].Radius = -1.0f;
    candidates[3].PrevRadius = -1.0f;
    candidates[4].Strength = -1.0f;
    candidates[5].Strength = 0.0f;
    if (SanitizeWorldFieldInteractors(candidates, output) != 3 || output.size() != 3 ||
        output[0].Radius != 0.0f || output[0].PrevRadius != 2.0f || output[0].Strength != 1.0f ||
        output[0].ChannelMask != WorldFieldChannelAll || output[1].Radius != 50.0f ||
        output[1].PrevRadius != 0.0f || output[2].PrevRadius != 50.0f ||
        valid.ChannelMask != WorldFieldChannelAll || candidates[0].Radius != -1.0f)
    {
        std::cout << "失敗: 半径・強さ・チャンネルの丸め、無効入力の除去が契約と違います\n";
        return 1;
    }

    WorldFieldInteractor many[40];
    for (uint32_t index = 0; index < 40; ++index)
    {
        many[index] = valid;
        many[index].P0[0] = static_cast<float>(index);
    }
    if (SanitizeWorldFieldInteractors(many, output) != 32 || output.size() != 32 || output.back().P0[0] != 31.0f)
    {
        std::cout << "失敗: 入力40件が先頭32件に制限されません\n";
        return 1;
    }
    many[0].Radius = infinity;
    if (SanitizeWorldFieldInteractors(many, output) != 31 || output.back().P0[0] != 31.0f ||
        SanitizeWorldFieldInteractors({output.data(), output.size()}, output) != 31 ||
        output.front().P0[0] != 1.0f)
    {
        std::cout << "失敗: 33件目以降の除外または自己参照入力の処理が違います\n";
        return 1;
    }
    if (SanitizeWorldFieldInteractors({}, output) != 0 || !output.empty())
    {
        std::cout << "失敗: 空入力で出力が空になりません\n";
        return 1;
    }

    WorldFieldSettings settings;
    settings.bEnabled = false;
    settings.Range = static_cast<WorldFieldRange>(255);
    settings.CenterMode = static_cast<WorldFieldCenterMode>(255);
    settings.DecayTimeConstantSeconds = -1.0f;
    settings.ForwardOffsetMeters = -1.0f;
    auto sanitized = SanitizeWorldFieldSettings(settings);
    if (sanitized.bEnabled || sanitized.Range != WorldFieldRange::Range64m ||
        sanitized.CenterMode != WorldFieldCenterMode::Camera || sanitized.DecayTimeConstantSeconds != 0.05f ||
        sanitized.ForwardOffsetMeters != 0.0f)
    {
        std::cout << "失敗: 未知の列挙値または設定値の下限が無害化されません\n";
        return 1;
    }
    settings.Range = WorldFieldRange::Range128m;
    settings.CenterMode = WorldFieldCenterMode::CameraForward;
    settings.DecayTimeConstantSeconds = 100.0f;
    settings.ForwardOffsetMeters = 200.0f;
    sanitized = SanitizeWorldFieldSettings(settings);
    if (sanitized.Range != settings.Range || sanitized.CenterMode != settings.CenterMode ||
        sanitized.DecayTimeConstantSeconds != 60.0f || sanitized.ForwardOffsetMeters != 100.0f ||
        GetWorldFieldExtentMeters(WorldFieldRange::Range64m) != 64.0f ||
        GetWorldFieldTexelSizeMeters(WorldFieldRange::Range64m) != 0.25f ||
        GetWorldFieldExtentMeters(WorldFieldRange::Range128m) != 128.0f ||
        GetWorldFieldTexelSizeMeters(WorldFieldRange::Range128m) != 0.5f)
    {
        std::cout << "失敗: 設定値の上限または範囲と画素の大きさが違います\n";
        return 1;
    }
    std::cout << "WorldFieldInputSanitizeTest: 不正値の除去・丸め・件数制限を確認しました\n";
    return 0;
}
