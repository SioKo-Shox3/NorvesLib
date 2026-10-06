#pragma once

#include "RHI/ICommandList.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief コマンドリストの GPU タイムスタンプの区間を、スコープの寿命で開閉する
     *
     * 統計が有効な構成では trace の Type=GPU 行（metrics.json の pass_median_ms）になる。それ以外では何もしない。
     * scopeName は区間の名前で、同じフレームに同じ名前を複数回開くと内訳に合算される。
     */
    class ScopedGpuTimestamp
    {
    public:
        ScopedGpuTimestamp(RHI::ICommandList *commandList, const char *scopeName)
            : m_CommandList(commandList)
        {
            if (m_CommandList)
            {
                m_Handle = m_CommandList->BeginGPUTimestampScope(scopeName);
            }
        }

        ~ScopedGpuTimestamp()
        {
            if (m_CommandList && m_Handle.IsValid())
            {
                m_CommandList->EndGPUTimestampScope(m_Handle);
            }
        }

        ScopedGpuTimestamp(const ScopedGpuTimestamp &) = delete;
        ScopedGpuTimestamp &operator=(const ScopedGpuTimestamp &) = delete;

    private:
        RHI::ICommandList *m_CommandList = nullptr;
        RHI::GPUTimestampScopeHandle m_Handle;
    };
} // namespace NorvesLib::Core::Rendering
