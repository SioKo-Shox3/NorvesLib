#pragma once
#include "CookCacheDecision.h"
namespace NorvesLib::Tools::AssetCook
{
    struct CookStateKey
    {
        Core::Container::AnsiString LogicalPath;
        Core::Asset::AssetKind Kind = Core::Asset::AssetKind::Unknown;
        Core::Container::AnsiString Variant;
    };
    struct CookStateBinding
    {
        // 識別子であり、改ざん防止/認証ではない。root文字列は比較専用でI/Oへ戻さない。
        Core::Container::AnsiString OwnerId, RuntimeRootIdentity, ManifestName;
    };
    struct CookOwnedRecord
    {
        CookStateKey PrimaryKey;
        CookOutputRecord Record;
    };
    struct CookOwnedState
    {
        uint32_t SchemaVersion = 1;
        CookStateBinding Binding;
        uint64_t Generation = 1;
        Core::Container::VariableArray<CookOwnedRecord> Records;
    };
    inline constexpr size_t MaximumCookStateBytes = 16 * 1024 * 1024;
    inline constexpr size_t MaximumCookStateRecords = 4096;
    inline constexpr size_t MaximumCookStateOutputs = 16384;
    inline constexpr size_t MaximumCookStateStringBytes = 4096;
    // 全fieldを値所有する。expectedは現在のcaller設定であり、保存JSONから組み立てない。
    // 失敗時outは保持。fileを開かず、成功は既存出力の採用/上書き許可を意味しない。
    [[nodiscard]] bool ParseCookOwnedState(Core::Container::Span<const uint8_t> bytes, const CookStateBinding& expected,
                                           CookOwnedState& out, Core::Container::AnsiString& error);
    [[nodiscard]] bool SerializeCookOwnedState(const CookOwnedState& state, Core::Container::AnsiString& outJson,
                                               Core::Container::AnsiString& error);
    // 戻り値の借用寿命はstateまで。複合keyを区切り文字で連結しない。
    [[nodiscard]] const CookOutputRecord* FindCookOwnedRecord(const CookOwnedState& state, const CookStateKey& key);
} // namespace NorvesLib::Tools::AssetCook
