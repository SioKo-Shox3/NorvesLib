include(CTest)
add_test(NAME FixedStepSchedulerTest COMMAND ApplicationFixedStepPipelineTest --test=FixedStepSchedulerTest)


norves_add_test_bundle(ApplicationFixedStepPipelineTest
    FOLDER "Test/Core/Engine"
    MEMBER ApplicationFixedStepPipelineTest ApplicationFixedStepPipelineTest.cpp
    MEMBER FixedStepSchedulerTest FixedStepSchedulerTest.cpp
    MEMBER TimeScaleMathTest TimeScaleMathTest.cpp
    MEMBER TimeSystemTest TimeSystemTest.cpp
    MEMBER RawMouseMotionTrackerTest RawMouseMotionTrackerTest.cpp
    MEMBER WindowsKeyRepeatGateTest WindowsKeyRepeatGateTest.cpp
    MEMBER InputFocusPipelineTest InputFocusPipelineTest.cpp
    MEMBER InputFramePipelineTest InputFramePipelineTest.cpp
    MEMBER InputDeviceLifecycleTest InputDeviceLifecycleTest.cpp
    MEMBER HapticsFramePipelineTest HapticsFramePipelineTest.cpp
)
target_link_libraries(ApplicationFixedStepPipelineTest PRIVATE Core)
target_include_directories(ApplicationFixedStepPipelineTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(ApplicationFixedStepPipelineTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

add_test(NAME ApplicationFixedStepPipelineTest COMMAND ApplicationFixedStepPipelineTest)
add_test(NAME InputFramePipelineTest COMMAND ApplicationFixedStepPipelineTest --test=InputFramePipelineTest)
add_test(NAME InputFocusPipelineTest COMMAND ApplicationFixedStepPipelineTest --test=InputFocusPipelineTest)
add_test(NAME WindowsKeyRepeatGateTest COMMAND ApplicationFixedStepPipelineTest --test=WindowsKeyRepeatGateTest)
add_test(NAME RawMouseMotionTrackerTest COMMAND ApplicationFixedStepPipelineTest --test=RawMouseMotionTrackerTest)
add_test(NAME InputDeviceLifecycleTest COMMAND ApplicationFixedStepPipelineTest --test=InputDeviceLifecycleTest)
add_test(NAME HapticsFramePipelineTest COMMAND ApplicationFixedStepPipelineTest --test=HapticsFramePipelineTest)

add_test(NAME TimeScaleMathTest COMMAND ApplicationFixedStepPipelineTest --test=TimeScaleMathTest)
add_test(NAME TimeSystemTest COMMAND ApplicationFixedStepPipelineTest --test=TimeSystemTest)
