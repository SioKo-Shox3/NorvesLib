set(MODULE_NAME NorvesModule_AI)

# 並列作業でソース一覧の編集が衝突しないよう、GLOB でソースを集める。
file(GLOB_RECURSE AI_SOURCES CONFIGURE_DEPENDS
    Public/*.h
    Private/*.h
    Private/*.cpp
)

add_library(${MODULE_NAME} STATIC ${AI_SOURCES})

target_link_libraries(${MODULE_NAME} PRIVATE Core)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_SOURCE_DIR}
)

target_compile_features(${MODULE_NAME} PRIVATE cxx_std_23)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${AI_SOURCES})
