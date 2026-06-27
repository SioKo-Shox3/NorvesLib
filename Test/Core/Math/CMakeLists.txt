add_executable(MathGeometryTest
    MathGeometryTest.cpp
)
target_link_libraries(MathGeometryTest PRIVATE Core)
target_include_directories(MathGeometryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MathGeometryTest PROPERTIES FOLDER "Test/Core/Math")

include(CTest)
add_test(NAME MathGeometryTest COMMAND MathGeometryTest)
