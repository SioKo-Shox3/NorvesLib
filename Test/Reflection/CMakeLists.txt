add_executable(StructPropertySerializeTest
    StructPropertySerializeTest.cpp
)
target_link_libraries(StructPropertySerializeTest PRIVATE Core)
target_include_directories(StructPropertySerializeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(StructPropertySerializeTest PROPERTIES FOLDER "Test/Reflection")

include(CTest)
add_test(NAME StructPropertySerializeTest COMMAND StructPropertySerializeTest)
