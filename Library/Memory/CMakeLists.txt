set(MODULE_NAME Memory)

file(GLOB_RECURSE PUBLIC_HEADERS
    Public/*.h
)

file(GLOB_RECURSE PRIVATE_SOURCES
    Private/*.cpp
)

# スタティックライブラリとして定義
add_library(${MODULE_NAME} STATIC ${PUBLIC_HEADERS} ${PRIVATE_SOURCES})

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

# 他のモジュールへの依存関係を設定
# target_link_libraries(${MODULE_NAME} PUBLIC OtherModule)