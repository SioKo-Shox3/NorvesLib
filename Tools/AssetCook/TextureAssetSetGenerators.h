#pragma once

// asset-set の generator を宣言順に増分実行し、記録と実行結果を返す。
#include "TextureAssetSetSpec.h"
#include "CookOwnedState.h"
#include "CookBatchReport.h"
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    struct TextureAssetSetGeneratorRunRequest
    {
        // ResolveTextureCliPath 済みの絶対 path。
        std::filesystem::path SourceRoot, RuntimeRoot, SpecPath;
        Core::Container::Span<const TextureAssetSetGenerator> Generators; // spec の宣言順
        Core::Container::Span<const CookGeneratorRecord> Previous; // 前回の記録。無ければ空
    };
    struct TextureAssetSetGeneratorRunResult
    {
        // 全部成功したときの、宣言順の新しい記録。
        Core::Container::VariableArray<CookGeneratorRecord> Records;
        // 失敗時も全件を返す。走らなかった残りは not_run。
        Core::Container::VariableArray<CookGeneratorReportRow> Rows;
    };
    // 宣言順に判断して走らせる。失敗時は error に名前と終了コードまたは理由を返す。
    [[nodiscard]] bool RunTextureAssetSetGenerators(const TextureAssetSetGeneratorRunRequest& request,
                                                   TextureAssetSetGeneratorRunResult& out,
                                                   Core::Container::AnsiString& error);
} // 名前空間 NorvesLib::Tools::AssetCook
