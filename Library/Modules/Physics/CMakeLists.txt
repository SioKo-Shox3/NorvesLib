set(MODULE_NAME NorvesModule_Physics)

set(PRIVATE_SOURCES
    Private/Physics/PhysicsModule.cpp
)

set(PUBLIC_HEADERS
    Public/Physics/IPhysicsModule.h
)

add_library(${MODULE_NAME} STATIC ${PRIVATE_SOURCES} ${PUBLIC_HEADERS})

target_link_libraries(${MODULE_NAME} PRIVATE Core)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)

source_group("Public Headers" FILES ${PUBLIC_HEADERS})
source_group("Private Sources" FILES ${PRIVATE_SOURCES})
