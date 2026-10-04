// stateの厳密schema、値所有、数値境界と所有path衝突を検証する。
#include "Tools/AssetCook/CookOwnedState.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace StateTest
{
    using StateText = NorvesLib::Core::Container::AnsiString;
    using StateBytes = NorvesLib::Core::Container::Span<const uint8_t>;
    using namespace NorvesLib::Tools::AssetCook;
    using namespace NorvesLib::Core::Asset;
    StateBytes Bytes(const StateText& text)
    {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }
    CookOwnedState Seed(bool bSkeletal = false)
    {
        CookOwnedState state;
        state.Binding.OwnerId = "0123456789abcdef0123456789abcdef";
        state.Binding.RuntimeRootIdentity = "C:/Game/Assets";
        state.Binding.ManifestName = "manifest.json";
        CookOwnedRecord owned;
        owned.PrimaryKey = {"Models/dog", bSkeletal ? AssetKind::Model : AssetKind::Raw, "default"};
        owned.Record.CookerRevision = 7;
        owned.Record.DependencyFingerprint = 0xfedcba9876543210ull;
        CookRecordedOutput output;
        auto& r = output.Reference;
        r.LogicalPath = owned.PrimaryKey.LogicalPath;
        r.Kind = owned.PrimaryKey.Kind;
        r.Variant = "default";
        r.SourceHash = 0xdeadbeef00000001ull;
        r.CookedHash = 0xffffffffffffffffull;
        r.Format = bSkeletal ? "nvskel.v0.skinned.pnujiw.u32" : "raw.v0";
        r.CookedPackage = "Cooked/dog.nvpkg";
        r.EntryName = "__asset__";
        r.EntryType =
            bSkeletal ? MakeAssetPackageFourCC('S', 'k', 'l', '0') : MakeAssetPackageFourCC('C', 'u', 's', 't');
        output.Package = {256, 0xaabbccddeeff0123ull};
        if (bSkeletal)
        {
            r.bHasSkeletalMetadata = true;
            r.SkeletalMetadata = {3, 3, 2, 1, true, 1, 1};
        }
        owned.Record.Outputs.push_back(output);
        state.Records.push_back(owned);
        return state;
    }
    StateText Json(const CookOwnedState& state)
    {
        StateText json, error;
        CHECK(SerializeCookOwnedState(state, json, error));
        CHECK(error.empty());
        return json;
    }
    StateText Replace(const StateText& original, const char* needle, const char* replacement)
    {
        const auto found = std::strstr(original.c_str(), needle);
        CHECK(found);
        const size_t start = static_cast<size_t>(found - original.c_str()), length = std::strlen(needle);
        StateText result;
        result.append(original.data(), start);
        result.append(replacement);
        result.append(original.data() + start + length, original.size() - start - length);
        return result;
    }
    void Reject(const StateText& json, const CookStateBinding& binding)
    {
        auto held = Seed();
        held.Generation = 123;
        held.Records[0].Record.DependencyFingerprint = 456;
        StateText error;
        CHECK(!ParseCookOwnedState(Bytes(json), binding, held, error));
        CHECK(!error.empty() && held.Generation == 123 && held.Records.size() == 1 &&
              held.Records[0].Record.DependencyFingerprint == 456);
    }
    void BadValue(const CookOwnedState& state)
    {
        StateText output = "held", error;
        CHECK(!SerializeCookOwnedState(state, output, error));
        CHECK(output == "held" && !error.empty());
    }
    void Additional(CookOwnedState& state, const char* logical, const char* package)
    {
        auto copy = state.Records[0];
        copy.PrimaryKey.LogicalPath = logical;
        copy.Record.Outputs[0].Reference.LogicalPath = logical;
        copy.Record.Outputs[0].Reference.CookedPackage = package;
        state.Records.push_back(std::move(copy));
    }
} // namespace StateTest
int main()
{
    using namespace StateTest;
    auto state = Seed();
    const auto binding = state.Binding;
    StateText error;
    CookOwnedState loaded;
    {
        auto json = Json(state);
        CHECK(ParseCookOwnedState(Bytes(json), binding, loaded, error));
        json.clear();
    }
    auto copy = loaded;
    loaded = {};
    auto moved = std::move(copy);
    CHECK(Json(moved) == Json(state));
    const auto* record = FindCookOwnedRecord(moved, state.Records[0].PrimaryKey);
    CHECK(record && record->Outputs.size() == 1);
    auto key = state.Records[0].PrimaryKey;
    key.Variant = "other";
    CHECK(!FindCookOwnedRecord(moved, key));
    const uint64_t numbers[] = {0, (uint64_t{1} << 53) - 1, uint64_t{1} << 53, (uint64_t{1} << 53) + 1, UINT64_MAX};
    for (const auto n : numbers)
    {
        auto value = state;
        value.Generation = n ? n : 1;
        auto& r = value.Records[0].Record;
        r.CookerRevision = n;
        r.DependencyFingerprint = n;
        r.Outputs[0].Reference.SourceHash = n;
        r.Outputs[0].Reference.CookedHash = n;
        r.Outputs[0].Package.ContentHash = n;
        r.Outputs[0].Package.Size = n >= 256 ? n : 256;
        const auto json = Json(value);
        CHECK(ParseCookOwnedState(Bytes(json), binding, loaded, error));
        CHECK(Json(loaded) == json);
        CHECK(loaded.Records[0].Record.CookerRevision == n &&
              loaded.Records[0].Record.Outputs[0].Reference.CookedHash == n);
    }
    state = Seed(true);
    const auto json = Json(state);
    CHECK(ParseCookOwnedState(Bytes(json), binding, loaded, error));
    CHECK(Json(loaded) == json);
    // 個々の階層で不明・重複keyを拒否する。既存runtime manifestの寛容なparserは使わない。
    const char* needles[] = {"\"schema\":1", "\"primary\":", "\"source_hash\":", "\"logical\":", "\"vertices\":"};
    const char* replacements[] = {"\"unknown\":1", "\"unknown\":", "\"unknown\":", "\"unknown\":", "\"unknown\":"};
    for (size_t i = 0; i < 5; ++i)
    {
        Reject(Replace(json, needles[i], replacements[i]), binding);
    }
    Reject(Replace(json, "\"schema\":1", "\"schema\":1,\"schema\":1"), binding);
    Reject(Replace(json, "\"revision\":", "\"dependency_hash\":"), binding);
    Reject(Replace(json, "\"source_hash\":", "\"cooked_hash\":"), binding);
    Reject(Replace(json, "\"logical\":\"Models/dog\"", "\"variant\":\"Models/dog\""), binding);
    Reject(Replace(json, "\"vertices\":3", "\"indices\":3"), binding);
    Reject(Replace(json, "\"schema\":1", "\"schema\":1.0"), binding);
    Reject(Replace(json, "\"schema\":1", "\"schema\":1e0"), binding);
    Reject(Replace(json, "\"cooked_version\":0", "\"cooked_version\":-0"), binding);
    Reject(Replace(json, "\"cooked_version\":0", "\"cooked_version\":4294967296"), binding);
    Reject(Replace(json, "\"cooked_version\":0", "\"cooked_version\":null"), binding);
    for (const char* invalid :
         {"\"000000000000000G\"", "\"000000000000000A\"", "\"1\"", "\"00000000000000001\"", "1", "-1", "null"})
    {
        Reject(Replace(json, "\"0000000000000007\"", invalid), binding);
    }
    Reject(Replace(json, "Cooked/dog.nvpkg", "Cooked/dog\\u0000.nvpkg"), binding);
    Reject(Replace(json, "Cooked/dog.nvpkg", "Cooked/\\u72ac.nvpkg"), binding);
    Reject(Replace(json, "Cooked/dog.nvpkg", "Cooked/\\ud800.nvpkg"), binding);
    auto invalidUtf8 = json;
    invalidUtf8[0] = static_cast<char>(0xff);
    Reject(invalidUtf8, binding);
    auto wrong = binding;
    wrong.OwnerId = "1123456789abcdef0123456789abcdef";
    Reject(json, wrong);
    wrong = binding;
    wrong.RuntimeRootIdentity = "D:/Other/Assets";
    Reject(json, wrong);
    wrong = binding;
    wrong.ManifestName = "other.json";
    Reject(json, wrong);
    for (const char* invalid : {"../escape", "Cooked/./a", "Cooked/../a", "Assets/a", "C:/a", "/a", "a//b", "a\\b",
                                "a:stream", "CON.nvpkg", "a.", "a ", "x?/a"})
    {
        auto bad = state;
        bad.Records[0].Record.Outputs[0].Reference.CookedPackage = invalid;
        BadValue(bad);
        // serializerを通らない不正保存byteも同じ安全境界で拒否する。
        if (!std::strchr(invalid, '\\'))
        {
            Reject(Replace(json, "Cooked/dog.nvpkg", invalid), binding);
        }
    }
    auto bad = state;
    bad.Records[0].PrimaryKey.LogicalPath = "other";
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.Outputs.clear();
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.SchemaVersion = 2;
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.DependencySchemaVersion = 2;
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.Outputs[0].Reference.EntryType = 0;
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.Outputs[0].Package.Size = 0;
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.Outputs[0].Reference.bHasSkeletalMetadata = false;
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.Outputs[0].Reference.SkeletalMetadata.bHasSubmeshCounts = false;
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.Outputs[0].Reference.SkeletalMetadata.JointCount = 0;
    BadValue(bad);
    bad = state;
    bad.Generation = 0;
    BadValue(bad);
    bad = state;
    bad.Binding.OwnerId = "00000000000000000000000000000000";
    BadValue(bad);
    bad = state;
    bad.Binding.ManifestName = "Cooked/dog.nvpkg";
    BadValue(bad);
    bad = state;
    bad.Binding.ManifestName = "Cooked/dog.nvpkg/m.json";
    BadValue(bad);
    bad = state;
    Additional(bad, "other", "cOOked/DOG.nvpkg");
    BadValue(bad);
    bad = state;
    Additional(bad, "Models/dog", "Cooked/other.nvpkg");
    BadValue(bad);
    bad = state;
    bad.Records[0].Record.Outputs[0].Reference.CookedPackage = "a";
    Additional(bad, "other", "a!");
    Additional(bad, "third", "a/x");
    BadValue(bad);
    bad = state;
    bad.Records[0].PrimaryKey.Variant = "a|b";
    bad.Records[0].Record.Outputs[0].Reference.Variant = "a|b";
    Additional(bad, "Models/other", "Cooked/other.nvpkg");
    CHECK(ParseCookOwnedState(Bytes(Json(bad)), binding, loaded, error));
    // 区切り連結なら衝突し得る組も、keyの各fieldを独立に照合する。
    auto tuples = Seed();
    tuples.Records[0].PrimaryKey.LogicalPath = "a";
    tuples.Records[0].PrimaryKey.Variant = "b/c";
    tuples.Records[0].Record.Outputs[0].Reference.LogicalPath = "a";
    tuples.Records[0].Record.Outputs[0].Reference.Variant = "b/c";
    auto tupleCopy = tuples.Records[0];
    tupleCopy.PrimaryKey.LogicalPath = "a/b";
    tupleCopy.PrimaryKey.Variant = "c";
    // logicalに禁止記号を増やさず、variantを含む実際の異なるkeyを往復する。
    tupleCopy.Record.Outputs[0].Reference.LogicalPath = tupleCopy.PrimaryKey.LogicalPath;
    tupleCopy.Record.Outputs[0].Reference.Variant = "c";
    tupleCopy.Record.Outputs[0].Reference.CookedPackage = "Cooked/second.nvpkg";
    tuples.Records.push_back(tupleCopy);
    CHECK(ParseCookOwnedState(Bytes(Json(tuples)), binding, loaded, error));
    CHECK(FindCookOwnedRecord(loaded, tuples.Records[0].PrimaryKey) &&
          FindCookOwnedRecord(loaded, tuples.Records[1].PrimaryKey));
    auto escaped = Seed();
    escaped.Records[0].Record.Outputs[0].Reference.Format = "custom \"format\" \\";
    CHECK(ParseCookOwnedState(Bytes(Json(escaped)), binding, loaded, error));
    CHECK(Json(loaded) == Json(escaped));
    auto empty = Seed();
    empty.Records.clear();
    CHECK(ParseCookOwnedState(Bytes(Json(empty)), binding, loaded, error));
    CHECK(loaded.Records.empty());
    StateText deep;
    for (size_t i = 0; i < 17; ++i)
    {
        deep.push_back('[');
    }
    for (size_t i = 0; i < 17; ++i)
    {
        deep.push_back(']');
    }
    Reject(deep, binding);
    StateText oversized;
    oversized.reserve(MaximumCookStateBytes + 1);
    for (size_t i = 0; i <= MaximumCookStateBytes; ++i)
    {
        oversized.push_back(' ');
    }
    Reject(oversized, binding);
    bad = state;
    auto& format = bad.Records[0].Record.Outputs[0].Reference.Format;
    format.clear();
    format.reserve(MaximumCookStateStringBytes + 1);
    for (size_t i = 0; i <= MaximumCookStateStringBytes; ++i)
    {
        format.push_back('x');
    }
    BadValue(bad);
    bad = state;
    const auto sample = bad.Records[0];
    bad.Records.reserve(MaximumCookStateRecords + 1);
    while (bad.Records.size() <= MaximumCookStateRecords)
    {
        bad.Records.push_back(sample);
    }
    BadValue(bad);
    bad = state;
    const auto outputSample = bad.Records[0].Record.Outputs[0];
    auto& outputs = bad.Records[0].Record.Outputs;
    outputs.reserve(MaximumCookStateOutputs + 1);
    while (outputs.size() <= MaximumCookStateOutputs)
    {
        outputs.push_back(outputSample);
    }
    BadValue(bad);
    std::puts("COOK_OWNED_STATE result=pass strict_schema_exact_u64_owned_scope_alias_limits_hold");
    return 0;
}
