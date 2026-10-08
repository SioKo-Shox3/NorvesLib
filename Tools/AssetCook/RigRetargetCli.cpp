#include "RigRetargetCook.h"
#include "NativeCookArguments.h"
#include "RigSingleCook.h"
#include "AssetCookOutput.h"
#include "CookManagedTransactionController.h"
#include "TextureAssetSetOutput.h"
#include <cstring>
#include <cstdio>
namespace NorvesLib::Tools::AssetCook::Detail
{
    namespace C = Core::Container;
    namespace
    {
        struct RetargetStage
        {
            std::filesystem::path Path;
            ~RetargetStage()
            {
                if (!Path.empty())
                {
                    std::error_code ec;
                    std::filesystem::remove_all(Path, ec);
                }
            }
        };
    } // namespace
    bool RunRigRetargetCommand(int argc, const char* const* argv, int& exitCode)
    {
        if (argc < 2 || !argv || !argv[1] || std::strcmp(argv[1], "--retarget-clip") != 0)
        {
            return false;
        }
        exitCode = 1;
        const auto reject = [&](const char* reason)
        {
            std::fprintf(stderr, "AssetCook retarget error: %s\n", reason);
            return true;
        };
        try
        {
            C::AnsiString error;
            C::VariableArray<C::AnsiString> args;
            if (!CollectNativeCookArguments(argc, argv, args, error))
            {
                return reject(error.c_str());
            }
            if (args.size() == 3 && args[2] == "--help")
            {
                std::puts(
                    "AssetCook --retarget-clip --input <BVH/glTF/GLB> --skeleton <target glTF/GLB> --role-profile <JSON> --out <new-directory> --logical <animation-path> [--clip <source clip>] [--clip-name <output name>] [--variant <name>]");
                std::puts(
                    "共有rootへの増分cookはasset-set v2のskeleton_path / role_profile / source_clipを使ってください。");
                exitCode = 0;
                return true;
            }
            SingleAssetCookRequest request;
            request.Kind = "animation";
            request.Format = "nvskel.v1.clips";
            request.EntryTypeText = "Anm1";
            request.EntryName = "__clips__";
            request.Variant = "default";
            std::filesystem::path destination;
            C::VariableArray<C::AnsiString> seen;
            for (size_t i = 2; i < args.size(); ++i)
            {
                const C::AnsiStringView token(args[i]);
                const auto equals = token.find('=');
                const auto key = equals == C::AnsiStringView::npos ? token : token.substr(0, equals);
                for (const auto& old : seen)
                {
                    if (C::AnsiStringView(old) == key)
                    {
                        return reject("duplicate_argument");
                    }
                }
                seen.push_back(C::AnsiString(key));
                C::AnsiStringView value;
                if (equals != C::AnsiStringView::npos)
                {
                    value = token.substr(equals + 1);
                }
                else
                {
                    if (++i >= args.size())
                    {
                        return reject("missing_value");
                    }
                    value = C::AnsiStringView(args[i]);
                }
                if (value.empty() || value.substr(0, 2) == C::AnsiStringView("--"))
                {
                    return reject("missing_value");
                }
                const auto path = [&]() { return std::filesystem::u8path(value.begin(), value.end()); };
                if (key == C::AnsiStringView("--input"))
                {
                    request.InputPath = path();
                }
                else if (key == C::AnsiStringView("--skeleton"))
                {
                    request.RetargetSkeletonPath = path();
                }
                else if (key == C::AnsiStringView("--role-profile"))
                {
                    request.RetargetProfilePath = path();
                }
                else if (key == C::AnsiStringView("--out"))
                {
                    destination = path();
                }
                else if (key == C::AnsiStringView("--logical"))
                {
                    request.LogicalPath = C::AnsiString(value);
                }
                else if (key == C::AnsiStringView("--clip"))
                {
                    request.RetargetSourceClip = C::AnsiString(value);
                }
                else if (key == C::AnsiStringView("--clip-name"))
                {
                    request.RetargetClipName = C::AnsiString(value);
                }
                else if (key == C::AnsiStringView("--variant"))
                {
                    request.Variant = C::AnsiString(value);
                }
                else
                {
                    return reject("unknown_argument");
                }
            }
            if (request.InputPath.empty() || request.RetargetSkeletonPath.empty() ||
                request.RetargetProfilePath.empty() || request.LogicalPath.empty() || destination.empty())
            {
                return reject("required_argument");
            }
            auto target = std::filesystem::absolute(destination).lexically_normal();
            while (target != target.root_path() && !target.has_filename())
            {
                target = target.parent_path();
            }
            std::error_code ec;
            if (std::filesystem::exists(target, ec) || ec)
            {
                return reject("output_already_exists_or_inaccessible");
            }
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec)
            {
                return reject("output_parent");
            }
#if !defined(_WIN32)
            return reject("windows_required");
#else
            C::AnsiString token;
            if (!ManagedTransaction::Token(token, error))
            {
                return reject(error.c_str());
            }
            const auto proposed = target.parent_path() / (C::AnsiString(".retarget-stage-") + token).c_str();
            RetargetStage stage;
            if (!std::filesystem::create_directory(proposed, ec) || ec)
            {
                return reject("output_stage");
            }
            stage.Path = proposed;
            request.ManifestPath = stage.Path / "manifest.json";
            request.PackagePath = stage.Path / "Cooked/Animations/clip.nvpk";
            CookPreparedPlan prepared;
            if (!PrepareCookOutputPlan(request, 1, nullptr, prepared, error) || !CookSingleAsset(request, error))
            {
                return reject(error.c_str());
            }
            // 実際に書いたpackage/manifestを既存captureで照合してから、新規directoryだけを公開する。
            C::VariableArray<uint8_t> manifestBytes;
            // 既存codecのエラー引数だけ標準文字列で受ける。所有データは独自型。
            std::string codecError;
            Core::Asset::AssetManifest manifest;
            CookOutputRecord record;
            if (!ReadSkeletalBinaryFile(request.ManifestPath, manifestBytes, codecError) ||
                !manifest.LoadFromJsonText(ToCoreString(
                    C::AnsiStringView(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()))) ||
                !CaptureCookOutputRecord(prepared.Context, manifest, record, error) ||
                !PublishNewTextureAssetSet(stage.Path, target, error))
            {
                return reject(error.empty() ? "retarget_publication_failed" : error.c_str());
            }
            stage.Path.clear();
            exitCode = 0;
            std::puts("AssetCook retarget_clip published");
            return true;
#endif
        }
        catch (const std::exception& exception)
        {
            return reject(exception.what());
        }
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
