cmake_minimum_required(VERSION 3.14)
project(NorvesLib VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Windows環境でのmin/maxマクロを無効化
if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

# NorvesLibライブラリ
add_subdirectory(Library)

# ツール
add_subdirectory(Tools)

# NorvesEditor Bridge engine SDK (Workstream L-P1, opt-in via cache var).
# Empty by default => a normal NorvesLib build is byte-for-byte unaffected.
set(NORVES_BRIDGE_SDK_DIR "" CACHE PATH "Path to the NorvesEditor bridge/cpp tree to embed the Bridge engine SDK (empty = disabled)")
if(NORVES_BRIDGE_SDK_DIR)
    # Embed the SDK without its examples/tests (L-P0 options). Set as normal
    # vars BEFORE add_subdirectory so the SDK option() picks them up (CMP0077 NEW).
    set(NORVES_BRIDGE_BUILD_EXAMPLES OFF)
    set(NORVES_BRIDGE_BUILD_TESTS OFF)
    add_subdirectory(${NORVES_BRIDGE_SDK_DIR} ${CMAKE_BINARY_DIR}/bridge_sdk)
endif()

# Gameアプリケーション
add_subdirectory(Game)

# テスト
add_subdirectory(Test)
