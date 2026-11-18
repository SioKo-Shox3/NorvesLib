// レベルの配置編集を ImGui の命令とゲームモードの更新へつなぐ。
#if defined(NORVES_ENABLE_IMGUI)

#pragma once

#include "Gameplay/Level/LevelEditOperations.h"
#include "Gameplay/Level/LevelMarkerDebugDraw.h"
#include "ImGuiModule/IImGuiView.h"

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
    class GameModeScope;
    class IGameModeController;
} // namespace NorvesLib::Core::GameMode

namespace Game::LevelEdit
{
    class LevelEditSubRoutine;

    /// 画面には値だけを保持し、World の読み書きはモードの更新で行う。
    class LevelEditView final : public NorvesLib::Modules::Gui::IImGuiView,
                                public NorvesLib::Modules::Gameplay::Level::ILevelEditEntityOwner
    {
    public:
        LevelEditView();
        ~LevelEditView() override;
        LevelEditView(const LevelEditView&) = delete;
        LevelEditView& operator=(const LevelEditView&) = delete;

        void SetGameModeController(NorvesLib::Core::GameMode::IGameModeController* controller);
        void Shutdown();
        void OnImGui() override;
        const char* GetViewName() const override
        {
            return "LevelEdit";
        }

        void AttachToMode(LevelEditSubRoutine& sub, NorvesLib::Core::GameMode::GameModeContext& ctx);
        void TickFromMode(LevelEditSubRoutine& sub, NorvesLib::Core::GameMode::GameModeContext& ctx);
        void DetachFromMode(LevelEditSubRoutine& sub);
        void OnSubRoutineDestroyed(LevelEditSubRoutine& sub);

        void OnLevelEditRootSpawned(NorvesLib::Core::Entity& root) override;
        void OnLevelEditBeforeDestroy(NorvesLib::Core::Entity& entity) override;

    private:
        enum class ECommandType
        {
            Place, Pick, Move, Delete, Rename, FindByKey, ReSnap, Save, Load
        };

        struct Command
        {
            ECommandType Type;
            NorvesLib::Modules::Gameplay::Level::ELevelMarkerKind Kind;
            NorvesLib::Core::Container::String Text;
        };

        void Enqueue(ECommandType type, const char* text = "");
        void ExecuteCommand(const Command& command, NorvesLib::Core::GameMode::GameModeContext& ctx,
                            const NorvesLib::Math::Ray* ray);
        void SetResult(const NorvesLib::Core::Container::String& text, bool bSuccess = true);
        void ResetModeState();

        // Controller と Scope は借用し、SubRoutine の所有権はモードへ渡す。
        NorvesLib::Core::GameMode::IGameModeController* m_pController = nullptr;
        NorvesLib::Core::GameMode::GameModeScope* m_pScope = nullptr;
        LevelEditSubRoutine* m_pSubRoutine = nullptr;
        bool m_bAttached = false;
        NorvesLib::Modules::Gameplay::Level::LevelEditOperations m_Operations;
        NorvesLib::Core::Container::VariableArray<Command> m_Commands;

        int m_MarkerKindIndex = 0;
        char m_PlaceKey[128] = {};
        char m_NewKey[128] = {};
        char m_FindKey[128] = {};
#if defined(NORVES_SOURCE_ASSET_DIR)
        char m_Path[512] = NORVES_SOURCE_ASSET_DIR "/Levels/Sample/Gameplay.scene.json";
#else
        char m_Path[512] = "Assets/Levels/Sample/Gameplay.scene.json";
#endif
        bool m_bDrawMarkers = true;
        LevelMarkerDebugDrawSettings m_DrawSettings;

        // Tick が写した表示値。Entity と Component のポインタは保存しない。
        size_t m_MarkerCount = 0;
        bool m_bHasSelection = false;
        NorvesLib::Core::Container::String m_SelectedKey;
        NorvesLib::Modules::Gameplay::Level::ELevelMarkerKind m_SelectedKind =
            NorvesLib::Modules::Gameplay::Level::ELevelMarkerKind::Territory;
        NorvesLib::Math::Vector3 m_SelectedPosition;
        LevelMarkerDebugDrawStats m_DrawStats;
        NorvesLib::Core::Container::String m_LastResult;
    };
} // namespace Game::LevelEdit

#endif // NORVES_ENABLE_IMGUI
