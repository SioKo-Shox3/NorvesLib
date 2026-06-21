set(MODULE_NAME NorvesLib)

# ImGui デバッグオーバーレイ(既定 OFF・opt-in ゲート)。option は本ファイル 1 箇所のみ。
# ON のときだけ imgui 本体(第三者)を専用 static lib NorvesThirdParty_ImGui としてビルドし、
# Core(imgui_impl_vulkan 用)と将来の NorvesModule_ImGui が PRIVATE 依存する。
# OFF のときは imgui を 1 ファイルもビルドせず Core も非リンク=素ビルド byte-for-byte 不変。
option(NORVES_ENABLE_IMGUI "Enable ImGui debug overlay" OFF)
if(NORVES_ENABLE_IMGUI)
    add_subdirectory(ThirdParty/imgui)
endif()

# ライブラリモジュールを追加
add_subdirectory(Core)

# 別 static lib モジュール(Core 依存・明示 Register 駆動)。INTERFACE NorvesLib には足さない。
add_subdirectory(Modules/Dummy)

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