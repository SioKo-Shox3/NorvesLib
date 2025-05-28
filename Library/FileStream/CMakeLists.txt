set(MODULE_NAME "FileStream")

# FileStreamモジュールのソースファイル
set(SOURCE_FILES
    "Private/FileStream.cpp"
    "Private/AsyncFileStream.cpp"
)

# FileStreamモジュールのヘッダーファイル  
set(HEADER_FILES
    "Public/IFileStream.h"
    "Public/FileStream.h"
    "Public/AsyncFileStream.h"
    "Public/FileStreamForward.h"
    "Public/FileStreamModule.h"
)

# スタティックライブラリとして作成
add_library(${MODULE_NAME} STATIC ${SOURCE_FILES} ${HEADER_FILES})

# ヘッダーファイルのインクルードディレクトリを設定
target_include_directories(${MODULE_NAME} 
    PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)

# 依存関係の設定 - Coreモジュールとメモリ管理
target_link_libraries(${MODULE_NAME} 
    PUBLIC 
        Core
        Thread
    PRIVATE
        Memory
)

# インクルードディレクトリの追加
target_include_directories(${MODULE_NAME} 
    PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/../"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/Public"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Thread/Public"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../Memory/Public"
)

# C++23標準を設定
target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)

# コンパイルオプション
if(MSVC)
    target_compile_options(${MODULE_NAME} PRIVATE 
        /W4 /WX /permissive- /Zc:__cplusplus
    )
else()
    target_compile_options(${MODULE_NAME} PRIVATE 
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

# IDEでのフォルダ構成
source_group("Public" FILES ${HEADER_FILES})
source_group("Private" FILES ${SOURCE_FILES})

# IDE用のターゲット（ヘッダーファイルを含む）
add_library(${MODULE_NAME}_IDE INTERFACE)
target_sources(${MODULE_NAME}_IDE INTERFACE ${HEADER_FILES})