// runtime manifestとは独立した、厳密な所有state schema v1の値codec。
#include "CookOwnedState.h"
#include "CookOutputPaths.h"
#include "Asset/AssetPath.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using StateText = Core::Container::AnsiString;
        using StateView = Core::Container::AnsiStringView;
        using Core::JsonValue;
        using Core::Asset::AssetCookedReference;
        using Core::Asset::AssetKind;
        namespace Paths = Detail::CookOutputPaths;
        constexpr const char* Producer = "NorvesLib.AssetCook";
        bool Equal(StateView a, StateView b)
        {
            return Paths::EqualName(a, b);
        }
        bool Fail(StateText& error, const char* code)
        {
            error = "cook_owned_state: ";
            error.append(code);
            return false;
        }
        bool TextValid(StateView text, bool bAllowEmpty = false)
        {
            if ((!bAllowEmpty && text.empty()) || text.size() > MaximumCookStateStringBytes)
            {
                return false;
            }
            for (const unsigned char c : text)
            {
                if (c < 32 || c >= 127)
                {
                    return false;
                }
            }
            return true;
        }
        bool KeyKind(AssetKind kind)
        {
            return kind == AssetKind::Raw || kind == AssetKind::Texture || kind == AssetKind::Audio ||
                   kind == AssetKind::Model;
        }
        bool Canonical(StateView text)
        {
            if (!TextValid(text) || !Paths::SafeOutputName(text))
            {
                return false;
            }
            const auto normalized = Core::Asset::AssetPath::Normalize(text);
            return normalized.HasLogicalPath() && Equal(normalized.GetLogicalPath(), text);
        }
        bool IsHex(StateView text, size_t length)
        {
            if (text.size() != length)
            {
                return false;
            }
            for (const char c : text)
            {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                {
                    return false;
                }
            }
            return true;
        }
        bool BindingValid(const CookStateBinding& binding)
        {
            if (!IsHex(binding.OwnerId, 32) || !Canonical(binding.ManifestName))
            {
                return false;
            }
            bool bNonzero = false;
            for (const auto c : binding.OwnerId)
            {
                bNonzero = bNonzero || c != '0';
            }
            const auto& root = binding.RuntimeRootIdentity;
            if (!bNonzero || !TextValid(root) || root.size() < 4 || root[1] != ':' || root[2] != '/' ||
                !((root[0] >= 'A' && root[0] <= 'Z') || (root[0] >= 'a' && root[0] <= 'z')))
            {
                return false;
            }
            // root直下のAssetsも有効。ここではruntime論理pathのprefix除去を使わない。
            return Paths::SafeOutputName(StateView(root.data() + 3, root.size() - 3));
        }
        bool SameBinding(const CookStateBinding& a, const CookStateBinding& b)
        {
            return Equal(a.OwnerId, b.OwnerId) && Equal(a.RuntimeRootIdentity, b.RuntimeRootIdentity) &&
                   Equal(a.ManifestName, b.ManifestName);
        }
        bool SameKey(const CookStateKey& a, const CookStateKey& b)
        {
            return a.Kind == b.Kind && Equal(a.LogicalPath, b.LogicalPath) && Equal(a.Variant, b.Variant);
        }
        CookStateKey Key(const AssetCookedReference& row)
        {
            return {row.LogicalPath, row.Kind, row.Variant};
        }
        bool KeyValid(const CookStateKey& key)
        {
            return KeyKind(key.Kind) && Canonical(key.LogicalPath) && TextValid(key.Variant);
        }
        int Compare(StateView a, StateView b, bool bFold = false, bool bPath = false)
        {
            const auto lower = [&](unsigned char c)
            {
                return bPath && c == '/' ? 0 : (bFold && c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
            };
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            {
                const auto x = lower(a[i]), y = lower(b[i]);
                if (x != y)
                {
                    return x < y ? -1 : 1;
                }
            }
            return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
        }
        bool PhysicalConflict(StateView a, StateView b)
        {
            const size_t n = std::min(a.size(), b.size());
            if (Compare(StateView(a.data(), n), StateView(b.data(), n), true) != 0)
            {
                return false;
            }
            return a.size() == b.size() || (a.size() > n ? a[n] == '/' : b[n] == '/');
        }
        bool Validate(const CookOwnedState& state, StateText& error)
        {
            if (state.SchemaVersion != 1 || state.Generation == 0 || !BindingValid(state.Binding) ||
                state.Records.size() > MaximumCookStateRecords)
            {
                return Fail(error, "invalid_header_or_binding");
            }
            Core::Container::VariableArray<const AssetCookedReference*> rows;
            for (const auto& owned : state.Records)
            {
                const auto& record = owned.Record;
                if (!KeyValid(owned.PrimaryKey) || record.SchemaVersion != 1 || record.DependencySchemaVersion != 1 ||
                    record.Outputs.empty() || record.Outputs.size() > MaximumCookStateOutputs - rows.size())
                {
                    return Fail(error, "invalid_record");
                }
                if (!SameKey(owned.PrimaryKey, Key(record.Outputs[0].Reference)))
                {
                    return Fail(error, "primary_key_mismatch");
                }
                for (const auto& output : record.Outputs)
                {
                    const auto& row = output.Reference;
                    if (!KeyValid(Key(row)) || !Canonical(row.CookedPackage) || !Canonical(row.EntryName) ||
                        !TextValid(row.Format) || output.Package.Size < Core::Asset::AssetPackageFormatV1::HeaderSize ||
                        PhysicalConflict(row.CookedPackage, state.Binding.ManifestName))
                    {
                        return Fail(error, "invalid_output_reference");
                    }
                    const auto typeText = Core::Asset::FormatAssetPackageFourCCText(row.EntryType);
                    Core::Asset::AssetPackageFourCC parsed = 0;
                    if (!Core::Asset::TryParseAssetPackageFourCCText(typeText, parsed) || parsed != row.EntryType)
                    {
                        return Fail(error, "invalid_entry_type");
                    }
                    const auto& m = row.SkeletalMetadata;
                    if (row.bHasSkeletalMetadata)
                    {
                        if (row.Kind != AssetKind::Model || !m.bHasSubmeshCounts || !m.VertexCount || !m.IndexCount ||
                            !m.JointCount || !m.ClipCount || !m.SubmeshCount || !m.MaterialSlotCount)
                        {
                            return Fail(error, "invalid_skeletal_metadata");
                        }
                    }
                    else if (m.bHasSubmeshCounts || m.VertexCount || m.IndexCount || m.JointCount || m.ClipCount ||
                             m.SubmeshCount || m.MaterialSlotCount)
                    {
                        return Fail(error, "unexpected_skeletal_metadata");
                    }
                    rows.push_back(&row);
                }
            }
            // 区切りを最小順にし、a と a/x の間へ a! が割り込んでprefix衝突を隠さない。
            std::sort(rows.begin(), rows.end(),
                      [](const auto* a, const auto* b)
                      {
                          return Compare(a->CookedPackage, b->CookedPackage, true, true) < 0;
                      });
            for (size_t i = 1; i < rows.size(); ++i)
            {
                if (PhysicalConflict(rows[i - 1]->CookedPackage, rows[i]->CookedPackage))
                {
                    return Fail(error, "output_path_alias");
                }
            }
            std::sort(rows.begin(), rows.end(),
                      [](const auto* a, const auto* b)
                      {
                          const int path = Compare(a->LogicalPath, b->LogicalPath);
                          if (path)
                          {
                              return path < 0;
                          }
                          if (a->Kind != b->Kind)
                          {
                              return static_cast<uint8_t>(a->Kind) < static_cast<uint8_t>(b->Kind);
                          }
                          return Compare(a->Variant, b->Variant) < 0;
                      });
            for (size_t i = 1; i < rows.size(); ++i)
            {
                if (SameKey(Key(*rows[i - 1]), Key(*rows[i])))
                {
                    return Fail(error, "duplicate_output_key");
                }
            }
            return true;
        }
        bool KeyName(const Core::Container::String& source, const char* name)
        {
            if (source.size() != std::strlen(name))
            {
                return false;
            }
            for (size_t i = 0; i < source.size(); ++i)
            {
                if (source[i] != name[i])
                {
                    return false;
                }
            }
            return true;
        }
        bool Shape(const JsonValue& value, std::initializer_list<const char*> fields)
        {
            if (!value.IsObject() || value.GetObjectSize() != fields.size() || fields.size() > 32)
            {
                return false;
            }
            uint32_t seen = 0;
            for (size_t i = 0; i < value.GetObjectSize(); ++i)
            {
                size_t index = 0;
                for (const auto* key : fields)
                {
                    if (KeyName(value.GetMemberName(i), key))
                    {
                        break;
                    }
                    ++index;
                }
                if (index == fields.size() || (seen & (uint32_t{1} << index)))
                {
                    return false;
                }
                seen |= uint32_t{1} << index;
            }
            return true;
        }
        bool ReadText(const JsonValue& value, StateText& out)
        {
            if (!value.IsString())
            {
                return false;
            }
            const auto& text = value.AsString();
            if (text.empty() || text.size() > MaximumCookStateStringBytes)
            {
                return false;
            }
            for (const auto unit : text)
            {
                if (unit < 32 || unit >= 127)
                {
                    return false;
                }
                out.push_back(static_cast<char>(unit));
            }
            return true;
        }
        bool Number(const JsonValue& value, uint32_t& out)
        {
            if (!value.IsIntegerLiteral())
            {
                return false;
            }
            const auto n = value.AsNumber();
            if (!(n >= 0 && n <= UINT32_MAX) || std::signbit(n))
            {
                return false;
            }
            out = static_cast<uint32_t>(n);
            return true;
        }
        bool Hex(const JsonValue& value, uint64_t& out)
        {
            StateText text;
            if (!ReadText(value, text) || !IsHex(text, 16))
            {
                return false;
            }
            uint64_t candidate = 0;
            for (const char c : text)
            {
                candidate = (candidate << 4) | static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
            }
            out = candidate;
            return true;
        }
        bool ReadKey(const JsonValue& value, CookStateKey& key)
        {
            StateText kind;
            return Shape(value, {"logical", "kind", "variant"}) &&
                   ReadText(value.FindMember("logical"), key.LogicalPath) && ReadText(value.FindMember("kind"), kind) &&
                   Core::Asset::TryParseAssetKind(kind, key.Kind) && ReadText(value.FindMember("variant"), key.Variant);
        }
        bool ReadOutput(const JsonValue& value, CookRecordedOutput& out)
        {
            if (!Shape(value, {"key", "source_hash", "format", "package", "entry", "entry_type", "cooked_hash",
                               "cooked_version", "package_size", "package_hash", "skeletal"}))
            {
                return false;
            }
            CookStateKey key;
            auto& row = out.Reference;
            uint64_t type = 0;
            if (!ReadKey(value.FindMember("key"), key) || !Hex(value.FindMember("source_hash"), row.SourceHash) ||
                !ReadText(value.FindMember("format"), row.Format) ||
                !ReadText(value.FindMember("package"), row.CookedPackage) ||
                !ReadText(value.FindMember("entry"), row.EntryName) || !Hex(value.FindMember("entry_type"), type) ||
                type > UINT32_MAX || !Hex(value.FindMember("cooked_hash"), row.CookedHash) ||
                !Number(value.FindMember("cooked_version"), row.CookedVersion) ||
                !Hex(value.FindMember("package_size"), out.Package.Size) ||
                !Hex(value.FindMember("package_hash"), out.Package.ContentHash))
            {
                return false;
            }
            row.LogicalPath = std::move(key.LogicalPath);
            row.Kind = key.Kind;
            row.Variant = std::move(key.Variant);
            row.EntryType = static_cast<uint32_t>(type);
            row.SourceHashHex = Core::Asset::FormatAssetHashHex(row.SourceHash);
            row.CookedHashHex = Core::Asset::FormatAssetHashHex(row.CookedHash);
            row.EntryTypeText = Core::Asset::FormatAssetPackageFourCCText(row.EntryType);
            const auto metadata = value.FindMember("skeletal");
            if (metadata.IsNull())
            {
                return true;
            }
            if (!Shape(metadata, {"vertices", "indices", "joints", "clips", "submeshes", "slots"}))
            {
                return false;
            }
            row.bHasSkeletalMetadata = true;
            auto& m = row.SkeletalMetadata;
            m.bHasSubmeshCounts = true;
            return Number(metadata.FindMember("vertices"), m.VertexCount) &&
                   Number(metadata.FindMember("indices"), m.IndexCount) &&
                   Number(metadata.FindMember("joints"), m.JointCount) &&
                   Number(metadata.FindMember("clips"), m.ClipCount) &&
                   Number(metadata.FindMember("submeshes"), m.SubmeshCount) &&
                   Number(metadata.FindMember("slots"), m.MaterialSlotCount);
        }
        bool ReadRecord(const JsonValue& value, CookOwnedRecord& out, size_t& total)
        {
            auto& r = out.Record;
            if (!Shape(value, {"primary", "schema", "dependency_schema", "revision", "dependency_hash", "outputs"}) ||
                !ReadKey(value.FindMember("primary"), out.PrimaryKey) ||
                !Number(value.FindMember("schema"), r.SchemaVersion) ||
                !Number(value.FindMember("dependency_schema"), r.DependencySchemaVersion) ||
                !Hex(value.FindMember("revision"), r.CookerRevision) ||
                !Hex(value.FindMember("dependency_hash"), r.DependencyFingerprint))
            {
                return false;
            }
            const auto outputs = value.FindMember("outputs");
            if (!outputs.IsArray() || outputs.GetArraySize() > MaximumCookStateOutputs - total)
            {
                return false;
            }
            total += outputs.GetArraySize();
            r.Outputs.reserve(outputs.GetArraySize());
            for (size_t i = 0; i < outputs.GetArraySize(); ++i)
            {
                CookRecordedOutput output;
                if (!ReadOutput(outputs.GetArrayElement(i), output))
                {
                    return false;
                }
                r.Outputs.push_back(std::move(output));
            }
            return true;
        }
        bool BoundedJson(Core::Container::Span<const uint8_t> bytes)
        {
            if (bytes.empty() || !bytes.data() || bytes.size() > MaximumCookStateBytes)
            {
                return false;
            }
            size_t depth = 0;
            bool bString = false, bEscape = false;
            for (const auto c : bytes)
            {
                if (bString)
                {
                    if (bEscape)
                    {
                        bEscape = false;
                    }
                    else if (c == '\\')
                    {
                        bEscape = true;
                    }
                    else if (c == '"')
                    {
                        bString = false;
                    }
                }
                else if (c == '"')
                {
                    bString = true;
                }
                else if (c == '{' || c == '[')
                {
                    if (++depth > 16)
                    {
                        return false;
                    }
                }
                else if (c == '}' || c == ']')
                {
                    if (depth == 0)
                    {
                        return false;
                    }
                    --depth;
                }
            }
            return !bString && depth == 0;
        }
        class Writer
        {
          public:
            StateText Text;
            bool bGood = true;
            void Add(StateView text)
            {
                if (!bGood || text.size() > MaximumCookStateBytes - Text.size())
                {
                    bGood = false;
                    return;
                }
                Text.append(text.data(), text.size());
            }
            void Quoted(StateView text)
            {
                Add("\"");
                for (const char c : text)
                {
                    if (c == '"' || c == '\\')
                    {
                        Add("\\");
                    }
                    Add(StateView(&c, 1));
                }
                Add("\"");
            }
            void NumberValue(uint32_t value)
            {
                char buffer[16];
                const auto encoded = std::to_chars(buffer, buffer + sizeof(buffer), value);
                Add(StateView(buffer, static_cast<size_t>(encoded.ptr - buffer)));
            }
            void HexValue(uint64_t value)
            {
                char buffer[16];
                for (size_t i = 0; i < 16; ++i)
                {
                    buffer[15 - i] = "0123456789abcdef"[(value >> (i * 4)) & 15];
                }
                Quoted(StateView(buffer, 16));
            }
            void KeyValue(const CookStateKey& key)
            {
                Add("{\"logical\":");
                Quoted(key.LogicalPath);
                Add(",\"kind\":");
                Quoted(Core::Asset::GetAssetKindName(key.Kind));
                Add(",\"variant\":");
                Quoted(key.Variant);
                Add("}");
            }
            void Output(const CookRecordedOutput& output)
            {
                const auto& r = output.Reference;
                Add("{\"key\":");
                KeyValue(Key(r));
                Add(",\"source_hash\":");
                HexValue(r.SourceHash);
                Add(",\"format\":");
                Quoted(r.Format);
                Add(",\"package\":");
                Quoted(r.CookedPackage);
                Add(",\"entry\":");
                Quoted(r.EntryName);
                Add(",\"entry_type\":");
                HexValue(r.EntryType);
                Add(",\"cooked_hash\":");
                HexValue(r.CookedHash);
                Add(",\"cooked_version\":");
                NumberValue(r.CookedVersion);
                Add(",\"package_size\":");
                HexValue(output.Package.Size);
                Add(",\"package_hash\":");
                HexValue(output.Package.ContentHash);
                Add(",\"skeletal\":");
                if (r.bHasSkeletalMetadata)
                {
                    const auto& m = r.SkeletalMetadata;
                    Add("{\"vertices\":");
                    NumberValue(m.VertexCount);
                    Add(",\"indices\":");
                    NumberValue(m.IndexCount);
                    Add(",\"joints\":");
                    NumberValue(m.JointCount);
                    Add(",\"clips\":");
                    NumberValue(m.ClipCount);
                    Add(",\"submeshes\":");
                    NumberValue(m.SubmeshCount);
                    Add(",\"slots\":");
                    NumberValue(m.MaterialSlotCount);
                    Add("}");
                }
                else
                {
                    Add("null");
                }
                Add("}");
            }
        };
    } // namespace
    bool IsValidCookStateBinding(const CookStateBinding& binding)
    {
        return BindingValid(binding);
    }
    bool ParseCookOwnedState(Core::Container::Span<const uint8_t> bytes, const CookStateBinding& expected,
                             CookOwnedState& out, Core::Container::AnsiString& error)
    {
        error.clear();
        if (!BindingValid(expected) || !BoundedJson(bytes))
        {
            return Fail(error, "invalid_binding_or_input_limit");
        }
        Core::JsonDocument document;
        if (!Core::JsonDocument::TryParseUtf8(bytes, document))
        {
            return Fail(error, "invalid_json");
        }
        const auto root = document.GetRoot();
        CookOwnedState candidate;
        StateText producer;
        if (!Shape(root, {"schema", "producer", "owner", "root", "manifest", "generation", "records"}) ||
            !Number(root.FindMember("schema"), candidate.SchemaVersion) ||
            !ReadText(root.FindMember("producer"), producer) || !Equal(producer, Producer) ||
            !ReadText(root.FindMember("owner"), candidate.Binding.OwnerId) ||
            !ReadText(root.FindMember("root"), candidate.Binding.RuntimeRootIdentity) ||
            !ReadText(root.FindMember("manifest"), candidate.Binding.ManifestName) ||
            !Hex(root.FindMember("generation"), candidate.Generation) || !SameBinding(candidate.Binding, expected))
        {
            return Fail(error, "invalid_header_or_scope");
        }
        const auto records = root.FindMember("records");
        if (!records.IsArray() || records.GetArraySize() > MaximumCookStateRecords)
        {
            return Fail(error, "record_limit");
        }
        size_t total = 0;
        candidate.Records.reserve(records.GetArraySize());
        for (size_t i = 0; i < records.GetArraySize(); ++i)
        {
            CookOwnedRecord record;
            if (!ReadRecord(records.GetArrayElement(i), record, total))
            {
                return Fail(error, "invalid_record_fields");
            }
            candidate.Records.push_back(std::move(record));
        }
        if (!Validate(candidate, error))
        {
            return false;
        }
        out = std::move(candidate);
        return true;
    }
    bool SerializeCookOwnedState(const CookOwnedState& state, Core::Container::AnsiString& outJson,
                                 Core::Container::AnsiString& error)
    {
        error.clear();
        if (!Validate(state, error))
        {
            return false;
        }
        Writer w;
        w.Add("{\"schema\":1,\"producer\":");
        w.Quoted(Producer);
        w.Add(",\"owner\":");
        w.Quoted(state.Binding.OwnerId);
        w.Add(",\"root\":");
        w.Quoted(state.Binding.RuntimeRootIdentity);
        w.Add(",\"manifest\":");
        w.Quoted(state.Binding.ManifestName);
        w.Add(",\"generation\":");
        w.HexValue(state.Generation);
        w.Add(",\"records\":[");
        bool bFirst = true;
        for (const auto& owned : state.Records)
        {
            if (!bFirst)
            {
                w.Add(",");
            }
            bFirst = false;
            w.Add("{\"primary\":");
            w.KeyValue(owned.PrimaryKey);
            w.Add(",\"schema\":");
            w.NumberValue(owned.Record.SchemaVersion);
            w.Add(",\"dependency_schema\":");
            w.NumberValue(owned.Record.DependencySchemaVersion);
            w.Add(",\"revision\":");
            w.HexValue(owned.Record.CookerRevision);
            w.Add(",\"dependency_hash\":");
            w.HexValue(owned.Record.DependencyFingerprint);
            w.Add(",\"outputs\":[");
            bool bFirstOutput = true;
            for (const auto& output : owned.Record.Outputs)
            {
                if (!bFirstOutput)
                {
                    w.Add(",");
                }
                bFirstOutput = false;
                w.Output(output);
            }
            w.Add("]}");
        }
        w.Add("]}\n");
        if (!w.bGood)
        {
            return Fail(error, "serialized_size_limit");
        }
        outJson = std::move(w.Text);
        return true;
    }
    const CookOutputRecord* FindCookOwnedRecord(const CookOwnedState& state, const CookStateKey& key)
    {
        for (const auto& owned : state.Records)
        {
            if (SameKey(owned.PrimaryKey, key))
            {
                return &owned.Record;
            }
        }
        return nullptr;
    }
} // namespace NorvesLib::Tools::AssetCook
