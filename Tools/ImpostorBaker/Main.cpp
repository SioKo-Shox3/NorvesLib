// メッシュを読み込み、インポスターの入力の内容を表示する CLI。
#include "ImpostorBakerMeshInput.h"

#include "Container/StringView.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <cwchar>
#include <exception>
#include <iostream>
#include <limits>

namespace NorvesLib::Tools::ImpostorBaker
{
    enum class ImpostorBakerLayout
    {
        Hemi,
        Full
    };

    struct ImpostorBakerOptions
    {
        std::filesystem::path InputPath;
        std::filesystem::path OutputDirectory;
        std::filesystem::path Name;
        uint32_t Cells = 8;
        uint32_t CellSize = 192;
        ImpostorBakerLayout Layout = ImpostorBakerLayout::Hemi;
        std::filesystem::path AssetRoot = NORVES_ASSET_DIR;
        ImpostorBakerMeshLoadOptions Mesh;
        bool bNoCook = false;
        bool bValidation = false;
        bool bHelp = false;
    };

    namespace
    {
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;

        void PrintUsage(std::ostream& output)
        {
            output << "使い方: ImpostorBaker --input <path> [オプション]\n"
                      "入力: .gltf・.glb・.nvmesh\n"
                      "  --output <dir>     出力先のディレクトリ\n"
                      "  --name <基本名>    既定: 入力ファイルの拡張子を除いた名前\n"
                      "  --cells <N>        X・Y のセル数（正の整数。既定: 8）\n"
                      "  --cell-size <px>   セルの辺の画素数（正の整数。既定: 192）\n"
                      "  --layout hemi|full 配置（既定: hemi）\n"
                      "  --asset-root <dir> NVTEX を探す根（既定: NORVES_ASSET_DIR）\n"
                      "  --albedo <png>     材質 0 の色画像\n"
                      "  --normal <png>     材質 0 の法線画像（エンジンの向き）\n"
                      "  --orm <png>        材質 0 の ORM 画像\n"
                      "  --no-cook          NVTEX の cook を無効にする\n"
                      "  --validation       Vulkan の検証レイヤーを有効にする\n"
                      "  --help             使い方を表示する\n";
        }

        bool ParsePositive(const char* value, uint32_t& out)
        {
            uint32_t number = 0;
            const char* end = value + std::strlen(value);
            const auto parsed = std::from_chars(value, end, number);
            if (parsed.ec != std::errc{} || parsed.ptr != end || number == 0)
            {
                return false;
            }
            out = number;
            return true;
        }

        bool ParseArguments(int argc, char** argv, ImpostorBakerOptions& options, AnsiString& error)
        {
            for (int i = 1; i < argc; ++i)
            {
                const AnsiStringView argument(argv[i]);
                if (argument == AnsiStringView("--help"))
                {
                    options.bHelp = true;
                    continue;
                }
                if (argument == AnsiStringView("--no-cook"))
                {
                    options.bNoCook = true;
                    continue;
                }
                if (argument == AnsiStringView("--validation"))
                {
                    options.bValidation = true;
                    continue;
                }
                const bool bValueOption = argument == AnsiStringView("--input") || argument == AnsiStringView("--output") || argument == AnsiStringView("--name") ||
                    argument == AnsiStringView("--cells") || argument == AnsiStringView("--cell-size") || argument == AnsiStringView("--layout") ||
                    argument == AnsiStringView("--asset-root") || argument == AnsiStringView("--albedo") || argument == AnsiStringView("--normal") || argument == AnsiStringView("--orm");
                if (!bValueOption)
                {
                    error = AnsiString("不明な引数です: ") + AnsiString(argument);
                    return false;
                }
                if (i + 1 >= argc || argv[i + 1][0] == '\0' || std::strncmp(argv[i + 1], "--", 2) == 0)
                {
                    error = AnsiString("値を指定してください: ") + AnsiString(argument);
                    return false;
                }
                const char* value = argv[++i];
                if (argument == AnsiStringView("--cells") || argument == AnsiStringView("--cell-size"))
                {
                    uint32_t& target = argument == AnsiStringView("--cells") ? options.Cells : options.CellSize;
                    if (!ParsePositive(value, target))
                    {
                        error = AnsiString(argument) + " には 1 以上の 32 ビット整数を指定してください";
                        return false;
                    }
                }
                else if (argument == AnsiStringView("--layout"))
                {
                    const AnsiStringView layout(value);
                    if (layout != AnsiStringView("hemi") && layout != AnsiStringView("full"))
                    {
                        error = "--layout は hemi または full を指定してください";
                        return false;
                    }
                    options.Layout = layout == AnsiStringView("hemi") ? ImpostorBakerLayout::Hemi : ImpostorBakerLayout::Full;
                }
                else if (argument == AnsiStringView("--input"))
                {
                    options.InputPath = value;
                }
                else if (argument == AnsiStringView("--output"))
                {
                    options.OutputDirectory = value;
                }
                else if (argument == AnsiStringView("--name"))
                {
                    options.Name = value;
                }
                else if (argument == AnsiStringView("--asset-root"))
                {
                    options.AssetRoot = value;
                }
                else if (argument == AnsiStringView("--albedo"))
                {
                    options.Mesh.AlbedoOverride = value;
                }
                else if (argument == AnsiStringView("--normal"))
                {
                    options.Mesh.NormalOverride = value;
                }
                else if (argument == AnsiStringView("--orm"))
                {
                    options.Mesh.OrmOverride = value;
                }
            }
            if (options.bHelp)
            {
                return true;
            }
            if (options.InputPath.empty())
            {
                error = "--input <path> は必須です";
                return false;
            }
            if (options.Name.empty())
            {
                options.Name = options.InputPath.stem();
            }
            if (options.Name.empty() || options.Name == "." || options.Name == ".." ||
                options.Name.has_parent_path() || options.Name.has_root_path() ||
                std::wcspbrk(options.Name.c_str(), L"<>:\"/\\|?*") != nullptr)
            {
                error = "--name にはディレクトリや予約文字を含まない基本名を指定してください";
                return false;
            }
            if (static_cast<uint64_t>(options.Cells) * options.CellSize > UINT32_MAX)
            {
                error = "--cells と --cell-size の積が 32 ビットの画像寸法を超えています";
                return false;
            }
            return true;
        }

