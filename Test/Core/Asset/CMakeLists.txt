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
    MEMBER SkeletalMaterialBindingsTest SkeletalMaterialBindingsTest.cpp
    MEMBER SkeletalDrawRangeTest SkeletalDrawRangeTest.cpp
    MEMBER CookedSkeletalWireContractTest CookedSkeletalWireContractTest.cpp
    MEMBER CookedSkeletalNameCodecTest CookedSkeletalNameCodecTest.cpp
    MEMBER CookedMaterialFormatTest CookedMaterialFormatTest.cpp
    MEMBER CookedMeshWireContractTest CookedMeshWireContractTest.cpp
    MEMBER CookedMeshV1LayoutTest CookedMeshV1LayoutTest.cpp
    MEMBER ResourceIONamespaceCompileTest ResourceIONamespaceCompileTest.cpp
    MEMBER CookedMeshV1Test CookedMeshV1Test.cpp
    MEMBER MaterialImportPolicyTest MaterialImportPolicyTest.cpp
    MEMBER MaterialImportSettingsTest MaterialImportSettingsTest.cpp
    MEMBER MaterialImportSettingsJsonTest MaterialImportSettingsJsonTest.cpp
    MEMBER MaterialImportDocumentTest MaterialImportDocumentTest.cpp
    MEMBER SingleAssetCookServiceTest SingleAssetCookServiceTest.cpp
    MEMBER ImportedOpaqueRuntimeTest ImportedOpaqueRuntimeTest.cpp
    MEMBER ImportedMaterialStagingTest ImportedMaterialStagingTest.cpp
    MEMBER SkeletalJointIndexTest SkeletalJointIndexTest.cpp
    MEMBER SkeletalBindRowMathTest SkeletalBindRowMathTest.cpp
    MEMBER SkeletalJointGlobalRowMathTest SkeletalJointGlobalRowMathTest.cpp
    MEMBER SkeletalJointMappingTest SkeletalJointMappingTest.cpp
    MEMBER SkeletalCoordinateConversionTest SkeletalCoordinateConversionTest.cpp
    MEMBER SkeletalRotationRetargetTest SkeletalRotationRetargetTest.cpp
    MEMBER SkeletalRotationRetargetNativeTest SkeletalRotationRetargetNativeTest.cpp
    MEMBER SkeletalBvhClipTimeTest SkeletalBvhClipTimeTest.cpp
    MEMBER SkeletalBvhClipImportTest SkeletalBvhClipImportTest.cpp
    MEMBER SkeletalBvhCookBridgeTest SkeletalBvhCookBridgeTest.cpp
    MEMBER SkeletalAssetLoaderTest SkeletalAssetLoaderTest.cpp
    MEMBER SkeletalAssetPublicationTest SkeletalAssetPublicationTest.cpp
    MEMBER SkeletalAssetRuntimeTest SkeletalAssetRuntimeTest.cpp
    MEMBER SkeletalAssetSessionTest SkeletalAssetSessionTest.cpp
    MEMBER BvhEvaluateTest BvhEvaluateTest.cpp
    MEMBER BvhDecodeTest BvhDecodeTest.cpp
    MEMBER GltfMaterialCookV1Test GltfMaterialCookV1Test.cpp
    MEMBER TextureManagedCliTest TextureManagedCliTest.cpp
    MEMBER TextureAssetSetSpecTest TextureAssetSetSpecTest.cpp
    MEMBER TextureAssetSetOutputTest TextureAssetSetOutputTest.cpp
    MEMBER CookOutputSetGuardTest CookOutputSetGuardTest.cpp
    MEMBER CookOutputPlanTest CookOutputPlanTest.cpp
    MEMBER CookStateFileTest CookStateFileTest.cpp
    MEMBER CookManagedUpdateTest CookManagedUpdateTest.cpp
    MEMBER CookManagedBootstrapTest CookManagedBootstrapTest.cpp
    MEMBER CookManagedTransactionIntentTest CookManagedTransactionIntentTest.cpp
    MEMBER CookManagedStoreInitializationTest CookManagedStoreInitializationTest.cpp
    MEMBER CookManagedStoreObservationTest CookManagedStoreObservationTest.cpp
    MEMBER CookManagedUpdateInventoryTest CookManagedUpdateInventoryTest.cpp
    MEMBER CookDestinationLockTest CookDestinationLockTest.cpp
    MEMBER CookOwnerResolverTest CookOwnerResolverTest.cpp
    MEMBER CookOwnerBindingTest CookOwnerBindingTest.cpp
    MEMBER CookOwnedStateTest CookOwnedStateTest.cpp
    MEMBER ModelNativePathTest ModelNativePathTest.cpp
    MEMBER CookCacheDecisionTest CookCacheDecisionTest.cpp
    MEMBER CookOutputPackageTest CookOutputPackageTest.cpp
    MEMBER CookDependencySnapshotTest CookDependencySnapshotTest.cpp
    MEMBER ArmModeTokenTest ArmModeTokenTest.cpp
    MEMBER UnicodeTextTest UnicodeTextTest.cpp
    MEMBER StringEmptyTest ${CMAKE_SOURCE_DIR}/Test/Core/Text/StringEmptyTest.cpp
    MEMBER JsonUnicodeInputTest JsonUnicodeInputTest.cpp
    MEMBER MaterialSelectionTest MaterialSelectionTest.cpp
    MEMBER ArmChannelFoldTest ArmChannelFoldTest.cpp
    MEMBER GltfMaterialSourceTest GltfMaterialSourceTest.cpp
    MEMBER GltfMaterialSourceJsonTest GltfMaterialSourceJsonTest.cpp
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
add_test(NAME SkeletalMaterialBindingsTest COMMAND CookedMeshTest --test=SkeletalMaterialBindingsTest)
add_test(NAME SkeletalDrawRangeTest COMMAND CookedMeshTest --test=SkeletalDrawRangeTest)
add_test(NAME CookedSkeletalWireContractTest COMMAND CookedMeshTest --test=CookedSkeletalWireContractTest)
add_test(NAME CookedSkeletalNameCodecTest COMMAND CookedMeshTest --test=CookedSkeletalNameCodecTest)
add_test(NAME CookedMaterialFormatTest COMMAND CookedMeshTest --test=CookedMaterialFormatTest)
add_test(NAME CookedMeshWireContractTest COMMAND CookedMeshTest --test=CookedMeshWireContractTest)
add_test(NAME CookedMeshV1LayoutTest COMMAND CookedMeshTest --test=CookedMeshV1LayoutTest)
add_test(NAME CookedMeshV1Test COMMAND CookedMeshTest --test=CookedMeshV1Test)
add_test(NAME MaterialImportPolicyTest COMMAND CookedMeshTest --test=MaterialImportPolicyTest)

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

