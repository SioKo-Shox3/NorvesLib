set(MODULE_NAME NorvesLib)

# ライブラリモジュールを追加
add_subdirectory(Core)
add_subdirectory(Math)
# add_subdirectory(RHI) # TODO: RHIモジュールはリファクタリング中のため一時的に無効化
add_subdirectory(Memory)
add_subdirectory(Thread)
add_subdirectory(Random)
add_subdirectory(GameMode) # GameModeモジュールを追加
add_subdirectory(FileStream) # FileStreamモジュールを追加

# 全てのモジュールをリンクするインターフェースライブラリを作成
add_library(${MODULE_NAME} INTERFACE)

# 各モジュールへの依存関係を設定
target_link_libraries(${MODULE_NAME} 
    INTERFACE 
        Core
        Math
        # RHI # TODO: RHIモジュールはリファクタリング中のため一時的に無効化
        Memory
        Thread
        Random
        GameMode # GameModeへの依存関係を追加
        FileStream # FileStreamへの依存関係を追加
)

# インクルードパスの設定
target_include_directories(${MODULE_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)