        void PrintImage(const char* label, const ImpostorBakerMaterialImage& image)
        {
            std::cout << "  " << label << ": ";
            if (!image.Rgba8.empty())
            {
                std::cout << "画像あり " << image.Width << " x " << image.Height;
            }
            else if (!image.CookedTexturePath.empty())
            {
                std::cout << "展開画像なし（cook 済み参照: " << image.CookedTexturePath.c_str() << "）";
            }
            else
            {
                std::cout << "画像なし";
            }
            std::cout << '\n';
        }

        void PrintSummary(const ImpostorBakerMeshInput& mesh)
        {
            std::cout << "頂点数: " << mesh.Vertices.size() << '\n'
                      << "三角形数: " << mesh.Indices.size() / 3 << '\n'
                      << "区間数: " << mesh.Sections.size() << '\n'
                      << "材質数: " << mesh.Materials.size() << '\n';
            for (size_t i = 0; i < mesh.Materials.size(); ++i)
            {
                const auto& material = mesh.Materials[i];
                std::cout << "材質 " << i << ": 物体色=(" << material.BaseColor[0] << ", "
                          << material.BaseColor[1] << ", " << material.BaseColor[2] << ")"
                          << " 金属度=" << material.Metallic << " 粗さ=" << material.Roughness
                          << " 遮蔽の強さ=" << material.OcclusionStrength
                          << " 切り抜きの閾値=" << material.AlphaCutoff
                          << " 法線の強さ=" << material.NormalScale << '\n';
                PrintImage("Albedo", mesh.Materials[i].Albedo);
                PrintImage("Normal", mesh.Materials[i].Normal);
                PrintImage("ORM", mesh.Materials[i].Orm);
            }
            float minimum[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max()};
            float maximum[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest()};
            // 読み込み済み配列に粗い LOD の頂点があっても、要約の範囲は描画する LOD0 から求める。
            for (const uint32_t index : mesh.Indices)
            {
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], mesh.Vertices[index].Position[axis]);
                    maximum[axis] = std::max(maximum[axis], mesh.Vertices[index].Position[axis]);
                }
            }
            std::cout << "AABB 最小: (" << minimum[0] << ", " << minimum[1] << ", " << minimum[2] << ")\n"
                      << "AABB 最大: (" << maximum[0] << ", " << maximum[1] << ", " << maximum[2] << ")\n";
        }
    } // namespace
} // namespace NorvesLib::Tools::ImpostorBaker

int main(int argc, char** argv)
{
    using namespace NorvesLib::Tools::ImpostorBaker;
    ImpostorBakerOptions options;
    NorvesLib::Core::Container::AnsiString error;
    try
    {
        if (!ParseArguments(argc, argv, options, error))
        {
            std::cerr << "引数の指定が不正です: " << error.c_str() << '\n';
            PrintUsage(std::cerr);
            return 2;
        }
    }
    catch (const std::exception&)
    {
        std::cerr << "引数のパスを解釈できないか、引数を保持するメモリを確保できません\n";
        PrintUsage(std::cerr);
        return 2;
    }
    if (options.bHelp)
    {
        PrintUsage(std::cout);
        return 0;
    }
    try
    {
        ImpostorBakerMeshInput mesh;
        if (!LoadImpostorBakerMesh(options.InputPath, options.Mesh, mesh, error))
        {
            std::cerr << "メッシュを読み込めません: " << error.c_str() << '\n';
            return 1;
        }
        PrintSummary(mesh);
    }
    catch (const std::exception&)
    {
        std::cerr << "メッシュの読み込み中に、パスの処理またはメモリの確保に失敗しました\n";
        return 1;
    }
    return 0;
}
