#include "Input/InputDebugOverlayController.h"
#include "Input/InputMapper.h"
#include "Input/InputRebindCaptureManager.h"
#include "Input/InputRouter.h"
#include "Input/InputSystem.h"

namespace NorvesLib::Core::Input
{
    InputDebugOverlayController::InputDebugOverlayController(InputSystem& system, InputRouter& router, InputMapper& mapper)
        : m_System(system), m_Router(router), m_Mapper(mapper)
    {
    }
    InputDebugOverlayController::~InputDebugOverlayController()
    {
        Detach();
    }
    bool InputDebugOverlayController::HasMatchingWiring() const
    {
        return m_System.m_Router == &m_Router && m_Mapper.m_Router == &m_Router &&
            &m_Mapper.m_State == &m_System.GetState();
    }
    bool InputDebugOverlayController::Attach()
    {
        if (!HasMatchingWiring() || (m_Mapper.m_DebugOverlayOwner && m_Mapper.m_DebugOverlayOwner != this))
        {
            return false;
        }
        if (!m_bAttached)
        {
            m_Router.RegisterController(this, InputRouter::PriorityDebugOverlayMask);
            m_Mapper.m_DebugOverlayOwner = this;
            m_bAttached = true;
        }
        if (m_System.m_bDeferredInputReset)
        {
            ResetOperations();
        }
        return true;
    }
    void InputDebugOverlayController::Detach()
    {
        if (m_State.IsMasking() && HasMatchingWiring())
        {
            // 所有終了中にobserverを再入させない。captureが残っていれば先に中止する。
            if (m_Mapper.m_CaptureOwner)
            {
                m_Mapper.m_CaptureOwner->Abort();
            }
            m_Mapper.CancelAll();
            // 未適用enterではlegacyがまだresetされていない。controller再生成でも失わない。
            m_System.DeferReleaseAll();
        }
        m_State.Reset();
        if (m_bAttached)
        {
            m_Router.UnregisterController(this);
            m_bAttached = false;
        }
        if (m_Mapper.m_DebugOverlayOwner == this)
        {
            m_Mapper.SetDebugOverlaySuppressed(false);
            m_Mapper.m_DebugOverlayOwner = nullptr;
        }
    }
    void InputDebugOverlayController::Advance()
    {
        if (!m_bAttached)
        {
            return;
        }
        if (!HasMatchingWiring() || m_Mapper.m_DebugOverlayOwner != this)
        {
            Detach();
            return;
        }
        if (!m_State.ApplyRequested())
        {
            return;
        }
        m_Mapper.SetDebugOverlaySuppressed(m_State.IsActive());
        // 通常eventの配送後、Mapper pollingより前にlegacyのheld/ドラッグも解除する。
        ResetOperations();
    }
    void InputDebugOverlayController::ResetOperations()
    {
        struct Guard
        {
            bool& Active;
            explicit Guard(bool& active) : Active(active)
            {
                Active = true;
            }
            ~Guard()
            {
                Active = false;
            }
        } guard(m_bInternalReset);
        m_System.ReleaseAll();
    }
    bool InputDebugOverlayController::OnKey(const KeyEvent& event)
    {
        if (!m_bAttached)
        {
            return false;
        }
        if (m_Mapper.IsFocused() && m_State.OnKey(event))
        {
            return true;
        }
        return IsMasking();
    }
    void InputDebugOverlayController::OnInputFocusChanged(bool focused)
    {
        if (!focused)
        {
            m_State.ResetKeys();
        }
    }
} // namespace NorvesLib::Core::Input
