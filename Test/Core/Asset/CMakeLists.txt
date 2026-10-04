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
    MEMBER ImportStaticIntegrationTest ImportStaticIntegrationTest.cpp
    MEMBER ImportSettingsHashTest ImportSettingsHashTest.cpp
    MEMBER ImportSettingsFileTest ImportSettingsFileTest.cpp
    MEMBER ImportTransformTest ImportTransformTest.cpp
    MEMBER ImportCliOptionsTest ImportCliOptionsTest.cpp
    MEMBER SkeletalCliOptionsTest SkeletalCliOptionsTest.cpp
    MEMBER SkeletalImportReportTest SkeletalImportReportTest.cpp
    MEMBER SkeletalCubicBoundsTest SkeletalCubicBoundsTest.cpp
    MEMBER SkeletalCubicBakeTest SkeletalCubicBakeTest.cpp
    MEMBER SkeletalInfluenceAttributeTest SkeletalInfluenceAttributeTest.cpp
    MEMBER SkeletalInfluenceSetPairTest SkeletalInfluenceSetPairTest.cpp
    MEMBER SkeletalInfluenceSetsJsonTest SkeletalInfluenceSetsJsonTest.cpp
    MEMBER SkeletalInfluenceReductionTest SkeletalInfluenceReductionTest.cpp
    MEMBER SkeletalImportPolicyTest SkeletalImportPolicyTest.cpp
    MEMBER SkeletalSubmeshLayoutTest SkeletalSubmeshLayoutTest.cpp
    MEMBER CookedSkeletalWireContractTest CookedSkeletalWireContractTest.cpp
    MEMBER CookedSkeletalNameCodecTest CookedSkeletalNameCodecTest.cpp
    MEMBER GeometryInspectionTest GeometryInspectionTest.cpp
    MEMBER ImageInspectionTest ImageInspectionTest.cpp
    MEMBER ImportSettingsValueTest ImportSettingsValueTest.cpp
    MEMBER ImportSettingsJsonTest ImportSettingsJsonTest.cpp
    MEMBER GltfLooseSourceTest GltfLooseSourceTest.cpp
    MEMBER GltfDocumentProfileTest GltfDocumentProfileTest.cpp
    MEMBER GltfAccessorStagingTest GltfAccessorStagingTest.cpp
    MEMBER GltfAccessorRangeTest GltfAccessorRangeTest.cpp
    MEMBER ImageBytesStagingTest ImageBytesStagingTest.cpp
    MEMBER GltfImageRangeTest GltfImageRangeTest.cpp
    MEMBER GltfImageSourceTest GltfImageSourceTest.cpp
    MEMBER StaticGltfBufferCookTest StaticGltfBufferCookTest.cpp
    MEMBER GltfContainerTest GltfContainerTest.cpp
    MEMBER GltfBufferSourceTest GltfBufferSourceTest.cpp
    MEMBER GltfBufferSetTest GltfBufferSetTest.cpp
    MEMBER GltfBufferJsonTest GltfBufferJsonTest.cpp
    MEMBER GltfBufferNumberTest GltfBufferNumberTest.cpp
    MEMBER GltfBufferUriTest GltfBufferUriTest.cpp
    MEMBER Base64DecodeTest Base64DecodeTest.cpp
    MEMBER CookedTextureTest CookedTextureTest.cpp
    MEMBER AudioCookedAssetTest AudioCookedAssetTest.cpp
    MEMBER CookedSkeletalAssetTest CookedSkeletalAssetTest.cpp
)
target_link_libraries(CookedMeshTest PRIVATE Core AssetCookLib)
target_include_directories(CookedMeshTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
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
add_test(NAME SkeletalSubmeshLayoutTest COMMAND CookedMeshTest --test=SkeletalSubmeshLayoutTest)
add_test(NAME CookedSkeletalWireContractTest COMMAND CookedMeshTest --test=CookedSkeletalWireContractTest)
add_test(NAME CookedSkeletalNameCodecTest COMMAND CookedMeshTest --test=CookedSkeletalNameCodecTest)

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

add_test(NAME GltfContainerTest COMMAND CookedMeshTest --test=GltfContainerTest)
add_test(NAME Base64DecodeTest COMMAND CookedMeshTest --test=Base64DecodeTest)
add_test(NAME GltfBufferSourceTest COMMAND CookedMeshTest --test=GltfBufferSourceTest)
add_test(NAME GltfBufferSetTest COMMAND CookedMeshTest --test=GltfBufferSetTest)
add_test(NAME GltfBufferUriTest COMMAND CookedMeshTest --test=GltfBufferUriTest)
add_test(NAME GltfBufferJsonTest COMMAND CookedMeshTest --test=GltfBufferJsonTest)
add_test(NAME GltfBufferNumberTest COMMAND CookedMeshTest --test=GltfBufferNumberTest)

add_test(NAME StaticGltfBufferCookTest COMMAND CookedMeshTest --test=StaticGltfBufferCookTest)

add_test(NAME GltfImageRangeTest COMMAND CookedMeshTest --test=GltfImageRangeTest)
add_test(NAME GltfImageSourceTest COMMAND CookedMeshTest --test=GltfImageSourceTest)

add_test(NAME GltfAccessorRangeTest COMMAND CookedMeshTest --test=GltfAccessorRangeTest)

add_test(NAME GltfAccessorStagingTest COMMAND CookedMeshTest --test=GltfAccessorStagingTest)

add_test(NAME ImageBytesStagingTest COMMAND CookedMeshTest --test=ImageBytesStagingTest)

add_test(NAME GltfDocumentProfileTest COMMAND CookedMeshTest --test=GltfDocumentProfileTest)

add_test(NAME GltfLooseSourceTest COMMAND CookedMeshTest --test=GltfLooseSourceTest)

add_test(NAME ImportSettingsValueTest COMMAND CookedMeshTest --test=ImportSettingsValueTest)
add_test(NAME ImportSettingsJsonTest COMMAND CookedMeshTest --test=ImportSettingsJsonTest)

add_test(NAME ImportTransformTest COMMAND CookedMeshTest --test=ImportTransformTest)

add_test(NAME ImportSettingsHashTest COMMAND CookedMeshTest --test=ImportSettingsHashTest)
add_test(NAME ImportSettingsFileTest COMMAND CookedMeshTest --test=ImportSettingsFileTest)

add_test(NAME ImportStaticIntegrationTest COMMAND CookedMeshTest --test=ImportStaticIntegrationTest)

add_test(NAME ImportCliOptionsTest COMMAND CookedMeshTest --test=ImportCliOptionsTest)

add_test(NAME GeometryInspectionTest COMMAND CookedMeshTest --test=GeometryInspectionTest)

add_test(NAME ImageInspectionTest COMMAND CookedMeshTest --test=ImageInspectionTest)

add_test(NAME SkeletalInfluenceAttributeTest COMMAND CookedMeshTest --test=SkeletalInfluenceAttributeTest)

add_test(NAME SkeletalInfluenceReductionTest COMMAND CookedMeshTest --test=SkeletalInfluenceReductionTest)

add_test(NAME SkeletalImportPolicyTest COMMAND CookedMeshTest --test=SkeletalImportPolicyTest)

add_test(NAME SkeletalInfluenceSetPairTest COMMAND CookedMeshTest --test=SkeletalInfluenceSetPairTest)
add_test(NAME SkeletalInfluenceSetsJsonTest COMMAND CookedMeshTest --test=SkeletalInfluenceSetsJsonTest)

add_test(NAME SkeletalCliOptionsTest COMMAND CookedMeshTest --test=SkeletalCliOptionsTest)

add_test(NAME SkeletalImportReportTest COMMAND CookedMeshTest --test=SkeletalImportReportTest)

add_test(NAME SkeletalCubicBoundsTest COMMAND CookedMeshTest --test=SkeletalCubicBoundsTest)

add_test(NAME SkeletalCubicBakeTest COMMAND CookedMeshTest --test=SkeletalCubicBakeTest)
