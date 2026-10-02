#include "Input/InputRebindCaptureManager.h"
#include "Input/InputMapper.h"
#include "Input/InputRouter.h"
#include "Input/InputSystem.h"
#include "Thread/Atomic.h"
#include <limits>

namespace NorvesLib::Core::Input
{
    namespace
    {
        uint64_t NextCaptureRequest()
        {
            // 値requestを別manager/再生成instanceへ誤適用しない登録識別。wrapしない。
            static NorvesLib::Thread::Atomic<uint64_t> next{1};
            auto value = next.Load(std::memory_order_relaxed);
            for (;;)
            {
                if (value == std::numeric_limits<uint64_t>::max())
                {
                    return 0;
                }
                if (next.CompareExchangeWeak(value, value + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    return value;
                }
            }
        }
    }
    InputRebindCaptureManager::InputRebindCaptureManager(InputSystem& system, InputRouter& router, InputMapper& mapper)
        : m_System(system), m_Router(router), m_Mapper(mapper)
    {
    }
    InputRebindCaptureManager::~InputRebindCaptureManager()
    {
        Detach();
    }
    bool InputRebindCaptureManager::HasMatchingWiring() const
    {
        return m_System.m_Router == &m_Router && m_Mapper.m_Router == &m_Router &&
            &m_Mapper.m_State == &m_System.GetState();
    }
    bool InputRebindCaptureManager::Attach()
    {
        if (!HasMatchingWiring() || (m_Mapper.m_CaptureOwner && m_Mapper.m_CaptureOwner != this))
        {
            return false;
        }
        if (m_bAttached)
        {
            return true;
        }
        // allocation成功後にだけownerを確保する。
        m_Router.RegisterController(this, InputRouter::PriorityInputCapture);
        m_Mapper.m_CaptureOwner = this;
        m_bAttached = true;
        return true;
    }
    void InputRebindCaptureManager::Detach()
    {
        // 所有終了時はcallbackを増やさず正本を中立化し、次Runへheldを持ち越さない。
        // legacy controllerはBegin時にreset済みで、capture中のeventは届いていない。
        if (m_bBlocking && HasMatchingWiring())
        {
            m_System.m_State.ReleaseAll();
        }
        Abort();
        if (m_bAttached)
        {
            m_Router.UnregisterController(this);
            m_bAttached = false;
        }
        if (m_Mapper.m_CaptureOwner == this)
        {
            m_Mapper.SetCaptureSuppressed(false);
            m_Mapper.m_CaptureOwner = nullptr;
        }
        m_bBlocking = false;
    }
    uint64_t InputRebindCaptureManager::Begin(const InputRebindCaptureOptions& options)
    {
        if (!m_bAttached || !HasMatchingWiring() || m_Mapper.m_CaptureOwner != this ||
            !m_Mapper.IsFocused() || m_bBlocking || !IsValidInputRebindCaptureOptions(options))
        {
            return 0;
        }
        const auto request = NextCaptureRequest();
        if (request == 0 || !m_Capture.Begin(options, m_System.GetState()))
        {
            return 0;
        }
        m_RequestId = request;
        m_bBlocking = true;
        m_Mapper.SetCaptureSuppressed(true);
        ResetOperations();
        return m_RequestId;
    }
    bool InputRebindCaptureManager::Cancel(uint64_t requestId)
    {
        if (!m_bBlocking || requestId == 0 || requestId != m_RequestId)
        {
            return false;
        }
        m_Capture.Cancel();
        return true;
    }
    void InputRebindCaptureManager::Abort()
    {
        m_Capture.Abort();
    }
    void InputRebindCaptureManager::BeginFrame()
    {
        if (m_bBlocking)
        {
            m_Capture.BeginFrame(m_System.GetState());
        }
    }
    void InputRebindCaptureManager::Advance()
    {
        if (!m_bBlocking)
        {
            return;
        }
        const bool bMatching = HasMatchingWiring() && m_Mapper.m_CaptureOwner == this;
        if (!bMatching || !m_Mapper.IsFocused())
        {
            Abort();
        }
        m_Capture.Advance(m_System.GetState());
        if (m_Capture.GetPhase() != EInputRebindPhase::Finished)
        {
            return;
        }
        // 通常配送が終わってから残留edge/相対量/legacy状態を取り消す。
        // 所有外の配線へ変更されていた場合、そのRouterへresetを発火しない。
        if (bMatching)
        {
            ResetOperations();
        }
        if (m_Mapper.m_CaptureOwner == this)
        {
            m_Mapper.SetCaptureSuppressed(false);
        }
        m_bBlocking = false;
    }
    bool InputRebindCaptureManager::TryGetResult(uint64_t requestId, InputRebindCaptureResult& result) const
    {
        if (m_bBlocking || requestId == 0 || requestId != m_RequestId ||
            m_Capture.GetPhase() != EInputRebindPhase::Finished)
        {
            return false;
        }
        result = {m_RequestId, m_Capture.GetOutcome(), m_Capture.GetControl()};
        return true;
    }
    void InputRebindCaptureManager::ResetOperations()
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
    bool InputRebindCaptureManager::OnKey(const KeyEvent& event)
    {
        if (!m_bBlocking)
        {
            return false;
        }
        m_Capture.OnKey(event, m_System.GetState());
        return true;
    }
    bool InputRebindCaptureManager::OnMouseButton(const MouseButtonEvent& event)
    {
        if (!m_bBlocking)
        {
            return false;
        }
        m_Capture.OnMouseButton(event, m_System.GetState());
        return true;
    }
    bool InputRebindCaptureManager::OnMouseRawMove(const MouseRawMoveEvent& event)
    {
        if (!m_bBlocking)
        {
            return false;
        }
        m_Capture.OnMouseRawMove(event, m_System.GetState());
        return true;
    }
    bool InputRebindCaptureManager::OnMouseScroll(const MouseScrollEvent& event)
    {
        if (!m_bBlocking)
        {
            return false;
        }
        m_Capture.OnMouseScroll(event, m_System.GetState());
        return true;
    }
    bool InputRebindCaptureManager::OnGamepadButton(const GamepadButtonEvent& event)
    {
        if (!m_bBlocking)
        {
            return false;
        }
        m_Capture.OnGamepadButton(event, m_System.GetState());
        return true;
    }
    bool InputRebindCaptureManager::OnGamepadSample(const GamepadSampleEvent& event)
    {
        if (!m_bBlocking)
        {
            return false;
        }
        m_Capture.OnGamepadSample(event, m_System.GetState());
        return true;
    }
    void InputRebindCaptureManager::OnGamepadConnection(const GamepadConnectionEvent& event)
    {
        if (m_bBlocking)
        {
            m_Capture.OnGamepadConnection(event);
        }
    }
    void InputRebindCaptureManager::OnInputReset()
    {
        if (!m_bInternalReset)
        {
            Abort();
        }
    }
    void InputRebindCaptureManager::OnInputFocusChanged(bool focused)
    {
        if (!focused)
        {
            Abort();
        }
    }
} // namespace NorvesLib::Core::Input
