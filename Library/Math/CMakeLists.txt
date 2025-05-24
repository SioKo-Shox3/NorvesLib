set(MODULE_NAME Math)

file(GLOB_RECURSE PUBLIC_HEADERS
    Public/*.h
)

# 空のインターフェースライブラリとして定義
add_library(${MODULE_NAME} INTERFACE)

target_include_directories(${MODULE_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
)

# ヘッダーをIDEに表示するための設定
add_custom_target(${MODULE_NAME}_IDE SOURCES ${PUBLIC_HEADERS})