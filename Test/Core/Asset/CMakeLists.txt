norves_add_test_bundle(AssetSystemTest
    FOLDER "Test/Core/Asset"
    MEMBER AssetSystemTest AssetSystemTest.cpp
    MEMBER AssetBlobLifetimeTest AssetBlobLifetimeTest.cpp
    MEMBER AssetPathTest AssetPathTest.cpp
    MEMBER AssetFileReaderTest AssetFileReaderTest.cpp
    MEMBER PackageFormatTest PackageFormatTest.cpp
    MEMBER AssetManifestTest AssetManifestTest.cpp
)
target_link_libraries(AssetSystemTest PRIVATE Core)
target_include_directories(AssetSystemTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

norves_add_test_bundle(CookedMeshTest
    FOLDER "Test/Core/Asset"
    MEMBER CookedMeshTest CookedMeshTest.cpp
    MEMBER CookedTextureTest CookedTextureTest.cpp
    MEMBER AudioCookedAssetTest AudioCookedAssetTest.cpp
    MEMBER CookedSkeletalAssetTest CookedSkeletalAssetTest.cpp
)
target_link_libraries(CookedMeshTest PRIVATE Core)
target_include_directories(CookedMeshTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_dependencies(CookedMeshTest AssetCook)

include(CTest)
add_test(NAME AssetBlobLifetimeTest COMMAND AssetSystemTest --test=AssetBlobLifetimeTest)
add_test(NAME AssetPathTest COMMAND AssetSystemTest --test=AssetPathTest)
add_test(NAME AssetFileReaderTest COMMAND AssetSystemTest --test=AssetFileReaderTest)
add_test(NAME PackageFormatTest COMMAND AssetSystemTest --test=PackageFormatTest)
add_test(NAME AssetManifestTest COMMAND AssetSystemTest --test=AssetManifestTest)
add_test(NAME AssetSystemTest COMMAND AssetSystemTest)
add_test(NAME CookedTextureTest COMMAND CookedMeshTest --test=CookedTextureTest)
add_test(NAME CookedMeshTest COMMAND CookedMeshTest)

add_test(NAME AudioCookedAssetTest COMMAND CookedMeshTest --test=AudioCookedAssetTest)

set(COOKED_SKELETAL_ASSET_SMOKE_DIR
    "${CMAKE_BINARY_DIR}/CookedSkeletalAssetTest/$<CONFIG>")
add_test(NAME CookedSkeletalAssetTest
    COMMAND ${CMAKE_COMMAND}
        "-DUNIT_TEST_EXE=$<TARGET_FILE:CookedMeshTest>"
        "-DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>"
        "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DFIXTURE_GLTF=${CMAKE_SOURCE_DIR}/Assets/Models/M9Skinned/ValidU8Float.gltf"
        "-DSMOKE_DIR=${COOKED_SKELETAL_ASSET_SMOKE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CookedSkeletalAssetTest.cmake.in"
)
set_property(TEST CookedSkeletalAssetTest APPEND PROPERTY ENVIRONMENT "NORVES_TEST_BUNDLE_MEMBER=CookedSkeletalAssetTest")
