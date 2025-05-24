set(MODULE_NAME Core)

file(GLOB_RECURSE PUBLIC_HEADERS
    Public/*.h
)

file(GLOB_RECURSE PRIVATE_SOURCES
    Private/*.cpp
)

# スタティックライブラリとして定義（以前はINTERFACEライブラリ）
add_library(${MODULE_NAME} STATIC ${PRIVATE_SOURCES} ${PUBLIC_HEADERS})

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_SOURCE_DIR}/Library
)

# Threadモジュールのパスを明示的に追加
target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_SOURCE_DIR}/Library/Thread/Public
)

# Memoryモジュールへの依存関係を追加
target_link_libraries(${MODULE_NAME} PUBLIC Memory)

# Threadモジュールへの依存関係を追加
target_link_libraries(${MODULE_NAME} PUBLIC Thread)

# ヘッダーをIDEに表示するための設定
add_custom_target(${MODULE_NAME}_IDE SOURCES ${PUBLIC_HEADERS} ${PRIVATE_SOURCES})