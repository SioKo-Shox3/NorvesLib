# NorvesThirdParty_ImGui — imgui 本体(第三者コード)を物理隔離した専用 static lib。
# imgui core 4本のみをコンパイルする(imgui_impl_vulkan.cpp は含めない=それは
# 生 Vulkan と共に Core の RHI/Vulkan 側でコンパイルされる)。
# Core(imgui_impl_vulkan 用)と将来の NorvesModule_ImGui(UI 構築用)が共に
# PRIVATE で依存する。NORVES_ENABLE_IMGUI が OFF のときは上位 CMake が本 lib を
# add_subdirectory しない=素ビルド byte-for-byte 不変。

add_library(NorvesThirdParty_ImGui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
)

# 依存者が imgui.h を直接 include 可能にする(Core の imgui_impl_vulkan.cpp、
# 将来の NorvesModule_ImGui の UI コードが imgui.h を解決する)。
target_include_directories(NorvesThirdParty_ImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(NorvesThirdParty_ImGui PUBLIC cxx_std_23)
