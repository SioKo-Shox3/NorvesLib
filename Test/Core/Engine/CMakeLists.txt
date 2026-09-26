include(CTest)
add_test(NAME FixedStepSchedulerTest COMMAND ApplicationFixedStepPipelineTest --test=FixedStepSchedulerTest)


norves_add_test_bundle(ApplicationFixedStepPipelineTest
    FOLDER "Test/Core/Engine"
    MEMBER ApplicationFixedStepPipelineTest ApplicationFixedStepPipelineTest.cpp
    MEMBER FixedStepSchedulerTest FixedStepSchedulerTest.cpp
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
