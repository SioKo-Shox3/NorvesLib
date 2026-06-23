#include "GameModes/Rendering3DTest/Rendering3DTestDebugInput.h"

#include "Core/Public/Input/InputTypes.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderTypes.h"

namespace Game::GameModes
{
    using NorvesLib::Core::Input::InputAction;
    using NorvesLib::Core::Input::KeyCode;
    using NorvesLib::Core::Input::KeyEvent;
    using NorvesLib::Core::Rendering::DebugViewMode;

    void Rendering3DTestDebugInput::SetRenderWorld(NorvesLib::Core::Rendering::RenderWorld *renderWorld)
    {
        m_pRenderWorld = renderWorld;
    }

    bool Rendering3DTestDebugInput::OnKey(const KeyEvent &event)
    {
        // Alt の押下/解放を記録する（切替抑止に使うが Alt 自体は透過させる）。
        if (event.Code == KeyCode::LeftAlt || event.Code == KeyCode::RightAlt)
        {
            m_bAltDown = (event.Action != InputAction::Released);
            return false;
        }

        // F1-F5 以外は透過させる（LightController 等下位へ）。
        const bool bIsDebugKey =
            event.Code == KeyCode::F1 || event.Code == KeyCode::F2 ||
            event.Code == KeyCode::F3 || event.Code == KeyCode::F4 ||
            event.Code == KeyCode::F5;
        if (!bIsDebugKey)
        {
            return false;
        }

#if !NORVES_BUILD_DEVELOPMENT
        // 非 development ビルドではデバッグビュー切替が無効なので F1-F5 を透過させる
        // （従来 inline 実装が #if NORVES_BUILD_DEVELOPMENT 内のみで F-key を扱っていたのと同じ挙動）。
        return false;
#endif

        // 押下の瞬間のみ作用させる（従来の IsKeyPressed 相当）。
        // ただし F1-F5 は本コントローラの所管キーなので Released/Repeat も consume する。
        if (event.Action != InputAction::Pressed)
        {
            return true;
        }

#if NORVES_BUILD_DEVELOPMENT
        // Alt 押下中は従来の `!IsAltDown()` ガードを踏襲し切替しない。
        if (!m_bAltDown && m_pRenderWorld != nullptr)
        {
            const DebugViewMode currentDebugViewMode = m_pRenderWorld->GetMainViewportDebugViewMode();
            DebugViewMode nextDebugViewMode = currentDebugViewMode;

            switch (event.Code)
            {
            case KeyCode::F1:
                nextDebugViewMode = DebugViewMode::Normal;
                break;
            case KeyCode::F2:
                nextDebugViewMode = DebugViewMode::Unlit;
                break;
            case KeyCode::F3:
                nextDebugViewMode = DebugViewMode::Wireframe;
                break;
            case KeyCode::F4:
                nextDebugViewMode = DebugViewMode::MegaGeometryClusters;
                break;
            case KeyCode::F5:
            {
                uint8_t nextModeIndex = static_cast<uint8_t>(currentDebugViewMode) + 1;
                if (nextModeIndex >= static_cast<uint8_t>(DebugViewMode::Count))
                {
                    nextModeIndex = 0;
                }
                nextDebugViewMode = static_cast<DebugViewMode>(nextModeIndex);
                break;
            }
            default:
                break;
            }

            if (nextDebugViewMode != currentDebugViewMode)
            {
                m_pRenderWorld->SetDebugViewModeAll(nextDebugViewMode);
                const DebugViewMode reflectedDebugViewMode = m_pRenderWorld->GetMainViewportDebugViewMode();
                NORVES_LOG_INFO("DebugView", "DebugViewMode -> %s", DebugViewModeToString(reflectedDebugViewMode));
            }
        }
#endif

        return true;
    }

} // namespace Game::GameModes
