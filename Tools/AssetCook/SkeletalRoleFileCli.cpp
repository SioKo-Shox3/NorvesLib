#include "SkeletalRoleFileCli.h"
#include "NativeCookArguments.h"
#include "SkeletalRoleFileCliTestAccess.h"
#include "SkeletalRoleFileCook.h"
#include "SkeletalRoleFileInput.h"
#include "NativeCookPath.h"
#include "Asset/AssetManifest.h"
#include "SkeletalCliOptions.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <cstring>
#include <cwchar>
#include <iostream>
#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace C = Core::Container;
        bool Equals(C::AnsiStringView a, const char* b)
        {
            return a == C::AnsiStringView(b);
        }
        C::AnsiStringView Key(C::AnsiStringView token)
        {
            const auto position = token.find('=');
            return position == C::AnsiStringView::npos ? token : token.substr(0, position);
        }
        bool RoleKey(C::AnsiStringView key)
        {
            return Equals(key, "--bvh") || Equals(key, "--role-profile") || Equals(key, "--clip-operation") ||
                   Equals(key, "--clip-name");
        }
        bool TakesValue(C::AnsiStringView key)
        {
            const char* names[] = {"--input",
                                   "--out",
                                   "--manifest",
                                   "--logical",
                                   "--kind",
                                   "--entry",
                                   "--entry-type",
                                   "--format",
                                   "--variant",
                                   "--import-settings",
                                   "--inspect",
                                   "--asset-set",
                                   "--runtime-root",
                                   "--source-root",
                                   "--skin-influences",
                                   "--skin-warn-dropped-weight",
                                   "--skin-fail-dropped-weight",
                                   "--morph",
                                   "--cubicspline",
                                   "--cubic-translation-tolerance",
                                   "--cubic-rotation-tolerance-deg",
                                   "--cubic-scale-tolerance",
                                   "--cubic-max-depth",
                                   "--cubic-max-channel-samples",
                                   "--cubic-max-asset-samples"};
            for (const auto* name : names)
            {
                if (Equals(key, name))
                {
                    return true;
                }
            }
            return false;
        }
        bool Detect(int argc, const char* const* argv)
        {
            if (!argv)
            {
                return false;
            }
            for (int i = 1; i < argc; ++i)
            {
                if (!argv[i])
                {
                    continue;
                }
                const C::AnsiStringView token(argv[i]);
                const auto key = Key(token);
                if (RoleKey(key))
                {
                    return true;
                }
                if (TakesValue(key) && token.find('=') == C::AnsiStringView::npos)
                {
                    ++i;
                }
            }
            return false;
        }
        bool Fail(C::AnsiString& error, const char* reason)
        {
            error = C::AnsiString("role_cli: ") + reason;
            return false;
        }
        bool Arguments(int argc, const char* const* argv, C::VariableArray<C::AnsiString>& out, C::AnsiString& error)
        {
            C::VariableArray<C::AnsiString> owned;
#ifdef _WIN32
            // narrow argvをUTF8と仮定せず、OSが保持する元のUTF16引数を厳密に変換する。
            int count = 0;
            struct WideArguments
            {
                wchar_t** Value = nullptr;
                ~WideArguments()
                {
                    if (Value)
                    {
                        LocalFree(Value);
                    }
                }
            } wide;
            wide.Value = CommandLineToArgvW(GetCommandLineW(), &count);
            if (!wide.Value || count < 1 || count > 128)
            {
                return Fail(error, "argument_count");
            }
            (void)argc;
            (void)argv;
            size_t total = 0;
            for (int i = 0; i < count; ++i)
            {
                const C::Span<const wchar_t> units{wide.Value[i], std::wcslen(wide.Value[i])};
                C::AnsiString token;
                if (!Detail::DecodeSkeletalRoleWideArgument(units, token, error) || token.size() > 256u * 1024u - total)
                {
                    return Fail(error, "argument_unicode_or_budget");
                }
                total += token.size();
                owned.push_back(std::move(token));
            }
#else
            if (!argv || argc < 1 || argc > 128)
            {
                return Fail(error, "argument_count");
            }
            size_t total = 0;
            for (int i = 0; i < argc; ++i)
            {
                if (!argv[i])
                {
                    return Fail(error, "null_argument");
                }
                const C::AnsiStringView text(argv[i]);
                C::String decoded;
                if (text.size() > 256u * 1024u - total || !DecodeSkeletalRoleUtf8Text(text, decoded))
                {
                    return Fail(error, "argument_unicode_or_budget");
                }
                owned.push_back(C::AnsiString(text));
                total += text.size();
            }
#endif
            out = std::move(owned);
            return true;
        }
        bool Parse(const C::VariableArray<C::AnsiString>& owned, SingleAssetCookRequest& out, C::AnsiString& error)
        {
            SingleAssetCookRequest candidate;
            SkeletalCliOptions skeletal;
            candidate.RoleProfile.bEnabled = true;
            C::VariableArray<const char*> argv;
            for (const auto& value : owned)
            {
                argv.push_back(value.c_str());
            }
            const char* names[] = {"--input",        "--out",
                                   "--manifest",     "--logical",
                                   "--kind",         "--entry",
                                   "--entry-type",   "--format",
                                   "--variant",      "--bvh",
                                   "--role-profile", "--clip-operation",
                                   "--clip-name",    "--import-settings",
                                   "--no-sidecar",   "--require-sidecar"};
            bool seen[16] = {};
            for (int i = 1; i < static_cast<int>(argv.size()); ++i)
            {
                const char* skeletalError = nullptr;
                const auto parsed =
                    ParseSkeletalArgument(static_cast<int>(argv.size()), argv.data(), i, skeletal, skeletalError);
                if (parsed == ImportArgumentResult::Rejected)
                {
                    error = skeletalError;
                    return false;
                }
                if (parsed == ImportArgumentResult::Accepted)
                {
                    continue;
                }
                const C::AnsiStringView token(argv[static_cast<size_t>(i)]);
                const auto key = Key(token);
                size_t option = 16;
                for (size_t n = 0; n < 16; ++n)
                {
                    if (Equals(key, names[n]))
                    {
                        option = n;
                        break;
                    }
                }
                if (option == 16)
                {
                    return Fail(error, "unknown_or_mixed_mode_argument");
                }
                if (seen[option])
                {
                    return Fail(error, "duplicate_argument");
                }
                seen[option] = true;
                const auto equals = token.find('=');
                if (option >= 14)
                {
                    if (equals != C::AnsiStringView::npos)
                    {
                        return Fail(error, "flag_has_value");
                    }
                    if (option == 14)
                    {
                        candidate.bNoSidecar = true;
                    }
                    else
                    {
                        candidate.bRequireSidecar = true;
                    }
                    continue;
                }
                C::AnsiStringView value;
                if (equals != C::AnsiStringView::npos)
                {
                    value = token.substr(equals + 1);
                }
                else
                {
                    if (++i >= static_cast<int>(argv.size()))
                    {
                        return Fail(error, "missing_value");
                    }
                    value = C::AnsiStringView(argv[static_cast<size_t>(i)]);
                }
                if (value.empty())
                {
                    return Fail(error, "empty_value");
                }
                const auto path = [&]() { return std::filesystem::u8path(value.data(), value.data() + value.size()); };
                switch (option)
                {
                case 0:
                    candidate.InputPath = path();
                    break;
                case 1:
                    candidate.PackagePath = path();
                    break;
                case 2:
                    candidate.ManifestPath = path();
                    break;
                case 3:
                    candidate.LogicalPath = C::AnsiString(value);
                    break;
                case 4:
                    candidate.Kind = C::AnsiString(value);
                    break;
                case 5:
                    candidate.EntryName = C::AnsiString(value);
                    break;
                case 6:
                    candidate.EntryTypeText = C::AnsiString(value);
                    break;
                case 7:
                    candidate.Format = C::AnsiString(value);
                    break;
                case 8:
                    candidate.Variant = C::AnsiString(value);
                    break;
                case 9:
                    candidate.RoleProfile.BvhPath = path();
                    break;
                case 10:
                    candidate.RoleProfile.ProfilePath = path();
                    break;
                case 11:
                    if (Equals(value, "add"))
                    {
                        candidate.RoleProfile.Operation = SkeletalBvhClipOperation::Add;
                    }
                    else if (Equals(value, "replace"))
                    {
                        candidate.RoleProfile.Operation = SkeletalBvhClipOperation::Replace;
                    }
                    else
                    {
                        return Fail(error, "invalid_operation");
                    }
                    break;
                case 12:
                    if (!DecodeSkeletalRoleUtf8Text(value, candidate.RoleProfile.ClipName))
                    {
                        return Fail(error, "clip_name_unicode");
                    }
                    break;
                case 13:
                    candidate.ImportSettingsOverridePath = path();
                    break;
                default:
                    return Fail(error, "invalid_option");
                }
            }
            for (size_t i = 0; i < 13; ++i)
            {
                if (!seen[i])
                {
                    return Fail(error, "required_argument");
                }
            }
            if (candidate.bNoSidecar && (candidate.bRequireSidecar || !candidate.ImportSettingsOverridePath.empty()))
            {
                return Fail(error, "sidecar_mode_conflict");
            }
            const char* skeletalError = nullptr;
            if (!ValidateSkeletalArguments(skeletal, true, skeletalError))
            {
                error = skeletalError;
                return false;
            }
            candidate.SkeletalDecode = skeletal.Decode;
            if (!ValidateSkeletalRoleFileRequest(candidate, error))
            {
                return false;
            }
            out = std::move(candidate);
            return true;
        }
    } // namespace
    bool Detail::CollectNativeCookArguments(int argc, const char* const* argv, C::VariableArray<C::AnsiString>& out,
                                            C::AnsiString& error)
    {
        return Arguments(argc, argv, out, error);
    }
    bool Detail::HasSkeletalRoleCliArguments(int argc, const char* const* argv)
    {
        return Detect(argc, argv);
    }
    bool Detail::DecodeSkeletalRoleWideArgument(C::Span<const wchar_t> units, C::AnsiString& out, C::AnsiString& error)
    {
        const auto measured = Core::Asset::MeasureSkeletalNameEncoding(2, units);
        if (!measured.Succeeded() || measured.ByteCount > 256u * 1024u)
        {
            return Fail(error, "argument_unicode_or_budget");
        }
        C::VariableArray<uint8_t> bytes(measured.ByteCount);
        if (!Core::Asset::EncodeSkeletalWireName(2, units, {bytes.data(), bytes.size()}).Succeeded())
        {
            return Fail(error, "argument_unicode");
        }
        C::AnsiString candidate(C::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        out = std::move(candidate);
        return true;
    }
    bool Detail::ValidateSkeletalRoleModeAgreement(int argc, const char* const* argv,
                                                   const C::VariableArray<C::AnsiString>& wideTokens,
                                                   C::AnsiString& error)
    {
        C::VariableArray<const char*> wide;
        for (const auto& token : wideTokens)
        {
            wide.push_back(token.c_str());
        }
        return Detect(argc, argv) == Detect(static_cast<int>(wide.size()), wide.data()) ||
               Fail(error, "narrow_wide_mode_mismatch");
    }
    bool Detail::ParseSkeletalRoleUtf8Arguments(const C::VariableArray<C::AnsiString>& tokens,
                                                SingleAssetCookRequest& out, C::AnsiString& error)
    {
        if (tokens.empty() || tokens.size() > 128)
        {
            return Fail(error, "argument_count");
        }
        size_t total = 0;
        for (const auto& token : tokens)
        {
            C::String decoded;
            if (token.size() > 256u * 1024u - total || !DecodeSkeletalRoleUtf8Text(token, decoded))
            {
                return Fail(error, "argument_unicode_or_budget");
            }
            total += token.size();
        }
        return Parse(tokens, out, error);
    }
    bool RunSkeletalRoleFileCommand(int argc, const char* const* argv, int& exitCode)
    {
        if (!Detect(argc, argv))
        {
            return false;
        }
        exitCode = 1;
        C::AnsiString error;
        try
        {
            C::VariableArray<C::AnsiString> owned;
            SingleAssetCookRequest request;
            SkeletalRoleFileCookResult result;
            if (Arguments(argc, argv, owned, error) &&
                Detail::ValidateSkeletalRoleModeAgreement(argc, argv, owned, error) &&
                Detail::ParseSkeletalRoleUtf8Arguments(owned, request, error) &&
                CookSkeletalRoleFile(request, result, error))
            {
                // 成功reportは実出力検証後だけ出す。追加のfile書き込みはしない。
                C::AnsiString sidecar = "none";
                if (result.Value.Cooked.Cook.bHasImportSettings &&
                    !Detail::EncodeCookPathUtf8(result.Value.Cooked.Cook.ImportSettingsPath, sidecar))
                {
                    throw std::runtime_error("role_cli: sidecar_unicode");
                }
                std::cout << "sidecar: " << sidecar.c_str() << " settings_hash="
                          << Core::Asset::FormatAssetHashHex(result.Value.Cooked.Cook.ImportSettingsHash).c_str()
                          << '\n';
                std::cerr << "role_profile_report=" << result.ReportJson.c_str() << '\n';
                std::cout << "AssetCook cooked role-profile operation="
                          << (request.RoleProfile.Operation == SkeletalBvhClipOperation::Add ? "add" : "replace")
                          << " clip_index=" << result.Value.Cooked.ClipIndex
                          << " source_hash=" << result.Value.Cooked.Cook.SourceHash
                          << " dependency_hash=" << result.DependencyFingerprint << '\n';
                exitCode = 0;
                return true;
            }
        }
        catch (const std::exception&)
        {
            error = "role_cli: execution_exception";
        }
        std::cerr << "AssetCook error: " << error.c_str() << '\n';
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
