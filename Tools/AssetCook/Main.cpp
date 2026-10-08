#include "AssetCookLegacyOptions.h"
#include "AssetCookOutput.h"
#include "RigRetargetCook.h"
#include "RigSplitFileCook.h"
#include "SingleAssetCook.h"
#include "SkeletalRoleFileCli.h"
#include "TextureAssetSetCook.h"
#include "TextureCooker.h"
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Tools::AssetCook::Detail;
    bool ReadNextValue(int argc, char **argv, int &index, std::string &outValue, std::string &error)
    {
        if (index + 1 >= argc)
        {
            error = std::string("missing value for ") + argv[index];
            return false;
        }

        ++index;
        outValue = argv[index];
        return true;
    }

    bool ResolveTextureUsageOptions(CookOptions &options, NorvesLib::Tools::AssetCook::ErrorString &error)
    {
        const bool bHasOrmSource = !options.OrmAoPath.empty() ||
                                   !options.OrmRoughnessPath.empty() ||
                                   !options.OrmMetallicPath.empty();
        if (options.Usage.empty())
        {
            if (bHasOrmSource || !options.Quality.empty())
            {
                error = "--orm-* と --quality は --usage と一緒に指定してください";
                return false;
            }
            return true;
        }

        NorvesLib::Tools::AssetCook::TextureUsage usage{};
        if (!NorvesLib::Tools::AssetCook::ParseTextureUsage(options.Usage, usage))
        {
            error = "--usage は albedo・normal・orm・single・height16 のどれかです";
            return false;
        }

        if (options.Kind != "texture")
        {
            error = "--usage は --kind texture と一緒に指定してください";
            return false;
        }

        if (!options.Format.empty())
        {
            error = "--usage が形式を決めるので --format は指定しないでください";
            return false;
        }
        options.Format = NorvesLib::Tools::AssetCook::GetTextureUsageManifestFormat(usage);

        if (!options.Quality.empty() && options.Quality != "fast" && options.Quality != "normal" && options.Quality != "best")
        {
            error = "--quality は fast・normal・best のどれかです";
            return false;
        }

        if (usage == NorvesLib::Tools::AssetCook::TextureUsage::Orm)
        {
            // 詰め済みの ORM（glTF の ARM など。R=AO・G=粗さ・B=メタリックの1枚）は --input で、
            // 別々の元画像は --orm-* で渡す。両方は指定できない。
            if (!options.InputPath.empty() && bHasOrmSource)
            {
                error = "--usage orm は --input（詰め済みの1枚）か "
                        "--orm-ao・--orm-roughness・--orm-metallic "
                        "のどちらか一方で指定してください";
                return false;
            }
            if (options.InputPath.empty() && !bHasOrmSource)
            {
                error = "--usage orm には --input か "
                        "--orm-ao・--orm-roughness・--orm-metallic のどれか 1 "
                        "つが要ります";
                return false;
            }
        }
        else if (bHasOrmSource)
        {
            error = "--orm-* は --usage orm と一緒に指定してください";
            return false;
        }

        return true;
    }

    bool ParseCommandLine(int argc, char **argv, CookOptions &outOptions, std::string &error)
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            error.clear();
            return false;
        }

        for (int index = 1; index < argc; ++index)
        {
            const char* importError = nullptr;
            const auto importArgument = NorvesLib::Tools::AssetCook::ParseImportArgument(
                argc, argv, index, outOptions.ImportSettings, importError);
            if (importArgument == NorvesLib::Tools::AssetCook::ImportArgumentResult::Rejected)
            {
                error = importError;
                return false;
            }
            if (importArgument == NorvesLib::Tools::AssetCook::ImportArgumentResult::Accepted)
            {
                continue;
            }
            const auto skipArgument = NorvesLib::Tools::AssetCook::ParseSkipArgument(
                argv[index], outOptions.bSkipIfUnchanged, importError);
            if (skipArgument == NorvesLib::Tools::AssetCook::ImportArgumentResult::Rejected)
            {
                error = importError;
                return false;
            }
            if (skipArgument == NorvesLib::Tools::AssetCook::ImportArgumentResult::Accepted)
            {
                continue;
            }
            const auto skeletalArgument = NorvesLib::Tools::AssetCook::ParseSkeletalArgument(
                argc, argv, index, outOptions.SkeletalImport, importError);
            if (skeletalArgument == NorvesLib::Tools::AssetCook::ImportArgumentResult::Rejected)
            {
                error = importError;
                return false;
            }
            if (skeletalArgument == NorvesLib::Tools::AssetCook::ImportArgumentResult::Accepted) continue;
            std::string argument = argv[index];
            std::string value;
            const size_t equals = argument.find('=');
            if (equals != std::string::npos)
            {
                value = argument.substr(equals + 1);
                argument = argument.substr(0, equals);
            }

            auto readValue = [&]() -> bool
            {
                if (equals != std::string::npos)
                {
                    return true;
                }

                return ReadNextValue(argc, argv, index, value, error);
            };

            if (argument == "--input")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.InputPath = value;
            }
            else if (argument == "--out")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.PackagePath = value;
            }
            else if (argument == "--manifest")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.ManifestPath = value;
            }
            else if (argument == "--logical")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.LogicalPath = value;
            }
            else if (argument == "--kind")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Kind = value;
            }
            else if (argument == "--entry")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.EntryName = value;
            }
            else if (argument == "--entry-type")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.EntryTypeText = value;
            }
            else if (argument == "--format")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Format = value;
            }
            else if (argument == "--variant")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Variant = value;
            }
            else if (argument == "--usage")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Usage = NorvesLib::Core::Container::AnsiString(value.c_str());
            }
            else if (argument == "--quality")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Quality = NorvesLib::Core::Container::AnsiString(value.c_str());
            }
            else if (argument == "--orm-ao")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.OrmAoPath = value;
            }
            else if (argument == "--orm-roughness")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.OrmRoughnessPath = value;
            }
            else if (argument == "--fallback-min-triangles")
            {
                if (!readValue())
                {
                    return false;
                }
                char *end = nullptr;
                const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
                if (value.empty() || *end != '\0' || parsed > 1000000ul)
                {
                    error = "--fallback-min-triangles は 0〜1000000 "
                            "の整数で指定してください";
                    return false;
                }
                outOptions.FallbackMinTriangles = static_cast<uint32_t>(parsed);
            }
            else if (argument == "--generate")
            {
                if (!readValue())
                {
                    return false;
                }
                if (value != "displaced-sphere")
                {
                    error = "--generate は displaced-sphere だけを指定できます";
                    return false;
                }
                outOptions.Generate = NorvesLib::Core::Container::AnsiString(value.c_str());
            }
            else if (argument == "--flip-normal-y")
            {
                outOptions.bFlipNormalY = true;
            }
            else if (argument == "--orm-metallic")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.OrmMetallicPath = value;
            }
            else
            {
                error = "unknown argument: " + argument;
                return false;
            }
        }

        NorvesLib::Tools::AssetCook::ErrorString usageError;
        if (!ResolveTextureUsageOptions(outOptions, usageError))
        {
            error = ToStdString(usageError);
            return false;
        }
        return ValidateCookOptions(outOptions, error);
    }

    void PrintUsage()
    {
        std::cerr
            << "Usage: AssetCook --inspect <model.gltf|model.glb>\n"
            << "       AssetCook --asset-set <spec.json> --runtime-root <new-or-managed-directory> [--source-root <directory>] [--manifest <file>]\n"
            << "       AssetCook --recover --runtime-root <directory>  (recover fixed pending in parent workspace, including siblings)\n"
            << "       AssetCook --input <file> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind raw --entry <entry> --entry-type Raw "
            << "--format raw.v0 --variant default\n"
            << "       AssetCook --input <image> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind texture --entry <entry.nvtex> --entry-type Tex0 "
            << "--format nvtex.v0.rgba8.srgb|nvtex.v0.rgba8.linear|nvtex.v0.rg8.linear|nvtex.v0.r8.linear "
            << "--variant default\n"
            << "       AssetCook --input <model.gltf> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind model --entry <entry.nvmesh> --entry-type Msh0 "
            << "--format nvmesh.v0.mesh3d.pnt.u32.clustered "
            << "--variant default\n"
            << "       AssetCook --input <model.gltf> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind model --entry <entry.nvskel> --entry-type Skl0 "
            << "--format nvskel.v0.skinned.pnujiw.u32 "
            << "--variant default\n"
            << "       AssetCook --input <audio.wav> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind audio --entry <entry.nvaud> --entry-type Aud0 "
            << "--format nvaud.v0.pcm16 --variant default\n"
            << "Model import: [--import-settings <file>] [--require-sidecar] OR [--no-sidecar] [--skip-if-unchanged]\n"
            << "Skeletal import: [--skin-influences strict|reduce] [--skin-warn-dropped-weight 0..1] [--skin-fail-dropped-weight 0..1]\n"
            << "Cubic bake: [--cubicspline reject|bake] [--cubic-translation-tolerance meters] [--cubic-rotation-tolerance-deg degrees] [--cubic-scale-tolerance value]\n"
            << "Bake budgets: [--cubic-max-depth 0..24] [--cubic-max-channel-samples 2..1048576] [--cubic-max-asset-samples 2..4194304]\n"
            << "Morph import: [--morph reject|drop]\n";
    }

    bool InspectModelFile(const std::filesystem::path& input, auto& error)
    {
        std::filesystem::path path;
        if (!MakeAbsolutePath(input,path,error))
        {
            return false;
        }
        NorvesLib::Core::Container::VariableArray<uint8_t> bytes;
        if (!ReadSkeletalBinaryFile(path,bytes,error))
        {
            return false;
        }
        NorvesLib::Tools::AssetCook::ModelInspection result;
        NorvesLib::Core::Container::AnsiString inspectError;
        if (!NorvesLib::Tools::AssetCook::InspectGltfModelNativePath(bytes.data(),bytes.size(),path,result,inspectError))
        {
            error.assign(inspectError.data(),inspectError.size());
            return false;
        }
        const auto& geometry=result.Geometry;
        const char* axes[]={"X","Y","Z"};
        std::cout << std::setprecision(17)
            << "inspection_version=1\nposition_space=mesh_local_before_import\nimport_settings_applied=false\n"
            << "weld_mode=exact_numeric_position\ncomponent_mode=shared_vertex_including_unreferenced\n"
            << "image_sample_domain=decoded_integer_unlinearized\nmaterial_scope=core_and_optional_emissive_strength\n"
            << "vertex_count=" << geometry.VertexCount << "\ntriangle_count=" << geometry.TriangleCount
            << "\nwelded_vertex_count=" << geometry.WeldedVertexCount
            << "\ncomponent_count=" << geometry.ConnectedComponentCount
            << "\nzero_normal_count=" << geometry.ZeroNormalCount << "\n";
        for (size_t axis=0;axis<3;++axis)
        {
            std::cout << "bounds." << axes[axis] << ".min=" << geometry.Minimum[axis]
                << "\nbounds." << axes[axis] << ".max=" << geometry.Maximum[axis]
                << "\nlength." << axes[axis] << "=" << geometry.Length[axis] << "\n";
        }
        std::cout << "longest_axis=" << axes[geometry.LongestAxis] << "\nimage_count=" << result.Images.size() << "\n";
        for (size_t index=0;index<result.Images.size();++index)
        {
            const auto& image=result.Images[index];
            std::cout << "image[" << index << "].width=" << image.Width << "\nimage[" << index << "].height=" << image.Height
                << "\nimage[" << index << "].channels=" << image.Channels << "\nimage[" << index << "].bits=" << image.BitsPerChannel << "\n";
            for (size_t channel=0;channel<image.Channels;++channel)
            {
                std::cout << "image[" << index << "].channel[" << channel << "].min=" << image.Channel[channel].Minimum
                    << "\nimage[" << index << "].channel[" << channel << "].max=" << image.Channel[channel].Maximum
                    << "\nimage[" << index << "].channel[" << channel << "].mean=" << image.Channel[channel].Mean << "\n";
            }
        }
        std::cout << "material_count=" << result.Materials.size() << "\n";
        if (result.bHasMaterial)
        {
            std::cout << "primitive_material=" << result.MaterialIndex << "\n";
        }
        else
        {
            std::cout << "primitive_material=default\ndefault_material.base_color=1,1,1,1\n"
                << "default_material.metallic=1\ndefault_material.roughness=1\ndefault_material.emissive=0,0,0\n"
                << "default_material.alpha_mode=OPAQUE\ndefault_material.double_sided=false\n";
        }
        for (size_t index=0;index<result.Materials.size();++index)
        {
            const auto& material=result.Materials[index];
            const char* alpha=material.AlphaMode==NorvesLib::Tools::AssetCook::InspectionAlphaMode::Opaque ? "OPAQUE" :
                material.AlphaMode==NorvesLib::Tools::AssetCook::InspectionAlphaMode::Mask ? "MASK" : "BLEND";
            std::cout << "material[" << index << "].base_color=" << material.BaseColor[0] << "," << material.BaseColor[1]
                << "," << material.BaseColor[2] << "," << material.BaseColor[3]
                << "\nmaterial[" << index << "].metallic=" << material.Metallic
                << "\nmaterial[" << index << "].roughness=" << material.Roughness
                << "\nmaterial[" << index << "].emissive=" << material.Emissive[0] << "," << material.Emissive[1] << "," << material.Emissive[2]
                << "\nmaterial[" << index << "].emissive_strength=" << material.EmissiveStrength
                << "\nmaterial[" << index << "].normal_scale=" << material.NormalScale
                << "\nmaterial[" << index << "].occlusion_strength=" << material.OcclusionStrength
                << "\nmaterial[" << index << "].alpha_mode=" << alpha
                << "\nmaterial[" << index << "].alpha_cutoff=" << material.AlphaCutoff
                << "\nmaterial[" << index << "].double_sided=" << (material.bDoubleSided ? "true" : "false") << "\n";
        }
        // boundsだけでは頭の向き/符号/実寸を決められない。恒等templateだけをstdoutへ出す。
        std::cout << "candidate_up_assumption=+Y\nforward_axis_candidates="
            << (geometry.Length[2]>=geometry.Length[0] ? "Z,X" : "X,Z")
            << "\nforward_sign=manual\nsettings_template_requires_manual_units_and_axes=true\n"
            << "settings_template={\"version\":1,\"units\":{\"scale\":1},\"axes\":{\"up\":\"+Y\",\"forward\":\"+Z\"},\"origin\":{\"mode\":\"keep\"}}\n";
        return true;
    }

}

