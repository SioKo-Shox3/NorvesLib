set(MODULE_NAME NorvesLib)

# ライブラリモジュールを追加
add_subdirectory(Core)

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