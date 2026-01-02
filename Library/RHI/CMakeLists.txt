set(MODULE_NAME RHI)

# Vulkan SDKを検索
find_package(Vulkan REQUIRED)

file(GLOB_RECURSE PUBLIC_HEADERS
    Public/*.h
)

file(GLOB_RECURSE PRIVATE_HEADERS
    Private/*.h
)

file(GLOB_RECURSE PRIVATE_SOURCES
    Private/*.cpp
)

# ライブラリとして定義
add_library(${MODULE_NAME} STATIC
    ${PUBLIC_HEADERS}
    ${PRIVATE_HEADERS}
    ${PRIVATE_SOURCES}
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_SOURCE_DIR}/Library
)

# Vulkan SDKへのリンク（vulkan.hppはVulkan SDKに含まれる）
target_link_libraries(${MODULE_NAME}
    PUBLIC
        Core
        Vulkan::Vulkan
)

# Vulkan.hppのための設定
target_compile_definitions(${MODULE_NAME}
    PRIVATE
        VULKAN_HPP_NO_EXCEPTIONS  # 例外を無効化する場合
        # VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1  # 動的ディスパッチを使用する場合
)

# C++23を使用
target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)