add_test(NAME ResourceIONamespaceCompileTest COMMAND CookedMeshTest --test=ResourceIONamespaceCompileTest)

add_test(NAME MaterialImportSettingsTest COMMAND CookedMeshTest --test=MaterialImportSettingsTest)

add_test(NAME MaterialImportSettingsJsonTest COMMAND CookedMeshTest --test=MaterialImportSettingsJsonTest)
add_test(NAME ArmModeTokenTest COMMAND CookedMeshTest --test=ArmModeTokenTest)

add_test(NAME GltfMaterialSourceTest COMMAND CookedMeshTest --test=GltfMaterialSourceTest)
add_test(NAME GltfMaterialSourceJsonTest COMMAND CookedMeshTest --test=GltfMaterialSourceJsonTest)

add_test(NAME ArmChannelFoldTest COMMAND CookedMeshTest --test=ArmChannelFoldTest)

add_test(NAME MaterialSelectionTest COMMAND CookedMeshTest --test=MaterialSelectionTest)

add_test(NAME UnicodeTextTest COMMAND CookedMeshTest --test=UnicodeTextTest)
add_test(NAME JsonUnicodeInputTest COMMAND CookedMeshTest --test=JsonUnicodeInputTest)

add_test(NAME MaterialImportDocumentTest COMMAND CookedMeshTest --test=MaterialImportDocumentTest)

add_test(NAME SingleAssetCookServiceTest COMMAND CookedMeshTest --test=SingleAssetCookServiceTest)

add_test(NAME TextureAssetSetSpecTest COMMAND CookedMeshTest --test=TextureAssetSetSpecTest)
set_tests_properties(TextureAssetSetSpecTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME TextureAssetSetOutputTest COMMAND CookedMeshTest --test=TextureAssetSetOutputTest)

