#include "GameApplicationHandler.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedAudioFormat.h"
#include "Core/Public/Asset/CookedSkeletalFormat.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Animation/AnimationClipResource.h"
#include "Core/Public/Animation/SkeletonResource.h"
#include "Core/Public/Animation/SkeletalAssetResource.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Resource/SkinnedMeshResource.h"
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

// モジュールシステム第1段1C-ii: --dummy-overlay ゲートで描画ダミーモジュールを登録する。
// 別 static lib(NorvesModule_Dummy)の自由関数を明示参照し、Core 在駐の ModuleRegistry へ
// 登録する(リンク引込も兼ねる)。フラグ無しでは一切登録せず seam は完全 no-op。
#include "Core/Public/Module/ModuleRegistry.h"
#include "DummyModule/DummyRenderModule.h"
#include "Physics/IPhysicsModule.h"

#if defined(NORVES_GAME_AUDIO)
#include "Audio/AudioClipResource.h"
#include "Audio/IAudioModule.h"
#endif

// モジュールシステム第2段 B-i: --imgui ゲートで ImGui モジュールを登録する。
// 別 static lib(NorvesModule_ImGui)の自由関数を明示参照し、Core 在駐の ModuleRegistry
// へ登録する(リンク引込も兼ねる)。NORVES_ENABLE_IMGUI OFF ビルドでは本ヘッダも登録も
// 一切無く、--imgui は未処理=完全 no-op(素ビルド byte-for-byte 不変)。
#if defined(NORVES_ENABLE_IMGUI)
#include "ImGuiModule/ImGuiModule.h"
#include "ImGuiModule/IImGuiView.h"
#endif

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
        constexpr const TCHAR *kRendering3DTestUseCookedModelOption = TEXT("--rendering3dtest-use-cooked-model");
        constexpr const TCHAR *kRendering3DTestBoardSmokeCountOption = TEXT("--rendering3dtest-board-smoke-count");
        constexpr const TCHAR *kRendering3DTestBillboardSmokeCountOption = TEXT("--rendering3dtest-billboard-smoke-count");
        constexpr const TCHAR *kRendering3DTestImpostorSmokeCountOption = TEXT("--rendering3dtest-impostor-smoke-count");
        constexpr const TCHAR *kRendering3DTestInstancedMeshCountOption = TEXT("--rendering3dtest-instanced-mesh-count");
        constexpr const TCHAR *kRendering3DTestLayerCompositeSmokeOption = TEXT("--rendering3dtest-layer-composite-smoke");
        constexpr const TCHAR* kRendering3DTestPhysicsSmokeOption = TEXT("--rendering3dtest-physics-smoke");
        constexpr const TCHAR* kM9WorldSmokeOption = TEXT("--m9-world-smoke");
        constexpr const TCHAR* kBridgePortOption = TEXT("--bridge-port");
        // 値を取らない bare フラグ(--enable-canvas-view と同類)。指定時のみ描画ダミー
        // モジュールを登録する不変条件ゲート。
        constexpr const TCHAR *kDummyOverlayOption = TEXT("--dummy-overlay");
        // 値を取らない bare フラグ(第2段 B-i)。指定時のみ ImGui モジュールを登録する
        // 不変条件ゲート。NORVES_ENABLE_IMGUI ビルドでのみ有効に処理される。
        constexpr const TCHAR *kImGuiOption = TEXT("--imgui");
        constexpr const TCHAR *kDefaultRendering3DTestModelPath = TEXT("Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf");
        constexpr uint32_t kRendering3DTestMaxInstancedMeshCount = 200u;
        uint32_t s_Rendering3DTestBoardSmokeCount = 0;
        uint32_t s_Rendering3DTestBillboardSmokeCount = 0;
        uint32_t s_Rendering3DTestImpostorSmokeCount = 0;
        uint32_t s_Rendering3DTestInstancedMeshCount = 0;
        bool s_bRendering3DTestLayerCompositeSmoke = false;
        bool s_bRendering3DTestPhysicsSmoke = false;

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

        if (NorvesLib::Modules::Physics::RegisterPhysicsModule(
                NorvesLib::Core::Module::GetModuleRegistry()) == nullptr)
        {
            LOG_ERROR("Physics module registration failed");
            return false;
        }

        m_M6ScriptSmokeController.Configure(args);

        m_bHasTextureAssetRuntimeConfig = false;
        m_bRendering3DTestUseCookedModel = false;
