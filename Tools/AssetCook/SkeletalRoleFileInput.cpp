#include "SkeletalRoleFileInput.h"
#include "SkeletalRoleFileInputTestAccess.h"
#include "SingleAssetCook.h"
#include "NativeCookPath.h"
#include "Resource/ImportSettingsFile.h"
#include <fstream>
#include <limits>
#include <type_traits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace C = Core::Container;
        bool Fail(C::AnsiString& error, const char* role, const std::filesystem::path& path, const char* reason)
        {
            C::AnsiString locator;
            if (!Detail::EncodeCookPathUtf8(path, locator))
            {
                locator = "<invalid Unicode>";
            }
            error = C::AnsiString("role_file: ") + role + " path=" + locator + " reason=" + reason;
            return false;
        }
        bool Read(const std::filesystem::path& path, size_t maximum, const char* role, C::VariableArray<uint8_t>& bytes,
                  C::AnsiString& error, Detail::SkeletalRoleReadProbe probe, void* context)
        {
            if (path.empty() || !Core::Gltf::IsValidNativeSourcePath(path))
            {
                return Fail(error, role, path, "invalid_locator");
            }
            std::error_code code;
            if (!std::filesystem::is_regular_file(path, code) || code)
            {
                return Fail(error, role, path, "not_regular_file");
            }
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                return Fail(error, role, path, "open_failed");
            }
            const auto length = file.tellg();
            if (length < 0 || static_cast<uintmax_t>(length) > maximum ||
                static_cast<uintmax_t>(length) > bytes.max_size() ||
                static_cast<uintmax_t>(length) > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return Fail(error, role, path, "invalid_size_or_budget");
            }
            if (probe)
            {
                probe(path, context);
            }
            bytes.resize(static_cast<size_t>(length));
            file.seekg(0);
            if (!bytes.empty())
            {
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            if (!file || file.peek() != std::char_traits<char>::eof() || file.bad())
            {
                return Fail(error, role, path, "read_changed_or_failed");
            }
            return true;
        }
        void Integer(uint64_t& hash, uint64_t value)
        {
            for (unsigned i = 0; i < 8; ++i)
            {
                hash ^= (value >> (i * 8)) & 255;
                hash *= 1099511628211ull;
            }
        }
    } // namespace
    bool DecodeSkeletalRoleUtf8Text(C::AnsiStringView bytes, C::String& out)
    {
        using Char = C::String::value_type;
        const C::Span<const uint8_t> input{reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()};
        const auto measured = Core::Asset::MeasureSkeletalNameDecoding<Char>(2, input);
        if (!measured.Succeeded() || measured.CodeUnitCount == SIZE_MAX)
        {
            return false;
        }
        C::VariableArray<Char> units(measured.CodeUnitCount + 1, Char{});
        if (!Core::Asset::DecodeSkeletalWireName<Char>(2, input, {units.data(), measured.CodeUnitCount}).Succeeded())
        {
            return false;
        }
        C::String candidate(units.data());
        out = std::move(candidate);
        return true;
    }
    bool ValidateSkeletalRoleFileRequest(const SingleAssetCookRequest& request, C::AnsiString& error)
    {
        const auto& r = request.RoleProfile;
        if (!r.bEnabled || r.BvhPath.empty() || r.ProfilePath.empty() || r.ClipName.empty() ||
            (r.Operation != SkeletalBvhClipOperation::Add && r.Operation != SkeletalBvhClipOperation::Replace) ||
            request.Kind != "model" || request.EntryTypeText != "Skl0" ||
            !IsSupportedSkeletalCookFormat(request.Format) || request.bSkipIfUnchanged || r.MaxNvskelBytes == 0 ||
            !Core::Gltf::IsValidNativeSourcePath(request.InputPath) ||
            !Core::Gltf::IsValidNativeSourcePath(request.ImportSettingsOverridePath) ||
            !Core::Gltf::IsValidNativeSourcePath(r.BvhPath) || !Core::Gltf::IsValidNativeSourcePath(r.ProfilePath))
        {
            error = "role_file: model/Skl0・完全なrole要求・常時cookが必要です";
            return false;
        }
        using Char = C::String::value_type;
        const auto name =
            Core::Asset::MeasureSkeletalNameEncoding(2, C::Span<const Char>{r.ClipName.data(), r.ClipName.size()});
        if (!name.Succeeded() || name.ByteCount > r.ClipLimits.MaxClipNameBytes)
        {
            error = "role_file: clip名のUnicodeまたはbyte予算が不正です";
            return false;
        }
        return true;
    }
    bool Detail::LoadSkeletalRoleFileInputsWithProbe(const SkeletalRoleFileRequest& request,
                                                     SkeletalRoleFileInputs& out, C::AnsiString& error,
                                                     SkeletalRoleReadProbe probe, void* context)
    {
        SkeletalRoleFileInputs candidate;
        if (!Read(request.BvhPath, request.DecodeLimits.MaxInputBytes, "bvh", candidate.BvhBytes, error, probe,
                  context) ||
            !Read(request.ProfilePath, request.ProfileLimits.MaxInputBytes, "profile", candidate.ProfileBytes, error,
                  probe, context))
        {
            return false;
        }
        out = std::move(candidate);
        return true;
    }
    bool LoadSkeletalRoleFileInputs(const SkeletalRoleFileRequest& request, SkeletalRoleFileInputs& out,
                                    C::AnsiString& error)
    {
        return Detail::LoadSkeletalRoleFileInputsWithProbe(request, out, error, nullptr, nullptr);
    }
    SkeletalRoleProfileCookRequest MakeSkeletalRoleBytesRequest(const SkeletalRoleFileRequest& r,
                                                                const SkeletalRoleFileInputs& inputs)
    {
        SkeletalRoleProfileCookRequest out;
        out.BvhBytes = {inputs.BvhBytes.data(), inputs.BvhBytes.size()};
        out.ProfileBytes = {inputs.ProfileBytes.data(), inputs.ProfileBytes.size()};
        out.ClipName = r.ClipName;
        out.Operation = r.Operation;
        out.ProfileLimits = r.ProfileLimits;
        out.ClipLimits = r.ClipLimits;
        out.MappingLimits = r.MappingLimits;
        out.DecodeLimits = r.DecodeLimits;
        out.MaxNvskelBytes = r.MaxNvskelBytes;
        return out;
    }
    bool AppendSkeletalRoleFileSettingsHash(uint64_t seed, const SkeletalRoleFileRequest& r, uint64_t& out,
                                            C::AnsiString& error)
    {
        const C::Span<const C::String::value_type> name{r.ClipName.data(), r.ClipName.size()};
        const auto measured = Core::Asset::MeasureSkeletalNameEncoding(2, name);
        if (!measured.Succeeded())
        {
            error = "role_file: clip名をencodeできません";
            return false;
        }
        C::VariableArray<uint8_t> bytes(measured.ByteCount);
        if (!Core::Asset::EncodeSkeletalWireName(2, name, {bytes.data(), bytes.size()}).Succeeded())
        {
            return false;
        }
        uint64_t hash = seed;
        Integer(hash, 0x524f4c4546494c45ull);
        Integer(hash, 1);
        Integer(hash, SkeletalRoleProfileParserRevision);
        Integer(hash, SkeletalRoleProfileVocabularyRevision);
        Integer(hash, static_cast<uint8_t>(r.Operation));
        Integer(hash, bytes.size());
        for (uint8_t value : bytes)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        const auto& p = r.ProfileLimits;
        const auto& l = r.ClipLimits;
        const uint64_t limits[] = {p.MaxInputBytes,
                                   p.MaxDepth,
                                   p.MaxSyntaxTokens,
                                   p.MaxSourceElements,
                                   p.MaxMappings,
                                   p.MaxNameBytes,
                                   p.MaxTotalNameBytes,
                                   l.Source.MaxJoints,
                                   l.Source.MaxDepth,
                                   l.Source.MaxFrames,
                                   l.Source.MaxValues,
                                   l.Source.MaxNameBytes,
                                   l.Source.MaxTotalNameBytes,
                                   l.TargetNames.MaxJoints,
                                   l.TargetNames.MaxNameBytes,
                                   l.TargetNames.MaxTotalBytes,
                                   l.MaxClipNameBytes,
                                   l.MaxOutputKeys,
                                   l.MaxJointFrames,
                                   l.MaxWorkUnits,
                                   l.MaxOwnedBytes,
                                   r.MappingLimits.MaxCatalogJoints,
                                   r.MappingLimits.MaxMappings,
                                   r.MappingLimits.MaxTotalNameBytes,
                                   r.DecodeLimits.MaxInputBytes,
                                   r.DecodeLimits.MaxJoints,
                                   r.DecodeLimits.MaxDepth,
                                   r.DecodeLimits.MaxFrames,
                                   r.DecodeLimits.MaxValues,
                                   r.DecodeLimits.MaxNameBytes,
                                   r.DecodeLimits.MaxNumberBytes,
                                   r.MaxNvskelBytes};
        for (uint64_t value : limits)
        {
            Integer(hash, value);
        }
        out = hash;
        return true;
    }
    bool FingerprintSkeletalRoleFileSource(const uint8_t* source, size_t size, const SingleAssetCookRequest& request,
                                           ModelCookFingerprint& out, C::AnsiString& error)
    {
        if (!ValidateSkeletalRoleFileRequest(request, error))
        {
            return false;
        }
        SkeletalRoleFileInputs inputs;
        if (!LoadSkeletalRoleFileInputs(request.RoleProfile, inputs, error))
        {
            return false;
        }
        const auto typed = MakeSkeletalRoleBytesRequest(request.RoleProfile, inputs);
        Core::AssetImport::ImportSettingsFileOptions settings;
        settings.OverridePath = request.ImportSettingsOverridePath;
        settings.bDisabled = request.bNoSidecar;
        settings.bRequired = request.bRequireSidecar;
        return FingerprintGltfWithRoleProfileNativePath(source, size, request.Format, request.InputPath,
                                                        request.LogicalPath, typed, out, error, &settings,
                                                        &request.SkeletalDecode);
    }
} // namespace NorvesLib::Tools::AssetCook
