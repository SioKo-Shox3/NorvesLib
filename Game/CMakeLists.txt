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
)

# 各ライブラリコンポーネントへの依存関係を設定
# GameModeはCoreに統合されたため、Coreへの依存のみで十分
target_link_libraries(${MODULE_NAME} 
    PRIVATE 
        Core
        Math
        # RHI # TODO: RHIモジュールはリファクタリング中のため一時的に無効化
        Memory
        Random
        # Thread # 一時的に除外
)