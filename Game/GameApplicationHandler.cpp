#include "GameApplicationHandler.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

// GameMode関連
#include "Core/Public/GameMode/GameModeStateMachine.h"
#include "Core/Public/GameMode/GameModeId.h"
#include "Core/Public/GameMode/GameModeParams.h"
#include "GameModes/GameModeIds.h"
#include "GameModes/Rendering3DTest/Rendering3DTestMode.h"
#include "GameModes/MemoryAgingTest/MemoryAgingTestMode.h"

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
        constexpr const TCHAR *kRendering3DTestBoardSmokeCountOption = TEXT("--rendering3dtest-board-smoke-count");
        constexpr const TCHAR *kRendering3DTestBillboardSmokeCountOption = TEXT("--rendering3dtest-billboard-smoke-count");
        constexpr const TCHAR *kRendering3DTestImpostorSmokeCountOption = TEXT("--rendering3dtest-impostor-smoke-count");
        constexpr const TCHAR *kRendering3DTestLayerCompositeSmokeOption = TEXT("--rendering3dtest-layer-composite-smoke");
        constexpr const TCHAR* kBridgePortOption = TEXT("--bridge-port");
        constexpr const TCHAR *kDefaultRendering3DTestModelPath = TEXT("Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf");
        uint32_t s_Rendering3DTestBoardSmokeCount = 0;
        uint32_t s_Rendering3DTestBillboardSmokeCount = 0;
        uint32_t s_Rendering3DTestImpostorSmokeCount = 0;
        bool s_bRendering3DTestLayerCompositeSmoke = false;

        /**
         * @brief 文字列を符号なし 16bit ポートとして解析する。先頭末尾に空白がない 10 進数のみ
         *        を受理し、0 / 範囲外 / 非数字は失敗（bValid=false）として返す。
         * @param text 解析対象の文字列。
         * @param outPort 解析に成功した場合のポート値。失敗時は 0。
         * @return 解析に成功したら true、失敗したら false。
         */
        bool TryParseBridgePort(const String& text, uint16_t& outPort)
        {
            outPort = 0;
            if (text.empty())
            {
                return false;
            }

            uint32_t value = 0;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const TCHAR ch = text[i];
                if (ch < TEXT('0') || ch > TEXT('9'))
                {
                    return false;
                }
                value = value * 10 + static_cast<uint32_t>(ch - TEXT('0'));
                if (value > 65535u)
                {
                    return false;
                }
            }

            if (value == 0)
            {
                return false;
            }

            outPort = static_cast<uint16_t>(value);
            return true;
        }

        bool TryParseUInt32(const String &text, uint32_t &outValue)
        {
            outValue = 0;
            if (text.empty())
            {
                return false;
            }

            uint64_t value = 0;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const TCHAR ch = text[i];
                if (ch < TEXT('0') || ch > TEXT('9'))
                {
                    return false;
                }

                value = value * 10u + static_cast<uint64_t>(ch - TEXT('0'));
                if (value > std::numeric_limits<uint32_t>::max())
                {
                    return false;
                }
            }

            outValue = static_cast<uint32_t>(value);
            return true;
        }

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

    }

    bool GameApplicationHandler::OnPreInitialize(const VariableArray<String>& args)
    {
        LOG_INFO("GameApplicationHandler::OnPreInitialize()");

        m_bHasTextureAssetRuntimeConfig = false;
        m_TextureAssetRoot = {};
        m_TextureAssetManifestPath = {};
        m_Rendering3DTestModelPath = {};
        s_Rendering3DTestBoardSmokeCount = 0;
        s_Rendering3DTestBillboardSmokeCount = 0;
        s_Rendering3DTestImpostorSmokeCount = 0;
        s_bRendering3DTestLayerCompositeSmoke = false;
        bool bHasRendering3DTestBoardSmokeCount = false;
        bool bHasRendering3DTestBillboardSmokeCount = false;
        bool bHasRendering3DTestImpostorSmokeCount = false;

        // Bridge（NorvesEditor 連携）の起動オプションを解析する。無効値は Bridge 無効の
        // まま警告を出すだけでクラッシュさせない（通常の NorvesLib 起動を妨げない）。
        ParseBridgePortOption(args);

        // コマンドライン引数の処理
        for (size_t i = 0; i < args.size(); ++i)
        {
            // コマンドライン引数のログ出力
            LOG_INFO_F("Arg[%zu]=%s", i, args[i].c_str());

            if (ToStdString(args[i]) == std::basic_string<TCHAR>(kRendering3DTestLayerCompositeSmokeOption))
            {
                s_bRendering3DTestLayerCompositeSmoke = true;
                continue;
            }

            bool bMatchedRoot = false;
            bool bRootHasInlineValue = false;
            String rootInlineValue;
            String parseError;
            if (!TryMatchTextureAssetOption(args[i],
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

                if (!ReadTextureAssetOptionValue(args,
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
            if (!TryMatchTextureAssetOption(args[i],
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

                if (!ReadTextureAssetOptionValue(args,
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
            if (!TryMatchTextureAssetOption(args[i],
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

                if (!ReadTextureAssetOptionValue(args,
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
                continue;
            }

            bool bMatchedBoardSmokeCount = false;
            bool bBoardSmokeCountHasInlineValue = false;
            String boardSmokeCountInlineValue;
            if (!TryMatchTextureAssetOption(args[i],
                                            kRendering3DTestBoardSmokeCountOption,
                                            bMatchedBoardSmokeCount,
                                            bBoardSmokeCountHasInlineValue,
                                            boardSmokeCountInlineValue,
                                            parseError))
            {
                LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                return false;
            }

            if (bMatchedBoardSmokeCount)
            {
                if (bHasRendering3DTestBoardSmokeCount)
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: duplicate --rendering3dtest-board-smoke-count");
                    return false;
                }

                String boardSmokeCountText;
                if (!ReadTextureAssetOptionValue(args,
                                                 i,
                                                 kRendering3DTestBoardSmokeCountOption,
                                                 bBoardSmokeCountHasInlineValue,
                                                 boardSmokeCountInlineValue,
                                                 boardSmokeCountText,
                                                 parseError))
                {
                    LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                    return false;
                }

                uint32_t parsedBoardSmokeCount = 0;
                if (!TryParseUInt32(boardSmokeCountText, parsedBoardSmokeCount))
                {
                    LOG_ERROR_F("Rendering3DTest command line parse failed: invalid --rendering3dtest-board-smoke-count value \"%s\"",
                                boardSmokeCountText.c_str());
                    return false;
                }

                s_Rendering3DTestBoardSmokeCount = parsedBoardSmokeCount;
                bHasRendering3DTestBoardSmokeCount = true;
            }

            bool bMatchedBillboardSmokeCount = false;
            bool bBillboardSmokeCountHasInlineValue = false;
            String billboardSmokeCountInlineValue;
            if (!TryMatchTextureAssetOption(args[i],
                                            kRendering3DTestBillboardSmokeCountOption,
                                            bMatchedBillboardSmokeCount,
                                            bBillboardSmokeCountHasInlineValue,
                                            billboardSmokeCountInlineValue,
                                            parseError))
            {
                LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                return false;
            }

            if (bMatchedBillboardSmokeCount)
            {
                if (bHasRendering3DTestBillboardSmokeCount)
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: duplicate --rendering3dtest-billboard-smoke-count");
                    return false;
                }

                String billboardSmokeCountText;
                if (!ReadTextureAssetOptionValue(args,
                                                 i,
                                                 kRendering3DTestBillboardSmokeCountOption,
                                                 bBillboardSmokeCountHasInlineValue,
                                                 billboardSmokeCountInlineValue,
                                                 billboardSmokeCountText,
                                                 parseError))
                {
                    LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                    return false;
                }

                uint32_t parsedBillboardSmokeCount = 0;
                if (!TryParseUInt32(billboardSmokeCountText, parsedBillboardSmokeCount))
                {
                    LOG_ERROR_F("Rendering3DTest command line parse failed: invalid --rendering3dtest-billboard-smoke-count value \"%s\"",
                                billboardSmokeCountText.c_str());
                    return false;
                }

                s_Rendering3DTestBillboardSmokeCount = parsedBillboardSmokeCount;
                bHasRendering3DTestBillboardSmokeCount = true;
            }

            bool bMatchedImpostorSmokeCount = false;
            bool bImpostorSmokeCountHasInlineValue = false;
            String impostorSmokeCountInlineValue;
            if (!TryMatchTextureAssetOption(args[i],
                                            kRendering3DTestImpostorSmokeCountOption,
                                            bMatchedImpostorSmokeCount,
                                            bImpostorSmokeCountHasInlineValue,
                                            impostorSmokeCountInlineValue,
                                            parseError))
            {
                LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                return false;
            }

            if (bMatchedImpostorSmokeCount)
            {
                if (bHasRendering3DTestImpostorSmokeCount)
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: duplicate --rendering3dtest-impostor-smoke-count");
                    return false;
                }

                String impostorSmokeCountText;
                if (!ReadTextureAssetOptionValue(args,
                                                 i,
                                                 kRendering3DTestImpostorSmokeCountOption,
                                                 bImpostorSmokeCountHasInlineValue,
                                                 impostorSmokeCountInlineValue,
                                                 impostorSmokeCountText,
                                                 parseError))
                {
                    LOG_ERROR_F("Rendering3DTest command line parse failed: %s", parseError.c_str());
                    return false;
                }

                uint32_t parsedImpostorSmokeCount = 0;
                if (!TryParseUInt32(impostorSmokeCountText, parsedImpostorSmokeCount))
                {
                    LOG_ERROR_F("Rendering3DTest command line parse failed: invalid --rendering3dtest-impostor-smoke-count value \"%s\"",
                                impostorSmokeCountText.c_str());
                    return false;
                }

                s_Rendering3DTestImpostorSmokeCount = parsedImpostorSmokeCount;
                bHasRendering3DTestImpostorSmokeCount = true;
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
        if (bHasRendering3DTestBoardSmokeCount)
        {
            LOG_INFO("Rendering3DTest board smoke count parsed count=%u",
                     s_Rendering3DTestBoardSmokeCount);
        }
        if (bHasRendering3DTestBillboardSmokeCount)
        {
            LOG_INFO("Rendering3DTest billboard smoke count parsed count=%u",
                     s_Rendering3DTestBillboardSmokeCount);
        }
        if (bHasRendering3DTestImpostorSmokeCount)
        {
            LOG_INFO("Rendering3DTest impostor smoke count parsed count=%u",
                     s_Rendering3DTestImpostorSmokeCount);
        }
        if (s_bRendering3DTestLayerCompositeSmoke)
        {
            LOG_INFO("Rendering3DTest layer composite smoke parsed enabled=true");
        }

        return true;
    }

    bool GameApplicationHandler::OnInitialize()
    {
        LOG_INFO("GameApplicationHandler::OnInitialize()");

        // ========================================
        // ライトコントローラーの初期化
        // ========================================
        // メインディレクショナルライトはRendering3DTestModeのEnterで
        // LightComponent経由で作成されるため、ここでは初期化しない

        LOG_INFO("LightController initialization skipped (managed by GameMode)");

        // テストオブジェクトの作成はRendering3DTestModeのEnterで行われる

        // Bridge サーバーを起動する（GEngine 有効後のここで bind し READY を出す）。
        // 失敗しても通常起動は継続する（Bridge は無効化するのみ）。
        if (m_bBridgeEnabled)
        {
#if defined(NORVES_BRIDGE_ENABLED)
            // adapter に実エンジン状態へのアクセスと、サーバー発イベント発火用の host を
            // 与えてから host を起動する。SetHost は Start の前に行う（イベント経路の配線）。
            m_BridgeAdapter.SetHandler(*this);
            m_BridgeAdapter.SetHost(m_BridgeHost);
            if (!m_BridgeHost.Start(m_BridgePort, m_BridgeAdapter))
            {
                LOG_WARNING_F("Bridge server failed to start on port %u; continuing without Bridge",
                              static_cast<unsigned>(m_BridgePort));
                m_bBridgeEnabled = false;
            }
#else
            // 非 SDK ビルドでは Bridge engine SDK が無く、m_BridgeAdapter / m_BridgeHost は
            // 不活性スタブ。--bridge-port は解析されるがサーバーは起動しないため、
            // m_bBridgeEnabled を false に倒して以降の経路（OnUpdate の DrainInbound、
            // ShouldAdvanceSimulation など）を従来挙動に保つ。
            m_bBridgeEnabled = false;
#endif
        }

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

        auto &textures = GEngine->GetRenderResources().Textures();
        if (!textures.SetTextureAssetRoot(m_TextureAssetRoot))
        {
            LOG_ERROR_F("Texture asset runtime setup failed: SetTextureAssetRoot rejected root=\"%s\"",
                        m_TextureAssetRoot.c_str());
            return false;
        }

        const String manifestText = MakeStringFromUtf8Bytes(manifestBytes);
        if (!textures.LoadTextureAssetManifestFromJsonText(manifestText, m_TextureAssetManifestPath))
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

    void GameApplicationHandler::ParseBridgePortOption(const VariableArray<String>& args)
    {
        m_bBridgeEnabled = false;
        m_BridgePort = 0;

        // 既存の --opt=val / --opt val 解析ヘルパを流用する（args[0]=exe、未知はスキップ）。
        for (size_t i = 0; i < args.size(); ++i)
        {
            bool bMatched = false;
            bool bHasInlineValue = false;
            String inlineValue;
            String parseError;
            if (!TryMatchTextureAssetOption(args[i],
                                            kBridgePortOption,
                                            bMatched,
                                            bHasInlineValue,
                                            inlineValue,
                                            parseError))
            {
                // 形式不正（--bridge-portXXX 等）。Bridge は無効のまま警告して継続。
                LOG_WARNING_F("Bridge command line parse warning: %s", parseError.c_str());
                continue;
            }

            if (!bMatched)
            {
                continue;
            }

            String portText;
            if (!ReadTextureAssetOptionValue(args,
                                             i,
                                             kBridgePortOption,
                                             bHasInlineValue,
                                             inlineValue,
                                             portText,
                                             parseError))
            {
                LOG_WARNING_F("Bridge disabled: %s", parseError.c_str());
                return;
            }

            uint16_t parsedPort = 0;
            if (!TryParseBridgePort(portText, parsedPort))
            {
                LOG_WARNING_F("Bridge disabled: invalid --bridge-port value \"%s\" (expected 1-65535)",
                              portText.c_str());
                return;
            }

            m_BridgePort = parsedPort;
            m_bBridgeEnabled = true;
            LOG_INFO_F("Bridge enabled on port %u", static_cast<unsigned>(m_BridgePort));
            return;
        }
    }

    void GameApplicationHandler::OnUpdate(float deltaTime)
    {
        // シーンの更新（カメラ・入力・ライト）はGameMode（Rendering3DTest）へ移動した。
        // ApplicationHandlerはアプリ全体の責務（boot/コマンドライン/テクスチャ設定/
        // レジストリ・初期モード選択/フォーカス）に集中する。
        (void)deltaTime;

        // Bridge が有効なら受信フレームをこのゲームスレッドで処理して応答する。
        // ポーズゲーティングは ShouldAdvanceSimulation で行う（OnUpdate 自体は常に回す）。
        if (m_bBridgeEnabled)
        {
            m_BridgeHost.DrainInbound();
        }
    }

    bool GameApplicationHandler::ShouldAdvanceSimulation() const
    {
        // Bridge 無効なら従来挙動（常に進行）。
        if (!m_bBridgeEnabled)
        {
            return true;
        }

        // Edit/Playing は進行、Paused/Stopped は停止。
        return m_BridgeRuntimeState == Game::Bridge::BridgeRuntimeState::Edit ||
               m_BridgeRuntimeState == Game::Bridge::BridgeRuntimeState::Playing;
    }

    void GameApplicationHandler::OnPreShutdown()
    {
        LOG_INFO("GameApplicationHandler::OnPreShutdown()");

        // Bridge を World/RenderWorld 破棄より前に停止する（close→join、冪等）。
        // 受信スレッドは GEngine に触れないが、確実に join してから先の解体へ進む。
        m_BridgeHost.Stop();

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

        auto stateMachine = MakeUnique<GameModeStateMachine>();
        stateMachine->Registry().Register(
            Rendering3DTest,
            [](const GameModeParams& params) -> Container::TUniquePtr<IGameMode>
            {
                auto mode = MakeUnique<Rendering3DTestMode>();
                mode->GetData().m_ModelPath = params.ModelPath;
                mode->GetData().m_BoardSmokeCount = s_Rendering3DTestBoardSmokeCount;
                mode->GetData().m_BillboardSmokeCount = s_Rendering3DTestBillboardSmokeCount;
                mode->GetData().m_ImpostorSmokeCount = s_Rendering3DTestImpostorSmokeCount;
                mode->GetData().m_bLayerCompositeSmoke = s_bRendering3DTestLayerCompositeSmoke;
                return mode;
            });
        stateMachine->Registry().Register(
            MemoryAgingTest,
            [](const GameModeParams&) -> Container::TUniquePtr<IGameMode>
            {
                return MakeUnique<MemoryAgingTestMode>();
            });

        GameModeParams params;
        params.ModelPath = m_Rendering3DTestModelPath.empty()
                               ? String(kDefaultRendering3DTestModelPath)
                               : m_Rendering3DTestModelPath;
        stateMachine->Start(Rendering3DTest, params);

        LOG_INFO("3Dレンダリングテストモードを開始します");

        return stateMachine;
    }

} // namespace Game
