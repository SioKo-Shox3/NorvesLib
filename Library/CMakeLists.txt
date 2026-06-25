set(MODULE_NAME NorvesLib)

# ImGui デバッグオーバーレイ(既定 ON・ゲート)。option は本ファイル 1 箇所のみ。
# ON のときだけ imgui 本体(第三者)を専用 static lib NorvesThirdParty_ImGui としてビルドし、
# ImGui モジュール(Library/Modules/ImGui)のみが PRIVATE 依存する(Core は ImGui を参照/リンクしない)。
# OFF(-DNORVES_ENABLE_IMGUI=OFF)のときは imgui を 1 ファイルもビルドせず Core も非リンク
# =素ビルド byte-for-byte 不変。実行時に ImGui を表示するには Game を --imgui で起動する
# (モジュール登録ゲート。ビルドゲート ON だけでは描画されない)。
option(NORVES_ENABLE_IMGUI "Enable ImGui debug overlay" ON)
if(NORVES_ENABLE_IMGUI)
    # ImGui ON は FreeType を vendoring した git submodule(Library/ThirdParty/freetype)に
    # 依存する。未取得(fresh clone で submodule 未初期化)だと add_subdirectory が分かりにくく
    # 失敗するため、先に存在を確認し明確なメッセージで止める。
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/freetype/CMakeLists.txt")
        message(FATAL_ERROR
            "NORVES_ENABLE_IMGUI=ON ですが FreeType submodule が未取得です。"
            "`git submodule update --init Library/ThirdParty/freetype` を実行するか、"
            "ImGui 不要なら -DNORVES_ENABLE_IMGUI=OFF で構成してください。")
    endif()

    # FreeType フォントラスタライザ(2B-ii-b)。imgui の既定 stb_truetype より crisp な
    # グリフを生成し、imgui_freetype.cpp(IMGUI_ENABLE_FREETYPE 経由)が build 時に選ぶ。
    # 外部依存(zlib/bzip2/png/harfbuzz/brotli)は全て無効化して素の TTF/OTF ラスタライズ
    # のみに絞る(余分な探索/リンクを持ち込まない)。freetype ターゲットは PUBLIC で
    # ft2build.h 等の include ディレクトリを提供するので、これに PRIVATE リンクする
    # imgui_freetype.cpp がヘッダを解決できる。NORVES_ENABLE_IMGUI OFF 時は本ブロックが
    # 走らず freetype は 1 ファイルもビルドされない=素ビルド byte-for-byte 不変。
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    add_subdirectory(ThirdParty/freetype)

    add_subdirectory(ThirdParty/imgui)
endif()

# ライブラリモジュールを追加
add_subdirectory(Core)

# 別 static lib モジュール(Core 依存・明示 Register 駆動)。INTERFACE NorvesLib には足さない。
add_subdirectory(Modules/Dummy)

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