int main(int argc, char **argv)
{
    int rigExitCode = 0;
    if (NorvesLib::Tools::AssetCook::Detail::RunRigRetargetCommand(argc, argv, rigExitCode))
    {
        return rigExitCode;
    }
    if (NorvesLib::Tools::AssetCook::RunRigSplitFileCommand(argc, argv, rigExitCode))
    {
        return rigExitCode;
    }
    int roleExitCode = 0;
    if (NorvesLib::Tools::AssetCook::RunSkeletalRoleFileCommand(argc, argv, roleExitCode)) return roleExitCode;
    int assetSetExitCode=0;
    if (NorvesLib::Tools::AssetCook::RunTextureAssetSetCommand(argc,argv,assetSetExitCode)) return assetSetExitCode;
    std::filesystem::path inspectPath;
    const char* inspectError=nullptr;
    const auto inspect=NorvesLib::Tools::AssetCook::ParseInspectCommandLine(argc,argv,inspectPath,inspectError);
    if (inspect!=NorvesLib::Tools::AssetCook::ImportArgumentResult::Unhandled)
    {
        std::string error;
        if (inspect==NorvesLib::Tools::AssetCook::ImportArgumentResult::Rejected || !InspectModelFile(inspectPath,error))
        {
            std::cerr << "AssetCook error: " << (inspectError ? inspectError : error.c_str()) << "\n";
            return 1;
        }
        return 0;
    }
    CookOptions options;
    std::string error;
    if (!ParseCommandLine(argc, argv, options, error))
    {
        PrintUsage();
        if (!error.empty())
        {
            std::cerr << "AssetCook error: " << error << "\n";
        }
        return error.empty() ? 0 : 1;
    }

    NorvesLib::Core::Container::AnsiString cookError;
    const auto request = NorvesLib::Tools::AssetCook::Detail::MakeSingleCookRequest(options);
    const bool bSucceeded = NorvesLib::Tools::AssetCook::CookSingleAsset(request, cookError);
    error = NorvesLib::Tools::AssetCook::Detail::ToStdString(cookError);
    if (!bSucceeded)
    {
        std::cerr << "AssetCook error: " << error << "\n";
        return 1;
    }

    return 0;
}