add_test(NAME CookDependencySnapshotTest COMMAND CookedMeshTest --test=CookDependencySnapshotTest)
set_tests_properties(CookDependencySnapshotTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME CookOutputPackageTest COMMAND CookedMeshTest --test=CookOutputPackageTest)
set_tests_properties(CookOutputPackageTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME CookCacheDecisionTest COMMAND CookedMeshTest --test=CookCacheDecisionTest)
set_tests_properties(CookCacheDecisionTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME ModelNativePathTest COMMAND CookedMeshTest --test=ModelNativePathTest)
set_tests_properties(ModelNativePathTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME CookOwnedStateTest COMMAND CookedMeshTest --test=CookOwnedStateTest)
set_tests_properties(CookOwnedStateTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME CookStateFileTest COMMAND CookedMeshTest --test=CookStateFileTest)
set_tests_properties(CookStateFileTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME CookOutputPlanTest COMMAND CookedMeshTest --test=CookOutputPlanTest)
set_tests_properties(CookOutputPlanTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME CookOutputSetGuardTest COMMAND CookedMeshTest --test=CookOutputSetGuardTest)
set_tests_properties(CookOutputSetGuardTest PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME CookOwnerBindingTest COMMAND CookedMeshTest --test=CookOwnerBindingTest)

add_test(NAME CookOwnerResolverTest COMMAND CookedMeshTest --test=CookOwnerResolverTest)

add_test(NAME CookDestinationLockTest COMMAND CookedMeshTest --test=CookDestinationLockTest)

add_test(NAME CookManagedUpdateInventoryTest COMMAND CookedMeshTest --test=CookManagedUpdateInventoryTest)

add_test(NAME CookManagedStoreObservationTest COMMAND CookedMeshTest --test=CookManagedStoreObservationTest)

add_test(NAME CookManagedStoreInitializationTest COMMAND CookedMeshTest --test=CookManagedStoreInitializationTest)

add_test(NAME CookManagedTransactionIntentTest COMMAND CookedMeshTest --test=CookManagedTransactionIntentTest)

add_test(NAME CookManagedBootstrapTest COMMAND CookedMeshTest --test=CookManagedBootstrapTest)

add_test(NAME CookManagedUpdateTest COMMAND CookedMeshTest --test=CookManagedUpdateTest)

add_test(NAME TextureManagedCliTest COMMAND CookedMeshTest --test=TextureManagedCliTest)

add_test(NAME GltfMaterialCookV1Test COMMAND CookedMeshTest --test=GltfMaterialCookV1Test)

add_test(NAME ImportedMaterialStagingTest COMMAND CookedMeshTest --test=ImportedMaterialStagingTest)

add_test(NAME ImportedOpaqueRuntimeTest COMMAND CookedMeshTest --test=ImportedOpaqueRuntimeTest)

add_test(NAME BvhDecodeTest COMMAND CookedMeshTest --test=BvhDecodeTest)

add_test(NAME BvhEvaluateTest COMMAND CookedMeshTest --test=BvhEvaluateTest)

add_test(NAME SkeletalJointIndexTest COMMAND CookedMeshTest --test=SkeletalJointIndexTest)
add_test(NAME SkeletalBindRowMathTest COMMAND CookedMeshTest --test=SkeletalBindRowMathTest)
add_test(NAME SkeletalJointGlobalRowMathTest COMMAND CookedMeshTest --test=SkeletalJointGlobalRowMathTest)
add_test(NAME SkeletalJointMappingTest COMMAND CookedMeshTest --test=SkeletalJointMappingTest)
add_test(NAME SkeletalCoordinateConversionTest COMMAND CookedMeshTest --test=SkeletalCoordinateConversionTest)
add_test(NAME SkeletalRotationRetargetTest COMMAND CookedMeshTest --test=SkeletalRotationRetargetTest)
add_test(NAME SkeletalRotationRetargetNativeTest COMMAND CookedMeshTest --test=SkeletalRotationRetargetNativeTest)
add_test(NAME SkeletalBvhClipTimeTest COMMAND CookedMeshTest --test=SkeletalBvhClipTimeTest)
add_test(NAME SkeletalBvhClipImportTest COMMAND CookedMeshTest --test=SkeletalBvhClipImportTest)
add_test(NAME SkeletalBvhCookBridgeTest COMMAND CookedMeshTest --test=SkeletalBvhCookBridgeTest)
add_test(NAME SkeletalAssetLoaderTest COMMAND CookedMeshTest --test=SkeletalAssetLoaderTest)
add_test(NAME SkeletalAssetPublicationTest COMMAND CookedMeshTest --test=SkeletalAssetPublicationTest)
add_test(NAME SkeletalAssetRuntimeTest COMMAND CookedMeshTest --test=SkeletalAssetRuntimeTest)
add_test(NAME SkeletalAssetSessionTest COMMAND CookedMeshTest --test=SkeletalAssetSessionTest)

# M9の実consumer helperをCPU owner試験でも使う。
target_sources(CookedMeshTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M9SkeletalPreparation.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M9WorldSkeletal.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M9WorldAcceptance.cpp
)
target_include_directories(CookedMeshTest PRIVATE ${CMAKE_SOURCE_DIR}/Game)
