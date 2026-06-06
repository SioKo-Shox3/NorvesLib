#include "GameApplicationHandler.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/WorldObject.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Input/InputSystem.h"
#include "Core/Public/Rendering/RenderResourceManager.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <Shellapi.h>
#include <string>
#include <system_error>
#include <utility>

// GameMode関連
#include "Core/Public/GameMode/TStateMachine.h"
#include "Core/Public/GameMode/GameModeFactory.h"
#include "GameModes/Rendering3DTest/Rendering3DTestMode.h"

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::Engine;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core;

namespace Game
{
    namespace
    {
        constexpr const TCHAR *kTextureAssetRootOption = TEXT("--texture-asset-root");
        constexpr const TCHAR *kTextureAssetManifestOption = TEXT("--texture-asset-manifest");
        constexpr const TCHAR *kRendering3DTestModelOption = TEXT("--rendering3dtest-model");
        constexpr const TCHAR *kDefaultRendering3DTestModelPath = TEXT("Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf");

        std::basic_string<TCHAR> ToStdString(const String &value)
        {
            if (value.empty())
            {
                return {};
            }

            return std::basic_string<TCHAR>(value.data(), value.size());
        }

        bool StartsWith(const std::basic_string<TCHAR> &value, const TCHAR *prefix)
        {
            const std::basic_string<TCHAR> prefixString(prefix);
            return value.size() >= prefixString.size() &&
                   value.compare(0, prefixString.size(), prefixString) == 0;
        }

        bool IsCommandLineOption(const String &argument)
        {
            return StartsWith(ToStdString(argument), TEXT("--"));
        }

        bool TryMatchTextureAssetOption(const String &argument,
                                        const TCHAR *option,
                                        bool &outMatched,
                                        bool &outHasInlineValue,
                                        String &outInlineValue,
                                        String &outError)
        {
            outMatched = false;
            outHasInlineValue = false;
            outInlineValue = {};

            const std::basic_string<TCHAR> text = ToStdString(argument);
            const std::basic_string<TCHAR> optionText(option);
            if (!StartsWith(text, option))
            {
                return true;
            }

            if (text.size() == optionText.size())
            {
                outMatched = true;
                return true;
            }

            if (text[optionText.size()] != TEXT('='))
            {
                outError = String("unknown texture asset option format: ");
                outError += argument;
                return false;
            }

            outMatched = true;
            outHasInlineValue = true;
            const std::basic_string<TCHAR> inlineValue = text.substr(optionText.size() + 1);
            outInlineValue = String(inlineValue);
            if (outInlineValue.empty())
            {
                outError = String("missing value for texture asset option: ");
                outError += option;
                return false;
            }

            return true;
        }

        bool ReadTextureAssetOptionValue(const VariableArray<String> &args,
                                         size_t &index,
                                         const TCHAR *option,
                                         bool bHasInlineValue,
                                         const String &inlineValue,
                                         String &outValue,
                                         String &outError)
        {
            if (bHasInlineValue)
            {
                outValue = inlineValue;
                return true;
            }

            if (index + 1 >= args.size())
            {
                outError = String("missing value for texture asset option: ");
                outError += option;
                return false;
            }

            const String &nextArgument = args[index + 1];
            if (nextArgument.empty() || IsCommandLineOption(nextArgument))
            {
                outError = String("missing value for texture asset option: ");
                outError += option;
                return false;
            }

            outValue = nextArgument;
            ++index;
            return true;
        }

        std::filesystem::path ToFilesystemPath(const String &value)
        {
            if (value.empty())
            {
                return {};
            }

            return std::filesystem::path(value.c_str());
        }

        String MakeStringFromUtf8Bytes(const std::string &bytes)
        {
            std::basic_string<TCHAR> converted;
            converted.reserve(bytes.size());
            for (unsigned char character : bytes)
            {
                converted.push_back(static_cast<TCHAR>(character));
            }
            return String(converted);
        }

        String MakeStringFromWideText(const wchar_t *pText)
        {
            if (!pText || pText[0] == L'\0')
            {
                return {};
            }

            const int requiredLength = WideCharToMultiByte(
                CP_UTF8,
                0,
                pText,
                -1,
                nullptr,
                0,
                nullptr,
                nullptr);
            if (requiredLength <= 1)
            {
                return {};
            }

            std::string converted(static_cast<size_t>(requiredLength - 1), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                pText,
                -1,
                converted.data(),
                requiredLength,
                nullptr,
                nullptr);
            return String(converted);
        }

