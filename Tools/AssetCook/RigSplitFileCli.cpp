#include "RigSplitFileCook.h"
#include "NativeCookArguments.h"
#include "SkeletalCliOptions.h"
#include <charconv>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <cmath>
namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    bool RunRigSplitFileCommand(int argc, const char* const* argv, int& exitCode)
    {
        // 先頭の明示modeだけを扱い、旧modeの値に同じ文字列が含まれても奪わない。
        if (argc < 2 || !argv || !argv[1] || std::strcmp(argv[1], "--rig-split") != 0)
        {
            return false;
        }
        exitCode = 1;
        C::AnsiString error;
        try
        {
            C::VariableArray<C::AnsiString> tokens;
            if (!Detail::CollectNativeCookArguments(argc, argv, tokens, error) || tokens.size() < 2 ||
                tokens[1] != "--rig-split")
            {
                throw std::runtime_error("rig_split: native argument mismatch");
            }
            if (tokens.size() == 3 && tokens[2] == "--help")
            {
                std::cout << "AssetCook --rig-split --input <GLB/glTF> --out <new-directory> --logical <stem>\n"
                             "  --variant <name> --root-joint <name> --loop auto|true|false --no-analysis\n"
                             "  --authored-fps <fps> --source-fps <fps> --time-scale <factor>\n"
                             "  --import-settings <file> --no-sidecar --require-sidecar\n"
                             "  --skin-influences / --cubicspline / --morph: existing skeletal import options\n";
                exitCode = 0;
                return true;
            }
            C::VariableArray<const char*> args;
            for (const auto& token : tokens)
            {
                args.push_back(token.c_str());
            }
            RigSplitCookRequest request;
            request.Profile = S::RigImportProfile::StaticRootFrame256;
            request.Limits.MaxJoints = 256;
            request.bAnalyzeClips = true;
            SkeletalCliOptions skeletal;
            std::filesystem::path destination;
            C::AnsiString logical;
            C::VariableArray<C::AnsiString> seen;
            bool bAnalysisOption = false;
            for (int i = 2; i < int(tokens.size()); ++i)
            {
                const char* skeletalError = nullptr;
                const auto parsed = ParseSkeletalArgument(int(args.size()), args.data(), i, skeletal, skeletalError);
                if (parsed == ImportArgumentResult::Rejected)
                {
                    throw std::runtime_error(skeletalError);
                }
                if (parsed == ImportArgumentResult::Accepted)
                {
                    continue;
                }
                C::AnsiStringView token(tokens[size_t(i)].data(), tokens[size_t(i)].size());
                const auto equals = token.find('=');
                const auto key = equals == C::AnsiStringView::npos ? token : token.substr(0, equals);
                for (const auto& old : seen)
                {
                    if (old == key)
                    {
                        throw std::runtime_error("rig_split: duplicate argument");
                    }
                }
                seen.push_back(C::AnsiString(key));
                const bool flag = key == "--no-analysis" || key == "--no-sidecar" || key == "--require-sidecar";
                if (flag)
                {
                    if (equals != C::AnsiStringView::npos)
                    {
                        throw std::runtime_error("rig_split: flag has a value");
                    }
                    if (key == "--no-analysis")
                    {
                        request.bAnalyzeClips = false;
                    }
                    if (key == "--no-sidecar")
                    {
                        request.ImportOptions.bDisabled = true;
                    }
                    if (key == "--require-sidecar")
                    {
                        request.ImportOptions.bRequired = true;
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
                    if (++i >= int(tokens.size()))
                    {
                        throw std::runtime_error("rig_split: missing argument value");
                    }
                    value = C::AnsiStringView(tokens[size_t(i)].data(), tokens[size_t(i)].size());
                }
                if (value.empty() || value.substr(0, 2) == "--")
                {
                    throw std::runtime_error("rig_split: empty argument value");
                }
                const auto path = [&]() { return std::filesystem::u8path(value.begin(), value.end()); };
                const auto number = [&]()
                {
                    double n = 0;
                    const auto r = std::from_chars(value.data(), value.data() + value.size(), n);
                    if (r.ec != std::errc{} || r.ptr != value.data() + value.size() || !std::isfinite(n) || n <= 0)
                    {
                        throw std::runtime_error("rig_split: expected a positive finite number");
                    }
                    return n;
                };
                if (key == "--input")
                {
                    request.SourcePath = path();
                }
                else if (key == "--out")
                {
                    destination = path();
                }
                else if (key == "--logical")
                {
                    logical = C::AnsiString(value);
                }
                else if (key == "--variant")
                {
                    request.Variant = C::AnsiString(value);
                }
                else if (key == "--import-settings")
                {
                    request.ImportOptions.OverridePath = path();
                }
                else if (key == "--root-joint")
                {
                    request.ClipRootJoint = C::AnsiString(value);
                    bAnalysisOption = true;
                }
                else if (key == "--loop")
                {
                    bAnalysisOption = true;
                    if (value == "auto")
                    {
                        request.ClipAnalysis.Loop = S::RigClipLoopMode::Auto;
                    }
                    else if (value == "true")
                    {
                        request.ClipAnalysis.Loop = S::RigClipLoopMode::Loop;
                    }
                    else if (value == "false")
                    {
                        request.ClipAnalysis.Loop = S::RigClipLoopMode::Once;
                    }
                    else
                    {
                        throw std::runtime_error("rig_split: invalid loop mode");
                    }
                }
                else if (key == "--time-scale")
                {
                    request.ClipAnalysis.TimeScale = number();
                    bAnalysisOption = true;
                }
                else if (key == "--source-fps")
                {
                    request.ClipAnalysis.SourceFps = number();
                    bAnalysisOption = true;
                }
                else if (key == "--authored-fps")
                {
                    request.ClipAnalysis.AuthoredFps = number();
                    bAnalysisOption = true;
                }
                else
                {
                    throw std::runtime_error("rig_split: unknown or mixed-mode argument");
                }
            }
            const char* skeletalError = nullptr;
            if (!ValidateSkeletalArguments(skeletal, true, skeletalError))
            {
                throw std::runtime_error(skeletalError);
            }
            request.DecodeOptions = skeletal.Decode;
            if (request.SourcePath.empty() || destination.empty() || logical.empty() ||
                (!request.bAnalyzeClips && bAnalysisOption) ||
                (request.ImportOptions.bDisabled &&
                 (request.ImportOptions.bRequired || !request.ImportOptions.OverridePath.empty())) ||
                ((request.ClipAnalysis.AuthoredFps == 0) != (request.ClipAnalysis.SourceFps == 0)))
            {
                throw std::runtime_error("rig_split: missing input/output/logical or conflicting settings");
            }
            request.SkeletonPath = logical + ".skeleton";
            request.MeshPath = logical + ".skinmesh";
            request.BankPath = logical + ".clips";
            RigSplitFileCookResult result;
            if (!CookRigSplitFile(request, destination, result, error))
            {
                throw std::runtime_error(error.c_str());
            }
            std::cout << "RIG_SPLIT_COOK result=pass joints=" << result.JointCount << " clips=" << result.ClipCount
                      << " textures=" << result.TextureCount << " source_hash=" << result.SourceHash << "\n";
            exitCode = 0;
        }
        catch (const std::exception& ex)
        {
            std::cerr << "AssetCook error: " << ex.what() << "\n";
        }
        catch (...)
        {
            std::cerr << "AssetCook error: rig_split execution_exception\n";
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
