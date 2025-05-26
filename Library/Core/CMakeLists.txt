cmake_minimum_required(VERSION 3.28)

# モジュール名の設定
set(MODULE_NAME Core)

# ソースファイルの定義
set(PRIVATE_SOURCES
    Private/CoreModule.cpp
    Private/Object/Object.cpp
    Private/Text/IdentityPool.cpp
    Private/Logging/Logger.cpp
    Private/Debug/DebugOutput.cpp
)

set(PUBLIC_HEADERS
    Public/CoreModule.h
    Public/Object/Object.h
    Public/Text/IdentityPool.h
    Public/Container/Containers.h
    Public/Container/PointerTypes.h
    Public/Container/VariableArray.h
    Public/Container/FixedArray.h
    Public/Container/List.h
    Public/Container/Map.h
    Public/Container/Set.h
    Public/Container/UnorderedMap.h
    Public/Container/UnorderedSet.h
    Public/Container/String.h
    Public/Container/StringView.h
    Public/Container/Span.h
    Public/Container/Deque.h
    Public/Container/Queue.h
    Public/Logging/LoggingModule.h
    Public/Logging/Logger.h
    Public/Logging/LogMacros.h
    Public/Debug/DebugOutput.h
)

# ライブラリの作成
add_library(${MODULE_NAME} STATIC ${PRIVATE_SOURCES} ${PUBLIC_HEADERS})

# インクルードディレクトリの設定
target_include_directories(${MODULE_NAME}
    PUBLIC 
        Public
    PRIVATE 
        Private
)

# 依存関係の設定
target_link_libraries(${MODULE_NAME}
    PUBLIC
        Thread
        FileStream
)

# C++23の設定
target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)

# Windows固有の設定
if(WIN32)
    target_compile_definitions(${MODULE_NAME} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# IDEでのファイル整理（Visual Studio等）
source_group("Public Headers" FILES ${PUBLIC_HEADERS})
source_group("Private Sources" FILES ${PRIVATE_SOURCES})