#if defined(NORVES_ENABLE_IMGUI)
        m_bImGuiRequested = false;
#endif
        m_TextureAssetRoot = {};
        m_TextureAssetManifestPath = {};
        m_Rendering3DTestModelPath = {};
        s_Rendering3DTestBoardSmokeCount = 0;
        s_Rendering3DTestBillboardSmokeCount = 0;
        s_Rendering3DTestImpostorSmokeCount = 0;
        s_Rendering3DTestInstancedMeshCount = 0;
        s_bRendering3DTestLayerCompositeSmoke = false;
        s_bRendering3DTestPhysicsSmoke = false;
        bool bHasRendering3DTestBoardSmokeCount = false;
        bool bHasRendering3DTestBillboardSmokeCount = false;
        bool bHasRendering3DTestImpostorSmokeCount = false;
        bool bHasRendering3DTestInstancedMeshCount = false;
        bool bHasRendering3DTestPhysicsSmoke = false;

        // Bridge（NorvesEditor 連携）の起動オプションを解析する。無効値は Bridge 無効の
        // まま警告を出すだけでクラッシュさせない（通常の NorvesLib 起動を妨げない）。
        ParseBridgePortOption(args);

        // コマンドライン引数の処理
        for (size_t i = 0; i < args.size(); ++i)
        {
            // コマンドライン引数のログ出力
            LOG_INFO_F("Arg[%zu]=%s", i, args[i].c_str());

            if (args[i] == kRendering3DTestPhysicsSmokeOption)
            {
                if (bHasRendering3DTestPhysicsSmoke)
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: duplicate --rendering3dtest-physics-smoke");
                    return false;
                }

                s_bRendering3DTestPhysicsSmoke = true;
                bHasRendering3DTestPhysicsSmoke = true;
                continue;
            }

            if (ToStdString(args[i]) == std::basic_string<TCHAR>(kRendering3DTestLayerCompositeSmokeOption))
            {
                s_bRendering3DTestLayerCompositeSmoke = true;
                continue;
            }

            if (ToStdString(args[i]) == std::basic_string<TCHAR>(kRendering3DTestUseCookedModelOption))
            {
                if (m_bRendering3DTestUseCookedModel)
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: duplicate --rendering3dtest-use-cooked-model");
                    return false;
                }

                m_bRendering3DTestUseCookedModel = true;
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

            bool bMatchedInstancedMeshCount = false;
            bool bInstancedMeshCountHasInlineValue = false;
            String instancedMeshCountInlineValue;
            if (!TryMatchTextureAssetOption(args[i],
                                            kRendering3DTestInstancedMeshCountOption,
                                            bMatchedInstancedMeshCount,
                                            bInstancedMeshCountHasInlineValue,
                                            instancedMeshCountInlineValue,
                                            parseError))
            {
                LOG_ERROR("Rendering3DTest command line parse failed: %s", parseError.c_str());
                return false;
            }

            if (bMatchedInstancedMeshCount)
            {
                if (bHasRendering3DTestInstancedMeshCount)
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: duplicate --rendering3dtest-instanced-mesh-count");
                    return false;
                }

                String instancedMeshCountText;
                if (!ReadTextureAssetOptionValue(args,
                                                 i,
                                                 kRendering3DTestInstancedMeshCountOption,
                                                 bInstancedMeshCountHasInlineValue,
                                                 instancedMeshCountInlineValue,
                                                 instancedMeshCountText,
                                                 parseError))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: %s", parseError.c_str());
                    return false;
                }

                uint32_t parsedInstancedMeshCount = 0;
                if (!TryParseUInt32(instancedMeshCountText, parsedInstancedMeshCount))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: invalid --rendering3dtest-instanced-mesh-count value \"%s\"",
                              instancedMeshCountText.c_str());
                    return false;
                }

                if (parsedInstancedMeshCount > kRendering3DTestMaxInstancedMeshCount)
                {
                    LOG_WARNING("Rendering3DTest instanced mesh count clamped requested=%u clamped=%u",
                                parsedInstancedMeshCount,
                                kRendering3DTestMaxInstancedMeshCount);
                    parsedInstancedMeshCount = kRendering3DTestMaxInstancedMeshCount;
                }

                s_Rendering3DTestInstancedMeshCount = parsedInstancedMeshCount;
                bHasRendering3DTestInstancedMeshCount = true;
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
        if (m_bRendering3DTestUseCookedModel &&
            (!m_bHasTextureAssetRuntimeConfig || m_Rendering3DTestModelPath.empty()))
        {
            LOG_ERROR("Rendering3DTest command line parse failed: --rendering3dtest-use-cooked-model requires --texture-asset-root, --texture-asset-manifest, and --rendering3dtest-model");
            return false;
        }
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
        if (bHasRendering3DTestInstancedMeshCount)
        {
            LOG_INFO("Rendering3DTest instanced mesh count parsed count=%u",
                     s_Rendering3DTestInstancedMeshCount);
        }
        if (s_bRendering3DTestLayerCompositeSmoke)
        {
            LOG_INFO("Rendering3DTest layer composite smoke parsed enabled=true");
        }
        if (bHasRendering3DTestPhysicsSmoke)
        {
            LOG_INFO("Rendering3DTest physics smoke parsed enabled=true");
        }

        // モジュールシステム第1段1C-ii: --dummy-overlay 不変条件ゲート。
        // 値を取らない bare フラグの厳密一致を走査し、指定時のみ描画ダミーモジュールを
        // ModuleRegistry へ登録する。フラグ無しでは一切登録せず、overlay seam は完全
        // no-op(F1 描画 baseline 不変)を保つ。OnPreInitialize は ApplicationProcessor の
        // InstallAll より前に走るため、ここで登録すれば InstallAll が寿命を駆動する。
        bool bDummyOverlay = false;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (ToStdString(args[i]) == std::basic_string<TCHAR>(kDummyOverlayOption))
            {
                bDummyOverlay = true;
                break;
            }
        }
        if (bDummyOverlay)
        {
            NorvesLib::Core::Module::RegisterDummyRenderModule(
                NorvesLib::Core::Module::GetModuleRegistry());
            LOG_INFO("Module runtime option --dummy-overlay: DummyRenderModule registered");
        }

        bool bM9WorldSmoke = false;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == kM9WorldSmokeOption)
            {
                bM9WorldSmoke = true;
                break;
            }
        }
        if (bM9WorldSmoke)
        {
            m_M9WorldAcceptance = MakeShared<Game::GameModes::M9WorldAcceptanceConfig>();
            m_M9WorldAcceptance->bRequested = true;
#if defined(NORVES_GAME_AUDIO)
            if (NorvesLib::Modules::Audio::RegisterAudioModule(
                    NorvesLib::Core::Module::GetModuleRegistry()) == nullptr)
            {
                LOG_ERROR("M9 world smoke failed to register the XAudio2 module");
                return false;
            }
#else
            LOG_ERROR("M9 world smoke is unavailable because Game was built without audio support");
            return false;
#endif
            LOG_INFO("M9_WORLD_SMOKE stage=registered");
#if defined(NORVES_GAME_AUDIO)
            Game::GameModes::EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=registered");
#endif
        }

        // 第2段 B-i: --imgui 不変条件ゲート。値を取らない bare フラグの厳密一致を走査し、
        // NORVES_ENABLE_IMGUI ビルドで指定された場合のみ ImGui モジュールを登録する。
        // フラグ無し or OFF ビルドでは一切登録せず、overlay seam は完全 no-op を保つ。
        bool bImGui = false;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (ToStdString(args[i]) == std::basic_string<TCHAR>(kImGuiOption))
            {
                bImGui = true;
                break;
            }
        }
        if (bImGui)
        {
#if defined(NORVES_ENABLE_IMGUI)
            NorvesLib::Core::Module::RegisterImGuiModule(
                NorvesLib::Core::Module::GetModuleRegistry());
            LOG_INFO("Module runtime option --imgui: ImGuiModule registered");
#else
            LOG_WARNING("Module runtime option --imgui ignored: build without NORVES_ENABLE_IMGUI");
#endif
        }
