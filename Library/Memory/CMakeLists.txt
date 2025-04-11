set(MODULE_NAME Memory)

file(GLOB_RECURSE PUBLIC_HEADERS
    Public/*.h
)

file(GLOB_RECURSE PRIVATE_SOURCES
    Private/*.cpp
)

# 空のインターフェースライブラリとして定義
add_library(${MODULE_NAME} INTERFACE)

target_include_directories(${MODULE_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
)

# ヘッダーをIDEに表示するための設定
add_custom_target(${MODULE_NAME}_IDE SOURCES ${PUBLIC_HEADERS} ${PRIVATE_SOURCES})

# 他のモジュールへの依存関係を設定
# target_link_libraries(${MODULE_NAME} INTERFACE OtherModule)