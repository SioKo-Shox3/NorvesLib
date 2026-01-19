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
# Memory, FileStream, Random, Math, Thread は Core に統合されたため、Core への依存のみで十分
target_link_libraries(${MODULE_NAME} 
    PRIVATE 
        Core
        # Memory # Core に統合されました
        # Random # Core に統合されました
        # FileStream # Core に統合されました
        # Math # Core に統合されました
        # Thread # Core に統合されました
        # RHI # TODO: RHIモジュールはリファクタリング中のため一時的に無効化
)