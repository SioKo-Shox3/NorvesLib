set(MODULE_NAME NorvesLib)

# ImGui デバッグオーバーレイ(既定 ON・ゲート)。option は本ファイル 1 箇所のみ。
# ON のときだけ imgui 本体(第三者)を専用 static lib NorvesThirdParty_ImGui としてビルドし、
# ImGui モジュール(Library/Modules/ImGui)のみが PRIVATE 依存する(Core は ImGui を参照/リンクしない)。
# OFF のときは ImGui をビルドしない。実行時に表示するには Game を --imgui で起動する。
option(NORVES_ENABLE_IMGUI "Enable ImGui debug overlay" ON)
option(NORVES_ENABLE_CORE_TEXT "Enable Core text rendering" ON)
if(NORVES_ENABLE_IMGUI OR NORVES_ENABLE_CORE_TEXT)
    # ImGui と Core の文字描画はどちらも FreeType を使うため、必要なときに取得済みか確認する。
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/freetype/CMakeLists.txt")
        message(FATAL_ERROR
            "ImGui と Core の文字描画に必要な FreeType サブモジュールが未取得です。"
            "`git submodule update --init Library/ThirdParty/freetype` を実行するか、"
            "両方とも不要なら -DNORVES_ENABLE_IMGUI=OFF -DNORVES_ENABLE_CORE_TEXT=OFF で構成してください。")
    endif()

    # ImGui と Core の文字描画で、フォントのラスタライズに FreeType を使う。
    # 外部依存(zlib/bzip2/png/harfbuzz/brotli)は全て無効化して素の TTF/OTF ラスタライズ
    # のみに絞る(余分な探索/リンクを持ち込まない)。
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    add_subdirectory(ThirdParty/freetype)

    if(NORVES_ENABLE_IMGUI)
        add_subdirectory(ThirdParty/imgui)
    endif()
endif()

add_subdirectory(ThirdParty/angelscript)

# ライブラリモジュールを追加
add_subdirectory(Core)

# 別 static lib モジュール(Core 依存・明示 Register 駆動)。INTERFACE NorvesLib には足さない。
add_subdirectory(Modules/Dummy)
add_subdirectory(Modules/Physics)
add_subdirectory(Modules/Gameplay)
add_subdirectory(Modules/Audio)

# ImGui モジュール本体(第2段 B-i)。NORVES_ENABLE_IMGUI ON 時のみ構築する
# (imgui core lib NorvesThirdParty_ImGui に PRIVATE 依存)。OFF 時は構築せず
# Game も非リンク=素ビルド byte-for-byte 不変。INTERFACE NorvesLib には足さない。
if(NORVES_ENABLE_IMGUI)
    add_subdirectory(Modules/ImGui)
endif()

# 全てのモジュールをリンクするインターフェースライブラリを作成
add_library(${MODULE_NAME} INTERFACE)

# 各モジュールへの依存関係を設定
target_link_libraries(${MODULE_NAME} 
    INTERFACE 
        Core
)

# インクルードパスの設定
target_include_directories(${MODULE_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)