        VariableArray<String> GetProcessCommandLineArguments()
        {
            VariableArray<String> result;

            int argumentCount = 0;
            LPWSTR *ppArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
            if (!ppArguments)
            {
                return result;
            }

            for (int argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
            {
                result.emplace_back(MakeStringFromWideText(ppArguments[argumentIndex]));
            }

            LocalFree(ppArguments);
            return result;
        }
    }

    bool GameApplicationHandler::OnPreInitialize(const VariableArray<String> &args)
    {
        LOG_INFO("GameApplicationHandler::OnPreInitialize()");

        m_bHasTextureAssetRuntimeConfig = false;
        m_TextureAssetRoot = {};
        m_TextureAssetManifestPath = {};
        m_Rendering3DTestModelPath = {};

        VariableArray<String> processArgs = GetProcessCommandLineArguments();
        const VariableArray<String> &parseArgs = processArgs.empty() ? args : processArgs;

        // コマンドライン引数の処理
        for (size_t i = 0; i < parseArgs.size(); ++i)
        {
            // コマンドライン引数のログ出力
            LOG_INFO_F("Arg[%zu]=%s", i, parseArgs[i].c_str());

            bool bMatchedRoot = false;
            bool bRootHasInlineValue = false;
            String rootInlineValue;
            String parseError;
            if (!TryMatchTextureAssetOption(parseArgs[i],
                                            kTextureAssetRootOption,
                                            bMatchedRoot,
                                            bRootHasInlineValue,
                                            rootInlineValue,
                                            parseError))
            {
                LOG_ERROR_F("Texture asset command line parse failed: %s", parseError.c_str());
                return false;
            }

            if (bMatchedRoot)
            {
                if (!m_TextureAssetRoot.empty())
                {
                    LOG_ERROR("Texture asset command line parse failed: duplicate --texture-asset-root");
                    return false;
                }

                if (!ReadTextureAssetOptionValue(parseArgs,
                                                 i,
                                                 kTextureAssetRootOption,
                                                 bRootHasInlineValue,
                                                 rootInlineValue,
                                                 m_TextureAssetRoot,
                                                 parseError))
                {
                    LOG_ERROR_F("Texture asset command line parse failed: %s", parseError.c_str());
                    return false;
                }
                continue;
            }

            bool bMatchedManifest = false;
            bool bManifestHasInlineValue = false;
            String manifestInlineValue;
            if (!TryMatchTextureAssetOption(parseArgs[i],
                                            kTextureAssetManifestOption,
                                            bMatchedManifest,
                                            bManifestHasInlineValue,
                                            manifestInlineValue,
                                            parseError))
            {
                LOG_ERROR_F("Texture asset command line parse failed: %s", parseError.c_str());
                return false;
            }

            if (bMatchedManifest)
            {
                if (!m_TextureAssetManifestPath.empty())
                {
                    LOG_ERROR("Texture asset command line parse failed: duplicate --texture-asset-manifest");
                    return false;
                }

                if (!ReadTextureAssetOptionValue(parseArgs,
                                                 i,
                                                 kTextureAssetManifestOption,
                                                 bManifestHasInlineValue,
                                                 manifestInlineValue,
                                                 m_TextureAssetManifestPath,
                                                 parseError))
                {
                    LOG_ERROR_F("Texture asset command line parse failed: %s", parseError.c_str());
                    return false;
                }
                continue;
            }

            bool bMatchedModel = false;
            bool bModelHasInlineValue = false;
            String modelInlineValue;
            if (!TryMatchTextureAssetOption(parseArgs[i],
                                            kRendering3DTestModelOption,
                                            bMatchedModel,
                                            bModelHasInlineValue,
                                            modelInlineValue,
                                            parseError))
            {
                LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                return false;
            }

            if (bMatchedModel)
            {
                if (!m_Rendering3DTestModelPath.empty())
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: duplicate --rendering3dtest-model");
                    return false;
                }

                if (!ReadTextureAssetOptionValue(parseArgs,
                                                 i,
                                                 kRendering3DTestModelOption,
                                                 bModelHasInlineValue,
                                                 modelInlineValue,
                                                 m_Rendering3DTestModelPath,
                                                 parseError))
                {
                    LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                    return false;
                }
            }
        }

