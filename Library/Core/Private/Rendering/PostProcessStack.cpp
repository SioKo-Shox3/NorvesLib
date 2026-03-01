#include "Rendering/PostProcessStack.h"
#include "Rendering/ViewRenderContext.h"
#include "Logging/LogMacros.h"
#include <cstring>

namespace NorvesLib::Core::Rendering
{

    PostProcessStack::~PostProcessStack()
    {
        Shutdown();
    }

    bool PostProcessStack::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        // 既に追加されているパスを初期化
        for (auto &pass : m_Passes)
        {
            if (pass && !pass->IsInitialized())
            {
                if (!pass->Initialize(context))
                {
                    NORVES_LOG_ERROR("PostProcessStack", "パス '%s' の初期化に失敗しました", pass->GetName());
                    return false;
                }
            }
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("PostProcessStack", "PostProcessStack initialized (%u passes)", GetPassCount());
        return true;
    }

    void PostProcessStack::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        for (auto &pass : m_Passes)
        {
            if (pass && pass->IsInitialized())
            {
                pass->Shutdown();
            }
        }
        m_Passes.clear();

        m_bInitialized = false;
        NORVES_LOG_INFO("PostProcessStack", "PostProcessStack shutdown");
    }

    // ========================================
    // パス管理
    // ========================================

    void PostProcessStack::AddPass(TUniquePtr<IViewPass> pass)
    {
        if (!pass)
        {
            return;
        }

        NORVES_LOG_INFO("PostProcessStack", "パス '%s' を追加しました", pass->GetName());
        m_Passes.push_back(std::move(pass));
    }

    void PostProcessStack::InsertPass(uint32_t index, TUniquePtr<IViewPass> pass)
    {
        if (!pass)
        {
            return;
        }

        if (index >= m_Passes.size())
        {
            m_Passes.push_back(std::move(pass));
        }
        else
        {
            m_Passes.insert(m_Passes.begin() + index, std::move(pass));
        }
    }

    void PostProcessStack::RemovePass(const char *name)
    {
        auto it = std::remove_if(m_Passes.begin(), m_Passes.end(),
                                 [name](const TUniquePtr<IViewPass> &pass)
                                 {
                                     return pass && std::strcmp(pass->GetName(), name) == 0;
                                 });
        if (it != m_Passes.end())
        {
            // シャットダウンしてから削除
            for (auto removeIt = it; removeIt != m_Passes.end(); ++removeIt)
            {
                if (*removeIt && (*removeIt)->IsInitialized())
                {
                    (*removeIt)->Shutdown();
                }
            }
            m_Passes.erase(it, m_Passes.end());
            NORVES_LOG_INFO("PostProcessStack", "パス '%s' を削除しました", name);
        }
    }

    void PostProcessStack::SetPassEnabled(const char *name, bool bEnabled)
    {
        for (auto &pass : m_Passes)
        {
            if (pass && std::strcmp(pass->GetName(), name) == 0)
            {
                pass->SetEnabled(bEnabled);
                return;
            }
        }
    }

    IViewPass *PostProcessStack::GetPass(const char *name) const
    {
        for (const auto &pass : m_Passes)
        {
            if (pass && std::strcmp(pass->GetName(), name) == 0)
            {
                return pass.get();
            }
        }
        return nullptr;
    }

    uint32_t PostProcessStack::GetEnabledPassCount() const
    {
        uint32_t count = 0;
        for (const auto &pass : m_Passes)
        {
            if (pass && pass->IsEnabled())
            {
                ++count;
            }
        }
        return count;
    }

    // ========================================
    // 実行
    // ========================================

    void PostProcessStack::Setup(ViewRenderContext &context)
    {
        for (auto &pass : m_Passes)
        {
            if (pass && pass->IsEnabled() && pass->IsInitialized())
            {
                pass->Setup(context);
            }
        }
    }

    void PostProcessStack::Execute(ViewRenderContext &context)
    {
        for (auto &pass : m_Passes)
        {
            if (pass && pass->IsEnabled() && pass->IsInitialized())
            {
                pass->Execute(context);
            }
        }
    }

} // namespace NorvesLib::Core::Rendering
