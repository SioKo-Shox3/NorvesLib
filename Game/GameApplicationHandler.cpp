#include "GameApplicationHandler.h"
#include "Core/Public/Animation/AnimationClipResource.h"
#include "Core/Public/Animation/SkeletalAssetResource.h"
#include "Core/Public/Animation/SkeletonResource.h"
#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedAudioFormat.h"
#include "Core/Public/Asset/CookedSkeletalFormat.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Input/InputMapper.h"
#include "Core/Public/Input/InputRouter.h"
#include "Core/Public/Input/InputSystem.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Resource/SkinnedMeshResource.h"
#include "GameModes/DogMovementSmoke/DogMovementSmokeMode.h"
#include "Input/GameHapticsSettings.h"
#include "Input/GameInputActions.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
#include "GameModes/Launcher/LauncherMode.h"
#include "GameModes/MegaBananas/MegaBananasMode.h"
#include "GameModes/MegaStress/MegaStressMode.h"
#include "GameModes/Sandbox/SandboxMode.h"
#include "Scenes/SceneCatalog.h"

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
        // --rendering3dtest-model-source=cooked|gltf: 起動画面の岩・小屋をクック済み（NVMESH v1・BC・VT）で読むか、glTF の実行時の経路で読むか。
        // 既定は cooked（クック済みが無ければ glTF へ戻して警告する）。gltf は見た目・VRAM の比較用。
        constexpr const TCHAR *kRendering3DTestModelSourceOption = TEXT("--rendering3dtest-model-source=");
        // --rendering3dtest-big-sphere-source=cooked|runtime: 起動画面の大きな球をクック済み（NVMESH v1）で読むか、実行時に生成するか。
        // 既定は cooked（クック済みが無ければ実行時の生成へ戻して警告する）。runtime は見た目・起動時間の比較用。
        constexpr const TCHAR *kRendering3DTestBigSphereSourceOption = TEXT("--rendering3dtest-big-sphere-source=");
        constexpr const TCHAR *kRendering3DTestBoardSmokeCountOption = TEXT("--rendering3dtest-board-smoke-count");
        constexpr const TCHAR *kRendering3DTestBillboardSmokeCountOption = TEXT("--rendering3dtest-billboard-smoke-count");
        constexpr const TCHAR *kRendering3DTestImpostorSmokeCountOption = TEXT("--rendering3dtest-impostor-smoke-count");
        constexpr const TCHAR *kRendering3DTestInstancedMeshCountOption = TEXT("--rendering3dtest-instanced-mesh-count");
        constexpr const TCHAR *kRendering3DTestLayerCompositeSmokeOption = TEXT("--rendering3dtest-layer-composite-smoke");
        constexpr const TCHAR *kRendering3DTestPhysicsSmokeOption = TEXT("--rendering3dtest-physics-smoke");
        constexpr const TCHAR* kM9WorldSmokeOption = TEXT("--m9-world-smoke");
        constexpr const TCHAR* kBridgePortOption = TEXT("--bridge-port");
        // 値を取らない bare フラグ(--enable-canvas-view と同類)。指定時のみ描画ダミー
        // モジュールを登録する不変条件ゲート。
        constexpr const TCHAR *kDummyOverlayOption = TEXT("--dummy-overlay");
        // 値を取らない bare フラグ(第2段 B-i)。指定時のみ ImGui モジュールを登録する
        // 不変条件ゲート。NORVES_ENABLE_IMGUI ビルドでのみ有効に処理される。
        constexpr const TCHAR *kImGuiOption = TEXT("--imgui");
        // --open-pause-menu: 起動して数フレーム後にポーズメニューを開く（開発ビルドだけ。値を取らない）。
        // 撮影の起動はポーズメニューを出さないが、この指定があれば撮影でも使い、開いた状態を撮れる。
        constexpr const TCHAR *kOpenPauseMenuOption = TEXT("--open-pause-menu");
        // 撮影・自動実行の起動（--capture-png・--capture-sequence・--exit-after-frames）はポーズメニューを出さない。
        constexpr const TCHAR *kCapturePngOption = TEXT("--capture-png=");
        constexpr const TCHAR *kExitAfterFramesOption = TEXT("--exit-after-frames=");
        // --open-pause-menu で開くまでに待つ OnUpdate の回数（シーンの入場とフォントの用意を待つ）
        constexpr uint32_t kPauseMenuStartupOpenTick = 3;
        // ポーズメニューの入力の優先度。シーンの入力（PriorityGame とその上のピッキング）より上、
        // デバッグの入力マスク（PriorityDebugOverlayMask）より下
        constexpr int32_t kPauseMenuInputPriority = NorvesLib::Core::Input::InputRouter::PriorityGame + 100;
        constexpr const TCHAR *kDefaultRendering3DTestModelPath = TEXT("Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf");
        constexpr uint32_t kRendering3DTestMaxInstancedMeshCount = 200u;
        uint32_t s_Rendering3DTestBoardSmokeCount = 0;
        uint32_t s_Rendering3DTestBillboardSmokeCount = 0;
        uint32_t s_Rendering3DTestImpostorSmokeCount = 0;
        uint32_t s_Rendering3DTestInstancedMeshCount = 0;
        bool s_bRendering3DTestLayerCompositeSmoke = false;
        bool s_bRendering3DTestPhysicsSmoke = false;
        // --scene=<id>: ランチャーを飛ばして直接入るシーン（Scenes/SceneCatalog.h の id。launcher でランチャー）。引数なしの起動はランチャー。
        // --scene-view=<name>: そのシーンの名前付き視点。どちらも未知の値はエラーをログに出して既定へ落ちる。
        constexpr const TCHAR *kSceneOption = TEXT("--scene=");
        constexpr const TCHAR *kSceneViewOption = TEXT("--scene-view=");
        // --scene-report=<path>: シーンの解決結果（未知の id・視点、欠けた資産）をテキストで書き出す。
        // ログを書かない構成（Release）でも撮影スクリプトが資産の欠けを検査できるよう、ログとは別に出す。
        constexpr const TCHAR *kSceneReportOption = TEXT("--scene-report=");
        // --camera-script=<file>: 自由飛行カメラへ決まった入力を流す台本（開発ビルドだけ。書式は Input/CameraScript.h）。
        // 撮影で「この操作でここへ動く」を再現するのに使う。
        constexpr const TCHAR *kCameraScriptOption = TEXT("--camera-script=");
        // --camera-path-time=<秒>: カメラパスを持つシーンで、起動時の視点をパスのこの時刻の位置と向きにする（停止した状態。撮影用）。
        constexpr const TCHAR *kCameraPathTimeOption = TEXT("--camera-path-time=");
        // --banana-count=<n>: mega-bananas のバナナの本数（1〜kMaxBananaCount。既定 kDefaultBananaCount）。
        constexpr const TCHAR *kBananaCountOption = TEXT("--banana-count=");
        // --stress-entities=<n>: mega-stress の Entity の数（1〜kMaxStressEntityCount。既定 kDefaultStressEntityCount）。
        constexpr const TCHAR *kStressEntitiesOption = TEXT("--stress-entities=");
        // --stress-moving-percent=<p>: mega-stress で毎フレーム動かす Entity の割合[%]（0〜100。既定 0）。
        // --stress-moving-time=<秒>: 動く Entity の動きの時刻の底上げ（撮影で時刻を変えるための指定。既定 0）。
        constexpr const TCHAR *kStressMovingPercentOption = TEXT("--stress-moving-percent=");
        constexpr const TCHAR *kStressMovingTimeOption = TEXT("--stress-moving-time=");
        // --stress-stream-cell=<m>: mega-stress で、この一辺[m]の区画ごとに Entity を読み込み・解放する（0 = 行わない。既定 0）。
        // --stress-stream-period=<秒>: 区画を 1 つ出入りさせる周期（既定 kDefaultStressStreamPeriod）。
        constexpr const TCHAR *kStressStreamCellOption = TEXT("--stress-stream-cell=");
        constexpr const TCHAR *kStressStreamPeriodOption = TEXT("--stress-stream-period=");
        // --mega-lod-error-px=<px>: MegaGeometry の LOD を選ぶ誤差の閾値（画素）を替える（開発ビルドだけ。計測用）。
        // 大きい値にすると全インスタンスが根の段で描かれる。環境変数 NORVES_MEGA_LOD_ERROR_PX と同じ効き方をする。
#if NORVES_BUILD_DEVELOPMENT
        constexpr const TCHAR *kMegaLodErrorPxOption = TEXT("--mega-lod-error-px=");