        const bool bHasRoot = !m_TextureAssetRoot.empty();
        const bool bHasManifest = !m_TextureAssetManifestPath.empty();
        if (bHasRoot != bHasManifest)
        {
            LOG_ERROR("Texture asset command line parse failed: --texture-asset-root and --texture-asset-manifest must be specified together");
            return false;
        }

        m_bHasTextureAssetRuntimeConfig = bHasRoot && bHasManifest;
        if (m_bHasTextureAssetRuntimeConfig)
        {
            LOG_INFO_F("Texture asset runtime config parsed root=\"%s\" manifest=\"%s\"",
                       m_TextureAssetRoot.c_str(),
                       m_TextureAssetManifestPath.c_str());
        }
        if (!m_Rendering3DTestModelPath.empty())
        {
            LOG_INFO_F("Rendering3DTest model path parsed path=\"%s\"",
                       m_Rendering3DTestModelPath.c_str());
        }

        return true;
    }

    bool GameApplicationHandler::OnInitialize()
    {
        LOG_INFO("GameApplicationHandler::OnInitialize()");

        // ========================================
        // カメラコントローラーの初期化
        // ========================================
        {
            // 原点を注視点とし、距離5.0、Yaw=0°、Pitch=30°で初期化
            m_CameraController.Initialize(
                NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), // target
                5.0f,                                       // distance
                0.0f,                                       // yaw
                30.0f                                       // pitch
            );

            LOG_INFO("MayaCameraController initialized");
        }

        // ========================================
        // ライトコントローラーの初期化
        // ========================================
        // メインディレクショナルライトはRendering3DTestModeのEnterで
        // LightComponent経由で作成されるため、ここでは初期化しない

        LOG_INFO("LightController initialization skipped (managed by GameMode)");

        // テストオブジェクトの作成はRendering3DTestModeのEnterで行われる

