#pragma once

// 重いサブシステムヘッダのインクルードを避け、前方宣言のみ使用する。
// GameModeContext は各 Enter/Tick/Leave 呼び出しサイトでローカルに
// 構築される一時オブジェクト。参照メンバを持つため代入不可（設計上の意図）。

namespace NorvesLib::Core
{
    class World;

    namespace Rendering
    {
        class RenderResources;
    }

    namespace Input
    {
        class InputSystem;
    }

    namespace Engine
    {
        class Engine;
    }
}

namespace NorvesLib::Core::GameMode
{
    class IGameModeController;
    class GameModeScope;

    /**
     * @brief ゲームモード実行コンテキスト
     *
     * IGameMode::Enter / Tick / Leave の呼び出しサイトで
     * スタック上に生成し、ゲームモードロジックへ渡す一時的な構造体。
     *
     * 設計上の注意:
     * - 参照メンバを持つためデフォルトコピー代入は削除されており、保存して
     *   再代入することはできない。各呼び出しフレームごとに新規構築すること。
     * - 格納するポインタ/参照はすべて非所有（各システムはエンジンが管理）。
     */
    struct GameModeContext
    {
        NorvesLib::Core::Engine::Engine&            EngineRef;
        NorvesLib::Core::World&                     WorldRef;
        NorvesLib::Core::Rendering::RenderResources& RenderResourcesRef;
        NorvesLib::Core::Input::InputSystem&         InputRef;
        IGameModeController&                         ControllerRef;
        GameModeScope&                               ScopeRef;
        float                                        DeltaTime = 0.0f;
    };

} // namespace NorvesLib::Core::GameMode
