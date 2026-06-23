set(MODULE_NAME Game)

# ソースファイルの収集
file(GLOB_RECURSE SOURCE_FILES 
    *.cpp
    *.h
)

# Windowsアプリケーションの実行ファイルを作成
add_executable(${MODULE_NAME} WIN32 ${SOURCE_FILES})

# インクルードパスの設定
target_include_directories(${MODULE_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}  # ルートディレクトリを追加
        ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb  # stb_image.h（ARM分解用）
)

# 各ライブラリコンポーネントへの依存関係を設定
# Memory, FileStream, Random, Math, Thread は Core に統合されたため、Core への依存のみで十分
target_link_libraries(${MODULE_NAME}
    PRIVATE
        Core
        # モジュールシステム第1段1C-ii: 描画ダミーモジュール lib。--dummy-overlay 時に
        # GameApplicationHandler が RegisterDummyRenderModule を明示参照するため、
        # その自由関数を含む別 static lib をリンク引込する(PUBLIC include で
        # DummyModule/*.h も解決)。フラグ無しでは登録されず seam は no-op。
        NorvesModule_Dummy
        # Memory # Core に統合されました
        # Random # Core に統合されました
        # FileStream # Core に統合されました
        # Math # Core に統合されました
        # Thread # Core に統合されました
        # RHI # TODO: RHIモジュールはリファクタリング中のため一時的に無効化
)

# NorvesEditor Bridge engine SDK link (Workstream L-P1). Only when the parent
# build opts in via -DNORVES_BRIDGE_SDK_DIR; a plain NorvesLib build skips it.
#
# Game/Bridge/*.cpp は上の GLOB_RECURSE で常に収集されコンパイルされるが、SDK ヘッダは
# NORVES_BRIDGE_SDK_DIR 設定時のみ利用可能なので、Bridge ソースの SDK 依存部はすべて
# NORVES_BRIDGE_ENABLED でガードしている（L-P2a）。この compile define は SDK 設定時
# のみ付与し、未設定の素の NorvesLib ビルドでは Bridge ソースが空コンパイルされて
# SDK ヘッダを一切要求しないようにする。
if(NORVES_BRIDGE_SDK_DIR)
    target_link_libraries(${MODULE_NAME} PRIVATE norves_bridge_engine_sdk)
    target_compile_definitions(${MODULE_NAME} PRIVATE NORVES_BRIDGE_ENABLED)
endif()

# ImGui モジュール(第2段 B-i)。NORVES_ENABLE_IMGUI ON 時のみ別 static lib
# NorvesModule_ImGui を PRIVATE 直リンクし、NORVES_ENABLE_IMGUI を Game TU へ付与する。
# GameApplicationHandler が --imgui ゲート時に RegisterImGuiModule を明示参照することで
# 別 lib の TU がリンクへ引き込まれる(dead-strip 本実証)。OFF 時はリンクも明示 Register
# も行わず、--imgui も未処理=素の NorvesLib ビルドを byte-for-byte 不変に保つ。
if(NORVES_ENABLE_IMGUI)
    target_link_libraries(${MODULE_NAME} PRIVATE NorvesModule_ImGui)
    # DirectionalLightEditSubRoutine の view が OnImGui で imgui.h / ImGui:: を直接使うため、
    # Game も imgui 本体 lib を直リンクする(NorvesThirdParty_ImGui の PUBLIC include で imgui.h を
    # 解決し、ImGui:: シンボルを引き込む)。NorvesModule_ImGui は imgui を PRIVATE 依存するので
    # Game へは透過しない=ここで明示直リンクが必要。
    target_link_libraries(${MODULE_NAME} PRIVATE NorvesThirdParty_ImGui)
    target_compile_definitions(${MODULE_NAME} PRIVATE NORVES_ENABLE_IMGUI)
    # 日本語ラベルの narrow リテラルを実行時 UTF-8 にする(ImGuiModule と同方式)。MSVC 既定の
    # 実行文字コードは CP932 で ImGui 要求の UTF-8 と食い違うため /utf-8 を付与する。OFF 時は
    # 付与されず素ビルド不変。
    if(MSVC)
        target_compile_options(${MODULE_NAME} PRIVATE /utf-8)
    endif()
endif()
