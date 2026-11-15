#pragma once

#include "Core/Public/Debug/DebugConfig.h"

// 確認用シーンは開発ビルドだけ。
#if NORVES_BUILD_DEVELOPMENT

// ---------------------------------------------------------------------------
// asset-preview（論理パスで指定した資産を床の上に並べる確認用シーン）
//
// --asset-preview-model=<path> と --asset-preview-skeletal=<path> で資産を指定する。
// --asset-preview-clip=<name> で骨格つき資産のクリップを再生し、
// --asset-preview-time=<seconds> を加えると指定時刻で停止する。
// --scene-view・--camera-script・--camera-path-time・--camera-path-play で視点を選び、
// --scene-report=<path> に読み込めなかった資産とクリップを追記する。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/RenderTypes.h"
#include "Core/Public/Thread/Atomic.h"
#include "GameModes/AssetPreview/AssetPreviewLayout.h"
#include "GameModes/AssetPreview/AssetPreviewSkeletal.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"
#include "Scenes/SceneViewSwitch.h"

#include <cstdint>

namespace Game::GameModes
{
    /// コールバックが GameMode の寿命を越えても結果を書ける共有状態。
    struct AssetPreviewModelState
    {
        NorvesLib::Thread::Atomic<bool> m_bCompleted{false};
        NorvesLib::Core::Rendering::ModelHandle m_Handle;
    };

    /// 指定順の静的モデルの要求と、その完了結果。
    struct AssetPreviewModelLoad
    {
        NorvesLib::Core::Container::String m_Path;
        uint32_t m_RequestId = 0;
        NorvesLib::Core::Container::TSharedPtr<AssetPreviewModelState> m_State;
    };

    struct AssetPreviewData
    {
        NorvesLib::Core::Container::String m_ViewName;
        NorvesLib::Core::Container::String m_CameraScriptPath;
        float m_CameraPathTime = -1.0f;
        bool m_bCameraPathPlay = false;
        const Game::Pause::PauseMenu* m_pPauseMenu = nullptr;
        Game::Scenes::SceneViewSwitch* m_pViewSwitch = nullptr;
        AssetPreviewArguments m_Arguments;
        NorvesLib::Core::Container::String m_SceneReportPath;
        NorvesLib::Core::Delegate<NorvesLib::Core::SkeletalAssetRuntime*> m_GetSkeletalRuntime;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;
        NorvesLib::Core::Container::VariableArray<AssetPreviewModelLoad> m_ModelLoads;
        AssetPreviewSkeletalLoads m_SkeletalLoads;
        // ヘッダ利用側へ Private の定義を公開せず、完全型がある実装側で生成・解放する。
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::ResourceIO::SkinnedMaterialBuilder> m_MaterialBuilder;
        NorvesLib::Core::Rendering::MaterialHandle m_FallbackMaterial;
        NorvesLib::Core::Rendering::MaterialHandle m_AxisMaterials[3];
        bool m_bPlaced = false;
        bool m_bReady = false;
    };

    class AssetPreviewRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                                            AssetPreviewData& data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, AssetPreviewData& data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, AssetPreviewData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char* DebugName = "AssetPreview";
    };

    using AssetPreviewMode = NorvesLib::Core::GameMode::TGameMode<AssetPreviewRoutine, AssetPreviewData>;
} // namespace Game::GameModes

#endif
