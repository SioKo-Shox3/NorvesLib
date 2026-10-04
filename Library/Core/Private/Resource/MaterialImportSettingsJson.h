// 材質設定blockの厳密なJSON解析。素材の選択規則とは分離する。
#pragma once
#include "Resource/ImportSettings.h"
#include "Resource/MaterialImportSettings.h"
namespace NorvesLib::Core::AssetImport
{
    // 省略は既定、null/未知/重複fieldは拒否。成功時だけ全出力を置換する。
    // 資産blockだけprofileを受ける。素材blockはprofileを上書きできない。
    [[nodiscard]] SettingsResult ParseAssetMaterialSettings(const JsonValue& block,
        MaterialSourceProfile& outProfile, MaterialSettingsLayer& outLayer);
    [[nodiscard]] SettingsResult ParseMaterialSettingsLayer(const JsonValue& block,
        MaterialSettingsLayer& outLayer);
} // namespace NorvesLib::Core::AssetImport
