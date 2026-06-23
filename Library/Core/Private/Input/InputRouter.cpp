#include "Input/InputRouter.h"
#include "Input/IInputController.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Input
{

    void InputRouter::RegisterController(IInputController *controller, int32_t priority)
    {
        if (controller == nullptr)
        {
            return;
        }

        // 既存登録なら priority を更新するために一旦取り除く（重複登録防止）。
        for (size_t i = 0; i < m_Controllers.size(); ++i)
        {
            if (m_Controllers[i].Controller == controller)
            {
                NORVES_LOG_WARNING("Input", "RegisterController: '%s' は登録済み。優先度を %d へ更新する",
                                   controller->DebugName(), priority);
                m_Controllers.erase(m_Controllers.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }

        // 優先度降順・同優先度は登録順（後着は同優先度の末尾）を保つ挿入位置を探す。
        // priority より「真に小さい」最初の要素の手前に入れることで、
        // 同 priority の既存要素より後ろ＝登録順の安定性を担保する。
        size_t insertAt = m_Controllers.size();
        for (size_t i = 0; i < m_Controllers.size(); ++i)
        {
            if (m_Controllers[i].Priority < priority)
            {
                insertAt = i;
                break;
            }
        }

        Entry entry;
        entry.Controller = controller;
        entry.Priority = priority;
        m_Controllers.insert(m_Controllers.begin() + static_cast<ptrdiff_t>(insertAt), entry);
    }

    void InputRouter::UnregisterController(IInputController *controller)
    {
        if (controller == nullptr)
        {
            return;
        }

        for (size_t i = 0; i < m_Controllers.size(); ++i)
        {
            if (m_Controllers[i].Controller == controller)
            {
                m_Controllers.erase(m_Controllers.begin() + static_cast<ptrdiff_t>(i));
                return;
            }
        }
        // 未登録なら no-op（冪等）。
    }

    void InputRouter::DispatchMouseButton(const MouseButtonEvent &event)
    {
        // m_Controllers は優先度降順に維持されている。consume で打ち切る。
        for (Entry &entry : m_Controllers)
        {
            if (entry.Controller->OnMouseButton(event))
            {
                return;
            }
        }
    }

    void InputRouter::DispatchMouseMove(const MouseMoveEvent &event)
    {
        for (Entry &entry : m_Controllers)
        {
            if (entry.Controller->OnMouseMove(event))
            {
                return;
            }
        }
    }

    void InputRouter::DispatchMouseScroll(const MouseScrollEvent &event)
    {
        for (Entry &entry : m_Controllers)
        {
            if (entry.Controller->OnMouseScroll(event))
            {
                return;
            }
        }
    }

    void InputRouter::DispatchKey(const KeyEvent &event)
    {
        for (Entry &entry : m_Controllers)
        {
            if (entry.Controller->OnKey(event))
            {
                return;
            }
        }
    }

    void InputRouter::DispatchChar(const CharEvent &event)
    {
        for (Entry &entry : m_Controllers)
        {
            if (entry.Controller->OnChar(event))
            {
                return;
            }
        }
    }

} // namespace NorvesLib::Core::Input
