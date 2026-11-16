// ヘッドレス Vulkan でメッシュを焼き込み、画像とメタデータを書き出す CLI。
#include "ImpostorBakerMeshInput.h"
#include "ImpostorBakerOutput.h"

#include "Rendering/ImpostorBake.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceFactory.h"
#include "Thread/JobSystem.h"

#include "Container/StringView.h"
#include <algorithm>
#include <charconv>
#include <climits>
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
            output << "使い方: ImpostorBaker --input <path> --output <dir> [オプション]\n"
                      "入力: .gltf・.glb・.nvmesh\n"
                      "  --output <dir>     出力先のディレクトリ（必須）\n"
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
            if (options.OutputDirectory.empty())
            {
                error = "--output <dir> は必須です";
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

        namespace C = Core::Container;
        namespace Render = Core::Rendering;

        // 登録した資源を、成功・早期終了・例外のすべてで片付ける。
        struct BakeSession
        {
            RHI::DevicePtr Device;
            Render::RenderResources Resources;
            Render::ShaderManager Shaders;
            Render::ImpostorBakeResult Result;
            C::VariableArray<Render::TextureHandle> MaterialTextures;
            bool bJobsInitialized = false;

            ~BakeSession()
            {
                Cleanup();
            }

            void Cleanup()
            {
                if (Device)
                {
                    // 提出の途中で失敗しても、参照中の資源を先に壊さない。
                    Device->WaitIdle();
                    auto& textures = Resources.Textures();
                    for (const auto handle : MaterialTextures)
                    {
                        textures.ReleaseTexture(handle);
                    }
                    MaterialTextures.clear();
                    textures.ReleaseTexture(Result.AtlasTexture);
                    textures.ReleaseTexture(Result.NormalAtlasTexture);
                    textures.ReleaseTexture(Result.OrmAtlasTexture);
                    textures.ReleaseTexture(Result.DepthAtlasTexture);
                    Result = {};
                    Shaders.Shutdown();
                    Device->WaitIdle();
                    Resources.Shutdown();
                    Device->ReleaseObjectCache();
                    Device.reset();
                }
                if (bJobsInitialized)
                {
                    Thread::JobSystem::Get().Shutdown();
                    bJobsInitialized = false;
                }
            }
        };

        bool LoadMaterialImage(BakeSession& session, const ImpostorBakerMaterialImage& image,
                               bool bSrgb, const char* overrideOption, Render::TextureHandle& handle,
                               C::AnsiString& error)
        {
            auto& textures = session.Resources.Textures();
            handle = Render::TextureHandle::Invalid();
            if (!image.Rgba8.empty())
            {
                const uint64_t expectedBytes = static_cast<uint64_t>(image.Width) * image.Height * 4;
                if (!image.Width || !image.Height || expectedBytes != image.Rgba8.size())
                {
                    error = "材質画像の寸法と画素のバイト数が一致しません";
                    return false;
                }
                Render::TextureCreateInfo info;
                info.Width = image.Width;
                info.Height = image.Height;
                info.PixelFormat = bSrgb ? Render::TextureCreateInfo::Format::RGBA8_SRGB :
                                          Render::TextureCreateInfo::Format::RGBA8_UNORM;
                // 初期データはミップ0のみ。残りは既存のアップロード経路で生成する。
                for (uint32_t edge = std::max(info.Width, info.Height); edge > 1; edge >>= 1)
                {
                    ++info.MipLevels;
                }
                info.DebugName = bSrgb ? "ImpostorMaterialAlbedo" : "ImpostorMaterialLinear";
                handle = textures.CreateTexture(info, image.Rgba8.data(), image.Rgba8.size());
                if (handle.IsValid())
                {
                    session.MaterialTextures.push_back(handle);
                    const auto texture = textures.GetRHITexturePtr(handle);
                    if (!texture)
                    {
                        error = "登録した材質画像の RHI テクスチャを取得できません";
                        return false;
                    }
                    if (texture->GetMipLevels() != info.MipLevels)
                    {
                        std::cerr << "材質画像の全ミップを生成できず、" << texture->GetMipLevels()
                                  << " 段で使用します: " << overrideOption << '\n';
                    }
                    return true;
                }
                error = C::AnsiString("材質画像を GPU に上げられません: ") + overrideOption;
                return false;
            }
            if (image.CookedTexturePath.empty())
            {
                return true;
            }
            struct LoadState
            {
                Render::TextureHandle Handle = Render::TextureHandle::Invalid();
                bool bCompleted = false;
            };
            const auto state = C::MakeShared<LoadState>();
            const uint32_t requestId = textures.LoadTextureAsync(image.CookedTexturePath,
                [state](Render::TextureHandle loaded)
                {
                    state->Handle = loaded;
                    state->bCompleted = true;
                }, bSrgb ? Render::TextureColorEncoding::Srgb : Render::TextureColorEncoding::Default);
            if (requestId != 0)
            {
                // CPU 側の読み込み完了後、呼び手のスレッドで GPU への登録と通知を行う。
                Thread::JobSystem::Get().DrainAcceptedFiniteTasks();
                textures.FlushCompletedTextureLoads();
            }
            handle = state->Handle;
            if (!state->bCompleted || !handle.IsValid())
            {
                error = C::AnsiString("NVTEX を読み込めません: ") + image.CookedTexturePath +
                    "（--asset-root を確認するか、" + overrideOption + " <png> で上書きできます）";
                return false;
            }
            session.MaterialTextures.push_back(handle);
            return true;
        }

        bool PrepareBake(BakeSession& session, const ImpostorBakerOptions& options,
                         const ImpostorBakerMeshInput& mesh, Render::ImpostorBakeRequest& request,
                         C::AnsiString& error)
        {
            auto& textures = session.Resources.Textures();
            if (!textures.SetTextureAssetRoot(ImpostorBakerPathUtf8(options.AssetRoot)) ||
                !textures.SetTextureAssetFallbackMode(Render::TextureAssetFallbackMode::FailOnCookedFailure))
            {
                error = "NVTEX のアセットの根または読み込みの設定を反映できません";
                return false;
            }
            C::VariableArray<Render::ImpostorBakeMaterial> materials;
            for (const auto& source : mesh.Materials)
            {
                Render::ImpostorBakeMaterial material;
                if (!LoadMaterialImage(session, source.Albedo, true, "--albedo", material.AlbedoTexture, error) ||
                    !LoadMaterialImage(session, source.Normal, false, "--normal", material.NormalTexture, error) ||
                    !LoadMaterialImage(session, source.Orm, false, "--orm", material.OrmTexture, error))
                {
                    return false;
                }
                if (material.NormalTexture.IsValid())
                {
                    const auto normal = textures.GetRHITexturePtr(material.NormalTexture);
                    if (!normal)
                    {
                        error = "読み込んだ法線の RHI テクスチャを取得できません";
                        return false;
                    }
                    material.bNormalTwoChannel = normal->GetFormat() == RHI::Format::BC5_UNORM;
                }
                material.BaseColor = Math::Vector3(source.BaseColor[0], source.BaseColor[1], source.BaseColor[2]);
                material.Metallic = source.Metallic;
                material.Roughness = source.Roughness;
                material.OcclusionStrength = source.OcclusionStrength;
                material.AlphaCutoff = source.AlphaCutoff;
                material.bApplyNormalScale = source.bHasNormalScale;
                material.NormalScale = source.NormalScale;
                materials.push_back(material);
            }
            request.Textures = &textures;
            request.Device = session.Device.get();
            request.Shaders = &session.Shaders;
            request.Input.Vertices = {mesh.Vertices.data(), mesh.Vertices.size()};
            request.Input.Indices = {mesh.Indices.data(), mesh.Indices.size()};
            request.Input.Layout = options.Layout == ImpostorBakerLayout::Hemi ?
                Render::ImpostorOctahedralLayout::HemiOctahedral : Render::ImpostorOctahedralLayout::FullOctahedral;
            request.Input.AxisCellCountX = options.Cells;
            request.Input.AxisCellCountY = options.Cells;
            request.Input.CellResolution = options.CellSize;
            request.Input.PixelFormat = Render::TextureCreateInfo::Format::RGBA8_SRGB;
            request.Input.DebugName = ImpostorBakerPathUtf8(options.Name);
            for (const auto& source : mesh.Sections)
            {
                if (source.MaterialIndex >= materials.size())
                {
                    error = "メッシュの区間が参照する材質の番号が範囲外です";
                    return false;
                }
                Render::ImpostorBakeSection section;
                section.IndexOffset = source.IndexOffset;
                section.IndexCount = source.IndexCount;
                section.Material = materials[source.MaterialIndex];
                request.Input.Sections.push_back(section);
            }
            return true;
        }

        bool ReadAtlas(BakeSession& session, Render::TextureHandle handle, uint32_t sourceChannels,
                       uint32_t outputChannels, ImpostorBakerPixels& pixels, C::AnsiString& error)
        {
            const auto texture = session.Resources.Textures().GetRHITexturePtr(handle);
            if (!texture)
            {
                error = "読み戻すアトラスを取得できません";
                return false;
            }
            const uint32_t width = session.Result.Metadata.AtlasWidth;
            const uint32_t height = session.Result.Metadata.AtlasHeight;
            const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
            if (!width || !height || pixelCount > std::numeric_limits<size_t>::max() / sourceChannels ||
                static_cast<uint64_t>(width) * outputChannels > INT_MAX || height > INT_MAX)
            {
                error = "読み戻すアトラスが PNG またはメモリの寸法の上限を超えています";
                return false;
            }
            const size_t size = static_cast<size_t>(pixelCount * sourceChannels);
            RHI::BufferDesc desc;
            desc.Size = size;
            desc.Usage = RHI::ResourceUsage::TransferDst;
            desc.CPUAccessible = true;
            const auto buffer = session.Device->CreateBuffer(desc);
            const auto commands = session.Device->CreateCommandList();
            if (!buffer || !commands)
            {
                error = "アトラスの読み戻し用のバッファまたはコマンドを作れません";
                return false;
            }
            commands->Begin();
            commands->TextureBarrier(texture, RHI::ResourceState::ShaderResource, RHI::ResourceState::CopySource);
            commands->CopyTextureToBuffer(texture, buffer, width, height, 0);
            commands->End();
            commands->Submit(true);
            session.Device->WaitIdle();
            pixels.Width = width;
            pixels.Height = height;
            pixels.Channels = outputChannels;
            pixels.Bytes.resize(static_cast<size_t>(pixelCount * outputChannels));
            const auto* mapped = static_cast<const uint8_t*>(buffer->Map(0, size));
            if (!mapped)
            {
                error = "アトラスの読み戻し用バッファを CPU へマップできません";
                return false;
            }
            // Vulkan のコピーは行を詰める。法線・ORM だけ A を捨てて RGB にする。
            for (size_t i = 0; i < static_cast<size_t>(pixelCount); ++i)
            {
                std::memcpy(pixels.Bytes.data() + i * outputChannels, mapped + i * sourceChannels, outputChannels);
            }
            buffer->Unmap();
            return true;
        }

        int BakeAndWrite(const ImpostorBakerOptions& options, const ImpostorBakerMeshInput& mesh)
        {
            BakeSession session;
            RHI::RHIDeviceDesc desc;
            desc.Api = RHI::GraphicsAPI::Vulkan;
            desc.bEnableValidation = options.bValidation;
            session.Device = RHI::CreateRHIDevice(desc);
            if (!session.Device)
            {
                std::cerr << "Vulkan のデバイスを作れません\n";
                return 1;
            }
            if (!session.Resources.Initialize(session.Device))
            {
                std::cerr << "描画資源を初期化できません\n";
                return 1;
            }
            if (!session.Shaders.Initialize(session.Device.get(), NORVES_SHADER_DIR))
            {
                std::cerr << "シェーダー管理を初期化できません\n";
                return 1;
            }
            Thread::JobSystem::Get().Initialize(1);
            session.bJobsInitialized = true;
            C::AnsiString error;
            Render::ImpostorBakeRequest request;
            if (!PrepareBake(session, options, mesh, request, error))
            {
                std::cerr << error.c_str() << '\n';
                return 1;
            }
            Render::ImpostorBakeService baker;
            if (!baker.BakeProceduralAtlas(request, session.Result) || !session.Result.Succeeded())
            {
                std::cerr << "インポスターのベイクに失敗しました\n";
                return 1;
            }
            ImpostorBakerPixels pixels[4];
            const Render::TextureHandle atlases[] = {session.Result.AtlasTexture, session.Result.NormalAtlasTexture,
                session.Result.OrmAtlasTexture, session.Result.DepthAtlasTexture};
            const uint32_t sourceChannels[] = {4, 4, 4, 1};
            const uint32_t outputChannels[] = {4, 3, 3, 1};
            for (uint32_t i = 0; i < 4; ++i)
            {
                if (!ReadAtlas(session, atlases[i], sourceChannels[i], outputChannels[i], pixels[i], error))
                {
                    std::cerr << "アトラス " << i << " を読み戻せません: " << error.c_str() << '\n';
                    return 1;
                }
            }
            C::VariableArray<std::filesystem::path> files;
            if (!WriteImpostorBakerOutput(options.OutputDirectory, options.Name, options.InputPath,
                                         session.Result.Metadata, pixels, options.bNoCook, files, error))
            {
                std::cerr << "書き出しに失敗しました: " << error.c_str() << '\n';
                return 1;
            }
            session.Cleanup();
            std::cout << "書き出したファイル:\n";
            for (const auto& file : files)
            {
                std::cout << "  " << ImpostorBakerPathUtf8(file).c_str() << '\n';
            }
            return 0;
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
        return BakeAndWrite(options, mesh);
    }
    catch (const std::exception&)
    {
        std::cerr << "インポスターの作成中に、パス・メモリ・GPU またはファイルの処理に失敗しました\n";
        return 1;
    }
    return 0;
}
