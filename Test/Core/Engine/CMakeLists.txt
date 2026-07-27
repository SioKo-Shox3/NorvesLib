add_executable(FixedStepSchedulerTest
    FixedStepSchedulerTest.cpp
)
target_link_libraries(FixedStepSchedulerTest PRIVATE Core)
target_include_directories(FixedStepSchedulerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(FixedStepSchedulerTest PROPERTIES FOLDER "Test/Core/Engine")

include(CTest)
add_test(NAME FixedStepSchedulerTest COMMAND FixedStepSchedulerTest)
