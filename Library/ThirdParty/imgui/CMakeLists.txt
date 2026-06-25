# NorvesThirdParty_ImGui — imgui 本体(第三者コード)を物理隔離した専用 static lib。
# imgui core 4本のみをコンパイルする(imgui_impl_vulkan.cpp は含めない=ImGui の描画は
# モジュール側が抽象 RHI::I* と汎用 Mesh2D 経路で行うため不要)。
# ImGui モジュール(Library/Modules/ImGui)のみが PRIVATE で依存する(Core は ImGui を
# 参照/リンクしない)。NORVES_ENABLE_IMGUI が OFF のときは上位 CMake が本 lib を
# add_subdirectory しない=素ビルド byte-for-byte 不変。

add_library(NorvesThirdParty_ImGui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    # FreeType フォントビルダ(2B-ii-b)。IMGUI_ENABLE_FREETYPE 定義時に imgui_draw.cpp の
    # ImFontAtlasBuildMain が GetFontLoader() で FreeType ローダを選ぶ。原本(misc/freetype
    # 配下・LF/非 BOM)は無改変でソースに追加する。
    misc/freetype/imgui_freetype.cpp
)

# 依存者が imgui.h を直接 include 可能にする(Core の imgui_impl_vulkan.cpp、
# 将来の NorvesModule_ImGui の UI コードが imgui.h を解決する)。
target_include_directories(NorvesThirdParty_ImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# FreeType ライブラリを PRIVATE リンク。freetype ターゲットの PUBLIC include で
# imgui_freetype.cpp が ft2build.h / FT_FREETYPE_H 等を解決する。FreeType は imgui 内部
# 実装の詳細であり、NorvesThirdParty_ImGui の消費者(Core/Module)へは漏らさない(PRIVATE)。
target_link_libraries(NorvesThirdParty_ImGui PRIVATE freetype)

# IMGUI_ENABLE_FREETYPE は imgui コア(imgui_draw.cpp のローダ選択・imgui.cpp のデバッグ UI)と
# imgui_internal.h の IMGUI_ENABLE_STB_TRUETYPE フォールバック分岐(関数宣言のみで構造体
# レイアウトに影響しない)でのみ参照される。公開 imgui.h はこの定義で ABI が変わらず、
# NorvesLib の消費者は誰も imgui_internal.h を include しないため、定義を全消費者へ波及
# させる必要はない。よって本コア lib の TU に閉じる PRIVATE が正しく安全(imconfig.h は
# 無改変のまま CMake define で注入する)。
target_compile_definitions(NorvesThirdParty_ImGui PRIVATE IMGUI_ENABLE_FREETYPE)

target_compile_features(NorvesThirdParty_ImGui PUBLIC cxx_std_23)
