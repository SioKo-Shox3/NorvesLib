set(MODULE_NAME Thread)

# ソースファイル
file(GLOB_RECURSE SOURCES 
    "Private/*.cpp"
)

# ヘッダーファイル
file(GLOB_RECURSE HEADERS
    "Public/*.h"
)

# スタティックライブラリとして作成
add_library(${MODULE_NAME} STATIC ${SOURCES} ${HEADERS})

# インクルードディレクトリの設定
target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
        ${CMAKE_SOURCE_DIR}/Library  # Libraryディレクトリを追加して相対パスによるインクルードを有効にする
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

# 依存関係の設定
target_link_libraries(${MODULE_NAME}
    PUBLIC
        Core
)

# IDEでの表示用設定
set_target_properties(${MODULE_NAME} PROPERTIES
    FOLDER "Library"
)

# 視覚的なIDE用のプロジェクト
if(MSVC)
    add_library(${MODULE_NAME}_IDE INTERFACE)
    target_sources(${MODULE_NAME}_IDE INTERFACE ${SOURCES} ${HEADERS})
    set_target_properties(${MODULE_NAME}_IDE PROPERTIES FOLDER "Library")
endif()
