# NorvesModule_Dummy — Core 依存の別 static lib(dead-strip 早期検証用の最小ダミー)。
# ImGui が使うのと同じ「Core 依存の別 lib + 明示 Register」機構を実証する。

set(MODULE_NAME NorvesModule_Dummy)

set(PRIVATE_SOURCES
    Private/DummyModule/DummyModule.cpp
)

set(PUBLIC_HEADERS
    Public/DummyModule/DummyModule.h
)

add_library(${MODULE_NAME} STATIC ${PRIVATE_SOURCES} ${PUBLIC_HEADERS})

# Core にリンク(ModuleRegistry/IModule/Identity 等を別 lib 越しに参照する)。
target_link_libraries(${MODULE_NAME} PRIVATE Core)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)

# IDE 整理(Visual Studio 等)
source_group("Public Headers" FILES ${PUBLIC_HEADERS})
source_group("Private Sources" FILES ${PRIVATE_SOURCES})
