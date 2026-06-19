#pragma once

#include "Container/Containers.h"
#include "Text/IdentityPool.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct RGNamedResourceVersionDiagnostic
    {
        Identity NamedResourceIdentity;
        bool BeforeVersionValid = false;
        uint32_t BeforeVersion = 0;
        uint32_t AfterVersion = 0;
        bool bCreatesNewHead = false;
        bool bMutatesCurrentHead = false;
    };

    struct RGDumpStrings
    {
        Container::String Text;
        Container::String Dot;
        Container::String Json;

        bool IsEmpty() const
        {
            return Text.empty() && Dot.empty() && Json.empty();
        }
    };

    using RenderGraphDumpData = RGDumpStrings;

    struct RGDumpOptions
    {
        bool bEnabled = false;
        bool bWriteFiles = false;
        bool bText = true;
        bool bDot = true;
        bool bJson = true;
        bool bDebugMarkers = false;
        uint32_t MaxFrameCount = 1;
        Container::String OutputDirectory;
        Container::String FilePrefix = "RenderGraph";
    };

    struct RenderGraphDebugStats
    {
        uint32_t DeclaredPassCount = 0;
        uint32_t CompiledPassCount = 0;
        uint32_t ExecutedPassCount = 0;
        uint32_t TransientAcquireCount = 0;
        uint32_t BarrierCount = 0;
    };

} // namespace NorvesLib::Core::Rendering
