#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Thread/Mutex.h"

#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t MaximumPublishedGPUTimingSamples = 4096u;

    struct RenderPassGPUTiming
    {
        uint64_t FrameNumber = 0u;
        Container::String PassName;
        float DurationMs = 0.0f;
        bool bValid = false;
    };

    namespace Detail
    {
        class GPUTimingMailbox
        {
        public:
            void Append(const Container::VariableArray<RenderPassGPUTiming>& timings)
            {
                Thread::ScopedLock lock(m_Mutex);
                for (const RenderPassGPUTiming& timing : timings)
                {
                    m_Timings.push_back(timing);
                }

                while (m_Timings.size() > MaximumPublishedGPUTimingSamples)
                {
                    uint64_t oldestFrameNumber = m_Timings.front().FrameNumber;
                    for (const RenderPassGPUTiming& timing : m_Timings)
                    {
                        if (timing.FrameNumber < oldestFrameNumber)
                        {
                            oldestFrameNumber = timing.FrameNumber;
                        }
                    }

                    auto destination = m_Timings.begin();
                    for (auto timing = m_Timings.begin(); timing != m_Timings.end(); ++timing)
                    {
                        if (timing->FrameNumber != oldestFrameNumber)
                        {
                            if (destination != timing)
                            {
                                *destination = std::move(*timing);
                            }
                            ++destination;
                        }
                    }
                    m_Timings.erase(destination, m_Timings.end());
                    ++m_DroppedFrameCount;
                }
            }

            bool Consume(Container::VariableArray<RenderPassGPUTiming>& outTimings,
                         uint64_t& outDroppedFrameCount)
            {
                Thread::ScopedLock lock(m_Mutex);
                outTimings = m_Timings;
                outDroppedFrameCount = m_DroppedFrameCount;
                m_Timings.clear();
                m_DroppedFrameCount = 0u;
                return !outTimings.empty() || outDroppedFrameCount > 0u;
            }

            void Clear()
            {
                Thread::ScopedLock lock(m_Mutex);
                m_Timings.clear();
                m_DroppedFrameCount = 0u;
            }

        private:
            Thread::Mutex m_Mutex;
            Container::VariableArray<RenderPassGPUTiming> m_Timings;
            uint64_t m_DroppedFrameCount = 0u;
        };
    } // namespace Detail
} // namespace NorvesLib::Core::Rendering
