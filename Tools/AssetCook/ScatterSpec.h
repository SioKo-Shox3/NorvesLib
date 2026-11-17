// 散布スペックと参照地形を読み、NVSCT v0 に焼く。
#pragma once

#include "ScatterBaker.h"
#include "Text/JsonDocument.h"
#include <filesystem>
#include <initializer_list>

namespace NorvesLib::Tools::AssetCook
{
    struct ScatterSpecCookResult
    {
        Core::Container::VariableArray<uint8_t> Bytes;
        Core::Container::VariableArray<Core::Container::AnsiString> SpeciesNames;
        ScatterBakeStats Stats;
        uint64_t SourceHash = 0;
        uint64_t PayloadHash = 0;
        uint64_t InstanceCount = 0;
        uint32_t ChunkCount = 0;
    };

    // 両入力形式で同じ文字列・地形・ハッシュの扱いを使う。
    namespace ScatterSpecInput
    {
        bool KeyEquals(const Core::Container::String& name, const char* key);
        bool Text(Core::JsonValue value, Core::Container::AnsiString& out, Core::Container::AnsiString& error);
        bool Keys(Core::JsonValue object, std::initializer_list<const char*> allowed, Core::Container::AnsiString& error);
        bool UInt64(Core::JsonValue value, uint64_t& out, Core::Container::AnsiString& error);
        bool SkyOccluder(Core::JsonValue value, ScatterSpeciesRule& rule, Core::Container::AnsiString& error);
        bool LoadTerrain(const std::filesystem::path& path, const Core::Container::AnsiString& terrainPathText,
                         uint64_t recordedHash, Core::Asset::CookedTerrainData& terrain, uint64_t& cookedHash,
                         Core::Container::VariableArray<std::filesystem::path>& inputs, Core::Container::AnsiString& error,
                         bool* bMismatch = nullptr);
        bool SourceHash(Core::Container::Span<const uint8_t> specBytes, uint64_t cookedHash,
                        const Core::Container::VariableArray<std::filesystem::path>& inputs,
                        uint64_t& sourceHash, Core::Container::AnsiString& error);
    } // namespace ScatterSpecInput

    // 参照地形の cooked_hash が記録と異なれば、散布を焼く前に失敗する。
    [[nodiscard]] bool CookScatterSpecFile(const std::filesystem::path& path, ScatterSpecCookResult& out,
                                            Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
