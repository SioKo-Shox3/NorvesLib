cmake_minimum_required(VERSION 3.10)
project(GameMode)

# ソースファイルとヘッダーファイルを追加
file(GLOB_RECURSE PUBLIC_HEADERS CONFIGURE_DEPENDS "Public/*.h")
file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "Private/*.cpp")

# ソースファイルが存在しない場合、ダミーソースファイルを作成してライブラリをビルド可能にする
if(NOT PRIVATE_SOURCES)
    set(DUMMY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/GameMode_dummy.cpp")
    file(WRITE ${DUMMY_SOURCE} "// Dummy source file for header-only library\n")
    set(PRIVATE_SOURCES ${DUMMY_SOURCE})
endif()

add_library(GameMode STATIC ${PUBLIC_HEADERS} ${PRIVATE_SOURCES})

# 公開ヘッダーのインクルードパスを設定
target_include_directories(GameMode
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>
        $<INSTALL_INTERFACE:include/GameMode> # インストール時のパス
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

# 依存関係 (必要に応じてCoreなどを追加)
target_link_libraries(GameMode PUBLIC Core) # 例としてCoreに依存

# IDE用のフォルダ設定 (任意)
set_target_properties(GameMode PROPERTIES FOLDER "Library/GameMode")
