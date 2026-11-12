// 実cook出力の増分判断と、現在要求から独立に導く出力一覧・安全境界を検証する。
#include "Tools/AssetCook/CookCacheDecision.h"
#include "Tools/AssetCook/CookOutputPlan.h"
#include "Tools/AssetCook/CookOutputSetGuard.h"
#include "Tools/AssetCook/CookOwnedState.h"
#include "Tools/AssetCook/CookStateFile.h"
#include "Tools/AssetCook/CookManagedUpdateInventory.h"
#include "Tools/AssetCook/CookCacheDecisionTestAccess.h"
#include "Tools/AssetCook/AssetCookLegacyOptions.h"
#include "Tools/AssetCook/CookOutputPaths.h"
#include "Tools/AssetCook/CookerRevisions.h"
#include "Text/JsonDocument.h"
#include "M9LooseFixture.h"
#include "ClipBankV1Fixture.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
#include "CookManagedIntentFixture.h"
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Tools::AssetCook;
namespace
{
    constexpr size_t AnimationSourceLabelBytes = 160;
    // CookedClipBankV1.cpp の W64(candidate, 48, ...) が書く bank の payload hash の位置。
    constexpr size_t ClipBankPayloadHashOffset = 48;
    using Bytes = Container::VariableArray<uint8_t>;
    using Text = Container::AnsiString;
    Bytes Read(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        CHECK(f);
        Bytes b;
        char c;
        while (f.get(c))
        {
            b.push_back(static_cast<uint8_t>(c));
        }
        CHECK(f.eof());
        return b;
    }
    void Write(const std::filesystem::path& path, Container::Span<const uint8_t> b)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        CHECK(f);
        if (!b.empty())
        {
            f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        f.close();
        CHECK(!f.fail());
    }
    void Put(Bytes& b, size_t offset, uint64_t value, size_t count)
    {
        CHECK(offset + count <= b.size());
        for (size_t i = 0; i < count; ++i)
        {
            b[offset + i] = static_cast<uint8_t>(value >> (8 * i));
        }
    }
    uint64_t Get(const Bytes& b, size_t offset, size_t count)
    {
        CHECK(offset + count <= b.size());
        uint64_t v = 0;
        for (size_t i = 0; i < count; ++i)
        {
            v |= static_cast<uint64_t>(b[offset + i]) << (8 * i);
        }
        return v;
    }
    void WriteText(const std::filesystem::path& path, const char* text)
    {
        Write(path, {reinterpret_cast<const uint8_t*>(text), std::strlen(text)});
    }
    AssetManifest Manifest(Container::Span<const uint8_t> bytes)
    {
        Container::String text;
        text.reserve(bytes.size());
        for (auto b : bytes)
        {
            text.push_back(static_cast<Container::String::value_type>(b));
        }
        AssetManifest m;
        CHECK(m.LoadFromJsonText(text));
        return m;
    }
    AssetManifest Load(const std::filesystem::path& path)
    {
        return Manifest(Read(path));
    }
    Bytes SkeletalBuffer()
    {
        const auto wf = [](Bytes& b, size_t o, float v)
        {
            Put(b, o, std::bit_cast<uint32_t>(v), 4);
        };
        const auto ws = [](Bytes& b, size_t o, uint16_t v)
        {
            Put(b, o, v, 2);
        };
        const auto wm = [&](Bytes& b, size_t o, float y)
        {
            for (size_t d : {0, 5, 10, 15})
            {
                wf(b, o + d * 4, 1);
            }
            wf(b, o + 13 * 4, y);
        };
        return NorvesLib::Tests::AssetFixtures::BuildM9LooseBuffer<Bytes>(wf, ws, wm);
    }
    struct Fixture
    {
        SingleAssetCookRequest Request;
        CookDecisionContext Before;
        CookOutputRecord Record;
        AssetManifest Live;
    };
    using Hashes = Container::VariableArray<Text>;
    Text HashList(const Hashes& hashes)
    {
        Text result = "[";
        for (size_t i = 0; i < hashes.size(); ++i)
        {
            if (i != 0)
            {
                result.append(", ");
            }
            result.append("\"");
            result.append(hashes[i]);
            result.append("\"");
        }
        result.append("]");
        return result;
    }
    Hashes CookedHashes(const Fixture& fixture)
    {
        Hashes hashes;
        for (const auto& output : fixture.Record.Outputs)
        {
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(output.Reference.CookedHash));
            hashes.push_back(hex);
        }
        return hashes;
    }
    Hashes MaskedAnimationHashes(const Fixture& fixture)
    {
        auto bytes = Read(fixture.Request.PackagePath);
        const auto label = std::filesystem::absolute(fixture.Request.InputPath).lexically_normal().generic_u8string();
        CHECK(label.size() == AnimationSourceLabelBytes);
        // cook が入力の絶対パスを rig snapshot の Label に書くため、置き場所で hash が変わる。
        // ラベルの長さをそろえ、パッケージ内の同じバイト列をすべて伏せて照合する。
        size_t replacements = 0;
        for (size_t i = 0; i + label.size() <= bytes.size();)
        {
            if (std::memcmp(bytes.data() + i, label.data(), label.size()) == 0)
            {
                std::memset(bytes.data() + i, 'X', label.size());
                ++replacements;
                i += label.size();
            }
            else
            {
                ++i;
            }
        }
        if (replacements == 0)
        {
            std::fprintf(stderr, "cooker の出力が変わった。animation の入力の絶対パスが出力にない。版を上げて表を更新する\n");
            CHECK(false);
        }
        namespace PackageFormat = AssetPackageFormatV1;
        const auto requireRange = [&](uint64_t offset, uint64_t count, const char* field)
        {
            if (offset > bytes.size() || count > bytes.size() - offset)
            {
                std::fprintf(stderr, "animation の %s の位置または長さがパッケージの範囲外\n", field);
                CHECK(false);
            }
        };
        requireRange(PackageFormat::HeaderOffset::EntryTableOffset, 8, "EntryTableOffset");
        requireRange(PackageFormat::HeaderOffset::EntryCount, 4, "EntryCount");
        const auto tableOffset = Get(bytes, PackageFormat::HeaderOffset::EntryTableOffset, 8);
        const auto entryCount = Get(bytes, PackageFormat::HeaderOffset::EntryCount, 4);
        requireRange(tableOffset, entryCount * PackageFormat::EntryRecordSize, "entry table");
        if (entryCount != 1)
        {
            std::fprintf(stderr, "animation の fixture のパッケージには entry が1つ必要\n");
            CHECK(false);
        }
        const auto payloadOffset = Get(bytes, static_cast<size_t>(tableOffset) + PackageFormat::EntryOffset::DataOffset, 8);
        requireRange(payloadOffset, ClipBankPayloadHashOffset + 8, "clip bank payload hash");
        // パスを含む payload から計算された hash も置き場所で変わるため、両方の欄を伏せる。
        // payload 自体はパス以外を保持するので、その内容の変更は照合で検出できる。
        for (uint64_t i = 0; i < entryCount; ++i)
        {
            const auto entryOffset = static_cast<size_t>(tableOffset + i * PackageFormat::EntryRecordSize);
            Put(bytes, entryOffset + PackageFormat::EntryOffset::PayloadHash, 0, 8);
        }
        Put(bytes, static_cast<size_t>(payloadOffset) + ClipBankPayloadHashOffset, 0, 8);
        uint64_t hash = 14695981039346656037ull;
        for (const auto byte : bytes)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
        return {hex};
    }
    Text CheckCookerRevisionRow(const char* kind, uint64_t tableRevision, const Hashes& tableHashes,
                               uint64_t currentRevision, const Hashes& actualHashes)
    {
        bool bSameHashes = tableHashes.size() == actualHashes.size();
        for (size_t i = 0; bSameHashes && i < tableHashes.size(); ++i)
        {
            bSameHashes = tableHashes[i] == actualHashes[i];
        }
        if (tableRevision == currentRevision && bSameHashes)
        {
            return {};
        }
        Text message;
        if (tableRevision == currentRevision)
        {
            message = "cooker の出力が変わった。";
            message.append(kind);
            message.append(" の版を上げて表を更新する");
        }
        else
        {
            char revisions[160];
            std::snprintf(revisions, sizeof(revisions), " の版(%llu)が表(%llu)と違う。",
                          static_cast<unsigned long long>(currentRevision), static_cast<unsigned long long>(tableRevision));
            message = kind;
            message.append(revisions);
            message.append("表の revision と cooked_hash を今の値へ更新する");
        }
        message.append("。実際の cooked_hash: ");
        message.append(HashList(actualHashes));
        return message;
    }
    void VerifyCookerRevisionComparator()
    {
        const Hashes a = {"000000000000000a"}, b = {"000000000000000b"};
        CHECK(CheckCookerRevisionRow("raw", 3, a, 3, b).find("の版を上げて表を更新する") != Text::npos);
        CHECK(CheckCookerRevisionRow("raw", 4, b, 4, b).empty());
        CHECK(!CheckCookerRevisionRow("raw", 3, a, 4, b).empty());
    }
    void VerifyCookerRevisionGolden(const Fixture& texture, const Fixture& model, const Fixture& skeletal,
                                    const Fixture& animation, const Fixture& audio, const Fixture& raw,
                                    const Hashes& animationHashes)
    {
        const char* kinds[] = {"texture", "model", "skeletal", "animation", "audio", "raw"};
        const Fixture* fixtures[] = {&texture, &model, &skeletal, &animation, &audio, &raw};
        const auto bytes = Read("Test/Core/Asset/CookerRevisionGolden.json");
        const size_t offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8({bytes.data() + offset, bytes.size() - offset}, document))
        {
            std::fprintf(stderr, "cooker の出力の基準表を JSON として読めない\n");
            CHECK(false);
        }
        const auto table = document.GetRoot();
        const auto rows = table.FindMember("rows");
        bool bValid = table.IsObject() && table.FindMember("version").IsIntegerLiteral() &&
                      table.FindMember("version").AsNumber() == 1 && rows.IsArray() && rows.GetArraySize() == 6;
        Container::VariableArray<Text> results;
        if (!bValid)
        {
            results.push_back("基準表の version は 1、rows は6種別を1行ずつにする");
        }
        size_t counts[6] = {};
        for (size_t i = 0; i < rows.GetArraySize(); ++i)
        {
            const auto row = rows.GetArrayElement(i);
            const auto kind = row.FindMember("kind");
            size_t index = 0;
            while (index < 6 && (!kind.IsString() || kind.AsString() != kinds[index]))
            {
                ++index;
            }
            if (index == 6)
            {
                results.push_back("基準表に未知の種別または kind が不正な行がある");
                bValid = false;
                continue;
            }
            ++counts[index];
            const auto format = row.FindMember("format");
            const auto revision = row.FindMember("revision");
            const auto hashes = row.FindMember("cooked_hash");
            const bool bRevisionValid = revision.IsIntegerLiteral() && revision.AsNumber() >= 0 &&
                                        revision.AsNumber() <= 9007199254740991.0;
            bool bRowValid = bRevisionValid && format.IsString() &&
                             format.AsString() == fixtures[index]->Request.Format.c_str() &&
                             hashes.IsArray() && hashes.GetArraySize() > 0;
            Hashes tableHashes;
            for (size_t j = 0; j < hashes.GetArraySize(); ++j)
            {
                const auto hash = hashes.GetArrayElement(j);
                Text text;
                bool bHashValid = hash.IsString() && hash.AsString().size() == 16;
                for (auto c : hash.AsString())
                {
                    bHashValid = bHashValid && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
                    text.push_back(static_cast<char>(c));
                }
                bRowValid = bRowValid && bHashValid;
                tableHashes.push_back(text);
            }
            const auto actualHashes = index == 3 ? animationHashes : CookedHashes(*fixtures[index]);
            const auto currentRevision = CookerRevisionForKind(Text(kinds[index]));
            Text result = kinds[index];
            result.append(": ");
            if (!bRowValid)
            {
                result.append("基準表の format・revision・cooked_hash が不正。実際の cooked_hash: ");
                result.append(HashList(actualHashes));
            }
            else
            {
                const auto message = CheckCookerRevisionRow(kinds[index], static_cast<uint64_t>(revision.AsNumber()),
                                                           tableHashes, currentRevision, actualHashes);
                bRowValid = message.empty();
                result.append(bRowValid ? "一致。実際の cooked_hash: " : message.c_str());
                if (bRowValid)
                {
                    result.append(HashList(actualHashes));
                }
            }
            results.push_back(result);
            bValid = bValid && bRowValid;
        }
        for (size_t i = 0; i < 6; ++i)
        {
            if (counts[i] != 1)
            {
                Text message = kinds[i];
                message.append(" の行はちょうど1行必要");
                results.push_back(message);
                bValid = false;
            }
        }
        if (!bValid)
        {
            for (const auto& result : results)
            {
                std::fprintf(stderr, "%s\n", result.c_str());
            }
            CHECK(false);
        }
    }
    CookDecision Decide(const SingleAssetCookRequest& r, const CookOutputRecord* record, const AssetManifest* manifest,
                        uint64_t revision = 7, bool bAllow = true, CookDecisionReason* reason = nullptr)
    {
        CookDecisionContext context;
        context.Request.LogicalPath = "held";
        context.Dependencies.Fingerprint = 123;
        Text error = "held";
        const auto result = DecideCookCache(r, revision, bAllow, record, manifest, context, error);
        if (result == CookDecision::Error)
        {
            CHECK(!error.empty() && context.Request.LogicalPath == "held" && context.Dependencies.Fingerprint == 123);
        }
        else
        {
            CHECK(error.empty() && !context.Request.bSkipIfUnchanged);
            if (reason)
            {
                *reason = context.Reason;
            }
        }
        return result;
    }
    Fixture Make(const std::filesystem::path& root, const char* name, const std::filesystem::path& source,
                 const char* kind, const char* type, const char* format, bool bDisabled = false,
                 AssetImport::EmissiveScale assetSetEmission = {})
    {
        Fixture f;
        auto& r = f.Request;
        r.InputPath = source;
        r.PackagePath = root / name / "Cooked/item.nvpkg";
        r.ManifestPath = root / name / "manifest.json";
        r.Kind = kind;
        r.Format = format;
        r.EntryTypeText = type;
        r.EntryName = "__asset__";
        r.LogicalPath = "Test/asset";
        r.Variant = "default";
        // 非modelでも新しい判定のskip意図は独立。実行要求へ旧skipを伝播させない。
        r.bSkipIfUnchanged = true;
        r.bNoSidecar = bDisabled;
        r.AssetSetEmission = assetSetEmission;
        Text error;
        if (DecideCookCache(r, 7, true, nullptr, nullptr, f.Before, error) != CookDecision::Cook)
        {
            std::fprintf(stderr, "prepare %s: %s\n", name, error.c_str());
            CHECK(false);
        }
        CHECK(!f.Before.Request.bSkipIfUnchanged);
        CHECK(CookSingleAsset(f.Before.Request, error));
        f.Live = Load(r.ManifestPath);
        if (!CaptureCookOutputRecord(f.Before, f.Live, f.Record, error))
        {
            std::fprintf(stderr, "capture %s: %s\n", name, error.c_str());
            CHECK(false);
        }
        CHECK(Decide(r, &f.Record, &f.Live) == CookDecision::Skip);
        // 全kindの実cook recordを所有stateで往復し、同じ共通判定へ戻す。
        CookOwnedState state;
        state.Binding.OwnerId = "0123456789abcdef0123456789abcdef";
        state.Binding.RuntimeRootIdentity = (root / name).generic_string().c_str();
        state.Binding.ManifestName = "manifest.json";
        CookOwnedRecord owned;
        const auto& primary = f.Record.Outputs[0].Reference;
        owned.PrimaryKey = {primary.LogicalPath, primary.Kind, primary.Variant};
        owned.Record = f.Record;
        state.Records.push_back(owned);
        Text saved;
        CHECK(SerializeCookOwnedState(state, saved, error));
        CookOwnedState loaded;
        CHECK(ParseCookOwnedState({reinterpret_cast<const uint8_t*>(saved.data()), saved.size()}, state.Binding, loaded, error));
        saved.clear();
        const auto* restored = FindCookOwnedRecord(loaded, owned.PrimaryKey);
        CHECK(restored && Decide(r, restored, &f.Live) == CookDecision::Skip);
        CookStateFileRequest stateFile;
        stateFile.RuntimeRoot = root / name;
        Text stateName = name; stateName.append(".state.json");
        stateFile.StatePath = root / std::filesystem::path(stateName.c_str());
        stateFile.ExpectedBinding = state.Binding;
        CHECK(WriteNewCookOwnedState(stateFile, state, error));
        CookOwnedState fromFile;
        CHECK(LoadCookOwnedState(stateFile, fromFile, error) == CookStateLoadResult::Loaded);
        const auto* diskRecord = FindCookOwnedRecord(fromFile, owned.PrimaryKey);
        CHECK(diskRecord && Decide(r, diskRecord, &f.Live) == CookDecision::Skip);
        CookPreparedPlan finalPlan, stagedPlan;
        CHECK(PrepareCookOutputPlan(r, 7, &f.Live, finalPlan, error));
        CHECK(finalPlan.Outputs.size() == f.Record.Outputs.size());
        CHECK(ValidateCookOutputSet({&finalPlan, 1}, {}, error));
        CookManagedUpdateInventory inventory;
        CHECK(BuildCookManagedUpdateInventory(stateFile, fromFile, {&finalPlan, 1}, inventory, error));
        CHECK(inventory.Assets.size() == 1 && inventory.Assets[0].Packages.size() == finalPlan.Outputs.size());
        CHECK(inventory.PreviousState.Records[0].Record.Outputs[0].Reference.CookedHash == f.Record.Outputs[0].Reference.CookedHash);
        for (size_t i = 0; i < finalPlan.Outputs.size(); ++i)
        {
            CHECK(finalPlan.Outputs[i].ExpectedIdentity.LogicalPath == f.Record.Outputs[i].Reference.LogicalPath);
            CHECK(finalPlan.Outputs[i].ExpectedIdentity.CookedPackage == f.Record.Outputs[i].Reference.CookedPackage);
        }
        Text stageName = name; stageName.append(".stage");
        const auto stageRoot = root / std::filesystem::path(stageName.c_str());
        CHECK(std::filesystem::create_directory(stageRoot));
        CHECK(PrepareCookStagingPlan(finalPlan, stageRoot, stagedPlan, error));
        CHECK(std::filesystem::is_empty(stageRoot));
        CHECK(CookSingleAsset(stagedPlan.Context.Request, error));
        const auto fragment = Load(stagedPlan.Context.Request.ManifestPath);
        CookOutputRecord stagedRecord;
        CHECK(CaptureStagedCookOutputRecord(finalPlan, stagedPlan, fragment, stagedRecord, error));
        CHECK(stagedRecord.DependencyFingerprint == f.Record.DependencyFingerprint);
        CHECK(stagedRecord.Outputs.size() == f.Record.Outputs.size());
        for (size_t i = 0; i < stagedRecord.Outputs.size(); ++i)
        {
            CHECK(stagedRecord.Outputs[i].Reference.CookedHash == f.Record.Outputs[i].Reference.CookedHash);
            CHECK(stagedRecord.Outputs[i].Package.ContentHash == f.Record.Outputs[i].Package.ContentHash);
        }



        CookOwnedRecord stagedOwned = owned;
        stagedOwned.Record = stagedRecord;
        ManagedIntentFixture::VerifyUpdate(root, name, stateFile, fromFile, stagedOwned, finalPlan, stagedPlan);
        CHECK(Decide(r, nullptr, &f.Live) == CookDecision::Cook);
        CHECK(Decide(r, &f.Record, &f.Live, 7, false) == CookDecision::Cook);
        return f;
    }
    void Misses(Fixture& f)
    {
        auto record = f.Record;
        record.SchemaVersion++;
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.DependencySchemaVersion++;
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        CHECK(Decide(f.Request, &f.Record, &f.Live, 8) == CookDecision::Cook);
        record = f.Record;
        record.DependencyFingerprint ^= 1;
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.Outputs.clear();
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        {
            const auto extra = record.Outputs[0];
            record.Outputs.push_back(extra);
        }
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.Outputs[0].Reference.CookedPackage = "../../never-open";
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.Outputs[0].Reference.SourceHash++;
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.Outputs[0].Reference.CookedHash++;
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.Outputs[0].Package.ContentHash++;
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.Outputs[0].Package.Size++;
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Cook);
        record = f.Record;
        record.Outputs[0].Reference.CookedHashHex = "ignored";
        record.Outputs[0].Reference.EntryTypeText = "ignored";
        CHECK(Decide(f.Request, &record, &f.Live) == CookDecision::Skip);
        const auto package = Read(f.Request.PackagePath);
        auto bad = package;
        bad.pop_back();
        Write(f.Request.PackagePath, bad);
        CHECK(Decide(f.Request, &f.Record, &f.Live) == CookDecision::Cook);
        Write(f.Request.PackagePath, package);
        auto moved = f.Request.PackagePath;
        moved += ".held";
        std::filesystem::rename(f.Request.PackagePath, moved);
        CHECK(Decide(f.Request, &f.Record, &f.Live) == CookDecision::Cook);
        std::filesystem::rename(moved, f.Request.PackagePath);
        const auto end = Get(package, AssetPackageFormatV1::HeaderOffset::NameTableOffset, 8) +
                         Get(package, AssetPackageFormatV1::HeaderOffset::NameTableSize, 8);
        CHECK(end < Get(package, AssetPackageFormatV1::HeaderOffset::BlobDataOffset, 8));
        bad = package;
        bad[static_cast<size_t>(end)] ^= 0x21;
        Write(f.Request.PackagePath, bad);
        CHECK(Decide(f.Request, &f.Record, &f.Live) == CookDecision::Cook);
        Write(f.Request.PackagePath, package);
        auto request = f.Request;
        request.PackagePath = f.Request.PackagePath.parent_path() / "relocated.nvpkg";
        CHECK(Decide(request, &f.Record, &f.Live) == CookDecision::Cook);
        request = f.Request;
        request.LogicalPath = "Assets/Test/./asset";
        request.EntryName = "Assets/./__asset__";
        CHECK(Decide(request, &f.Record, &f.Live) == CookDecision::Skip);
        CHECK(Decide(f.Request, &f.Record, nullptr) == CookDecision::Cook);
        AssetManifest invalid;
        CHECK(Decide(f.Request, &f.Record, &invalid) == CookDecision::Cook);
        CHECK(Decide(f.Request, &f.Record, &f.Live) == CookDecision::Skip);
    }
    AssetManifest Foreign(const Fixture& f, const char* package, bool bFirst)
    {
        const auto bytes = Read(f.Request.ManifestPath);
        Text text;
        text.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Text row =
            "{\"logical_path\":\"Other/asset\",\"kind\":\"raw\",\"source_hash\":\"0000000000000001\",\"variant\":\"default\",\"format\":\"raw.v0\",\"cooked_package\":\"";
        row.append(package);
        row.append(
            "\",\"entry_name\":\"x\",\"entry_type\":\"Raw \",\"cooked_hash\":\"0000000000000002\",\"cooked_version\":0}");
        const char* position = bFirst ? std::strchr(text.c_str(), '[') : std::strrchr(text.c_str(), ']');
        CHECK(position);
        const size_t offset = static_cast<size_t>(position - text.c_str()) + (bFirst ? 1 : 0);
        Text joined;
        joined.append(text.data(), offset);
        if (!bFirst)
        {
            joined.push_back(',');
        }
        joined.append(row);
        if (bFirst)
        {
            joined.push_back(',');
        }
        joined.append(text.data() + offset, text.size() - offset);
        joined.append(" \n\t");
        return Manifest({reinterpret_cast<const uint8_t*>(joined.data()), joined.size()});
    }
    Bytes NamedMaterialGlb(const Bytes& original)
    {
        CHECK(original.size() >= 20 && Get(original, 0, 4) == 0x46546c67 && Get(original, 16, 4) == 0x4e4f534a);
        const size_t jsonEnd = 20 + static_cast<size_t>(Get(original, 12, 4));
        CHECK(jsonEnd <= original.size());
        Text json;
        json.append(reinterpret_cast<const char*>(original.data() + 20), jsonEnd - 20);
        const char* marker = "\"materials\":[{";
        const auto at = json.find(marker);
        CHECK(at != Text::npos);
        const size_t insert = 20 + at + std::strlen(marker);
        const char field[] = "\"name\":\"Body\",";
        Bytes named;
        named.insert(named.end(), original.begin(), original.begin() + insert);
        named.insert(named.end(), field, field + sizeof(field) - 1);
        named.insert(named.end(), original.begin() + insert, original.begin() + jsonEnd);
        while ((named.size() - 20) % 4 != 0)
        {
            named.push_back(' ');
        }
        const size_t jsonSize = named.size() - 20;
        named.insert(named.end(), original.begin() + jsonEnd, original.end());
        Put(named, 8, named.size(), 4);
        Put(named, 12, jsonSize, 4);
        return named;
    }
    constexpr const char* ValidMaterialSidecar = R"({"version":1,"materials":[{"name":"Body"}]})";
    Fixture MakeMaterialPreflightFixture(const std::filesystem::path& root, const char* name,
                                         const std::filesystem::path& source, const char* kind,
                                         const char* type, const char* format)
    {
        Fixture f;
        auto& r = f.Request;
        r.InputPath = source;
        r.PackagePath = root / name / "Cooked/item.nvpkg";
        r.ManifestPath = root / name / "manifest.json";
        r.Kind = kind;
        r.EntryTypeText = type;
        r.Format = format;
        r.EntryName = "__asset__";
        r.LogicalPath = "Test/asset";
        r.Variant = "default";
        auto sidecar = source;
        sidecar += ".import.json";
        WriteText(sidecar, ValidMaterialSidecar);
        Text error;
        if (DecideCookCache(r, 7, true, nullptr, nullptr, f.Before, error) != CookDecision::Cook ||
            !CookSingleAsset(f.Before.Request, error))
        {
            std::fprintf(stderr, "material prepare %s: %s\n", name, error.c_str());
            CHECK(false);
        }
        f.Live = Load(r.ManifestPath);
        if (!CaptureCookOutputRecord(f.Before, f.Live, f.Record, error))
        {
            std::fprintf(stderr, "material capture %s: %s\n", name, error.c_str());
            CHECK(false);
        }
        CHECK(Decide(r, &f.Record, &f.Live) == CookDecision::Skip);
        return f;
    }
    void VerifyMaterialPreflight(Fixture& f, const char* selectorError)
    {
        struct InvalidSidecar
        {
            const char* Name;
            const char* Json;
            const char* Error;
        };
        const InvalidSidecar cases[] = {
            {"unmatched", R"({"version":1,"materials":[{"name":"Missing"}]})", selectorError},
            {"duplicate", R"({"version":1,"materials":[{"name":"Body"},{"index":0,"expectedName":"Body"}]})",
             selectorError},
            {"surface", R"({"version":1,"materials":[{"name":"Body","surface":"Stone"}]})",
             "surface_requires_GR81"}};
        auto sidecar = f.Request.InputPath;
        sidecar += ".import.json";
        for (const auto& test : cases)
        {
            WriteText(sidecar, test.Json);
            CookDependencySnapshot current;
            Text error;
            CHECK(CaptureCookDependencySnapshot(f.Before.Request, 7, current, error));
            // 現在の依存印だけ合わせた記録でも、検証を経ずにskipできないことを確かめる。
            auto matched = f.Record;
            matched.DependencyFingerprint = current.Fingerprint;
            for (const auto* record : {&f.Record, &matched})
            {
                CookDecisionContext context;
                const auto result = DecideCookCache(f.Request, 7, true, record, &f.Live, context, error);
                if (result != CookDecision::Error || std::strstr(error.c_str(), test.Error) == nullptr)
                {
                    std::fprintf(stderr, "material preflight %s %s: result=%u error=%s\n",
                                 f.Request.Format.c_str(), test.Name, unsigned(result), error.c_str());
                }
                CHECK(result == CookDecision::Error);
                CHECK(std::strstr(error.c_str(), test.Error) != nullptr);
            }
        }
        WriteText(sidecar, ValidMaterialSidecar);
        CHECK(Decide(f.Request, &f.Record, &f.Live) == CookDecision::Skip);
        std::printf("COOK_CACHE_MATERIAL_PREFLIGHT format=%s result=pass unmatched_duplicate_surface\n",
                    f.Request.Format.c_str());
    }
    struct Mutation
    {
        std::filesystem::path Path;
        Bytes Changed;
    };
    void Mutate(void* p)
    {
        const auto& m = *static_cast<Mutation*>(p);
        Write(m.Path, m.Changed);
    }
} // namespace
int main()
{
#if defined(_WIN32)
    char name[100];
    std::snprintf(name, sizeof(name), "norves-cache-%lu-%llu", GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));
