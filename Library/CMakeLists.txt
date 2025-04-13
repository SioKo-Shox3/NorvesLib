set(MODULE_NAME NorvesLib)

# ライブラリモジュールを追加
add_subdirectory(Core)
add_subdirectory(Math)
add_subdirectory(RHI)
add_subdirectory(Memory)
add_subdirectory(Thread)
add_subdirectory(Random)

# 全てのモジュールをリンクするインターフェースライブラリを作成
add_library(${MODULE_NAME} INTERFACE)

# 各モジュールへの依存関係を設定
target_link_libraries(${MODULE_NAME} 
    INTERFACE 
        Core
        Math
        RHI
        Memory
        Thread
        Random
)

# インクルードパスの設定
target_include_directories(${MODULE_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)