#endif
        // --engine-checks=<名前,...>: 開発ビルドの検査（Debug/EngineChecks.h）を名前で有効にする。結果は ENGINE_CHECK の行でログへ出る。
        // --engine-checks-selftest-fail: 配線の確認用の検査 ENGINE_CHECK_SELFTEST を fail で報告させる（落ちる出力の確認用）。
        constexpr const TCHAR *kEngineChecksOption = TEXT("--engine-checks=");
        constexpr const TCHAR *kEngineChecksSelfTestFailOption = TEXT("--engine-checks-selftest-fail");
        bool s_bEngineChecksSelfTestFail = false;
        bool s_bEngineChecksSelfTestReported = false;
        // --mega-scene-full-sync: MegaGeometry の同期を全量にする（ECS の全行を毎フレーム公開し直し、パケットは配列を全量で写す）。
        // 変わった行だけの同期（既定）との見比べと切り戻しに使う。値を取らない。
        constexpr const TCHAR *kMegaSceneFullSyncOption = TEXT("--mega-scene-full-sync");
        bool s_bMegaSceneFullSync = false;
        // --open-scene-info: mega-bananas を起動時から情報表示（H キーの表示）を出した状態にする（開発ビルドだけ。値を取らない）。
        // 撮影の起動は ImGui を使わないが、この指定があれば撮影でも使い、表示を撮れる。
        constexpr const TCHAR *kOpenSceneInfoOption = TEXT("--open-scene-info");
        // --scene-cycle-smoke=<id,...>: ランチャーと指定のシーンの間を周回し、資源の数が増えないことを確かめる。
        // --scene-cycle-count=<n>: その周回数（既定 3。2 以上で1周目と比べる）。
        constexpr const TCHAR *kSceneCycleSmokeOption = TEXT("--scene-cycle-smoke=");
        constexpr const TCHAR *kSceneCycleCountOption = TEXT("--scene-cycle-count=");
        // --startup-camera=<yaw>,<pitch>,<arm>: 起動時のカメラ（SpringArm の角度[度]と腕の長さ）。
        constexpr const TCHAR *kStartupCameraOption = TEXT("--startup-camera=");
        bool s_bRendering3DTestHasStartupCamera = false;
        float s_Rendering3DTestStartupCamera[3] = {0.0f, 0.0f, 0.0f};
        // --sun-elevation=<deg> / --sun-azimuth=<deg>: 起動時の空の太陽の仰角（0〜90）と方位（-180〜180）。
        // --exposure-ev100=<ev>: 起動時の手動露出（EV100、-6〜24）。
        constexpr const TCHAR *kSunElevationOption = TEXT("--sun-elevation=");
        constexpr const TCHAR *kSunAzimuthOption = TEXT("--sun-azimuth=");
        constexpr const TCHAR *kExposureEV100Option = TEXT("--exposure-ev100=");
        // --height-fog-density=<1/m>: 起動画面の高さフォグの地面での密度（0で無効、0〜1）。見比べと調整に使う。
        constexpr const TCHAR *kHeightFogDensityOption = TEXT("--height-fog-density=");
        // --height-fog-falloff=<1/m>: 起動画面の高さフォグの高さ方向の減衰（0〜1）。見比べと調整に使う。
        constexpr const TCHAR *kHeightFogFalloffOption = TEXT("--height-fog-falloff=");
        // --orbit-degrees-per-second=<deg/s>: カメラを一定の速さで回す（-360〜360、撮影で動くカメラを確かめる）。
        constexpr const TCHAR *kOrbitDegreesPerSecondOption = TEXT("--orbit-degrees-per-second=");
        float s_Rendering3DTestOrbitDegreesPerSecond = 0.0f;
        // --anti-aliasing=<taa|fxaa>: 起動画面のアンチエイリアシング（既定は taa）。
        constexpr const TCHAR *kAntiAliasingOption = TEXT("--anti-aliasing=");
        bool s_bRendering3DTestTemporalAA = true;
        // --render-scale=<0.5〜1>: 起動画面を内部解像度（画面解像度×倍率）で描いて拡大する（既定は1）。
        constexpr const TCHAR *kRenderScaleOption = TEXT("--render-scale=");
        float s_Rendering3DTestRenderScale = 1.0f;
        // --debug-draw-test-lines: 起動画面の大きな球を囲む箱をデバッグの線で毎フレーム描く（値を取らない）。
        // デバッグ描画が最終解像度でジッタ無しに描かれることを撮影で確かめるのに使う。
        constexpr const TCHAR *kDebugDrawTestLinesOption = TEXT("--debug-draw-test-lines");
        bool s_bRendering3DTestDebugDrawTestLines = false;
        // --startup-skinned-probe: 検証用の骨付きのパネルを地面の上へ 1 枚置く（値を取らない。既定は置かない）。
        // ビジビリティバッファの経路でスキニングの塊が描かれることの撮影（-SkinnedProbe）に使う。
        constexpr const TCHAR *kStartupSkinnedProbeOption = TEXT("--startup-skinned-probe");
        bool s_bRendering3DTestSkinnedProbe = false;
        // --startup-scan-props=on|off: 起動画面の地面の外周に並べる高ポリのスキャン資産（Poly Haven）を置くか（既定は on）。
        // off は、スキャン資産を足す前と同じ描画量を撮って、足した分を差し引くための基準に使う。
        constexpr const TCHAR *kStartupScanPropsOption = TEXT("--startup-scan-props=");
        bool s_bRendering3DTestScanProps = true;
        // --stress-mega-instances=<N>: 置いたスキャン資産のメッシュを N 個、地面の奥へ格子に複製して置く（0 は置かない）。
        // MegaGeometry のインスタンスを増やしたときの GPU・CPU の時間を測るための一時の負荷（ジオメトリの負荷モードの前段）。
        constexpr const TCHAR *kStressMegaInstancesOption = TEXT("--stress-mega-instances=");
        uint32_t s_Rendering3DTestStressMegaInstances = 0;
        // --stress-geometry[=<N>]: ジオメトリの負荷モード。スキャン資産・岩・小屋・大きな球を、変換を変えて地面の外側へ
        // N 個（既定 300）並べ、カメラの軸をその中心へ移す。ジオメトリのページのストリーミングと追い出しを、
        // 絞った --vram-budget-mb で確かめるのに使う。0 は無効。
        constexpr const TCHAR *kStressGeometryOption = TEXT("--stress-geometry");
        constexpr const TCHAR *kStressGeometryValueOption = TEXT("--stress-geometry=");
        constexpr uint32_t kStressGeometryDefaultCount = 300u;
        uint32_t s_Rendering3DTestStressGeometryCount = 0;
        // --capture-sequence=<接頭辞> と --capture-sequence-rendered-frames=<n1,n2,...>: 1回の起動の中で、
        // アセットが落ち着いてから n 枚目の描画フレームの最終出力を <接頭辞><n>.png に保存する。
        constexpr const TCHAR *kCaptureSequenceOption = TEXT("--capture-sequence=");
        constexpr const TCHAR *kCaptureSequenceRenderedFramesOption = TEXT("--capture-sequence-rendered-frames=");
        // --vram-budget-mb=<MB>: VRAM の上限（0 は上限なし）。RenderResources へ渡し、VRAM_BUDGET のログに出す。
        constexpr const TCHAR *kVramBudgetOption = TEXT("--vram-budget-mb=");
        uint32_t s_VramBudgetCapMb = 0;
        // --no-cooked-textures: クック済みのテクスチャ（build/CookedAssets/）を使わず、ばらの元画像を読む。
        constexpr const TCHAR *kNoCookedTexturesOption = TEXT("--no-cooked-textures");
        // --night: 起動画面を夜にする（空と空の太陽を消し、静的HDRの環境光を月明かり程度へ落とす。値を取らない）。
        constexpr const TCHAR *kNightOption = TEXT("--night");
        bool s_bRendering3DTestNight = false;
        // --debug-view=normal|clusters|lod|wireframe: 起動時のデバッグの表示（F3・F4・F5 で切り替えるものと同じ。既定は normal）。
        // MegaGeometry のクラスタの色・LOD の段・ワイヤーフレームを、--visibility-buffer の on と off で撮り比べる用。
        constexpr const TCHAR *kDebugViewOption = TEXT("--debug-view=");
        NorvesLib::Core::Rendering::DebugViewMode s_Rendering3DTestDebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::Normal;
        // --virtual-texture=on|off: 起動画面の材質のアルベド・法線・ORM・高さを VT（sparse）で描くか。既定は on（sparse に対応しない GPU は全常駐へ戻る）。
        // off は VT を使わず、段1の全常駐で描く（見た目・VRAM の比較用）。
        constexpr const TCHAR *kVirtualTextureOption = TEXT("--virtual-texture=");
        bool s_bRendering3DTestVirtualTexture = true;
        // --rendering3dtest-model-source=gltf のとき true（岩・小屋を glTF の実行時の経路で読む）。
        bool s_bRendering3DTestModelSourceGltf = false;
        // --rendering3dtest-big-sphere-source=runtime のとき true（大きな球を実行時に生成する）。
        bool s_bRendering3DTestBigSphereRuntime = false;
        // --mega-occlusion=on|off: MegaGeometry（岩・小屋など）の遮蔽カリング（2パス）を使うか。既定は on。
        // off は遮蔽の判定なしの従来の1回の判定で描く（見た目・描画数の比較用）。
        constexpr const TCHAR *kMegaOcclusionOption = TEXT("--mega-occlusion=");
        bool s_bMegaOcclusion = true;
        // --geometry-streaming=on|off: ページを持つメッシュ（NVMESH v1.1。岩・小屋・スキャン資産・大きな球）を、根のページだけ常駐させて
        // 残りを要求から読み込むか。既定は on。off は全てのページを常駐させる（見た目・VRAM の比較用）。
        constexpr const TCHAR *kGeometryStreamingOption = TEXT("--geometry-streaming=");
        bool s_bGeometryStreaming = true;
        // --stress-textures: テクスチャの負荷モード。起動画面の地面の外側へ、負荷用の材質（4K、24 種）を貼った板を格子に並べ、
        // カメラの軸を格子の中心へ移す。--vram-budget-mb と併せて、VT が目標の中で描けることを確かめる。
        constexpr const TCHAR *kStressTexturesOption = TEXT("--stress-textures");
        bool s_bRendering3DTestStressTextures = false;
        bool s_bRendering3DTestHasHeightFogDensity = false;
        float s_Rendering3DTestHeightFogDensity = 0.0f;
        bool s_bRendering3DTestHasHeightFogFalloff = false;
        float s_Rendering3DTestHeightFogFalloff = 0.0f;
        bool s_bRendering3DTestHasSunElevation = false;
        bool s_bRendering3DTestHasSunAzimuth = false;
        bool s_bRendering3DTestHasExposureEV100 = false;
        float s_Rendering3DTestSunElevation = 0.0f;
        float s_Rendering3DTestSunAzimuth = 0.0f;
        float s_Rendering3DTestExposureEV100 = 0.0f;

        /**
         * @brief 1つの有限の小数を読み、[minimum, maximum] に入っていれば成功とする。
         */
        bool TryParseBoundedFloat(const String &text, float minimum, float maximum, float &outValue)
        {
            char buffer[64] = {};
            if (text.empty() || text.size() >= sizeof(buffer))
            {
                return false;
            }
            for (size_t i = 0; i < text.size(); ++i)
            {
                const TCHAR ch = text[i];
                if (ch < 0x20 || ch > 0x7E)
                {
                    return false;
                }
                buffer[i] = static_cast<char>(ch);
            }

            char *pEnd = nullptr;
            const float value = std::strtof(buffer, &pEnd);
            if (pEnd == buffer || *pEnd != '\0' || !std::isfinite(value) || value < minimum || value > maximum)
            {
                return false;
            }
            outValue = value;
            return true;
        }

        /**
         * @brief "<yaw>,<pitch>,<arm>" を3つの有限の小数として読む。腕の長さは正でなければならない。
         */
        bool TryParseStartupCamera(const String &text, float (&outValues)[3])
        {
            char buffer[128] = {};
            if (text.empty() || text.size() >= sizeof(buffer))
            {
                return false;
            }
            for (size_t i = 0; i < text.size(); ++i)
            {
                const TCHAR ch = text[i];
                if (ch < 0x20 || ch > 0x7E)
                {
                    return false;
                }
                buffer[i] = static_cast<char>(ch);
            }

            const char *pCursor = buffer;
            for (uint32_t component = 0; component < 3u; ++component)
            {
                char *pEnd = nullptr;
                const float value = std::strtof(pCursor, &pEnd);
                if (pEnd == pCursor || !std::isfinite(value))
                {
                    return false;
                }
                outValues[component] = value;
                pCursor = pEnd;
                if (component < 2u)
                {
                    if (*pCursor != ',')
                    {
                        return false;
                    }
                    ++pCursor;
                }
            }
            return *pCursor == '\0' && outValues[2] > 0.0f;
        }

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

        /**
         * @brief argument が prefix で始まるとき、残りを outRest へ入れて true を返す。
         */
        bool TryStripPrefix(const String &argument, const TCHAR *prefix, String &outRest)
        {
            size_t prefixLength = 0;
            while (prefix[prefixLength] != 0)
            {
                if (prefixLength >= argument.size() || argument[prefixLength] != prefix[prefixLength])
                {
                    return false;
                }
                ++prefixLength;
            }
            outRest = argument.substr(prefixLength);
            return true;
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
        m_bAnimationDebugRequested = false;
        m_bDogMovementSmoke = false;
        m_StartupSceneId = {};
        m_StartupSceneView = {};
        m_SceneReportPath = {};
        m_CameraScriptPath = {};
        m_CameraPathTime = -1.0f;
        m_BananaCount = Game::GameModes::kDefaultBananaCount;
        m_bBananaCountSet = false;
        m_StressEntityCount = Game::GameModes::kDefaultStressEntityCount;
        m_bStressEntityCountSet = false;
        m_StressMovingPercent = 0.0f;
        m_bStressMovingPercentSet = false;
        m_StressMovingStartTime = 0.0f;
        s_bEngineChecksSelfTestFail = false;
        s_bEngineChecksSelfTestReported = false;
        s_bMegaSceneFullSync = false;
        m_StressStreamCellMeters = 0.0f;
        m_bStressStreamCellSet = false;
        m_StressStreamPeriod = Game::GameModes::kDefaultStressStreamPeriod;
        m_bOpenSceneInfo = false;
        m_DogDrive = NorvesLib::Modules::Physics::CharacterDriveMode::Fixed;
        bool bHasCharacterDrive = false;
        m_bRendering3DTestUseCookedModel = false;
        m_bNoCookedTextures = false;
        m_TextureLooseAssetRoot = {};
        m_bLauncherStartup = false;
        m_bPauseMenuWanted = false;
        m_bOpenPauseMenuAtStartup = false;
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
        s_bRendering3DTestHasStartupCamera = false;
        s_bRendering3DTestHasSunElevation = false;
        s_bRendering3DTestHasSunAzimuth = false;
        s_bRendering3DTestHasExposureEV100 = false;
        s_bRendering3DTestHasHeightFogDensity = false;
        s_bRendering3DTestHasHeightFogFalloff = false;
        s_Rendering3DTestOrbitDegreesPerSecond = 0.0f;
        s_bRendering3DTestTemporalAA = true;
        s_Rendering3DTestRenderScale = 1.0f;
        s_VramBudgetCapMb = 0;
        s_bRendering3DTestDebugDrawTestLines = false;
        s_bRendering3DTestSkinnedProbe = false;
        s_bRendering3DTestScanProps = true;
        s_Rendering3DTestStressMegaInstances = 0;
        s_Rendering3DTestStressGeometryCount = 0;
        s_bRendering3DTestNight = false;
        s_Rendering3DTestDebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::Normal;
        s_bRendering3DTestVirtualTexture = true;
        s_bRendering3DTestModelSourceGltf = false;
        s_bRendering3DTestBigSphereRuntime = false;
        s_bMegaOcclusion = true;
        s_bGeometryStreaming = true;
        String captureSequencePrefix;
        VariableArray<uint64_t> captureSequenceRenderedFrames;
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

            String startupCameraValue;
            if (TryStripPrefix(args[i], kStartupCameraOption, startupCameraValue))
            {
                if (!TryParseStartupCamera(startupCameraValue, s_Rendering3DTestStartupCamera))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: --startup-camera は <yaw>,<pitch>,<arm>（arm > 0）で指定する");
                    return false;
                }
                s_bRendering3DTestHasStartupCamera = true;
                LOG_INFO("Rendering3DTest startup camera yaw=%g pitch=%g arm=%g",
                         static_cast<double>(s_Rendering3DTestStartupCamera[0]),
                         static_cast<double>(s_Rendering3DTestStartupCamera[1]),
                         static_cast<double>(s_Rendering3DTestStartupCamera[2]));
                continue;
            }

            String sunElevationValue;
            if (TryStripPrefix(args[i], kSunElevationOption, sunElevationValue))
            {
                if (!TryParseBoundedFloat(sunElevationValue, 0.0f, 90.0f, s_Rendering3DTestSunElevation))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: --sun-elevation は 0〜90 の度で指定する");
                    return false;
                }
                s_bRendering3DTestHasSunElevation = true;
                continue;
            }

            String sunAzimuthValue;
            if (TryStripPrefix(args[i], kSunAzimuthOption, sunAzimuthValue))
            {
                if (!TryParseBoundedFloat(sunAzimuthValue, -180.0f, 180.0f, s_Rendering3DTestSunAzimuth))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: --sun-azimuth は -180〜180 の度で指定する");
                    return false;
                }
                s_bRendering3DTestHasSunAzimuth = true;
                continue;
            }

            String exposureValue;
            if (TryStripPrefix(args[i], kExposureEV100Option, exposureValue))
            {
                if (!TryParseBoundedFloat(exposureValue, -6.0f, 24.0f, s_Rendering3DTestExposureEV100))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: --exposure-ev100 は -6〜24 で指定する");
                    return false;
                }
                s_bRendering3DTestHasExposureEV100 = true;
                continue;
            }

            String orbitValue;
            if (TryStripPrefix(args[i], kOrbitDegreesPerSecondOption, orbitValue))
            {
                if (!TryParseBoundedFloat(orbitValue, -360.0f, 360.0f, s_Rendering3DTestOrbitDegreesPerSecond))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: --orbit-degrees-per-second は -360〜360 の度/秒で指定する");
                    return false;
                }
                continue;
            }

            String antiAliasingValue;
            if (TryStripPrefix(args[i], kAntiAliasingOption, antiAliasingValue))
            {
                if (antiAliasingValue == String(TEXT("taa")))
                {
                    s_bRendering3DTestTemporalAA = true;
                }
                else if (antiAliasingValue == String(TEXT("fxaa")))
                {
                    s_bRendering3DTestTemporalAA = false;
                }
                else
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: --anti-aliasing は taa か fxaa で指定する");
                    return false;
                }
                continue;
            }

            String heightFogDensityValue;
            if (TryStripPrefix(args[i], kHeightFogDensityOption, heightFogDensityValue))
            {
                if (!TryParseBoundedFloat(heightFogDensityValue, 0.0f, 1.0f, s_Rendering3DTestHeightFogDensity))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: --height-fog-density は 0〜1 で指定する");
                    return false;
                }
                s_bRendering3DTestHasHeightFogDensity = true;
                continue;
            }

            String heightFogFalloffValue;
            if (TryStripPrefix(args[i], kHeightFogFalloffOption, heightFogFalloffValue))
            {
                if (!TryParseBoundedFloat(heightFogFalloffValue, 0.0f, 1.0f, s_Rendering3DTestHeightFogFalloff))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: "
                              "--height-fog-falloff は 0〜1 で指定する");
                    return false;
                }
                s_bRendering3DTestHasHeightFogFalloff = true;
                continue;
            }

            if (args[i] == kNightOption)
            {
                s_bRendering3DTestNight = true;
                continue;
            }

            if (args[i] == kStressTexturesOption)
            {
                s_bRendering3DTestStressTextures = true;
                continue;
            }

            String debugViewValue;
            if (TryStripPrefix(args[i], kDebugViewOption, debugViewValue))
            {
                if (debugViewValue == String(TEXT("normal")))
                {
                    s_Rendering3DTestDebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::Normal;
                }
                else if (debugViewValue == String(TEXT("clusters")))
                {
                    s_Rendering3DTestDebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::MegaGeometryClusters;
                }
                else if (debugViewValue == String(TEXT("lod")))
                {
                    s_Rendering3DTestDebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::LODLevel;
                }
                else if (debugViewValue == String(TEXT("wireframe")))
                {
                    s_Rendering3DTestDebugViewMode = NorvesLib::Core::Rendering::DebugViewMode::Wireframe;
                }
                else
                {
                    LOG_ERROR("Rendering3DTest の引数の解析に失敗: --debug-view は "
                              "normal・clusters・lod・wireframe のどれかで指定する");
                    return false;
                }
                continue;
            }

            String virtualTextureValue;
            if (TryStripPrefix(args[i], kVirtualTextureOption, virtualTextureValue))
            {
                if (virtualTextureValue == String(TEXT("on")))
                {
                    s_bRendering3DTestVirtualTexture = true;
                }
                else if (virtualTextureValue == String(TEXT("off")))
                {
                    s_bRendering3DTestVirtualTexture = false;
                }
                else
                {
                    LOG_ERROR("Rendering3DTest の引数の解析に失敗: "
                              "--virtual-texture は on か off で指定する");
                    return false;
                }
                continue;
            }

            String modelSourceValue;
            if (TryStripPrefix(args[i], kRendering3DTestModelSourceOption, modelSourceValue))
            {
                if (modelSourceValue == String(TEXT("cooked")))
                {
                    s_bRendering3DTestModelSourceGltf = false;
                }
                else if (modelSourceValue == String(TEXT("gltf")))
                {
                    s_bRendering3DTestModelSourceGltf = true;
                }
                else
                {
                    LOG_ERROR("Rendering3DTest の引数の解析に失敗: "
                              "--rendering3dtest-model-source は cooked か gltf "
                              "で指定する");
                    return false;
                }
                continue;
            }

            String bigSphereSourceValue;
            if (TryStripPrefix(args[i], kRendering3DTestBigSphereSourceOption, bigSphereSourceValue))
            {
                if (bigSphereSourceValue == String(TEXT("cooked")))
                {
                    s_bRendering3DTestBigSphereRuntime = false;
                }
                else if (bigSphereSourceValue == String(TEXT("runtime")))
                {
                    s_bRendering3DTestBigSphereRuntime = true;
                }
                else
                {
                    LOG_ERROR("Rendering3DTest の引数の解析に失敗: "
                              "--rendering3dtest-big-sphere-source は cooked か "
                              "runtime で指定する");
                    return false;
                }
                continue;
            }

            String megaOcclusionValue;
            if (TryStripPrefix(args[i], kMegaOcclusionOption, megaOcclusionValue))
            {
                if (megaOcclusionValue == String(TEXT("on")))
                {
                    s_bMegaOcclusion = true;
                }
                else if (megaOcclusionValue == String(TEXT("off")))
                {
                    s_bMegaOcclusion = false;
                }
                else
                {
                    LOG_ERROR("Game command line parse failed: --mega-occlusion は "
                              "on か off で指定する");
                    return false;
                }
                continue;
            }

            String geometryStreamingValue;
            if (TryStripPrefix(args[i], kGeometryStreamingOption, geometryStreamingValue))
            {
                if (geometryStreamingValue == String(TEXT("on")))
                {
                    s_bGeometryStreaming = true;
                }
                else if (geometryStreamingValue == String(TEXT("off")))
                {
                    s_bGeometryStreaming = false;
                }
                else
                {
                    LOG_ERROR("Game command line parse failed: "
                              "--geometry-streaming は on か off で指定する");
                    return false;
                }
                continue;
            }

            String renderScaleValue;
            if (TryStripPrefix(args[i], kRenderScaleOption, renderScaleValue))
            {
                if (!TryParseBoundedFloat(renderScaleValue, 0.5f, 1.0f, s_Rendering3DTestRenderScale))
                {
                    LOG_ERROR("Rendering3DTest command line parse failed: "
                              "--render-scale は 0.5〜1 で指定する");
                    return false;
                }
                continue;
            }

            String vramBudgetValue;
            if (TryStripPrefix(args[i], kVramBudgetOption, vramBudgetValue))
            {
                if (!TryParseUInt32(vramBudgetValue, s_VramBudgetCapMb))
                {
                    LOG_ERROR("Game command line parse failed: --vram-budget-mb は "
                              "0 以上の整数（MB）で指定する");
                    return false;
                }
                continue;
            }

            if (args[i] == kNoCookedTexturesOption)
            {
                m_bNoCookedTextures = true;
                continue;
            }

            if (args[i] == TEXT("--dog-movement-smoke"))
            {
                if (m_bDogMovementSmoke)
                {
                    LOG_ERROR("--dog-movement-smoke が重複しています");
                    return false;
                }
                m_bDogMovementSmoke = true;
                continue;
            }
            String sceneValue;
            if (TryStripPrefix(args[i], kSceneOption, sceneValue))
            {
                if (!m_StartupSceneId.empty())
                {
                    LOG_ERROR("--scene が重複しています");
                    return false;
                }
                if (sceneValue.empty())
                {
                    LOG_ERROR("--scene にはシーンの id を指定してください");
                    return false;
                }
                m_StartupSceneId = sceneValue;
                continue;
            }
            if (TryStripPrefix(args[i], kSceneViewOption, sceneValue))
            {
                if (!m_StartupSceneView.empty())
                {
                    LOG_ERROR("--scene-view が重複しています");
                    return false;
                }
                if (sceneValue.empty())
                {
                    LOG_ERROR("--scene-view には視点の名前を指定してください");
                    return false;
                }
                m_StartupSceneView = sceneValue;
                continue;
            }
            if (TryStripPrefix(args[i], kSceneReportOption, sceneValue))
            {
                if (!m_SceneReportPath.empty())
                {
                    LOG_ERROR("--scene-report が重複しています");
                    return false;
                }
                if (sceneValue.empty())
                {
                    LOG_ERROR("--scene-report には出力先のパスを指定してください");
                    return false;
                }
                m_SceneReportPath = sceneValue;
                continue;
            }
            if (TryStripPrefix(args[i], kCameraPathTimeOption, sceneValue))
            {
                float pathTime = 0.0f;
                if (m_CameraPathTime >= 0.0f)
                {
                    LOG_ERROR("--camera-path-time が重複しています");
                    return false;
                }
                if (!TryParseBoundedFloat(sceneValue, 0.0f, 100000.0f, pathTime))
                {
                    LOG_ERROR("--camera-path-time は 0 以上の秒数で指定してください");
                    return false;
                }
                m_CameraPathTime = pathTime;
                continue;
            }
            if (TryStripPrefix(args[i], kBananaCountOption, sceneValue))
            {
                uint32_t bananaCount = 0;
                if (m_bBananaCountSet)
                {
                    LOG_ERROR("--banana-count が重複しています");
                    return false;
                }
                if (!TryParseUInt32(sceneValue, bananaCount) || bananaCount == 0 ||
                    bananaCount > Game::GameModes::kMaxBananaCount)
                {
                    LOG_ERROR_F("--banana-count は 1〜%u の整数で指定してください", Game::GameModes::kMaxBananaCount);
                    return false;
                }
                m_BananaCount = bananaCount;
                m_bBananaCountSet = true;
                continue;
            }
            if (TryStripPrefix(args[i], kStressMovingPercentOption, sceneValue))
            {
                float movingPercent = 0.0f;
                if (m_bStressMovingPercentSet)
                {
                    LOG_ERROR("--stress-moving-percent が重複しています");
                    return false;
                }
                if (!TryParseBoundedFloat(sceneValue, 0.0f, 100.0f, movingPercent))
                {
                    LOG_ERROR("--stress-moving-percent は 0〜100 の数で指定してください");
                    return false;
                }
                m_StressMovingPercent = movingPercent;
                m_bStressMovingPercentSet = true;
                continue;
            }
            if (TryStripPrefix(args[i], kStressMovingTimeOption, sceneValue))
            {
                float movingStartTime = 0.0f;
                if (!TryParseBoundedFloat(sceneValue, 0.0f, 1.0e6f, movingStartTime))
                {
                    LOG_ERROR("--stress-moving-time は 0〜1000000 の秒数で指定してください");
                    return false;
                }
                m_StressMovingStartTime = movingStartTime;
                continue;
            }
            if (TryStripPrefix(args[i], kStressStreamCellOption, sceneValue))
            {
                float streamCell = 0.0f;
                if (m_bStressStreamCellSet)
                {
                    LOG_ERROR("--stress-stream-cell が重複しています");
                    return false;
                }
                if (!TryParseBoundedFloat(sceneValue, Game::GameModes::kMinStressStreamCellMeters,
                                          Game::GameModes::kMaxStressStreamCellMeters, streamCell))
                {
                    LOG_ERROR_F("--stress-stream-cell は %.0f〜%.0f の数（m）で指定してください",
                                static_cast<double>(Game::GameModes::kMinStressStreamCellMeters),
                                static_cast<double>(Game::GameModes::kMaxStressStreamCellMeters));
                    return false;
                }
                m_StressStreamCellMeters = streamCell;
                m_bStressStreamCellSet = true;
                continue;
            }
            if (TryStripPrefix(args[i], kStressStreamPeriodOption, sceneValue))
            {
                float streamPeriod = 0.0f;
                if (!TryParseBoundedFloat(sceneValue, Game::GameModes::kMinStressStreamPeriod, 3600.0f, streamPeriod))
                {
                    LOG_ERROR_F("--stress-stream-period は %.2f〜3600 の秒数で指定してください",
                                static_cast<double>(Game::GameModes::kMinStressStreamPeriod));
                    return false;
                }
                m_StressStreamPeriod = streamPeriod;
                continue;
            }
            if (TryStripPrefix(args[i], kStressEntitiesOption, sceneValue))
            {
                uint32_t stressEntityCount = 0;
                if (m_bStressEntityCountSet)
                {
                    LOG_ERROR("--stress-entities が重複しています");
                    return false;
                }
                if (!TryParseUInt32(sceneValue, stressEntityCount) || stressEntityCount == 0 ||
                    stressEntityCount > Game::GameModes::kMaxStressEntityCount)
                {
                    LOG_ERROR_F("--stress-entities は 1〜%u の整数で指定してください", Game::GameModes::kMaxStressEntityCount);
                    return false;
                }
                m_StressEntityCount = stressEntityCount;
                m_bStressEntityCountSet = true;
#if NORVES_BUILD_DEVELOPMENT
                // MegaGeometry の区間ごとの描画コマンドの上限を 100 万に抑える（環境変数が無いときだけ）。既定の 419 万のままだと、
                // メッシュの種類 × Entity が増えたとき（約 3000 個から）コマンドのバッファの容量が 2^26 件（1.3 GB）へ跳ねて、
                // 何も描かれなくなる。MegaGeometryPass が最初に作られる前に環境変数へ渡す。
                {
                    char *existingMaxDraw = nullptr;
                    size_t existingLength = 0;
                    _dupenv_s(&existingMaxDraw, &existingLength, "NORVES_MEGA_MAX_DRAW_COUNT");
                    if (existingMaxDraw == nullptr)
                    {
                        _putenv_s("NORVES_MEGA_MAX_DRAW_COUNT", "1000000");
                    }
                    std::free(existingMaxDraw);
                }
#endif
                continue;
            }
