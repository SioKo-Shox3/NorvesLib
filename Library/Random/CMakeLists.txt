set(MODULE_NAME Random)

# ソースファイルとヘッダーファイルを収集
file(GLOB_RECURSE SOURCES Private/*.cpp)
file(GLOB_RECURSE HEADERS Public/*.h)

# IDE用のインターフェースライブラリ（ヘッダーのみ）
add_library(${MODULE_NAME}_IDE INTERFACE)
target_sources(${MODULE_NAME}_IDE INTERFACE ${HEADERS})

# 実際のライブラリ
add_library(${MODULE_NAME} ${SOURCES} ${HEADERS})

# インクルードディレクトリを設定
target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

# 依存関係の設定（必要に応じて他のモジュールを追加）
target_link_libraries(${MODULE_NAME}
    PUBLIC
        Core
        Math
)