#else
    const char* name = "norves-cache-windows-required";
#endif
    const auto root = std::filesystem::temp_directory_path() / name;
    CHECK(std::filesystem::create_directory(root));
    WriteText(root / "raw.bin", "abc");
    auto raw = Make(root, "raw", root / "raw.bin", "raw", "Raw", "raw.v0");
    Misses(raw);
    auto custom = Make(root, "custom", root / "raw.bin", "raw", "Cust", "raw.v0");
    Misses(custom);
    Write(root / "empty.bin", {});
    auto empty = Make(root, "empty", root / "empty.bin", "raw", "Raw", "raw.v0");
    Misses(empty);
    const uint8_t ppm[] = {'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 21, 57, 99};
    Write(root / "tiny.ppm", ppm);
    const char* formats[] = {"nvtex.v0.rgba8.srgb", "nvtex.v0.rgba8.linear", "nvtex.v0.rg8.linear",
                             "nvtex.v0.r8.linear"};
    const char* labels[] = {"srgb", "linear", "rg", "r"};
    Fixture srgb;
    for (size_t i = 0; i < 4; ++i)
    {
        auto f = Make(root, labels[i], root / "tiny.ppm", "texture", "Tex0", formats[i]);
        Misses(f);
        if (i == 0)
        {
            srgb = f;
        }
    }
    const auto unicode = root / std::filesystem::path(L"\u753b\u50cf.ppm");
    Write(unicode, ppm);
    auto textureUnicode = Make(root, "unicode", unicode, "texture", "Tex0", formats[0]);
    CHECK(Decide(textureUnicode.Request, &textureUnicode.Record, &textureUnicode.Live) == CookDecision::Skip);
    const char* hex =
        "524946462C00000057415645666D7420100000000100010044AC0000885801000200100064617461080000000000010002000300";
    Bytes wav;
    for (size_t i = 0; i < std::strlen(hex); i += 2)
    {
        const auto d = [](char c)
        {
            return c >= 'A' ? c - 'A' + 10 : c - '0';
        };
        wav.push_back(static_cast<uint8_t>(d(hex[i]) * 16 + d(hex[i + 1])));
    }
    Write(root / "tone.wav", wav);
    auto audio = Make(root, "audio", root / "tone.wav", "audio", "Aud0", "nvaud.v0.pcm16");
    Misses(audio);
    const auto glb = root / "tri.glb";
    std::filesystem::copy_file("Test/Core/Asset/Fixtures/AssetCook/embedded_tri.glb", glb);
    auto mesh = Make(root, "mesh", glb, "model", "Msh0", "nvmesh.v0.mesh3d.pnt.u32.clustered");
    CHECK(mesh.Record.Outputs.size() == 4);
    Misses(mesh);
    // 換算なしの記録は、asset-set 換算を追加すると失効する。
    {
        auto request = mesh.Request;
        request.AssetSetEmission = {true, 10};
        CHECK(Decide(request, &mesh.Record, &mesh.Live) == CookDecision::Cook);
    }
    // 同じ換算は skip を保ち、換算値の変更と削除は記録を失効させる。
    {
        auto withEmission = Make(root, "mesh-emission", glb, "model", "Msh0", mesh.Request.Format.c_str(), false, {true, 10});
        CHECK(Decide(withEmission.Request, &withEmission.Record, &withEmission.Live) == CookDecision::Skip);
        auto request = withEmission.Request;
        request.AssetSetEmission = {true, 20};
        CHECK(Decide(request, &withEmission.Record, &withEmission.Live) == CookDecision::Cook);
        request.AssetSetEmission = {false, 10};
        CHECK(Decide(request, &withEmission.Record, &withEmission.Live) == CookDecision::Cook);
    }
    auto swapped = mesh.Record;
    std::swap(swapped.Outputs[1], swapped.Outputs[2]);
    CHECK(Decide(mesh.Request, &swapped, &mesh.Live) == CookDecision::Cook);
    auto derived = mesh.Request.PackagePath;
    derived += ".img0.nvpkg";
    const auto derivedBytes = Read(derived);
    Write(derived, {});
    CHECK(Decide(mesh.Request, &mesh.Record, &mesh.Live) == CookDecision::Cook);
    Write(derived, derivedBytes);
    std::filesystem::copy("Assets/Models/M9Skinned", root / "skeletal", std::filesystem::copy_options::recursive);
    const auto buffer = SkeletalBuffer();
    Write(root / "skeletal/fixture.bin", buffer);
    auto skeletalFixture =
        Make(root, "skel", root / "skeletal/ValidU8Float.gltf", "model", "Skl0", "nvskel.v0.skinned.pnujiw.u32");
    Misses(skeletalFixture);
    auto wrongCounts = skeletalFixture.Record;
    wrongCounts.Outputs[0].Reference.SkeletalMetadata.JointCount++;
    CHECK(Decide(skeletalFixture.Request, &wrongCounts, &skeletalFixture.Live) == CookDecision::Cook);
    auto changedOptions = skeletalFixture.Request;
    changedOptions.SkeletalDecode.InfluencePolicy = Skeletal::SkeletalInfluencePolicy::ReduceToFour;
    CHECK(Decide(changedOptions, &skeletalFixture.Record, &skeletalFixture.Live) == CookDecision::Cook);
    // 作業ディレクトリを変えず、分割 rig の animation と同じ入力を組み立てる。
    const auto initialLabel = std::filesystem::absolute(root / "rig/rig.gltf").lexically_normal().generic_u8string();
    if (initialLabel.size() > AnimationSourceLabelBytes)
    {
        std::fprintf(stderr, "animation の入力の絶対パスが %zu バイトあり、%zu バイトにそろえられない\n",
                     initialLabel.size(), AnimationSourceLabelBytes);
        CHECK(false);
    }
    Text rigDirectoryName = "rig";
    for (size_t i = initialLabel.size(); i < AnimationSourceLabelBytes; ++i)
    {
        rigDirectoryName.push_back('p');
    }
    const auto rigDirectory = root / rigDirectoryName.c_str();
    CHECK(std::filesystem::create_directory(rigDirectory));
    const auto rigJson = NorvesLib::Tests::RigV1Fixture::BaseJson();
    WriteText(rigDirectory / "rig.gltf", rigJson.c_str());
    Write(rigDirectory / "fixture.bin", NorvesLib::Tests::RigV1Fixture::Buffer());
    auto animation = Make(root, "animation", rigDirectory / "rig.gltf", "animation", "Anm1", "nvskel.v1.clips");
    VerifyCookerRevisionComparator();
    VerifyCookerRevisionGolden(srgb, mesh, skeletalFixture, animation, audio, raw, MaskedAnimationHashes(animation));
    // 実cookの記録を使い、skip許可時も不正な材質設定がpreflightで拒否される。
    {
        const auto materialGlb = root / "material.glb";
        Write(materialGlb, NamedMaterialGlb(Read(glb)));
        const auto rigPath = root / "skeletal/material.gltf";
        const auto rigSource = Read(root / "skeletal/ValidU8Float.gltf");
        Text rigJson;
        rigJson.append(reinterpret_cast<const char*>(rigSource.data()), rigSource.size());
        const char* mode = "\"mode\": 4";
        const auto at = rigJson.find(mode);
        CHECK(at != Text::npos && !rigJson.empty() && rigJson[0] == '{');
        Text namedRig = "{\"materials\":[{\"name\":\"Body\"}],";
        namedRig.append(rigJson.data() + 1, at - 1);
        namedRig.append("\"mode\": 4, \"material\": 0");
        namedRig.append(rigJson.data() + at + std::strlen(mode), rigJson.size() - at - std::strlen(mode));
        Write(rigPath, {reinterpret_cast<const uint8_t*>(namedRig.data()), namedRig.size()});
        auto materialMesh = MakeMaterialPreflightFixture(root, "material-mesh", materialGlb, "model", "Msh0",
                                                          "nvmesh.v1.mesh3d.pnt.u32.clustered");
        auto materialSkin = MakeMaterialPreflightFixture(root, "material-skin", rigPath, "model", "Skm1",
                                                          "nvskel.v1.skinmesh.pnujiw.u32");
        auto materialClips = MakeMaterialPreflightFixture(root, "material-clips", rigPath, "animation", "Anm1",
                                                           "nvskel.v1.clips");
        VerifyMaterialPreflight(materialMesh, "material_selector_or_settings");
        VerifyMaterialPreflight(materialSkin, "split_material_selector_or_settings");
        VerifyMaterialPreflight(materialClips, "clip_material_selector_or_settings");
    }
    // 同じ意味のsidecarでも生byte変更はcache miss。disabledでは探索しない。
    auto sidecar = mesh.Request.InputPath;
    sidecar += ".import.json";
    WriteText(sidecar, "{\"version\":1}");
    CHECK(Decide(mesh.Request, &mesh.Record, &mesh.Live) == CookDecision::Cook);
    auto withSidecar = Make(root, "sidecar", glb, "model", "Msh0", mesh.Request.Format.c_str());
    WriteText(sidecar, "{ \"version\":1 }\n");
    CHECK(Decide(withSidecar.Request, &withSidecar.Record, &withSidecar.Live) == CookDecision::Cook);
    std::filesystem::remove(sidecar);
    CHECK(Decide(mesh.Request, &mesh.Record, &mesh.Live) == CookDecision::Skip);
    auto disabledFixture = Make(root, "disabled", glb, "model", "Msh0", mesh.Request.Format.c_str(), true);
    WriteText(sidecar, "invalid disabled settings");
    CHECK(Decide(disabledFixture.Request, &disabledFixture.Record, &disabledFixture.Live) == CookDecision::Skip);
    CHECK(Decide(mesh.Request, nullptr, nullptr) == CookDecision::Error);
    std::filesystem::remove(sidecar);
    auto required = mesh.Request;
    required.bRequireSidecar = true;
    CHECK(Decide(required, nullptr, nullptr) == CookDecision::Error);
    const auto unicodeModel = root / std::filesystem::path(L"\u72ac.glb");
    std::filesystem::copy_file(glb, unicodeModel);
    auto modelUnicode = Make(root, "unicode-model", unicodeModel, "model", "Msh0", mesh.Request.Format.c_str());
    CHECK(Decide(modelUnicode.Request, &modelUnicode.Record, &modelUnicode.Live) == CookDecision::Skip);
    // 外部imageは生成package一覧へ足さないが、未使用分も依存印に含める。
    std::filesystem::copy("Assets/Models/Rendering3DTestSilverGltf", root / "external-source",
                          std::filesystem::copy_options::recursive);
    const auto externalPath = root / "external-source/Rendering3DTestSilverGltf.gltf";
    const auto externalBytes = Read(externalPath);
    Text externalText;
    externalText.append(reinterpret_cast<const char*>(externalBytes.data()), externalBytes.size());
    const char* images = std::strstr(externalText.c_str(), "\"images\"");
    CHECK(images);
    const char* arrayEnd = std::strchr(images, ']');
    CHECK(arrayEnd);
    const size_t insert = static_cast<size_t>(arrayEnd - externalText.c_str());
    Text extended;
    extended.append(externalText.data(), insert);
    extended.append(",{\"uri\":\"unused.bin\"}");
    extended.append(externalText.data() + insert, externalText.size() - insert);
    Write(externalPath, {reinterpret_cast<const uint8_t*>(extended.data()), extended.size()});
    WriteText(root / "external-source/unused.bin", "unused");
    auto external = Make(root, "external", externalPath, "model", "Msh0", mesh.Request.Format.c_str());
    CHECK(external.Record.Outputs.size() == 1);
    for (const auto& dependency :
         {root / "external-source/Rendering3DTestSilverGltf.bin", root / "external-source/textures/silver_albedo.png",
          root / "external-source/textures/silver_normal-ogl.png", root / "external-source/textures/silver_arm.png",
          root / "external-source/unused.bin"})
    {
        const auto held = Read(dependency);
        auto changed = held;
        changed.push_back(0);
        Write(dependency, changed);
        CHECK(Decide(external.Request, &external.Record, &external.Live) == CookDecision::Cook);
        Write(dependency, held);
        auto temporary = dependency;
        temporary += ".held";
        std::filesystem::rename(dependency, temporary);
        CHECK(Decide(external.Request, &external.Record, &external.Live) == CookDecision::Error);
        std::filesystem::rename(temporary, dependency);
        CHECK(Decide(external.Request, &external.Record, &external.Live) == CookDecision::Skip);
    }

    auto absentAlias = mesh.Request;
    absentAlias.ManifestPath = root / "manifest.json";
    absentAlias.PackagePath = sidecar;
    CHECK(Decide(absentAlias, nullptr, nullptr) == CookDecision::Error && !std::filesystem::exists(sidecar));
    // 集約manifestの他key・並び・空白はhitを保つ。別keyが自分の出力を指す場合は拒否。
    const auto foreignFirst = Foreign(raw, "Cooked/foreign.nvpkg", true),
               foreignLast = Foreign(raw, "Cooked/foreign.nvpkg", false);
    CHECK(Decide(raw.Request, &raw.Record, &foreignFirst) == CookDecision::Skip);
    CHECK(Decide(raw.Request, &raw.Record, &foreignLast) == CookDecision::Skip);
    const auto aliases = Foreign(raw, raw.Record.Outputs[0].Reference.CookedPackage.c_str(), true);
    CHECK(Decide(raw.Request, &raw.Record, &aliases) == CookDecision::Error);
    auto liveBytes = Read(raw.Request.ManifestPath);
    Text liveText;
    liveText.append(reinterpret_cast<const char*>(liveBytes.data()), liveBytes.size());
    const auto field = std::strstr(liveText.c_str(), raw.Record.Outputs[0].Reference.SourceHashHex.c_str());
    CHECK(field);
    liveText[static_cast<size_t>(field - liveText.c_str())] = field[0] == '0' ? '1' : '0';
    const auto changedLive = Manifest({reinterpret_cast<const uint8_t*>(liveText.data()), liveText.size()});
    CHECK(Decide(raw.Request, &raw.Record, &changedLive) == CookDecision::Cook);
    auto r = raw.Request;
    r.Format = "bad";
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    r = raw.Request;
    r.InputPath = root / "missing";
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    CHECK(Decide(raw.Request, nullptr, nullptr, 0) == CookDecision::Error);
    for (const char* leaf : {"CON.nvpkg", "a:stream", "a.", "a ", "x?/a", "Assets/item.nvpkg"})
    {
        r = raw.Request;
        r.PackagePath = r.ManifestPath.parent_path() / leaf;
        SingleAssetCookRequest normalized;
        Text normalizeError, normalizedPath;
        if (NorvesLib::Tools::AssetCook::Detail::NormalizeCacheCookRequest(r, normalized, normalizeError))
        {
            CHECK(NorvesLib::Tools::AssetCook::Detail::CookOutputPaths::AsciiPath(normalized.PackagePath, normalizedPath));
            std::fprintf(stderr, "unsafe_leaf=%s normalized=%s\n", leaf, normalizedPath.c_str());
        }
        const auto decision = Decide(r, nullptr, nullptr);
        if (decision != CookDecision::Error)
        {
            std::fprintf(stderr, "unexpected_decision=%d unsafe_leaf=%s\n", static_cast<int>(decision), leaf);
        }
        CHECK(decision == CookDecision::Error);
    }
    for (const char* leaf : {"manifest.json.", "manifest.json ", "NUL.json"})
    {
        r = raw.Request;
        r.ManifestPath = r.ManifestPath.parent_path() / leaf;
        CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    }
    r = raw.Request;
    r.PackagePath = r.ManifestPath.parent_path() / "../outside.nvpkg";
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    r = raw.Request;
    r.ManifestPath = r.PackagePath / "manifest.json";
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    r = raw.Request;
    r.ManifestPath = root / "manifest.json";
    r.PackagePath = r.InputPath;
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    const auto link = raw.Request.PackagePath.parent_path() / "hardlink.nvpkg";
    std::filesystem::create_hard_link(raw.Request.InputPath, link);
    r = raw.Request;
    r.PackagePath = link;
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    std::filesystem::remove(link);
    r = raw.Request;
    r.ManifestPath = root / "manifest.json";
    r.PackagePath = root / "RAW.BIN";
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
#if defined(_WIN32)
    const auto junction = root / "junction", junctionTarget = root / "junction-target";
    std::filesystem::create_directory(junctionTarget);
    Text junctionText, targetText;
    CHECK(NorvesLib::Tools::AssetCook::Detail::CookOutputPaths::AsciiPath(junction, junctionText));
    CHECK(NorvesLib::Tools::AssetCook::Detail::CookOutputPaths::AsciiPath(junctionTarget, targetText));
    for (auto& c : junctionText)
    {
        if (c == '/')
        {
            c = '\\';
        }
    }
    for (auto& c : targetText)
    {
        if (c == '/')
        {
            c = '\\';
        }
    }
    Text command = "cmd /d /c mklink /J \"";
    command.append(junctionText);
    command.append("\" \"");
    command.append(targetText);
    command.append("\" >nul");
    CHECK(std::system(command.c_str()) == 0);
    r = raw.Request;
    r.ManifestPath = root / "manifest.json";
    r.PackagePath = junction / "item.nvpkg";
    CHECK(Decide(r, nullptr, nullptr) == CookDecision::Error);
    std::filesystem::remove(junction);
#endif
    // 採取開始後のsource変更はCookでもSkipでもError。工場も古いrecordを保持する。
    const auto original = Read(raw.Request.InputPath);
    Mutation mutation{raw.Request.InputPath, original};
    mutation.Changed.push_back('!');
    CookDecisionContext held;
    held.Request.LogicalPath = "held";
    held.Dependencies.Fingerprint = 321;
    Text error;
    for (bool bAllowSkip : {false, true})
    {
        CHECK(NorvesLib::Tools::AssetCook::Detail::DecideCookCacheWithProbe(raw.Request, 7, bAllowSkip, bAllowSkip ? &raw.Record : nullptr,
                                               &raw.Live, held, error, Mutate, &mutation) == CookDecision::Error);
        CHECK(held.Request.LogicalPath == "held" && held.Dependencies.Fingerprint == 321);
        Write(raw.Request.InputPath, original);
    }
    auto heldRecord = raw.Record;
    heldRecord.SchemaVersion = 99;
    CHECK(!NorvesLib::Tools::AssetCook::Detail::CaptureCookOutputRecordWithProbe(raw.Before, raw.Live, heldRecord, error, Mutate, &mutation));
    CHECK(heldRecord.SchemaVersion == 99 && heldRecord.Outputs.size() == raw.Record.Outputs.size());
    CHECK(!CaptureCookOutputRecord(raw.Before, raw.Live, heldRecord, error));
    Write(raw.Request.InputPath, original);
    CHECK(Decide(raw.Request, &raw.Record, &raw.Live) == CookDecision::Skip);
    std::filesystem::remove_all(root);
    std::puts("CookCacheDecisionTest PASS: owned_records_all_kinds_incremental_inventory_aliases_race_hold");
    return 0;
}
