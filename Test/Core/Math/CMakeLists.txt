norves_add_test_bundle(MathGeometryTest
    FOLDER "Test/Core/Math"
    MEMBER MathGeometryTest MathGeometryTest.cpp
    MEMBER GeometryIntersectionExtraTest GeometryIntersectionExtraTest.cpp
    MEMBER GeometryContactTest GeometryContactTest.cpp
    MEMBER GeometrySeparationTest GeometrySeparationTest.cpp
    MEMBER GeometrySweepTest GeometrySweepTest.cpp
    MEMBER CurvesTest CurvesTest.cpp
    MEMBER MatrixAbstractionTest MatrixAbstractionTest.cpp
)
target_link_libraries(MathGeometryTest PRIVATE Core)
target_include_directories(MathGeometryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

include(CTest)
add_test(NAME MathGeometryTest COMMAND MathGeometryTest)

add_test(NAME GeometryIntersectionExtraTest COMMAND MathGeometryTest --test=GeometryIntersectionExtraTest)

add_test(NAME GeometryContactTest COMMAND MathGeometryTest --test=GeometryContactTest)
add_test(NAME GeometrySeparationTest COMMAND MathGeometryTest --test=GeometrySeparationTest)
add_test(NAME GeometrySweepTest COMMAND MathGeometryTest --test=GeometrySweepTest)

add_test(NAME MatrixAbstractionTest COMMAND MathGeometryTest --test=MatrixAbstractionTest)

add_test(NAME CurvesTest COMMAND MathGeometryTest --test=CurvesTest)
