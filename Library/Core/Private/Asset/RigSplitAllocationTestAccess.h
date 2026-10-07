#pragma once
// 新splitの確保直前だけを観測する限定probe。通常はthread-localのnullで何もしない。
namespace NorvesLib::Core::Skeletal::Detail
{
    using SplitAllocationProbe = void (*)(const char* stage, void* context);
    void SetSplitAllocationProbe(SplitAllocationProbe, void*) noexcept;
    void ObserveSplitAllocation(const char* stage);
} // namespace NorvesLib::Core::Skeletal::Detail
