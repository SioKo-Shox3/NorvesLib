# NorvesLibテスト

# テストサブディレクトリの追加
add_subdirectory(Core)
add_subdirectory(Reflection)
add_subdirectory(Thread)
add_subdirectory(Modules/Dummy)
add_subdirectory(Modules/Physics)
add_subdirectory(Modules/Audio)
if(TARGET norves_bridge_engine_sdk)
    add_subdirectory(Game)
endif()

# 共通のテスト用設定があればここに追加
enable_testing()
