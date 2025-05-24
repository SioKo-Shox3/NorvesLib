cmake_minimum_required(VERSION 3.14)
project(NorvesLib VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows環境でのmin/maxマクロを無効化
if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

# NorvesLibライブラリ
add_subdirectory(Library)

# Gameアプリケーション
add_subdirectory(Game)

# テスト
add_subdirectory(Test)