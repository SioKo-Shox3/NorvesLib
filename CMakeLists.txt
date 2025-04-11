cmake_minimum_required(VERSION 3.14)
project(NorvesLib VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# NorvesLibライブラリ
add_subdirectory(Library)

# Gameアプリケーション
add_subdirectory(Game)