        return true;
    }

    void GameApplicationHandler::OnPostInitialize()
    {
        LOG_INFO("GameApplicationHandler::OnPostInitialize()");

        if (m_bHasTextureAssetRuntimeConfig && !ApplyTextureAssetRuntimeConfig())
        {
            if (GEngine)
            {
                GEngine->RequestExit(1);
            }
            return;
        }

        // メインディレクショナルライトはGameMode（Rendering3DTest）内の
        // LightComponent経由でSceneViewに登録されるため、
        // ここでの直接登録は行わない
    }

    bool GameApplicationHandler::ApplyTextureAssetRuntimeConfig()
    {
        if (!GEngine)
        {
            LOG_ERROR("Texture asset runtime setup failed: GEngine is null");
            return false;
        }

        const std::filesystem::path rootPath = ToFilesystemPath(m_TextureAssetRoot);
        const std::filesystem::path manifestPath = ToFilesystemPath(m_TextureAssetManifestPath);

        std::error_code errorCode;
        if (!std::filesystem::exists(rootPath, errorCode) || errorCode)
        {
            LOG_ERROR_F("Texture asset runtime setup failed: root does not exist path=\"%s\"",
                        m_TextureAssetRoot.c_str());
            return false;
        }

        errorCode = {};
        if (!std::filesystem::is_directory(rootPath, errorCode) || errorCode)
        {
            LOG_ERROR_F("Texture asset runtime setup failed: root is not a directory path=\"%s\"",
                        m_TextureAssetRoot.c_str());
            return false;
        }

        errorCode = {};
        if (!std::filesystem::exists(manifestPath, errorCode) || errorCode)
        {
            LOG_ERROR_F("Texture asset runtime setup failed: manifest does not exist path=\"%s\"",
                        m_TextureAssetManifestPath.c_str());
            return false;
        }

        errorCode = {};
        if (!std::filesystem::is_regular_file(manifestPath, errorCode) || errorCode)
        {
            LOG_ERROR_F("Texture asset runtime setup failed: manifest is not a regular file path=\"%s\"",
                        m_TextureAssetManifestPath.c_str());
            return false;
        }

        std::ifstream manifestInput(manifestPath, std::ios::binary);
        if (!manifestInput.is_open())
        {
            LOG_ERROR_F("Texture asset runtime setup failed: manifest read failed path=\"%s\"",
                        m_TextureAssetManifestPath.c_str());
            return false;
        }

        const std::string manifestBytes((std::istreambuf_iterator<char>(manifestInput)),
                                        std::istreambuf_iterator<char>());
        if (!manifestInput.eof() && manifestInput.fail())
        {
            LOG_ERROR_F("Texture asset runtime setup failed: manifest read failed path=\"%s\"",
                        m_TextureAssetManifestPath.c_str());
            return false;
        }

        auto &resourceManager = GEngine->GetRenderWorld().GetResourceManager();
        if (!resourceManager.SetTextureAssetRoot(m_TextureAssetRoot))
        {
            LOG_ERROR_F("Texture asset runtime setup failed: SetTextureAssetRoot rejected root=\"%s\"",
                        m_TextureAssetRoot.c_str());
            return false;
        }

        const String manifestText = MakeStringFromUtf8Bytes(manifestBytes);
        if (!resourceManager.LoadTextureAssetManifestFromJsonText(manifestText, m_TextureAssetManifestPath))
        {
            LOG_ERROR_F("Texture asset runtime setup failed: texture asset manifest parse failed path=\"%s\"",
                        m_TextureAssetManifestPath.c_str());
            return false;
        }

        LOG_INFO_F("Texture asset runtime setup completed root=\"%s\" manifest=\"%s\"",
                   m_TextureAssetRoot.c_str(),
                   m_TextureAssetManifestPath.c_str());
        return true;
    }

    void GameApplicationHandler::OnUpdate(float deltaTime)
    {
        // ポーズ中は更新をスキップ
        if (m_bIsPaused)
        {
            return;
        }

        // ========================================
        // 入力に基づくカメラ・ライト更新
        // ========================================

        auto &inputSystem = GEngine->GetInputSystem();
        const auto &inputState = inputSystem.GetState();

        // カメラコントローラー更新
        m_CameraController.Update(inputState, deltaTime);

        // デバッグ: スクロール値とカメラ距離を出力
        {
            float scroll = inputState.GetMouseState().ScrollDelta;
            if (std::abs(scroll) > 0.0f)
            {
                float dist = m_CameraController.GetDistance();
                auto pos = m_CameraController.GetPosition();
                NORVES_LOG_DEBUG("Input", "ScrollDelta={:.3f}, CamDist={:.3f}, CamPos=({:.2f}, {:.2f}, {:.2f})",
                                 scroll, dist, pos.x, pos.y, pos.z);
            }
        }

        // カメラ状態をRenderWorldに反映
        {
            NorvesLib::Core::Rendering::CameraProxy cameraProxy;
            m_CameraController.ApplyTo(cameraProxy);
            GEngine->GetRenderWorld().SetMainCamera(cameraProxy);
        }

        // ライトコントローラー更新（現在はGameMode管理のため無効化）
        // m_LightController.Update(inputState, deltaTime);

        // ライト状態のSceneView反映はWorld::SyncToSceneViewで
        // LightComponent経由で自動的に行われる
    }

    void GameApplicationHandler::OnPreShutdown()
    {
        LOG_INFO("GameApplicationHandler::OnPreShutdown()");

        // 終了前の保存処理など
        // - セーブデータの保存
        // - 設定の保存
    }

    void GameApplicationHandler::OnShutdown()
    {
        LOG_INFO("GameApplicationHandler::OnShutdown()");

        // ゲーム固有の終了処理
        // - リソースの解放
        // - オーディオシステムの終了
        // - ネットワーク切断
    }

    void GameApplicationHandler::OnFocusGained()
    {
        LOG_INFO("GameApplicationHandler::OnFocusGained()");
        m_bIsPaused = false;
    }

    void GameApplicationHandler::OnFocusLost()
    {
        LOG_INFO("GameApplicationHandler::OnFocusLost()");
        // ゲームによってはフォーカスを失った時にポーズ
        // m_bIsPaused = true;
    }

    NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::GameMode::IStateMachine>
    GameApplicationHandler::CreateGameModeStateMachine()
    {
        LOG_INFO("GameApplicationHandler::CreateGameModeStateMachine()");

        // 3Dレンダリングテスト用のステートマシンを作成
        using namespace Game::GameModes;

        auto stateMachine = MakeUnique<TStateMachine<IGameMode, GameModeFactory>>();

        // 3Dレンダリングテストモードを初期ステートとして設定
        auto rendering3DTestMode = MakeUnique<Rendering3DTestMode>();
        rendering3DTestMode->GetData().m_ModelPath =
            m_Rendering3DTestModelPath.empty()
                ? String(kDefaultRendering3DTestModelPath)
                : m_Rendering3DTestModelPath;
        stateMachine->ReserveState(std::move(rendering3DTestMode));

        LOG_INFO("3Dレンダリングテストモードを開始します");

        return stateMachine;
    }

} // namespace Game
