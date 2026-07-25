add_executable(MemorySystemBootstrapTest
    MemorySystemBootstrapTest.cpp
)
target_link_libraries(MemorySystemBootstrapTest PRIVATE Core)
set_target_properties(MemorySystemBootstrapTest PROPERTIES FOLDER "Test/Core/Memory")

include(CTest)
add_test(NAME MemorySystemBootstrapTest COMMAND MemorySystemBootstrapTest)
