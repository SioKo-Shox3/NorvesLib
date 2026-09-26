#include "Rendering/FrameCaptureAssignmentGuard.h"

#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    int RunFrameCaptureAssignmentGuardTestSuite();
}

int main()
{
    static_assert(!std::is_copy_constructible_v<
                  NorvesLib::Core::Rendering::FrameCaptureAssignmentGuard>);
    static_assert(!std::is_move_constructible_v<
                  NorvesLib::Core::Rendering::FrameCaptureAssignmentGuard>);
    return NorvesLib::Core::Rendering::RunFrameCaptureAssignmentGuardTestSuite();
}
