add_executable(InputRouterTest
    InputRouterTest.cpp
)
target_link_libraries(InputRouterTest PRIVATE Core)
target_include_directories(InputRouterTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(InputRouterTest PROPERTIES FOLDER "Test/Core/Input")

include(CTest)
add_test(NAME InputRouterTest COMMAND InputRouterTest)
