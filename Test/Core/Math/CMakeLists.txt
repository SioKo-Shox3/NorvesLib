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

add_executable(GeometryIntersectionExtraTest
    GeometryIntersectionExtraTest.cpp
)
target_link_libraries(GeometryIntersectionExtraTest PRIVATE Core)
target_include_directories(GeometryIntersectionExtraTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(GeometryIntersectionExtraTest PROPERTIES FOLDER "Test/Core/Math")
add_test(NAME GeometryIntersectionExtraTest COMMAND GeometryIntersectionExtraTest)

add_executable(MatrixAbstractionTest
    MatrixAbstractionTest.cpp
)
target_link_libraries(MatrixAbstractionTest PRIVATE Core)
target_include_directories(MatrixAbstractionTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MatrixAbstractionTest PROPERTIES FOLDER "Test/Core/Math")
add_test(NAME MatrixAbstractionTest COMMAND MatrixAbstractionTest)
