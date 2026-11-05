norves_add_test_bundle(MathGeometryTest
    FOLDER "Test/Core/Math"
    MEMBER MathGeometryTest MathGeometryTest.cpp
    MEMBER GeometryIntersectionExtraTest GeometryIntersectionExtraTest.cpp
    MEMBER GeometryContactTest GeometryContactTest.cpp
    MEMBER GeometrySeparationTest GeometrySeparationTest.cpp
    MEMBER GeometrySweepTest GeometrySweepTest.cpp
    MEMBER CurvesTest CurvesTest.cpp
    MEMBER GrassFieldQueryTest ${CMAKE_SOURCE_DIR}/Test/Core/Vegetation/GrassFieldQueryTest.cpp
    MEMBER TerrainHeightFieldTest ${CMAKE_SOURCE_DIR}/Test/Core/Terrain/TerrainHeightFieldTest.cpp
    MEMBER TerrainHeightFieldQueryTest ${CMAKE_SOURCE_DIR}/Test/Core/Terrain/TerrainHeightFieldQueryTest.cpp
    MEMBER MatrixAbstractionTest MatrixAbstractionTest.cpp
)
target_link_libraries(MathGeometryTest PRIVATE Core AssetCookLib NorvesThirdParty_TinyEXR)
target_include_directories(MathGeometryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/tinyexr/include
)

include(CTest)
add_test(NAME MathGeometryTest COMMAND MathGeometryTest)

add_test(NAME GeometryIntersectionExtraTest COMMAND MathGeometryTest --test=GeometryIntersectionExtraTest)

add_test(NAME GeometryContactTest COMMAND MathGeometryTest --test=GeometryContactTest)
add_test(NAME GeometrySeparationTest COMMAND MathGeometryTest --test=GeometrySeparationTest)
add_test(NAME GeometrySweepTest COMMAND MathGeometryTest --test=GeometrySweepTest)

add_test(NAME MatrixAbstractionTest COMMAND MathGeometryTest --test=MatrixAbstractionTest)

add_test(NAME CurvesTest COMMAND MathGeometryTest --test=CurvesTest)
add_test(NAME GrassFieldQueryTest COMMAND MathGeometryTest --test=GrassFieldQueryTest)
add_test(NAME TerrainHeightFieldTest COMMAND MathGeometryTest --test=TerrainHeightFieldTest)
add_test(NAME TerrainHeightFieldQueryTest COMMAND MathGeometryTest --test=TerrainHeightFieldQueryTest)
