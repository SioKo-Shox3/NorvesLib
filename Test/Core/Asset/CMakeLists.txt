add_executable(AssetBlobLifetimeTest
    AssetBlobLifetimeTest.cpp
)
target_link_libraries(AssetBlobLifetimeTest PRIVATE Core)
target_include_directories(AssetBlobLifetimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(AssetBlobLifetimeTest PROPERTIES FOLDER "Test/Core/Asset")

add_executable(AssetPathTest
    AssetPathTest.cpp
)
target_link_libraries(AssetPathTest PRIVATE Core)
target_include_directories(AssetPathTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(AssetPathTest PROPERTIES FOLDER "Test/Core/Asset")

add_executable(AssetFileReaderTest
    AssetFileReaderTest.cpp
)
target_link_libraries(AssetFileReaderTest PRIVATE Core)
target_include_directories(AssetFileReaderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(AssetFileReaderTest PROPERTIES FOLDER "Test/Core/Asset")

add_executable(PackageFormatTest
    PackageFormatTest.cpp
)
target_link_libraries(PackageFormatTest PRIVATE Core)
target_include_directories(PackageFormatTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(PackageFormatTest PROPERTIES FOLDER "Test/Core/Asset")

add_executable(AssetManifestTest
    AssetManifestTest.cpp
)
target_link_libraries(AssetManifestTest PRIVATE Core)
target_include_directories(AssetManifestTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(AssetManifestTest PROPERTIES FOLDER "Test/Core/Asset")

add_executable(AssetSystemTest
    AssetSystemTest.cpp
)
target_link_libraries(AssetSystemTest PRIVATE Core)
target_include_directories(AssetSystemTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(AssetSystemTest PROPERTIES FOLDER "Test/Core/Asset")

include(CTest)
add_test(NAME AssetBlobLifetimeTest COMMAND AssetBlobLifetimeTest)
add_test(NAME AssetPathTest COMMAND AssetPathTest)
add_test(NAME AssetFileReaderTest COMMAND AssetFileReaderTest)
add_test(NAME PackageFormatTest COMMAND PackageFormatTest)
add_test(NAME AssetManifestTest COMMAND AssetManifestTest)
add_test(NAME AssetSystemTest COMMAND AssetSystemTest)
