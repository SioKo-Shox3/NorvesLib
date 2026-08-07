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

add_executable(ApplicationFixedStepPipelineTest
    ApplicationFixedStepPipelineTest.cpp
)
target_link_libraries(ApplicationFixedStepPipelineTest PRIVATE Core)
target_include_directories(ApplicationFixedStepPipelineTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(ApplicationFixedStepPipelineTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(ApplicationFixedStepPipelineTest PROPERTIES FOLDER "Test/Core/Engine")

add_executable(ApplicationFixedStepTickSourceContractTest
    ApplicationFixedStepTickSourceContractTest.cpp
)
target_link_libraries(ApplicationFixedStepTickSourceContractTest PRIVATE Core)
target_include_directories(ApplicationFixedStepTickSourceContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ApplicationFixedStepTickSourceContractTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(ApplicationFixedStepTickSourceContractTest PROPERTIES FOLDER "Test/Core/Engine")

add_test(NAME ApplicationFixedStepPipelineTest COMMAND ApplicationFixedStepPipelineTest)
add_test(NAME ApplicationFixedStepTickSourceContractTest COMMAND ApplicationFixedStepTickSourceContractTest)
