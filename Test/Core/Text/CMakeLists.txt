add_executable(IdentityLiteralTest
    IdentityLiteralTest.cpp
)
target_link_libraries(IdentityLiteralTest PRIVATE Core)
target_include_directories(IdentityLiteralTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(IdentityLiteralTest PROPERTIES FOLDER "Test/Core/Text")

add_executable(JsonWriterTest
    JsonWriterTest.cpp
)
target_link_libraries(JsonWriterTest PRIVATE Core)
target_include_directories(JsonWriterTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(JsonWriterTest PROPERTIES FOLDER "Test/Core/Text")

include(CTest)
add_test(NAME IdentityLiteralTest COMMAND IdentityLiteralTest)
add_test(NAME JsonWriterTest COMMAND JsonWriterTest)
