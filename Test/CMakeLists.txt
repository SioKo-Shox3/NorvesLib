# NorvesLibテスト

# テストサブディレクトリの追加
add_subdirectory(TestBundle)
add_subdirectory(TestSupport)
add_subdirectory(Core)
add_subdirectory(Reflection)
add_subdirectory(Thread)
add_subdirectory(Modules/Physics)
add_subdirectory(Modules/Audio)
add_subdirectory(M9)
if(TARGET norves_bridge_engine_sdk)
    add_subdirectory(Game)
endif()

# 新しいテストの実行ファイルにも設定が漏れないよう、一括して共通の処理を組み込む。
function(norves_attach_test_no_dialog directory outputCount)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set(count 0)
    foreach(target IN LISTS targets)
        get_target_property(targetType "${target}" TYPE)
        get_target_property(targetImported "${target}" IMPORTED)
        if(targetType STREQUAL "EXECUTABLE" AND NOT targetImported)
            target_link_libraries("${target}" PRIVATE NorvesTestNoDialog)
            math(EXPR count "${count} + 1")
        endif()
    endforeach()

    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        norves_attach_test_no_dialog("${subdirectory}" childCount)
        math(EXPR count "${count} + ${childCount}")
    endforeach()
    set("${outputCount}" "${count}" PARENT_SCOPE)
endfunction()

norves_attach_test_no_dialog("${CMAKE_CURRENT_SOURCE_DIR}" norvesTestExecutableCount)
message(STATUS "NorvesTestNoDialog: テスト実行ファイル ${norvesTestExecutableCount} 個に適用")

# 共通のテスト用設定があればここに追加
enable_testing()
