#include "Asset/RigSplitAllocationTestAccess.h"
namespace NorvesLib::Core::Skeletal::Detail
{
    namespace
    {
        thread_local SplitAllocationProbe Probe = nullptr;
        thread_local void* Context = nullptr;
    } // namespace
    void SetSplitAllocationProbe(SplitAllocationProbe probe, void* context) noexcept
    {
        Probe = probe;
        Context = context;
    }
    void ObserveSplitAllocation(const char* stage)
    {
        if (Probe)
        {
            Probe(stage, Context);
        }
    }
} // namespace NorvesLib::Core::Skeletal::Detail
