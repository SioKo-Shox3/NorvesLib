# NorvesModule_ImGui — Core 依存の別 static lib(ImGui デバッグオーバーレイ本体)。
# 第2段 B-i: ImGui コンテキスト所有 + 既定スタイルのデモ描画 + ImDrawData
# ディープクローン。imgui core(imgui.h/シンボル)は専用 lib NorvesThirdParty_ImGui を
# PRIVATE リンクして共有する(モジュール lib への内包/ソース直追加はしない=Core と
# 同一 imgui 実体を共有し ODR/二重コンパイル回避)。
#
# 本 CMakeLists は上位 Library/CMakeLists.txt が NORVES_ENABLE_IMGUI ON のときだけ
# add_subdirectory する。OFF のときは本 lib は構築されず Game も非リンク=素ビルド不変。

set(MODULE_NAME NorvesModule_ImGui)

set(PRIVATE_SOURCES
    Private/ImGuiModule/ImGuiModule.cpp
    Private/ImGuiModule/ImGuiOverlayPass.cpp
    Private/ImGuiModule/ImGuiDrawSnapshot.cpp
    Private/ImGuiModule/ImGuiViewRegistry.cpp
    Private/ImGuiModule/NorvesImGuiStyle.cpp
    # imgui core はここに直追加しない。専用 lib NorvesThirdParty_ImGui を PRIVATE
    # リンクして imgui.h/シンボルを解決する(2A-i 再構成で確定)。
)

set(PUBLIC_HEADERS
    Public/ImGuiModule/ImGuiModule.h
    Public/ImGuiModule/IImGuiView.h
)

# ThirdParty コードヘッダ(FontAwesome アイコンコード定義)。実行時アセットではなく
# ビルド時 include 対象のため Assets ではなくモジュールのコード木に置く(原本 LF/非 BOM)。
set(THIRDPARTY_HEADERS
    ThirdParty/IconsFontAwesome6.h
)

add_library(${MODULE_NAME} STATIC ${PRIVATE_SOURCES} ${PUBLIC_HEADERS} ${THIRDPARTY_HEADERS})

# Core への単一方向依存(枠組みヘッダ + 抽象 IImGuiRenderer/IViewPass を提供) +
# imgui core(NorvesThirdParty_ImGui)を共に PRIVATE 依存。両者 PRIVATE のため
# imgui/Core 詳細は本 lib の消費者(Game/Test)へ漏れない。
target_link_libraries(${MODULE_NAME} PRIVATE Core NorvesThirdParty_ImGui)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty
)

target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)

# ImGui コードを見る TU が一貫して見るゲート。本 lib は ON 時のみ構築されるが、
# 明示しておく(VulkanImGuiRenderer/impl_vulkan と同じゲート名で揃える)。
target_compile_definitions(${MODULE_NAME} PRIVATE NORVES_ENABLE_IMGUI)

# フォント(TTF/OTF)を実行時ロードするためのアセットルート。Core と同値で揃える
# (Core 側 CMakeLists で PRIVATE 定義のため本 lib には届かず、ここで明示注入する)。
set(NORVES_ASSET_DIR_IMGUI "${CMAKE_SOURCE_DIR}/Assets")
string(REPLACE "\\" "/" NORVES_ASSET_DIR_IMGUI "${NORVES_ASSET_DIR_IMGUI}")
target_compile_definitions(${MODULE_NAME} PRIVATE NORVES_ASSET_DIR="${NORVES_ASSET_DIR_IMGUI}")

if(WIN32)
    target_compile_definitions(${MODULE_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# 日本語/アイコンの narrow 文字列リテラルを実行時 UTF-8 にするため /utf-8 を付与する
# (MSVC 既定の実行文字コードは CP932 で ImGui が要求する UTF-8 と食い違うため)。
# ソースは BOM+UTF-8、/utf-8 で実行文字コードも UTF-8 に揃え ImGui へ素直に渡す。
if(MSVC)
    target_compile_options(${MODULE_NAME} PRIVATE /utf-8)
endif()

# IDE 整理(Visual Studio 等)
source_group("Public Headers" FILES ${PUBLIC_HEADERS})
source_group("Private Sources" FILES ${PRIVATE_SOURCES})
source_group("ThirdParty" FILES ${THIRDPARTY_HEADERS})
