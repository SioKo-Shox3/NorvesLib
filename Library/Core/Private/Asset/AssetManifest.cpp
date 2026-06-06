#include "Asset/AssetManifest.h"

#include "Asset/AssetPath.h"
#include "Text/JsonDocument.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        std::string ToStdString(Container::AnsiStringView view)
        {
            if (view.data() == nullptr || view.size() == 0)
            {
                return {};
            }

            return std::string(view.data(), view.size());
        }

        Container::AnsiString ToAnsiString(Container::AnsiStringView view)
        {
            return Container::AnsiString(ToStdString(view));
        }

        bool IsAsciiPrintable(char value)
        {
            const unsigned char byte = static_cast<unsigned char>(value);
            return byte >= 0x20 && byte <= 0x7e;
        }

        bool IsLowerHex(char value)
        {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        }

        uint8_t HexValue(char value)
        {
            if (value >= '0' && value <= '9')
            {
                return static_cast<uint8_t>(value - '0');
            }

            return static_cast<uint8_t>(10 + value - 'a');
        }

        bool IsAsciiJsonString(const Container::String &value)
        {
            for (const auto character : value)
            {
                const uint32_t codepoint = static_cast<uint32_t>(character);
                if (codepoint < 0x20 || codepoint == 0x7f || codepoint > 0x7f)
                {
                    return false;
                }
            }

            return true;
        }

        Container::AnsiString JsonStringToAnsi(const Container::String &value)
        {
            std::string result;
            result.reserve(value.size());
            for (const auto character : value)
            {
                result.push_back(static_cast<char>(character));
            }

            return Container::AnsiString(result);
        }

        bool TryReadStringMember(const JsonValue &object, const char *fieldName, Container::AnsiString &outValue)
        {
            const JsonValue field = object.FindMember(fieldName);
            if (!field.IsString())
            {
                return false;
            }

            const Container::String &jsonString = field.AsString();
            if (!IsAsciiJsonString(jsonString))
            {
                return false;
            }

            outValue = JsonStringToAnsi(jsonString);
            return true;
        }

        bool TryReadUInt32Member(const JsonValue &object, const char *fieldName, uint32_t &outValue)
        {
            const JsonValue field = object.FindMember(fieldName);
            if (!field.IsNumber())
            {
                return false;
            }

            const double number = field.AsNumber(-1.0);
            if (number < 0.0 || number > static_cast<double>(UINT32_MAX))
            {
                return false;
            }

            const uint32_t asUInt = field.AsUInt32(UINT32_MAX);
            if (static_cast<double>(asUInt) != number)
            {
                return false;
            }

            outValue = asUInt;
            return true;
        }

        bool IsValidLogicalManifestPath(Container::AnsiStringView input, Container::AnsiString &outNormalized)
        {
            const AssetPath path = AssetPath::Normalize(input);
            if (!path.IsValid() || path.IsAbsolute() || !path.HasLogicalPath())
            {
                return false;
            }

            outNormalized = path.GetLogicalPath();
            return !outNormalized.empty();
        }

        Container::AnsiString MakeDuplicateKey(const AssetCookedReference &reference)
        {
            Container::AnsiString key = reference.LogicalPath;
            key += "|";
            key += GetAssetKindName(reference.Kind);
            key += "|";
            key += reference.Variant;
            return key;
        }

        AssetManifestResolveResult MakeResolveResult(AssetManifestResolveStatus status)
        {
            AssetManifestResolveResult result;
            result.Status = status;
            return result;
        }
    }

    bool TryParseAssetKind(Container::AnsiStringView text, AssetKind &outKind)
    {
        if (text == Container::AnsiStringView("texture"))
        {
            outKind = AssetKind::Texture;
            return true;
        }

        if (text == Container::AnsiStringView("model"))
        {
            outKind = AssetKind::Model;
            return true;
        }

        if (text == Container::AnsiStringView("raw"))
        {
            outKind = AssetKind::Raw;
            return true;
        }

        outKind = AssetKind::Unknown;
        return false;
    }

    Container::AnsiString GetAssetKindName(AssetKind kind)
    {
        switch (kind)
        {
        case AssetKind::Texture:
            return Container::AnsiString("texture");
        case AssetKind::Model:
            return Container::AnsiString("model");
        case AssetKind::Raw:
            return Container::AnsiString("raw");
        default:
            return Container::AnsiString("unknown");
        }
    }

    bool TryParseAssetHashHex(Container::AnsiStringView text, uint64_t &outHash)
    {
        if (text.size() != 16)
        {
            return false;
        }

        uint64_t value = 0;
        for (size_t index = 0; index < text.size(); ++index)
        {
            const char character = text[index];
            if (!IsLowerHex(character))
            {
                return false;
            }

            value = (value << 4) | static_cast<uint64_t>(HexValue(character));
        }

        outHash = value;
        return true;
    }

    Container::AnsiString FormatAssetHashHex(uint64_t hash)
    {
        char buffer[17] = {};
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
        return Container::AnsiString(buffer);
    }

    bool TryParseAssetPackageFourCCText(Container::AnsiStringView text, AssetPackageFourCC &outFourCC)
    {
        if (text.size() != 4)
        {
            return false;
        }

        for (size_t index = 0; index < text.size(); ++index)
        {
            if (!IsAsciiPrintable(text[index]))
            {
                return false;
            }
        }

        outFourCC = MakeAssetPackageFourCC(text[0], text[1], text[2], text[3]);
        return true;
    }

    Container::AnsiString FormatAssetPackageFourCCText(AssetPackageFourCC fourCC)
    {
        char text[5] = {};
        text[0] = static_cast<char>(fourCC & 0xffu);
        text[1] = static_cast<char>((fourCC >> 8) & 0xffu);
        text[2] = static_cast<char>((fourCC >> 16) & 0xffu);
        text[3] = static_cast<char>((fourCC >> 24) & 0xffu);
        return Container::AnsiString(text);
    }

    void AssetManifest::Reset()
    {
        m_ParseStatus = AssetManifestParseStatus::ParseError;
        m_ParseError.clear();
        m_bLoadAttempted = false;
        m_bLoaded = false;
        m_References.clear();
    }

    bool AssetManifest::LoadFromJsonText(const Container::String &jsonText, Container::AnsiStringView sourceName)
    {
        Reset();
        m_bLoadAttempted = true;

        JsonDocument document;
        Container::String parseError;
        if (!JsonDocument::TryParse(jsonText, document, &parseError))
        {
            Container::AnsiString message("JSON parse failed");
            if (!sourceName.empty())
            {
                message += " in ";
                message += ToAnsiString(sourceName);
            }

            SetParseFailure(AssetManifestParseStatus::ParseError, message);
            return false;
        }

        return LoadFromDocument(document);
    }

    AssetManifestResolveResult AssetManifest::Resolve(Container::AnsiStringView logicalPath,
                                                      AssetKind kind,
                                                      Container::AnsiStringView variant) const
    {
        if (kind == AssetKind::Unknown || logicalPath.empty() || variant.empty())
        {
            return MakeResolveResult(AssetManifestResolveStatus::InvalidRequest);
        }

        Container::AnsiString normalizedLogicalPath;
        if (!IsValidLogicalManifestPath(logicalPath, normalizedLogicalPath))
        {
            return MakeResolveResult(AssetManifestResolveStatus::InvalidRequest);
        }

        if (!m_bLoaded)
        {
            if (m_bLoadAttempted)
            {
                return MakeResolveResult(AssetManifestResolveStatus::InvalidManifest);
            }

            return MakeManifestMissingFallback(normalizedLogicalPath, kind, variant);
        }

        for (const AssetCookedReference &reference : m_References)
        {
            if (reference.LogicalPath == normalizedLogicalPath &&
                reference.Kind == kind &&
                Container::AnsiStringView(reference.Variant) == variant)
            {
                AssetManifestResolveResult result;
                result.Status = AssetManifestResolveStatus::CookedReferenceFound;
                result.Reference = reference;
                return result;
            }
        }

        return MakeResolveResult(AssetManifestResolveStatus::LooseFallbackVariantMissing);
    }

    AssetManifestResolveResult AssetManifest::MakeManifestMissingFallback(Container::AnsiStringView,
                                                                          AssetKind,
                                                                          Container::AnsiStringView)
    {
        return MakeResolveResult(AssetManifestResolveStatus::LooseFallbackManifestMissing);
    }

    AssetFallbackDecision AssetManifest::DecideCookedFailureFallback(const AssetCookedReference &reference,
                                                                     AssetCookedFailureKind failureKind,
                                                                     AssetFallbackMode fallbackMode,
                                                                     Container::AnsiStringView reason)
    {
        AssetFallbackDecision decision;
        decision.FailureKind = failureKind;
        decision.LogicalPath = reference.LogicalPath;
        decision.CookedPackage = reference.CookedPackage;
        decision.EntryName = reference.EntryName;
        decision.Reason = ToAnsiString(reason);

        if (fallbackMode == AssetFallbackMode::DebugAllowLooseFallback)
        {
            decision.Action = AssetFallbackAction::UseLoose;
            decision.bRequiresExplicitLog = true;
            return decision;
        }

        decision.Action = AssetFallbackAction::Fail;
        decision.bRequiresExplicitLog = false;
        return decision;
    }

    bool AssetManifest::LoadFromDocument(const JsonDocument &document)
    {
        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            SetParseFailure(AssetManifestParseStatus::RootNotObject, Container::AnsiStringView("manifest root must be an object"));
            return false;
        }

        uint32_t manifestVersion = 0;
        if (!TryReadUInt32Member(root, "version", manifestVersion) || manifestVersion != 1)
        {
            SetParseFailure(AssetManifestParseStatus::UnsupportedVersion, Container::AnsiStringView("manifest version must be 1"));
            return false;
        }

        const JsonValue assetsValue = root.FindMember("assets");
        if (!assetsValue.IsArray())
        {
            SetParseFailure(AssetManifestParseStatus::AssetsNotArray, Container::AnsiStringView("manifest assets must be an array"));
            return false;
        }

        for (size_t index = 0; index < assetsValue.GetArraySize(); ++index)
        {
            const JsonValue assetValue = assetsValue.GetArrayElement(index);
            if (!assetValue.IsObject())
            {
                SetParseFailure(AssetManifestParseStatus::InvalidField, Container::AnsiStringView("asset entry must be an object"));
                return false;
            }

            AssetCookedReference reference;
            Container::AnsiString logicalPath;
            Container::AnsiString cookedPackage;
            Container::AnsiString entryName;
            Container::AnsiString kindText;
            Container::AnsiString sourceHashText;
            Container::AnsiString cookedHashText;

            if (!TryReadStringMember(assetValue, "logical_path", logicalPath) ||
                !TryReadStringMember(assetValue, "kind", kindText) ||
                !TryReadStringMember(assetValue, "source_hash", sourceHashText) ||
                !TryReadStringMember(assetValue, "variant", reference.Variant) ||
                !TryReadStringMember(assetValue, "format", reference.Format) ||
                !TryReadStringMember(assetValue, "cooked_package", cookedPackage) ||
                !TryReadStringMember(assetValue, "entry_name", entryName) ||
                !TryReadStringMember(assetValue, "entry_type", reference.EntryTypeText) ||
                !TryReadStringMember(assetValue, "cooked_hash", cookedHashText) ||
                !TryReadUInt32Member(assetValue, "cooked_version", reference.CookedVersion))
            {
                SetParseFailure(AssetManifestParseStatus::RequiredFieldMissing, Container::AnsiStringView("asset entry is missing a required field or has the wrong type"));
                return false;
            }

            if (!IsValidLogicalManifestPath(logicalPath, reference.LogicalPath) ||
                !IsValidLogicalManifestPath(cookedPackage, reference.CookedPackage) ||
                !IsValidLogicalManifestPath(entryName, reference.EntryName))
            {
                SetParseFailure(AssetManifestParseStatus::InvalidField, Container::AnsiStringView("asset entry has an invalid path"));
                return false;
            }

            if (!TryParseAssetKind(kindText, reference.Kind) || reference.Kind == AssetKind::Unknown)
            {
                SetParseFailure(AssetManifestParseStatus::InvalidField, Container::AnsiStringView("asset entry has an invalid kind"));
                return false;
            }

            if (!TryParseAssetHashHex(sourceHashText, reference.SourceHash) ||
                !TryParseAssetHashHex(cookedHashText, reference.CookedHash))
            {
                SetParseFailure(AssetManifestParseStatus::InvalidField, Container::AnsiStringView("asset entry has an invalid hash"));
                return false;
            }

            if (reference.Variant.empty() || reference.Format.empty())
            {
                SetParseFailure(AssetManifestParseStatus::InvalidField, Container::AnsiStringView("asset entry has an empty variant or format"));
                return false;
            }

            if (!TryParseAssetPackageFourCCText(reference.EntryTypeText, reference.EntryType))
            {
                SetParseFailure(AssetManifestParseStatus::InvalidField, Container::AnsiStringView("asset entry has an invalid entry_type"));
                return false;
            }

            reference.SourceHashHex = sourceHashText;
            reference.CookedHashHex = cookedHashText;

            if (!AddReference(reference))
            {
                SetParseFailure(AssetManifestParseStatus::DuplicateAsset, Container::AnsiStringView("asset entry duplicates logical_path, kind, and variant"));
                return false;
            }
        }

        m_ParseStatus = AssetManifestParseStatus::Success;
        m_ParseError.clear();
        m_bLoadAttempted = true;
        m_bLoaded = true;
        return true;
    }

    bool AssetManifest::AddReference(const AssetCookedReference &reference)
    {
        const Container::AnsiString newKey = MakeDuplicateKey(reference);
        for (const AssetCookedReference &existing : m_References)
        {
            if (MakeDuplicateKey(existing) == newKey)
            {
                return false;
            }
        }

        m_References.push_back(reference);
        return true;
    }

    void AssetManifest::SetParseFailure(AssetManifestParseStatus status, Container::AnsiStringView message)
    {
        m_ParseStatus = status;
        m_ParseError = ToAnsiString(message);
        m_bLoadAttempted = true;
        m_bLoaded = false;
        m_References.clear();
    }
}
