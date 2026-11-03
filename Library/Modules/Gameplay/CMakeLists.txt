set(MODULE_NAME NorvesModule_Gameplay)

set(GAMEPLAY_PUBLIC_HEADERS
    Public/Gameplay/IGameplayModule.h
)

set(GAMEPLAY_PRIVATE_SOURCES
    Private/Gameplay/GameplayModule.h
    Private/Gameplay/GameplayModule.cpp
)

add_library(${MODULE_NAME} STATIC ${GAMEPLAY_PUBLIC_HEADERS} ${GAMEPLAY_PRIVATE_SOURCES})

target_link_libraries(${MODULE_NAME} PRIVATE Core)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_SOURCE_DIR}
)

target_compile_features(${MODULE_NAME} PRIVATE cxx_std_23)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GAMEPLAY_PUBLIC_HEADERS} ${GAMEPLAY_PRIVATE_SOURCES})