#if defined(NORVES_ENABLE_IMGUI)
        m_bImGuiRequested = bImGui;
#endif
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

        m_M6ScriptSmokeController.Initialize();
        return true;
    }

    void GameApplicationHandler::OnPostInitialize()
    {
        LOG_INFO("GameApplicationHandler::OnPostInitialize()");

        if (m_bHasTextureAssetRuntimeConfig && !ReloadConfiguredAssetManifest())
        {
            if (NorvesLib::Core::Engine::GEngine)
            {
                NorvesLib::Core::Engine::GEngine->RequestExit(1);
            }
            return;
        }

        if (m_M9WorldAcceptance && !PrepareM9WorldAssets())
        {
            LOG_ERROR("M9_WORLD_SMOKE stage=failure reason=asset_prepare");
#if defined(NORVES_GAME_AUDIO)
            Game::GameModes::EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=failure reason=asset_prepare");
#endif
            if (NorvesLib::Core::Engine::GEngine)
            {
                NorvesLib::Core::Engine::GEngine->RequestExit(1);
            }
            return;
        }

        // メインディレクショナルライトはGameMode（Rendering3DTest）内の
        // LightComponent経由でSceneViewに登録されるため、
        // ここでの直接登録は行わない
#if defined(NORVES_ENABLE_IMGUI)
        if (m_bImGuiRequested)
        {
            NorvesLib::Core::Engine::Engine* engine = NorvesLib::Core::Engine::GEngine;
            if (engine == nullptr || !engine->GetRenderWorld().IsInitialized())
            {
                LOG_ERROR("EngineStats ImGui view registration failed: RenderWorld unavailable");
                if (engine != nullptr)
                {
                    engine->RequestExit(1);
                }
                return;
            }

            m_EngineStatsImGuiView.SetRenderingCoordinator(
                &engine->GetRenderWorld().GetRenderingCoordinator());
            NorvesLib::Modules::Gui::RegisterImGuiView(&m_EngineStatsImGuiView);
            m_bEngineStatsImGuiViewRegistered = true;
            LOG_INFO("EngineStats ImGui view registered");
        }
#endif
    }

    bool GameApplicationHandler::ReloadConfiguredAssetManifest()
    {
        if (!m_bHasTextureAssetRuntimeConfig ||
            m_TextureAssetRoot.empty() ||
            m_TextureAssetManifestPath.empty())
        {
            LOG_WARNING("Asset runtime snapshot reload rejected: asset runtime config is not set");
            return false;
        }

        if (!NorvesLib::Core::Engine::GEngine)
        {
            LOG_ERROR("Asset runtime snapshot reload failed: GEngine is null");
            return false;
        }

        const std::filesystem::path rootPath = ToFilesystemPath(m_TextureAssetRoot);
        const std::filesystem::path manifestPath = ToFilesystemPath(m_TextureAssetManifestPath);

        std::error_code errorCode;
        if (!std::filesystem::exists(rootPath, errorCode) || errorCode)
        {
            LOG_ERROR("Asset runtime snapshot reload failed: root does not exist path=\"%s\"",
                      m_TextureAssetRoot.c_str());
            return false;
        }

        errorCode = {};
        if (!std::filesystem::is_directory(rootPath, errorCode) || errorCode)
        {
            LOG_ERROR("Asset runtime snapshot reload failed: root is not a directory path=\"%s\"",
                      m_TextureAssetRoot.c_str());
            return false;
        }

        errorCode = {};
        if (!std::filesystem::exists(manifestPath, errorCode) || errorCode)
        {
            LOG_ERROR("Asset runtime snapshot reload failed: manifest does not exist path=\"%s\"",
                      m_TextureAssetManifestPath.c_str());
            return false;
        }

        errorCode = {};
        if (!std::filesystem::is_regular_file(manifestPath, errorCode) || errorCode)
        {
            LOG_ERROR("Asset runtime snapshot reload failed: manifest is not a regular file path=\"%s\"",
                      m_TextureAssetManifestPath.c_str());
            return false;
        }

        std::ifstream manifestInput(manifestPath, std::ios::binary);
        if (!manifestInput.is_open())
        {
            LOG_ERROR("Asset runtime snapshot reload failed: manifest read failed path=\"%s\"",
                      m_TextureAssetManifestPath.c_str());
            return false;
        }

        const std::string manifestBytes((std::istreambuf_iterator<char>(manifestInput)),
                                        std::istreambuf_iterator<char>());
        if (!manifestInput.eof() && manifestInput.fail())
        {
            LOG_ERROR("Asset runtime snapshot reload failed: manifest read failed path=\"%s\"",
                      m_TextureAssetManifestPath.c_str());
            return false;
        }

        const String manifestText = MakeStringFromUtf8Bytes(manifestBytes);
        auto candidate = MakeShared<Asset::AssetSystem>(AnsiString(m_TextureAssetRoot.c_str()));
        const AnsiStringView sourceName(m_TextureAssetManifestPath.data(),
                                        m_TextureAssetManifestPath.size());
        if (!candidate->LoadManifestFromJsonText(manifestText, sourceName))
        {
            LOG_ERROR("Asset runtime snapshot reload failed: manifest parse failed path=\"%s\"",
                      m_TextureAssetManifestPath.c_str());
            return false;
        }

        TSharedPtr<const Asset::AssetSystem> immutableCandidate = candidate;
        if (!NorvesLib::Core::Engine::GEngine->GetRenderResources().ReloadAssetRuntimeSnapshot(
                m_TextureAssetRoot,
                immutableCandidate))
        {
            LOG_WARNING("Asset runtime snapshot reload rejected by runtime root=\"%s\" manifest=\"%s\"",
                        m_TextureAssetRoot.c_str(),
                        m_TextureAssetManifestPath.c_str());
            return false;
        }

        m_AssetSystemSnapshot = immutableCandidate;
        LOG_INFO("Asset runtime snapshot reload completed root=\"%s\" manifest=\"%s\"",
                 m_TextureAssetRoot.c_str(),
                 m_TextureAssetManifestPath.c_str());
        return true;
    }

    TSharedPtr<const Asset::AssetSystem> GameApplicationHandler::GetAssetSystemSnapshot() const
    {
        return m_AssetSystemSnapshot;
    }

    bool GameApplicationHandler::PrepareM9WorldAssets()
    {
        if (!m_M9WorldAcceptance || !m_M9WorldAcceptance->bRequested || !m_AssetSystemSnapshot ||
            !NorvesLib::Core::Engine::GEngine)
        {
            return false;
        }

#if !defined(NORVES_GAME_AUDIO)
        return false;
#else
        const Asset::AssetResolveResult skeletalResult = m_AssetSystemSnapshot->ResolveAsset(
            "Models/M9Skinned/ValidU8Float.gltf", Asset::AssetKind::Model);
        const Asset::AssetResolveResult effectResult = m_AssetSystemSnapshot->ResolveAsset(
            "Audio/M9/effect.wav", Asset::AssetKind::Audio);
        const Asset::AssetResolveResult loopResult = m_AssetSystemSnapshot->ResolveAsset(
            "Audio/M9/loop.wav", Asset::AssetKind::Audio);
        if (!skeletalResult.UsedCooked() || !effectResult.UsedCooked() || !loopResult.UsedCooked())
        {
            LOG_ERROR("M9_WORLD_SMOKE asset resolution requires all three cooked assets");
            return false;
        }

        Asset::CookedSkeletalParseResult skeletal = Asset::ParseCookedSkeletal(skeletalResult.Blob);
        Asset::CookedAudioParseResult effect = Asset::ParseCookedAudio(effectResult.Blob);
        Asset::CookedAudioParseResult loop = Asset::ParseCookedAudio(loopResult.Blob);
        if (!skeletal.Succeeded() || !effect.Succeeded() || !loop.Succeeded() || skeletal.Data.Skeletal.Clips.empty())
        {
            LOG_ERROR("M9_WORLD_SMOKE cooked asset parsing failed");
            return false;
        }

        auto& resources = NorvesLib::Core::GEngine.GetResourceRegistry();
        auto mesh = resources.CreateTransient<SkinnedMeshResource>("M9WorldSkinnedMesh");
        auto skeleton = resources.CreateTransient<SkeletonResource>("M9WorldSkeleton");
        auto clip = resources.CreateTransient<AnimationClipResource>("M9WorldClip");
        auto skeletalAsset = resources.CreateTransient<SkeletalAssetResource>("M9WorldAsset");
        if (!mesh || !skeleton || !clip || !skeletalAsset)
        {
            return false;
        }
        mesh->SetMeshNodeGlobalTransform(skeletal.Data.Skeletal.MeshNodeGlobalTransform);
        mesh->SetVertices(std::move(skeletal.Data.Skeletal.Vertices));
        mesh->SetIndices(std::move(skeletal.Data.Skeletal.Indices));
        skeleton->SetJoints(std::move(skeletal.Data.Skeletal.Joints));
        clip->SetClip(std::move(skeletal.Data.Skeletal.Clips[0]));
        if (!mesh->Load() || !skeleton->Load() || !clip->Load())
        {
            return false;
        }
        skeletalAsset->SetResources(mesh, skeleton, clip);
        if (!skeletalAsset->IsLoaded())
        {
            return false;
        }

        const NorvesLib::Modules::Audio::AudioPcmFormat effectFormat{
            effect.Audio.SampleRate, effect.Audio.ChannelCount, effect.Audio.BitsPerSample, effect.Audio.BlockAlignment};
        const NorvesLib::Modules::Audio::AudioPcmFormat loopFormat{
            loop.Audio.SampleRate, loop.Audio.ChannelCount, loop.Audio.BitsPerSample, loop.Audio.BlockAlignment};
        Asset::AssetBlob effectPcm = effect.Audio.SourceBlob.TryCreateSubBlob(
            static_cast<size_t>(effect.Audio.PayloadOffset), static_cast<size_t>(effect.Audio.PayloadSize));
        Asset::AssetBlob loopPcm = loop.Audio.SourceBlob.TryCreateSubBlob(
            static_cast<size_t>(loop.Audio.PayloadOffset), static_cast<size_t>(loop.Audio.PayloadSize));
        if (!effectPcm.IsValid() || effectPcm.IsEmpty() || !loopPcm.IsValid() || loopPcm.IsEmpty())
        {
            return false;
        }
        auto effectClip = MakeShared<NorvesLib::Modules::Audio::AudioClipResource>(
            effectPcm, effectFormat, effect.Audio.FrameCount);
        auto loopClip = MakeShared<NorvesLib::Modules::Audio::AudioClipResource>(
            loopPcm, loopFormat, loop.Audio.FrameCount);
        if (!effectClip || !loopClip || !effectClip->IsValid() || !loopClip->IsValid())
        {
            return false;
        }

        m_M9WorldAcceptance->SkeletalAsset = skeletalAsset;
        m_M9WorldAcceptance->EffectClip = effectClip;
        m_M9WorldAcceptance->LoopClip = loopClip;
        m_M9WorldAcceptance->bAssetsReady = true;
        LOG_INFO("M9_WORLD_SMOKE stage=assets_ready prepared=3 snapshot=1");
        Game::GameModes::EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=assets_ready prepared=3 snapshot=1");
        return true;
#endif
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
        m_M6ScriptSmokeController.Update();
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
        m_M6ScriptSmokeController.Shutdown();
        LOG_INFO("GameApplicationHandler::OnPreShutdown()");

#if defined(NORVES_ENABLE_IMGUI)
        if (m_bEngineStatsImGuiViewRegistered)
        {
            NorvesLib::Modules::Gui::UnregisterImGuiView(&m_EngineStatsImGuiView);
            LOG_INFO("EngineStats ImGui view unregistered");
            m_bEngineStatsImGuiViewRegistered = false;
        }
        m_EngineStatsImGuiView.ClearRenderingCoordinator();
#endif

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
        const bool bUseCookedModel = m_bRendering3DTestUseCookedModel;
        const bool bPhysicsSmoke = s_bRendering3DTestPhysicsSmoke;
        const TSharedPtr<M9WorldAcceptanceConfig> m9WorldAcceptance = m_M9WorldAcceptance;
        stateMachine->Registry().Register(
            Rendering3DTest,
            [bUseCookedModel, bPhysicsSmoke, m9WorldAcceptance](const GameModeParams& params) -> Container::TUniquePtr<IGameMode>
            {
                auto mode = MakeUnique<Rendering3DTestMode>();
                mode->GetData().m_ModelPath = params.ModelPath;
                mode->GetData().m_bUseCookedModel = bUseCookedModel;
                mode->GetData().m_BoardSmokeCount = s_Rendering3DTestBoardSmokeCount;
                mode->GetData().m_BillboardSmokeCount = s_Rendering3DTestBillboardSmokeCount;
                mode->GetData().m_ImpostorSmokeCount = s_Rendering3DTestImpostorSmokeCount;
                mode->GetData().m_InstancedMeshCount = s_Rendering3DTestInstancedMeshCount;
                mode->GetData().m_bLayerCompositeSmoke = s_bRendering3DTestLayerCompositeSmoke;
                mode->GetData().m_bPhysicsSmoke = bPhysicsSmoke;
                mode->GetData().m_M9WorldAcceptance = m9WorldAcceptance;
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
