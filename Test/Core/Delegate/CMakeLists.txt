add_executable(DelegateCopyTest
    DelegateCopyTest.cpp
)
target_link_libraries(DelegateCopyTest PRIVATE Core)
target_include_directories(DelegateCopyTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DelegateCopyTest PROPERTIES FOLDER "Test/Core/Delegate")

include(CTest)
add_test(NAME DelegateCopyTest COMMAND DelegateCopyTest)
