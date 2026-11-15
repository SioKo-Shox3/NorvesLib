#include "SkeletalRoleFileCli.h"
#include "NativeCookArguments.h"
#include "SkeletalRoleFileCliTestAccess.h"
#include "SkeletalRoleFileCook.h"
#include "SkeletalRoleFileInput.h"
#include "NativeCookPath.h"
#include "Asset/AssetManifest.h"
#include "SkeletalCliOptions.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Resource/BvhEvaluate.h"
#include <charconv>
#include <limits>
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
                   Equals(key, "--clip-name") || Equals(key, "--dump-fk");
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
                                   "--cubic-max-asset-samples",
                                   "--clip-max-vertex-error-m",
                                   "--clip-max-joint-error-m",
                                   "--clip-max-rotation-error-deg",
                                   "--clip-report"};
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
                                   "--no-sidecar",   "--require-sidecar",
                                   "--clip-optimize", "--clip-max-vertex-error-m",
                                   "--clip-max-joint-error-m", "--clip-max-rotation-error-deg",
                                   "--clip-report"};
            constexpr size_t optionCount = sizeof(names) / sizeof(names[0]);
            bool seen[optionCount] = {};
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
                size_t option = optionCount;
                for (size_t n = 0; n < optionCount; ++n)
                {
                    if (Equals(key, names[n]))
                    {
                        option = n;
                        break;
                    }
                }
                if (option == optionCount)
                {
                    return Fail(error, "unknown_or_mixed_mode_argument");
                }
                if (seen[option])
                {
                    return Fail(error, "duplicate_argument");
                }
                seen[option] = true;
                const auto equals = token.find('=');
                if (option >= 16)
                {
                    const char* raw = equals != C::AnsiStringView::npos ? token.data() + equals + 1 : nullptr;
                    if (!raw && option != 16 && i + 1 < static_cast<int>(argv.size()))
                    {
                        raw = argv[static_cast<size_t>(++i)];
                    }
                    // 共通解析に指定状態を反映し、パスは UTF-8 から構築する。
                    const bool nativeReport = option == 20 && raw && *raw && std::strncmp(raw, "--", 2) != 0;
                    const char* parsedValue = nativeReport ? "." : raw;
                    bool consumed = false;
                    std::string clipError;
                    if (!TryParseClipOptimizeArgument({key.data(), key.size()}, parsedValue, candidate.ClipOptimize,
                                                      consumed, clipError))
                    {
                        error = clipError.c_str();
                        return false;
                    }
                    if (nativeReport)
                    {
                        candidate.ClipOptimize.ReportPath = std::filesystem::u8path(raw);
                    }
                    continue;
                }
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
            std::string clipError;
            if (!ValidateClipOptimizeOptions(candidate.ClipOptimize, clipError))
            {
                error = clipError.c_str();
                return false;
            }
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
    bool Detail::RunSkeletalBvhFkDump(const C::VariableArray<C::AnsiString>& tokens,
                                      C::AnsiString& json, C::AnsiString& error)
    {
        // 出力先との衝突は、ほかの引数の位置や妥当性より先に拒否する。
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            const auto key = Key(tokens[i]);
            if (Equals(key, "--out") || Equals(key, "--manifest") || Equals(key, "--clip-report"))
            {
                return Fail(error, "dump_fk_output_conflict");
            }
        }
        const char* names[] = {"--dump-fk", "--bvh", "--position-convention"};
        bool seen[3] = {};
        C::AnsiStringView values[3] = {{}, {}, "absolute"};
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            const C::AnsiStringView token(tokens[i]);
            const auto key = Key(token);
            size_t option = 3;
            for (size_t n = 0; n < 3; ++n)
            {
                if (Equals(key, names[n]))
                {
                    option = n;
                    break;
                }
            }
            if (option == 3)
            {
                return Fail(error, "dump_fk_unexpected_argument");
            }
            if (seen[option])
            {
                return Fail(error, "duplicate_argument");
            }
            seen[option] = true;
            const auto equals = token.find('=');
            if (equals != C::AnsiStringView::npos)
            {
                values[option] = token.substr(equals + 1);
            }
            else
            {
                if (++i >= tokens.size() || Equals(C::AnsiStringView(tokens[i]).substr(0, 2), "--"))
                {
                    return Fail(error, "missing_value");
                }
                values[option] = C::AnsiStringView(tokens[i]);
            }
            if (values[option].empty())
            {
                return Fail(error, "empty_value");
            }
        }
        if (!seen[1])
        {
            return Fail(error, "dump_fk_missing_bvh");
        }
        if (!seen[0])
        {
            return Fail(error, "missing_value");
        }
        auto convention = Core::Bvh::TranslationConvention::AbsoluteLocalChannels;
        if (Equals(values[2], "additive"))
        {
            convention = Core::Bvh::TranslationConvention::OffsetPlusChannels;
        }
        else if (!Equals(values[2], "absolute"))
        {
            return Fail(error, "dump_fk_position_convention");
        }
        C::VariableArray<uint32_t> frames;
        size_t begin = 0;
        while (true)
        {
            const auto comma = values[0].find(',', begin);
            const auto end = comma == C::AnsiStringView::npos ? values[0].size() : comma;
            const size_t length = end - begin;
            if (length == 0 || length > 10 || frames.size() == 4096)
            {
                return Fail(error, "dump_fk_frames");
            }
            uint64_t frame = 0;
            for (size_t i = begin; i < end; ++i)
            {
                const char digit = values[0][i];
                if (digit < '0' || digit > '9')
                {
                    return Fail(error, "dump_fk_frames");
                }
                frame = frame * 10 + static_cast<unsigned>(digit - '0');
            }
            if (frame > UINT32_MAX)
            {
                return Fail(error, "dump_fk_frames");
            }
            frames.push_back(static_cast<uint32_t>(frame));
            if (comma == C::AnsiStringView::npos)
            {
                break;
            }
            begin = comma + 1;
        }
        for (size_t i = 0; i < frames.size(); ++i)
        {
            for (size_t j = 0; j < i; ++j)
            {
                if (frames[i] == frames[j])
                {
                    return Fail(error, "dump_fk_duplicate_frame");
                }
            }
        }
        const auto path = std::filesystem::u8path(values[1].data(), values[1].data() + values[1].size());
        C::VariableArray<uint8_t> bytes;
        if (!LoadSkeletalBvhFileBytes(path, Core::Bvh::BvhDecodeLimits{}.MaxInputBytes, bytes, error))
        {
            return false;
        }
        Core::Bvh::BvhDocument document;
        if (!Core::Bvh::DecodeBvh({bytes.data(), bytes.size()}, {}, document).Succeeded())
        {
            return Fail(error, "dump_fk_decode");
        }
        for (uint32_t frame : frames)
        {
            if (frame >= document.FrameCount)
            {
                return Fail(error, "dump_fk_frame_out_of_range");
            }
        }
        C::AnsiString candidate =
            "{\"format\":\"norves-bvh-fk\",\"version\":1,\"producer\":\"AssetCook\",\"position_convention\":\"";
        candidate += values[2];
        candidate += "\",\"frames\":[";
        const auto appendFrame = [&](uint32_t frame)
        {
            char number[16];
            const auto result = std::to_chars(number, number + sizeof(number), frame);
            candidate += C::AnsiStringView(number, static_cast<size_t>(result.ptr - number));
        };
        const auto appendPosition = [&](double value)
        {
            // general と max_digits10 でロケールに依存せず double を往復できる桁数を残す。
            char number[64];
            const auto result = std::to_chars(number, number + sizeof(number), value, std::chars_format::general,
                                              std::numeric_limits<double>::max_digits10);
            candidate += C::AnsiStringView(number, static_cast<size_t>(result.ptr - number));
        };
        for (size_t i = 0; i < frames.size(); ++i)
        {
            if (i != 0)
            {
                candidate += ',';
            }
            appendFrame(frames[i]);
        }
        candidate += "],\"positions\":[";
        bool bFirst = true;
        for (uint32_t frame : frames)
        {
            Core::Bvh::BvhPose pose;
            if (!Core::Bvh::EvaluateBvhFrame(document, frame, convention, pose).Succeeded())
            {
                return Fail(error, "dump_fk_evaluate");
            }
            for (size_t j = 0; j < document.Joints.size(); ++j)
            {
                if (!bFirst)
                {
                    candidate += ',';
                }
                bFirst = false;
                candidate += "{\"joint\":\"";
                for (uint8_t byte : document.Joints[j].Name)
                {
                    if (byte == '"' || byte == '\\')
                    {
                        candidate += '\\';
                        candidate += static_cast<char>(byte);
                    }
                    else if (byte < 0x20)
                    {
                        constexpr char hex[] = "0123456789abcdef";
                        candidate += "\\u00";
                        candidate += hex[byte >> 4];
                        candidate += hex[byte & 15];
                    }
                    else
                    {
                        candidate += static_cast<char>(byte);
                    }
                }
                candidate += "\",\"frame\":";
                appendFrame(frame);
                const auto& position = pose.Joints[j].World.Translation;
                candidate += ",\"x\":";
                appendPosition(position.X);
                candidate += ",\"y\":";
                appendPosition(position.Y);
                candidate += ",\"z\":";
                appendPosition(position.Z);
                candidate += '}';
            }
        }
        candidate += "]}";
        json = std::move(candidate);
        return true;
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
            if (!Arguments(argc, argv, owned, error) ||
                !Detail::ValidateSkeletalRoleModeAgreement(argc, argv, owned, error))
            {
                std::cerr << "AssetCook error: " << error.c_str() << '\n';
                return true;
            }
            for (const auto& token : owned)
            {
                if (Equals(Key(token), "--dump-fk"))
                {
                    C::AnsiString json;
                    if (Detail::RunSkeletalBvhFkDump(owned, json, error))
                    {
                        std::cout << json.c_str() << '\n';
                        exitCode = 0;
                    }
                    else
                    {
                        std::cerr << "AssetCook error: " << error.c_str() << '\n';
                    }
                    return true;
                }
            }
            if (Detail::ParseSkeletalRoleUtf8Arguments(owned, request, error) &&
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