#if NORVES_BUILD_DEVELOPMENT
            if (TryStripPrefix(args[i], kMegaLodErrorPxOption, sceneValue))
            {
                // 画素の閾値は正の有限な数。MegaGeometryPass が最初に作られる前に環境変数へ渡す。
                float threshold = 0.0f;
                if (!TryParseBoundedFloat(sceneValue, 1.0e-6f, 1.0e9f, threshold))
                {
                    LOG_ERROR("--mega-lod-error-px は 0 より大きい数で指定してください");
                    return false;
                }
                char thresholdText[32] = {};
                std::snprintf(thresholdText, sizeof(thresholdText), "%.9g", static_cast<double>(threshold));
                _putenv_s("NORVES_MEGA_LOD_ERROR_PX", thresholdText);
                continue;
            }
#endif
            if (TryStripPrefix(args[i], kEngineChecksOption, sceneValue))
            {
#if NORVES_BUILD_DEVELOPMENT
                auto *checksEngine = NorvesLib::Core::Engine::GEngine;
                if (checksEngine == nullptr || !checksEngine->GetEngineChecks().EnableList(sceneValue.c_str()))
                {
                    LOG_ERROR("--engine-checks は大文字・数字・下線の名前をカンマで区切って指定してください");
                    return false;
                }
#else
                LOG_WARNING("--engine-checks は開発ビルドだけで使えます。無視します");
#endif
                continue;
            }
            if (args[i] == kEngineChecksSelfTestFailOption)
            {
                s_bEngineChecksSelfTestFail = true;
                continue;
            }
            if (args[i] == kMegaSceneFullSyncOption)
            {
                s_bMegaSceneFullSync = true;
                continue;
            }
            if (TryStripPrefix(args[i], kCameraScriptOption, sceneValue))
            {
                if (!m_CameraScriptPath.empty())
                {
                    LOG_ERROR("--camera-script が重複しています");
                    return false;
                }
                if (sceneValue.empty())
                {
                    LOG_ERROR("--camera-script には台本のパスを指定してください");
                    return false;
                }
#if NORVES_BUILD_DEVELOPMENT
                m_CameraScriptPath = sceneValue;
#if defined(NORVES_SOURCE_ASSET_DIR)
                {
                    // 作業ディレクトリは実行ファイルのディレクトリなので、相対パスで見つからなければリポジトリのルートを基準に探す。
                    std::error_code existsError;
                    const std::filesystem::path given(sceneValue.c_str());
                    if (given.is_relative() && !std::filesystem::exists(given, existsError))
                    {
                        const std::filesystem::path fromRepo =
                            std::filesystem::path(NORVES_SOURCE_ASSET_DIR).parent_path() / given;
                        if (std::filesystem::exists(fromRepo, existsError))
                        {
                            m_CameraScriptPath = String(fromRepo.string().c_str());
                        }
                    }
                }
#endif
#else
                LOG_WARNING("--camera-script は開発ビルドだけで使えます。無視します");
#endif
                continue;
            }
            if (TryStripPrefix(args[i], kSceneCycleSmokeOption, sceneValue))
            {
                if (m_SceneCycle.IsEnabled())
                {
                    LOG_ERROR("--scene-cycle-smoke が重複しています");
                    return false;
                }
                if (!m_SceneCycle.ParseScenes(sceneValue.c_str()))
                {
                    return false;
                }
                continue;
            }
            if (TryStripPrefix(args[i], kSceneCycleCountOption, sceneValue))
            {
                uint32_t cycleCount = 0;
                if (!TryParseUInt32(sceneValue, cycleCount) || cycleCount == 0 || cycleCount > 1000)
                {
                    LOG_ERROR("--scene-cycle-count には 1〜1000 の整数を指定してください");
                    return false;
                }
                m_SceneCycle.SetCycleCount(cycleCount);
                continue;
            }
            const auto driveArgument = Gameplay::ParseCharacterDriveArgument(args[i].c_str(), m_DogDrive);
            if (driveArgument != Gameplay::CharacterDriveArgumentResult::Unrecognized)
            {
                if (driveArgument == Gameplay::CharacterDriveArgumentResult::Invalid || bHasCharacterDrive)
                {
                    LOG_ERROR("--character-drive は fixed または variable を1回だけ指定してください");
                    return false;
                }
                bHasCharacterDrive = true;
                continue;
            }
            if (args[i] == TEXT("--animation-debug"))
            {
                m_bAnimationDebugRequested = true;
                continue;
            }
            if (args[i] == kStartupSkinnedProbeOption)
            {
                s_bRendering3DTestSkinnedProbe = true;
                continue;
            }
            if (args[i] == kDebugDrawTestLinesOption)
            {
                s_bRendering3DTestDebugDrawTestLines = true;
                continue;
            }

            if (args[i] == kStressGeometryOption)
            {
                s_Rendering3DTestStressGeometryCount = kStressGeometryDefaultCount;
                continue;
            }

            String stressGeometryValue;
            if (TryStripPrefix(args[i], kStressGeometryValueOption, stressGeometryValue))
            {
                if (!TryParseUInt32(stressGeometryValue, s_Rendering3DTestStressGeometryCount) ||
                    s_Rendering3DTestStressGeometryCount == 0u || s_Rendering3DTestStressGeometryCount > 4096u)
                {
                    LOG_ERROR("Rendering3DTest の引数の解析に失敗: --stress-geometry "
                              "は個数を付けるなら 1〜4096 の整数で指定する");
                    return false;
                }
                continue;
            }

            String stressMegaInstancesValue;
            if (TryStripPrefix(args[i], kStressMegaInstancesOption, stressMegaInstancesValue))
            {
                if (!TryParseUInt32(stressMegaInstancesValue, s_Rendering3DTestStressMegaInstances) ||
                    s_Rendering3DTestStressMegaInstances > 4096u)
                {
                    LOG_ERROR("Rendering3DTest の引数の解析に失敗: "
                              "--stress-mega-instances は 0〜4096 の整数で指定する");
                    return false;
                }
                continue;
            }

            String startupScanPropsValue;
            if (TryStripPrefix(args[i], kStartupScanPropsOption, startupScanPropsValue))
            {
                if (startupScanPropsValue == String(TEXT("on")))
                {
                    s_bRendering3DTestScanProps = true;
                }
                else if (startupScanPropsValue == String(TEXT("off")))
                {
                    s_bRendering3DTestScanProps = false;
                }
                else
                {
                    LOG_ERROR("Rendering3DTest の引数の解析に失敗: "
                              "--startup-scan-props は on か off で指定する");
                    return false;
                }
                continue;
            }

            String captureSequenceRenderedFramesValue;
            if (TryStripPrefix(args[i], kCaptureSequenceRenderedFramesOption, captureSequenceRenderedFramesValue))
            {
                // 「60,75,90」の形。各値は1以上の描画フレーム数。
                captureSequenceRenderedFrames.clear();
                size_t begin = 0;
                while (begin <= captureSequenceRenderedFramesValue.size())
                {
                    size_t end = begin;
                    while (end < captureSequenceRenderedFramesValue.size() &&
                           captureSequenceRenderedFramesValue[end] != TEXT(','))
                    {
                        ++end;
                    }
                    uint32_t frames = 0;
                    if (!TryParseUInt32(captureSequenceRenderedFramesValue.substr(begin, end - begin), frames) ||
                        frames == 0)
                    {
                        LOG_ERROR("Game command line parse failed: "
                                  "--capture-sequence-rendered-frames は 1 "
                                  "以上の整数をカンマで区切って指定する");
                        return false;
                    }
                    captureSequenceRenderedFrames.push_back(frames);
                    begin = end + 1;
                }
                continue;
            }

            String captureSequenceValue;
            if (TryStripPrefix(args[i], kCaptureSequenceOption, captureSequenceValue))
            {
                if (captureSequenceValue.empty())
                {
                    LOG_ERROR("Game command line parse failed: --capture-sequence "
                              "の接頭辞が空");
                    return false;
                }
                captureSequencePrefix = captureSequenceValue;
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
        if (!bHasRoot && bHasManifest)
        {
            LOG_ERROR("テクスチャ資産の引数を解析できません: --texture-asset-manifest "
                      "には --texture-asset-root の指定も必要です");
            return false;
        }

        if (bHasRoot && !bHasManifest)
        {
            // root だけの指定は、クック済みの置き場の差し替えとして扱う（マニフェストは <root>/manifest.json）。
            // 既定と同じく、足りない項目はばらの元画像で読み、クック済みの色は元画像と同じ標本値で描く。
            String rootPrefix = m_TextureAssetRoot;
            while (!rootPrefix.empty() &&
                   (rootPrefix[rootPrefix.size() - 1] == '/' || rootPrefix[rootPrefix.size() - 1] == '\\'))
            {
                rootPrefix = rootPrefix.substr(0, rootPrefix.size() - 1);
            }
            m_TextureAssetManifestPath = rootPrefix + "/manifest.json";
#if defined(NORVES_SOURCE_ASSET_DIR)
            if (!m_bNoCookedTextures)
            {
                m_TextureLooseAssetRoot = String(NORVES_SOURCE_ASSET_DIR);
            }
#endif
        }

        m_bHasTextureAssetRuntimeConfig = !m_TextureAssetRoot.empty() && !m_TextureAssetManifestPath.empty();
        if (m_bRendering3DTestUseCookedModel &&
            (!m_bHasTextureAssetRuntimeConfig || m_Rendering3DTestModelPath.empty()))
        {
            LOG_ERROR("Rendering3DTest command line parse failed: --rendering3dtest-use-cooked-model requires --texture-asset-root, --texture-asset-manifest, and --rendering3dtest-model");
            return false;
        }
#if defined(NORVES_COOKED_ASSET_DIR) && defined(NORVES_SOURCE_ASSET_DIR)
        if (!m_bHasTextureAssetRuntimeConfig && !m_bNoCookedTextures)
        {
            // 既定は build/CookedAssets/（CookAssets 対象が焼く）。マニフェストが無ければ従来どおりばらの元画像を読む。
            // マニフェストにない項目は Assets/ のばらの元画像を無圧縮で読む。
            const String cookedRoot(NORVES_COOKED_ASSET_DIR);
            const String cookedManifest = cookedRoot + "/manifest.json";
            std::error_code manifestError;
            if (std::filesystem::is_regular_file(ToFilesystemPath(cookedManifest), manifestError) && !manifestError)
            {
                m_TextureAssetRoot = cookedRoot;
                m_TextureAssetManifestPath = cookedManifest;
                m_TextureLooseAssetRoot = String(NORVES_SOURCE_ASSET_DIR);
                m_bHasTextureAssetRuntimeConfig = true;
            }
            else
            {
                LOG_WARNING_F("COOKED_ASSETS_MISSING manifest=\"%s\" "
                              "ばらの元画像を無圧縮で読みます"
                              "（ビルド対象 CookAssets でクック済みを作れます）",
                              cookedManifest.c_str());
                // 読む各パスに TEXTURE_COOKED_MISSING を出すため、ばらの元画像の root を持ち、起動後に入れ直す。
                m_TextureLooseAssetRoot = String(NORVES_SOURCE_ASSET_DIR);
                m_bCookedManifestUnavailable = true;
            }
        }
#endif
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
        if (!ResolveStartupScene())
        {
            return false;
        }
        if ((bHasCharacterDrive && !m_bDogMovementSmoke) || (m_bDogMovementSmoke && m_bAnimationDebugRequested))
        {
            LOG_ERROR("犬比較用引数は --dog-movement-smoke と組み合わせ、--animation-debug と併用しないでください");
            return false;
        }
#if !defined(NORVES_ENABLE_IMGUI)
        if (m_bAnimationDebugRequested)
        {
            LOG_ERROR("--animation-debug は NORVES_ENABLE_IMGUI=ON のビルドが必要です");
            return false;
        }
#endif
        bool bImGui = m_bAnimationDebugRequested;
        bool bAutomatedRun = !captureSequencePrefix.empty();
        bool bOpenPauseMenu = false;
        bool bOpenSceneInfo = false;
        for (size_t i = 0; i < args.size(); ++i)
        {
            String ignoredValue;
            if (ToStdString(args[i]) == std::basic_string<TCHAR>(kImGuiOption))
            {
                bImGui = true;
            }
            else if (ToStdString(args[i]) == std::basic_string<TCHAR>(kOpenPauseMenuOption))
            {
                bOpenPauseMenu = true;
            }
            else if (ToStdString(args[i]) == std::basic_string<TCHAR>(kOpenSceneInfoOption))
            {
                bOpenSceneInfo = true;
            }
            else if (TryStripPrefix(args[i], kCapturePngOption, ignoredValue) ||
                     TryStripPrefix(args[i], kExitAfterFramesOption, ignoredValue))
            {
                bAutomatedRun = true;
            }
        }
#if !NORVES_BUILD_DEVELOPMENT
        if (bOpenPauseMenu)
        {
            LOG_WARNING("起動オプション --open-pause-menu は開発ビルドでのみ有効なので無視しました");
            bOpenPauseMenu = false;
        }
        if (bOpenSceneInfo)
        {
            LOG_WARNING("起動オプション --open-scene-info は開発ビルドでのみ有効なので無視しました");
            bOpenSceneInfo = false;
        }
#endif
        m_bOpenSceneInfo = bOpenSceneInfo;
        // ImGui モジュールは、デバッグ用の窓（--imgui）のほかに、ランチャーのメニューのためにも登録する。
        // デバッグ用の窓（EngineStats など）は m_bImGuiRequested のときだけ出す。
#if defined(NORVES_ENABLE_IMGUI)
        // ポーズメニュー（Esc）のためにも登録する。撮影・自動実行の起動は、描画やキー割り当てを変えないよう使わない。
        m_bPauseMenuWanted = !bAutomatedRun || bOpenPauseMenu;
        m_bOpenPauseMenuAtStartup = bOpenPauseMenu;
        if (bImGui || m_bLauncherStartup || m_bPauseMenuWanted || bOpenSceneInfo)
        {
            NorvesLib::Core::Module::RegisterImGuiModule(
                NorvesLib::Core::Module::GetModuleRegistry());
            // バナナのシーンの情報表示（H キー）の文字。シーンは ImGui モジュールがあるときだけ表示を登録する
            Game::GameModes::RegisterBananasInfoDisplayText();
            if (m_bLauncherStartup)
            {
                // ランチャーの文字を表示用フォントへ載せる。ImGui モジュールの Initialize より前に渡す。
                Game::GameModes::RegisterLauncherDisplayText();
            }
            if (m_bPauseMenuWanted)
            {
                Game::Pause::RegisterPauseMenuDisplayText();
            }
            LOG_INFO_F("ImGuiModule registered debug=%d launcher=%d pause=%d", bImGui ? 1 : 0,
                       m_bLauncherStartup ? 1 : 0, m_bPauseMenuWanted ? 1 : 0);
        }
#else
        if (bImGui)
        {
            LOG_WARNING("Module runtime option --imgui ignored: build without NORVES_ENABLE_IMGUI");
        }
#endif
#if defined(NORVES_ENABLE_IMGUI)
        m_bImGuiRequested = bImGui;
#else
        m_bPauseMenuWanted = false;
        m_bOpenPauseMenuAtStartup = false;
#endif

        if (captureSequencePrefix.empty() != captureSequenceRenderedFrames.empty())
        {
            LOG_ERROR("Game command line parse failed: --capture-sequence と "
                      "--capture-sequence-rendered-frames は併せて指定する");
            return false;
        }
        m_SequenceFrameCapture.Configure(captureSequencePrefix, captureSequenceRenderedFrames);
        if (m_SequenceFrameCapture.IsEnabled())
        {
            LOG_INFO_F("SEQUENCE_CAPTURE configured frames=%zu", captureSequenceRenderedFrames.size());
        }
        return true;
    }

    bool GameApplicationHandler::OnInitialize()
    {
        LOG_INFO("GameApplicationHandler::OnInitialize()");

        if (auto* engine = NorvesLib::Core::Engine::GEngine)
        {
            NorvesLib::Core::Container::String hapticsError;
            if (!Game::Input::InitializeGameHaptics(engine->GetHapticsService(), hapticsError))
            {
                LOG_WARNING_F("振動効果を適用できません。既存設定で続行します: %s",
                    hapticsError.empty() ? "" : hapticsError.c_str());
            }
            if (!m_InputSettings.Initialize(engine->GetInputMapper()))
            {
                const auto& error = m_InputSettings.GetLastError();
                LOG_WARNING_F("入力設定を適用できません。既存camera操作で続行します: %s", error.empty() ? "" : error.c_str());
            }
            else
            {
                const auto& configuration = m_InputSettings.GetConfiguration();
                if (configuration.UserStatus == NorvesLib::Core::Input::EInputBindingUserStatus::Invalid)
                {
                    const auto& error = configuration.UserReport.Error;
                    LOG_WARNING_F("user入力設定が不正なため既定を使います: %s", error.empty() ? "" : error.c_str());
                }
                else if (configuration.UserStatus == NorvesLib::Core::Input::EInputBindingUserStatus::ReadError)
                {
                    const auto& error = configuration.StoreError;
                    LOG_WARNING_F("user入力設定を読めないため既定を使います: %s", error.empty() ? "" : error.c_str());
                }
            }
        }

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

        // --vram-budget-mb の上限を RenderResources へ渡す（0 は上限なし）。
        if (NorvesLib::Core::Engine::GEngine)
        {
            NorvesLib::Core::Engine::GEngine->GetRenderResources().SetVideoMemoryCapMb(s_VramBudgetCapMb);
            // --mega-occlusion の指定（既定は有効）を MegaGeometry へ渡す
            NorvesLib::Core::Engine::GEngine->GetRenderResources().MegaGeometry().SetOcclusionCullingEnabled(s_bMegaOcclusion);
            // --geometry-streaming の指定（既定は有効）。メッシュを作る前に決める
            NorvesLib::Core::Engine::GEngine->GetRenderResources().MegaGeometry().SetPageStreamingEnabled(s_bGeometryStreaming);

            // --mega-scene-full-sync: MegaGeometry の同期を全量へ戻す（最初のフレームの前に、メインの SceneView へ渡す）
            if (s_bMegaSceneFullSync)
            {
                auto mainSceneView = NorvesLib::Core::Engine::GEngine->GetRenderWorld().GetRenderingCoordinator().GetMainSceneView();
                if (mainSceneView)
                {
                    mainSceneView->SetMegaFullSync(true);
                    LOG_INFO("MegaGeometry の同期を全量にしました (--mega-scene-full-sync)");
                }
                else
                {
                    LOG_ERROR("--mega-scene-full-sync: メインの SceneView が無いため、全量の同期にできません");
                }
            }
        }

        if (m_bHasTextureAssetRuntimeConfig && !ReloadConfiguredAssetManifest())
        {
            if (!m_TextureLooseAssetRoot.empty())
            {
                // 既定のクック済みの設定が読めないときは、終了せずばらの元画像で続ける。
                LOG_WARNING("COOKED_ASSETS_UNUSABLE "
                            "クック済みのマニフェストを読めないため、ばらの元画像を"
                            "無圧縮で読みます");
                m_bCookedManifestUnavailable = true;
            }
            else
        {
            if (NorvesLib::Core::Engine::GEngine)
            {
                NorvesLib::Core::Engine::GEngine->RequestExit(1);
            }
            return;
            }
        }

        if (m_bCookedManifestUnavailable)
        {
            InstallCookedManifestUnavailableAssetSystem();
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

        // ImGui モジュールは入力ルーターの最上位で F1 を予約し、解除中は下位の入力を止める（--imgui のデバッグ用）。
        // ランチャーやポーズメニューのためだけに登録したときは、この予約を外す。
        // 外さないと F1 が Rendering3DTest のデバッグ表示へ届かず、続く Esc もポーズメニューへ届かない。
        if (!m_bImGuiRequested && NorvesLib::Core::Engine::GEngine != nullptr)
        {
            NorvesLib::Core::Engine::GEngine->GetInputDebugOverlay().SetEnabled(false);
        }

        // ポーズメニュー。ImGui モジュールが無い（登録に失敗した）ときは使わず、Esc は従来どおり何もしない。
        if (m_bPauseMenuWanted && NorvesLib::Core::Engine::GEngine != nullptr &&
            Module::GetModuleRegistry().FindModule(Identity("NorvesImGuiModule")) != nullptr)
        {
            NorvesLib::Core::Engine::GEngine->GetInputRouter().RegisterController(&m_PauseMenu,
                                                                                  kPauseMenuInputPriority);
            NorvesLib::Modules::Gui::RegisterImGuiView(&m_PauseMenuView);
            m_PauseMenu.SetEnabled(true);
            m_bPauseMenuRegistered = true;
            LOG_INFO("ポーズメニューを登録しました");
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

        if (NorvesLib::Core::GEngine.GetSkeletalAssetSession().HasPinnedSnapshot())
        {
            NORVES_LOG_WARNING("SkeletalAssets", "snapshotの変更を拒否します reason=skeletal_snapshot_pinned");
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

        if (!m_TextureLooseAssetRoot.empty())
        {
            // 既定のクック済みの設定: 足りない項目はばらの元画像で読み、クック済みの色は元画像と同じ標本値で描く。
            candidate->SetLooseAssetRoot(AnsiString(m_TextureLooseAssetRoot.c_str()));
            candidate->SetTreatSrgbTexturesAsLinear(true);
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

        if (!m_TextureLooseAssetRoot.empty() &&
            !NorvesLib::Core::Engine::GEngine->GetRenderResources().Textures().SetTextureAssetFallbackMode(
                NorvesLib::Core::Rendering::TextureAssetFallbackMode::DebugAllowLooseFallback))
        {
            LOG_WARNING("COOKED_FALLBACK_NOT_SET "
                        "クック済みのパッケージが無いときにばらの元画像へ戻す設定"
                        "を反映できませんでした");
        }

        m_AssetSystemSnapshot = immutableCandidate;
        LOG_INFO("Asset runtime snapshot reload completed root=\"%s\" manifest=\"%s\"",
                 m_TextureAssetRoot.c_str(),
                 m_TextureAssetManifestPath.c_str());
        return true;
    }

    void GameApplicationHandler::InstallCookedManifestUnavailableAssetSystem()
    {
        if (m_TextureLooseAssetRoot.empty() || !NorvesLib::Core::Engine::GEngine)
        {
            return;
        }

        // マニフェストが無いのでクック済みは引けず、全パスをばらの元画像で読む。クック済みを使う前提の印を付け、
        // 読んだ各パスに TEXTURE_COOKED_MISSING を1回ずつ警告させる。
        auto candidate = MakeShared<Asset::AssetSystem>(AnsiString(m_TextureLooseAssetRoot.c_str()));
        candidate->SetCookedExpected(true);
        TSharedPtr<const Asset::AssetSystem> immutableCandidate = candidate;
        if (!NorvesLib::Core::Engine::GEngine->GetRenderResources().ReloadAssetRuntimeSnapshot(
                m_TextureLooseAssetRoot,
                immutableCandidate))
        {
            LOG_WARNING("COOKED_MISSING_WARN_NOT_SET "
                        "マニフェストが無いときの警告の設定を反映できませんでした");
        }
    }

    TSharedPtr<const Asset::AssetSystem> GameApplicationHandler::GetAssetSystemSnapshot() const
    {
        return m_AssetSystemSnapshot;
    }

    bool GameApplicationHandler::IsTextureCooked(const String &logicalPath) const
    {
        if (!m_AssetSystemSnapshot || logicalPath.empty())
        {
            return false;
        }

        const AnsiString ansiPath(logicalPath.c_str());
        return m_AssetSystemSnapshot->FindCookedVariant(ansiPath, Asset::AssetKind::Texture).ShouldUseCooked();
    }

    bool GameApplicationHandler::PrepareM9WorldAssets()
    {
        if (!m_M9WorldAcceptance || !m_M9WorldAcceptance->bRequested || !m_AssetSystemSnapshot ||
            !NorvesLib::Core::Engine::GEngine || !m_M9WorldAcceptance->Preparation.CanPrepare())
        {
            return false;
        }

#if !defined(NORVES_GAME_AUDIO)
        return false;
#else
        const Asset::AssetResolveResult effectResult = m_AssetSystemSnapshot->ResolveAsset(
            "Audio/M9/effect.wav", Asset::AssetKind::Audio);
        const Asset::AssetResolveResult loopResult = m_AssetSystemSnapshot->ResolveAsset(
            "Audio/M9/loop.wav", Asset::AssetKind::Audio);
        if (!effectResult.UsedCooked() || !loopResult.UsedCooked())
        {
            LOG_ERROR("M9_WORLD_SMOKE audio resolution requires both cooked assets");
            return false;
        }

        Asset::CookedAudioParseResult effect = Asset::ParseCookedAudio(effectResult.Blob);
        Asset::CookedAudioParseResult loop = Asset::ParseCookedAudio(loopResult.Blob);
        if (!effect.Succeeded() || !loop.Succeeded())
        {
            LOG_ERROR("M9_WORLD_SMOKE cooked asset parsing failed");
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

        m_M9WorldAcceptance->EffectClip = effectClip;
        m_M9WorldAcceptance->LoopClip = loopClip;
        m_M9WorldAcceptance->bAssetsReady = false;
        auto& session = NorvesLib::Core::GEngine.GetSkeletalAssetSession();
        const auto bound = session.BindSnapshot(m_AssetSystemSnapshot);
        if (bound != SkeletalRuntimeStatus::Success && bound != SkeletalRuntimeStatus::Unchanged)
        {
            return false;
        }
        auto* runtime = session.GetRuntime();
        if (!runtime)
        {
            return false;
        }
        const auto admitted = m_M9WorldAcceptance->Preparation.Start(
            *runtime, m_M9WorldAcceptance->SkeletalPath, Container::StringView(m_M9WorldAcceptance->ClipName));
        return admitted.Status == SkeletalRuntimeStatus::Accepted;
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
        m_CameraLateUpdateSlot->Reset();
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
        UpdatePauseMenu();

#if NORVES_BUILD_DEVELOPMENT
        // 検査の窓口の配線の確認: ENGINE_CHECK_SELFTEST が有効なら、起動の後の最初の更新で結果を 1 回出す。
        if (!s_bEngineChecksSelfTestReported && NorvesLib::Core::Engine::GEngine != nullptr)
        {
            s_bEngineChecksSelfTestReported = true;
            NorvesLib::Core::Engine::GEngine->GetEngineChecks().Report(
                NorvesLib::Debug::EngineChecks::kSelfTestName, !s_bEngineChecksSelfTestFail, 1);
        }
#endif

        if (m_SceneCycle.IsEnabled() && m_pGameModeStateMachine != nullptr && NorvesLib::Core::Engine::GEngine)
        {
            m_SceneCycle.Update(*NorvesLib::Core::Engine::GEngine, *m_pGameModeStateMachine);
        }
    }

    void GameApplicationHandler::OnLateUpdate(float deltaTime)
    {
        m_CameraLateUpdateSlot->Dispatch(deltaTime);
    }

    void GameApplicationHandler::OnPreRender()
    {
        if (m_SequenceFrameCapture.IsEnabled() && NorvesLib::Core::Engine::GEngine)
        {
            m_SequenceFrameCapture.OnPreRender(NorvesLib::Core::Engine::GEngine->GetRenderWorld());
        }
    }

    void GameApplicationHandler::OnPostRender()
    {
        if (m_SequenceFrameCapture.IsEnabled() && NorvesLib::Core::Engine::GEngine)
        {
            m_SequenceFrameCapture.OnPostRender(NorvesLib::Core::Engine::GEngine->GetRenderWorld());
        }
        LogVramLedgerOnce();
    }

    void GameApplicationHandler::LogVramLedgerOnce()
    {
        // 読み込み中を一度も見ない場合の待ち上限（描画フレーム数）。
        constexpr uint32_t kVramLedgerFallbackFrames = 600u;

        if (m_bVramLedgerLogged || !NorvesLib::Core::Engine::GEngine)
        {
            return;
        }

        ++m_VramLedgerFrameCount;
        const bool bPending = NorvesLib::Core::Engine::GEngine->GetRenderWorld().HasPendingAsyncAssets();
        if (bPending)
        {
            m_bVramLedgerSawPending = true;
            return;
        }
        if (!m_bVramLedgerSawPending && m_VramLedgerFrameCount < kVramLedgerFallbackFrames)
        {
            return;
        }

        m_bVramLedgerLogged = true;
        const auto stats = NorvesLib::Core::Engine::GEngine->GetRenderResources().GetResourceStats();
        constexpr double kBytesPerMb = 1024.0 * 1024.0;
        LOG_INFO("VRAM_LEDGER textures=%u texture_mb=%.1f buffers_mb=%.1f",
                 static_cast<unsigned>(stats.TextureCount),
                 static_cast<double>(stats.TextureBytes) / kBytesPerMb,
                 static_cast<double>(stats.TotalBufferMemory) / kBytesPerMb);
    }

    void GameApplicationHandler::UpdatePauseMenu()
    {
#if defined(NORVES_ENABLE_IMGUI)
        if (!m_bPauseMenuRegistered || NorvesLib::Core::Engine::GEngine == nullptr)
        {
            return;
        }
        NorvesLib::Core::Engine::Engine &engine = *NorvesLib::Core::Engine::GEngine;

        ++m_PauseMenuTickCount;
        if (m_bOpenPauseMenuAtStartup && m_PauseMenuTickCount >= kPauseMenuStartupOpenTick &&
            m_PauseMenu.IsEnabled())
        {
            m_bOpenPauseMenuAtStartup = false;
            m_PauseMenu.Open();
        }

        // 決定された項目を実行する。閉じてから遷移や終了を要求する（開いた状態を次のシーンへ持ち越さない）
        Game::Pause::PauseMenu::Item item = Game::Pause::PauseMenu::Item::Resume;
        if (m_PauseMenu.IsOpen() && m_PauseMenu.ConsumeActivation(item))
        {
            m_PauseMenu.Close();
            switch (item)
            {
            case Game::Pause::PauseMenu::Item::Resume:
                LOG_INFO("PAUSE_MENU action=resume");
                break;
            case Game::Pause::PauseMenu::Item::ReturnToLauncher:
                LOG_INFO("PAUSE_MENU action=return_to_launcher");
                if (m_pGameModeStateMachine != nullptr)
                {
                    m_pGameModeStateMachine->RequestChange(Game::GameModes::Launcher);
                }
                break;
            case Game::Pause::PauseMenu::Item::Quit:
                LOG_INFO("PAUSE_MENU action=quit");
                engine.RequestExit(0);
                break;
            }
        }

        // 開閉をエンジンへ反映する。入力イベントの配送中は入力文脈を替えられないので、ここ（更新）で行う。
        const bool bOpen = m_PauseMenu.IsOpen();
        if (bOpen && !m_bPauseMenuApplied)
        {
            m_bPauseMenuApplied = true;
            // メニューの入力文脈（カーソルを出す。シーンの移動・視点・攻撃などの写像を止める）を積む
            m_bPauseMenuPushedContext = engine.GetInputMapper().PushContext(Game::InputActions::MenuContext);
            // 開く前に押していたキー・ボタンの状態を捨てる（カメラ操作の Alt などが押しっぱなしにならないように）
            engine.GetInputSystem().ReleaseAll();
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(true);
            LOG_INFO("PAUSE_MENU state=open");
        }
        else if (!bOpen && m_bPauseMenuApplied)
        {
            m_bPauseMenuApplied = false;
            if (m_bPauseMenuPushedContext)
            {
                (void)engine.GetInputMapper().RemoveContext(Game::InputActions::MenuContext);
                m_bPauseMenuPushedContext = false;
            }
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(false);
            LOG_INFO("PAUSE_MENU state=closed");
        }
#endif
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
        m_pGameModeStateMachine = nullptr;
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
        if (m_bPauseMenuRegistered)
        {
            m_PauseMenu.SetEnabled(false);
            NorvesLib::Modules::Gui::UnregisterImGuiView(&m_PauseMenuView);
            if (NorvesLib::Core::Engine::GEngine != nullptr)
            {
                NorvesLib::Core::Engine::GEngine->GetInputRouter().UnregisterController(&m_PauseMenu);
            }
            m_bPauseMenuRegistered = false;
        }
#endif

        // Bridge を World/RenderWorld 破棄より前に停止する（close→join、冪等）。
        // 受信スレッドは GEngine に触れないが、確実に join してから先の解体へ進む。
        m_BridgeHost.Stop();

        // 終了前の保存処理など
        // - セーブデータの保存
        // - 設定の保存
    }

    bool GameApplicationHandler::HasPendingAssetConsumers() const
    {
        return m_M9WorldAcceptance && m_M9WorldAcceptance->bRequested &&
               m_M9WorldAcceptance->Preparation.HasPendingWork();
    }

    void GameApplicationHandler::OnShutdown()
    {
        LOG_INFO("GameApplicationHandler::OnShutdown()");
        // Enter前の失敗でも、runtime終了より先に借用取消先とeventを解放する。
        if (m_M9WorldAcceptance)
        {
            m_M9WorldAcceptance->Preparation.Cancel();
        }

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

    bool GameApplicationHandler::ResolveStartupScene()
    {
        if (m_bDogMovementSmoke)
        {
            if (!m_StartupSceneId.empty() || !m_StartupSceneView.empty())
            {
                LOG_ERROR("--scene・--scene-view は --dog-movement-smoke と併用できません");
                return false;
            }
            if (m_SceneCycle.IsEnabled())
            {
                LOG_ERROR("--scene-cycle-smoke は --dog-movement-smoke と併用できません");
                return false;
            }
            return true;
        }
        if (m_SceneCycle.IsEnabled() && (!m_StartupSceneId.empty() || !m_StartupSceneView.empty()))
        {
            LOG_ERROR("--scene・--scene-view は --scene-cycle-smoke と併用できません");
            return false;
        }

        // 解決結果のレポート（--scene-report）。ログとは別に、未知の id・視点と欠けた資産を行ごとに残す。
        String sceneReport;

        // ランチャーから始める: --scene=launcher、--scene を省いた起動、--scene-cycle-smoke（ランチャーと行き来する）。
        // --animation-debug は Rendering3DTest の機能なので、--scene を省いても既定のシーンへ入る。
        const bool bExplicitLauncher = Scenes::SceneTextEquals(m_StartupSceneId.c_str(), Scenes::kLauncherSceneId);
        const bool bImplicitLauncher = m_StartupSceneId.empty() && !m_bAnimationDebugRequested;
        if (bExplicitLauncher || bImplicitLauncher || m_SceneCycle.IsEnabled())
        {
            m_bLauncherStartup = true;
            if (!m_StartupSceneView.empty())
            {
                LOG_ERROR_F("SCENE_VIEW_UNKNOWN scene=%s view=\"%s\" ランチャーに名前付き視点はありません",
                            Scenes::kLauncherSceneId, m_StartupSceneView.c_str());
                sceneReport += "unknown_view=" + m_StartupSceneView + "\n";
                m_StartupSceneView = {};
            }
            if (!m_SceneReportPath.empty())
            {
                const String reportBody = String("scene=") + Scenes::kLauncherSceneId + "\n" + sceneReport;
                std::ofstream reportOutput(m_SceneReportPath.c_str(), std::ios::binary | std::ios::trunc);
                if (!reportOutput.write(reportBody.c_str(), static_cast<std::streamsize>(reportBody.size())))
                {
                    LOG_ERROR_F("SCENE_REPORT_WRITE_FAILED path=%s", m_SceneReportPath.c_str());
                    return false;
                }
            }
            LOG_INFO_F("SCENE_SELECTED id=%s view=default", Scenes::kLauncherSceneId);
            return true;
        }

        const Scenes::SceneDescriptor *scene = nullptr;
        if (!m_StartupSceneId.empty())
        {
            scene = Scenes::FindScene(m_StartupSceneId.c_str());
            if (scene == nullptr)
            {
                sceneReport += "unknown_scene=" + m_StartupSceneId + "\n";
                LOG_ERROR_F("SCENE_UNKNOWN id=\"%s\" 既定のシーン（%s）で起動します", m_StartupSceneId.c_str(),
                            Scenes::kDefaultSceneId);
                m_StartupSceneView = {};
            }
        }
        if (scene == nullptr)
        {
            scene = Scenes::FindScene(Scenes::kDefaultSceneId);
        }
        if (scene == nullptr)
        {
            LOG_ERROR_F("SCENE_CATALOG_INVALID 既定のシーン（%s）が一覧に無い", Scenes::kDefaultSceneId);
            return false;
        }
        m_StartupSceneId = String(scene->Id);

        // 視点の一覧。先頭が既定の視点。撮影スクリプトが -Views を省いたときに、全視点を撮るのに読む。
        {
            String viewList;
            for (std::size_t index = 0; index < scene->ViewCount; ++index)
            {
                if (index > 0)
                {
                    viewList += ",";
                }
                viewList += scene->Views[index].Name;
            }
            sceneReport += "views=" + viewList + "\n";
        }

        if (!m_StartupSceneView.empty())
        {
            const Scenes::SceneViewDescriptor *view = Scenes::FindSceneView(*scene, m_StartupSceneView.c_str());
            if (view == nullptr)
            {
                LOG_ERROR_F("SCENE_VIEW_UNKNOWN scene=%s view=\"%s\" シーンの既定の視点で起動します", scene->Id,
                            m_StartupSceneView.c_str());
                sceneReport += "unknown_view=" + m_StartupSceneView + "\n";
                m_StartupSceneView = {};
            }
            else if (view->Kind == Scenes::SceneViewKind::SpringArm)
            {
                if (s_bRendering3DTestHasStartupCamera)
                {
                    LOG_WARNING_F("SCENE_VIEW_OVERRIDDEN scene=%s view=%s --startup-camera の指定を優先します",
                                  scene->Id, view->Name);
                }
                else
                {
                    s_bRendering3DTestHasStartupCamera = true;
                    s_Rendering3DTestStartupCamera[0] = view->Yaw;
                    s_Rendering3DTestStartupCamera[1] = view->Pitch;
                    s_Rendering3DTestStartupCamera[2] = view->ArmLength;
                }
            }
        }

        for (std::size_t index = 0; index < scene->RequiredAssetCount; ++index)
        {
            std::error_code existsError;
            const char *assetPath = scene->RequiredAssets[index];
            std::filesystem::path resolvedPath(assetPath);
#if defined(NORVES_SOURCE_ASSET_DIR)
            // 作業ディレクトリは実行ファイルのディレクトリなので、リポジトリの Assets を基準に探す。
            resolvedPath = std::filesystem::path(NORVES_SOURCE_ASSET_DIR).parent_path() / resolvedPath;
#endif
            if (!std::filesystem::exists(resolvedPath, existsError) || existsError)
            {
                LOG_WARNING_F("SCENE_ASSET_MISSING scene=%s path=%s", scene->Id, assetPath);
                sceneReport += String("missing_asset=") + assetPath + "\n";
            }
        }

        if (!m_SceneReportPath.empty())
        {
            const String reportBody = String("scene=") + scene->Id + "\n" + sceneReport;
            std::ofstream reportOutput(m_SceneReportPath.c_str(), std::ios::binary | std::ios::trunc);
            if (!reportOutput.write(reportBody.c_str(), static_cast<std::streamsize>(reportBody.size())))
            {
                LOG_ERROR_F("SCENE_REPORT_WRITE_FAILED path=%s", m_SceneReportPath.c_str());
                return false;
            }
        }

        LOG_INFO_F("SCENE_SELECTED id=%s view=%s", scene->Id,
                   m_StartupSceneView.empty() ? scene->Views[0].Name : m_StartupSceneView.c_str());
        return true;
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
        const Container::TWeakPtr<CameraLateUpdateSlot> lateCameraSlot = m_CameraLateUpdateSlot;
        const auto dogDrive = m_DogDrive;
        stateMachine->Registry().Register(
            DogMovementSmoke, [lateCameraSlot, dogDrive](const GameModeParams&) -> Container::TUniquePtr<IGameMode> {
                auto mode = MakeUnique<DogMovementSmokeMode>();
                mode->GetData().LateSlot = lateCameraSlot;
                mode->GetData().Drive = dogDrive;
                return mode;
            });
        stateMachine->Registry().Register(
            Rendering3DTest,
            [this, bUseCookedModel, bPhysicsSmoke, m9WorldAcceptance,
             lateCameraSlot](const GameModeParams &params) -> Container::TUniquePtr<IGameMode>
            {
                auto mode = MakeUnique<Rendering3DTestMode>();
                mode->GetData().m_LateCameraSlot = lateCameraSlot;
                mode->GetData().m_ModelPath = params.ModelPath;
                mode->GetData().m_bUseCookedModel = bUseCookedModel;
                mode->GetData().m_BoardSmokeCount = s_Rendering3DTestBoardSmokeCount;
                mode->GetData().m_BillboardSmokeCount = s_Rendering3DTestBillboardSmokeCount;
                mode->GetData().m_ImpostorSmokeCount = s_Rendering3DTestImpostorSmokeCount;
                mode->GetData().m_InstancedMeshCount = s_Rendering3DTestInstancedMeshCount;
                mode->GetData().m_bLayerCompositeSmoke = s_bRendering3DTestLayerCompositeSmoke;
                mode->GetData().m_bPhysicsSmoke = bPhysicsSmoke;
                mode->GetData().m_bHasStartupCamera = s_bRendering3DTestHasStartupCamera;
                mode->GetData().m_StartupCameraYaw = s_Rendering3DTestStartupCamera[0];
                mode->GetData().m_StartupCameraPitch = s_Rendering3DTestStartupCamera[1];
                mode->GetData().m_StartupCameraArmLength = s_Rendering3DTestStartupCamera[2];
                mode->GetData().m_bHasStartupSunElevation = s_bRendering3DTestHasSunElevation;
                mode->GetData().m_StartupSunElevation = s_Rendering3DTestSunElevation;
                mode->GetData().m_bHasStartupSunAzimuth = s_bRendering3DTestHasSunAzimuth;
                mode->GetData().m_StartupSunAzimuth = s_Rendering3DTestSunAzimuth;
                mode->GetData().m_bHasStartupExposureEV100 = s_bRendering3DTestHasExposureEV100;
                mode->GetData().m_StartupExposureEV100 = s_Rendering3DTestExposureEV100;
                mode->GetData().m_bHasStartupHeightFogDensity = s_bRendering3DTestHasHeightFogDensity;
                mode->GetData().m_StartupHeightFogDensity = s_Rendering3DTestHeightFogDensity;
                mode->GetData().m_bHasStartupHeightFogFalloff = s_bRendering3DTestHasHeightFogFalloff;
                mode->GetData().m_StartupHeightFogFalloff = s_Rendering3DTestHeightFogFalloff;
                mode->GetData().m_OrbitDegreesPerSecond = s_Rendering3DTestOrbitDegreesPerSecond;
                mode->GetData().m_StartupRenderScale = s_Rendering3DTestRenderScale;
                mode->GetData().m_bDebugDrawTestLines = s_bRendering3DTestDebugDrawTestLines;
                mode->GetData().m_bStartupSkinnedProbe = s_bRendering3DTestSkinnedProbe;
                mode->GetData().m_bAnimationDebug = m_bAnimationDebugRequested;
#if defined(NORVES_ENABLE_IMGUI)
                mode->GetData().m_bDebugImGui = m_bImGuiRequested;
#endif
                mode->GetData().m_bStartupScanProps = s_bRendering3DTestScanProps;
                mode->GetData().m_StressMegaInstanceCount = s_Rendering3DTestStressMegaInstances;
                if (s_Rendering3DTestStressGeometryCount > 0u)
                {
                    mode->GetData().m_bStressGeometry = true;
                    mode->GetData().m_StressMegaInstanceCount = s_Rendering3DTestStressGeometryCount;
                    mode->GetData().m_StressGroundMegaInstanceCount = s_Rendering3DTestStressMegaInstances;
                }
                mode->GetData().m_bStartupTemporalAA = s_bRendering3DTestTemporalAA;
                mode->GetData().m_bStartupNight = s_bRendering3DTestNight;
                mode->GetData().m_StartupDebugViewMode = s_Rendering3DTestDebugViewMode;
                mode->GetData().m_bVirtualTexture = s_bRendering3DTestVirtualTexture;
                // --no-cooked-textures はクック済みを使わない指定なので、岩・小屋も glTF の経路で読む。
                mode->GetData().m_bStartupModelsFromGltf = s_bRendering3DTestModelSourceGltf || m_bNoCookedTextures;
                mode->GetData().m_bBigSphereFromRuntime = s_bRendering3DTestBigSphereRuntime || m_bNoCookedTextures;
                mode->GetData().m_GetAssetSystem = [this]()
                {
                    return GetAssetSystemSnapshot();
                };
                mode->GetData().m_bStressTextures = s_bRendering3DTestStressTextures;
                mode->GetData().m_M9WorldAcceptance = m9WorldAcceptance;
                mode->GetData().m_IsTextureCooked = [this](const String &logicalPath)
                {
                    return IsTextureCooked(logicalPath);
                };
                return mode;
            });
        // ランチャーがシーンへ渡す引数（Rendering3DTest の岩のパスなど）。シーンから戻ってきたときも同じ値を渡す。
        GameModeParams params;
        params.ModelPath = m_Rendering3DTestModelPath.empty()
                               ? String(kDefaultRendering3DTestModelPath)
                               : m_Rendering3DTestModelPath;
        stateMachine->Registry().Register(
            Launcher,
            [params, pauseMenu = &m_PauseMenu](const GameModeParams&) -> Container::TUniquePtr<IGameMode>
            {
                auto mode = MakeUnique<LauncherMode>();
                mode->GetData().m_SceneParams = params;
                mode->GetData().m_pPauseMenu = pauseMenu;
                return mode;
            });
        stateMachine->Registry().Register(
            MemoryAgingTest,
            [](const GameModeParams&) -> Container::TUniquePtr<IGameMode>
            {
                return MakeUnique<MemoryAgingTestMode>();
            });
        // バナナのシーン。本数・視点・台本は起動引数から渡す。
        stateMachine->Registry().Register(
            MegaBananas,
            [viewName = m_StartupSceneView, scriptPath = m_CameraScriptPath, pathTime = m_CameraPathTime,
             bananaCount = m_BananaCount, bShowInfo = m_bOpenSceneInfo, pauseMenu = &m_PauseMenu,
             getAssetSystem = [this]() { return GetAssetSystemSnapshot(); }](const GameModeParams&)
                -> Container::TUniquePtr<IGameMode>
            {
                auto mode = MakeUnique<MegaBananasMode>();
                mode->GetData().m_ViewName = viewName;
                mode->GetData().m_CameraScriptPath = scriptPath;
                mode->GetData().m_CameraPathTime = pathTime;
                mode->GetData().m_BananaCount = bananaCount;
                mode->GetData().m_bShowInfoAtStart = bShowInfo;
                mode->GetData().m_pPauseMenu = pauseMenu;
                mode->GetData().m_GetAssetSystem = getAssetSystem;
                return mode;
            });
#if NORVES_BUILD_DEVELOPMENT
        // 確認用の sandbox は開発ビルドだけ（ランチャーのカードも同じ）。視点と台本は起動引数から渡す。
        stateMachine->Registry().Register(
            Sandbox,
            [viewName = m_StartupSceneView, scriptPath = m_CameraScriptPath, pathTime = m_CameraPathTime,
             pauseMenu = &m_PauseMenu](const GameModeParams&) -> Container::TUniquePtr<IGameMode>
            {
                auto mode = MakeUnique<SandboxMode>();
                mode->GetData().m_ViewName = viewName;
                mode->GetData().m_CameraScriptPath = scriptPath;
                mode->GetData().m_CameraPathTime = pathTime;
                mode->GetData().m_pPauseMenu = pauseMenu;
                return mode;
            });
        // 負荷シーン mega-stress も開発ビルドだけ。Entity の数・視点・台本は起動引数から渡す。
        stateMachine->Registry().Register(
            MegaStress,
            [viewName = m_StartupSceneView, scriptPath = m_CameraScriptPath, pathTime = m_CameraPathTime,
             entityCount = m_StressEntityCount, movingPercent = m_StressMovingPercent,
             movingStartTime = m_StressMovingStartTime, streamCell = m_StressStreamCellMeters,
             streamPeriod = m_StressStreamPeriod, streamMaxCycles = m_SceneCycle.IsEnabled() ? Game::GameModes::kSmokeStreamCycles : 0u,
             pauseMenu = &m_PauseMenu,
             getAssetSystem = [this]() { return GetAssetSystemSnapshot(); }](const GameModeParams&)
                -> Container::TUniquePtr<IGameMode>
            {
                auto mode = MakeUnique<MegaStressMode>();
                mode->GetData().m_ViewName = viewName;
                mode->GetData().m_CameraScriptPath = scriptPath;
                mode->GetData().m_CameraPathTime = pathTime;
                mode->GetData().m_EntityCount = entityCount;
                mode->GetData().m_MovingPercent = movingPercent;
                mode->GetData().m_MovingStartTime = movingStartTime;
                mode->GetData().m_StreamCellMeters = streamCell;
                mode->GetData().m_StreamPeriod = streamPeriod;
                mode->GetData().m_StreamMaxCycles = streamMaxCycles;
                mode->GetData().m_pPauseMenu = pauseMenu;
                mode->GetData().m_GetAssetSystem = getAssetSystem;
                return mode;
            });
#endif

        // 起動するモード: 犬の比較は専用の引数、ランチャーから始めるときはランチャー、それ以外は --scene のシーンの GameMode。
        const Scenes::SceneDescriptor *startupScene = Scenes::FindScene(m_StartupSceneId.c_str());
        NorvesLib::Core::GameMode::GameModeId startMode = Rendering3DTest;
        if (m_bDogMovementSmoke)
        {
            startMode = DogMovementSmoke;
        }
        else if (m_SceneCycle.IsEnabled())
        {
            // 周回の検査は、ランチャーから始める（シーンへは検査が RequestChange で入る）
            startMode = Launcher;
            m_SceneCycle.SetModeParams(params);
        }
        else if (m_bLauncherStartup)
        {
            startMode = Launcher;
        }
        else if (startupScene != nullptr)
        {
            startMode = startupScene->Mode;
        }
        stateMachine->Start(startMode, params);
        m_pGameModeStateMachine = stateMachine.get();

        if (m_bDogMovementSmoke)
        {
            LOG_INFO("犬の移動比較モードを開始します");
        }
        else if (m_SceneCycle.IsEnabled())
        {
            LOG_INFO_F("シーンの周回の検査を開始します（周回数 %u）", m_SceneCycle.GetCycleCount());
        }
        else if (m_bLauncherStartup)
        {
            LOG_INFO("ランチャーから開始します");
        }
        else if (startMode == Rendering3DTest)
        {
            LOG_INFO("3Dレンダリングテストモードを開始します");
        }
        else
        {
            LOG_INFO_F("シーン %s を開始します", m_StartupSceneId.c_str());
        }

        return stateMachine;
    }

} // namespace Game
