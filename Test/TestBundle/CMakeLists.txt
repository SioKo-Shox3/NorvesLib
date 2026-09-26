# 小さなテストの実行ファイルを1つに束ねる。
#
# テストの実行ファイルはどれも Core を静的にリンクするので、数が多いとリンクとディスクが重くなる。
# 束ねた実行ファイルでは、各テストのソースの main を NorvesTestMain_<名前> へ改名してコンパイルし、
# 共通の入口（TestBundle/NorvesTestBundleMain.cpp）が1つを選んで呼ぶ。
#
#   norves_add_test_bundle(<ターゲット>
#       [FOLDER <IDEのフォルダ>]
#       MEMBER <ターゲット> <ソース> [<そのソースだけに付ける定義>...]
#       MEMBER <名前> <ソース> [<そのソースだけに付ける定義>...]
#       ...
#       [SOURCES <mainを持たない補助のソース>...])
#
# 最初の MEMBER はターゲットと同名にし、引数で選ばないときはこれを呼ぶ（元の実行ファイルと同じ振る舞い）。
# ほかのテストは add_test(NAME <テスト> COMMAND <ターゲット> --test=<名前> ...) で呼ぶ。スクリプトを介して
# 実行ファイルを起動するテストは、環境変数 NORVES_TEST_BUNDLE_MEMBER=<名前> で選ぶ。
# 同じ実行ファイルへ入れるソース同士は、無名名前空間の外で同じ名前のクラスや関数を定義してはいけない（ODR）。
#
# このファイルは Test/CMakeLists.txt が最初に add_subdirectory で読み込む（関数はプロジェクト全体から見える）。

function(norves_add_test_bundle target)
    set(members)
    set(helperSources)
    set(folder "")
    set(mode "")
    set(memberField 0)
    set(currentMember "")
    foreach(argument IN LISTS ARGN)
        if(argument STREQUAL "MEMBER" OR argument STREQUAL "SOURCES" OR argument STREQUAL "FOLDER")
            set(mode ${argument})
            set(memberField 0)
            continue()
        endif()
        if(mode STREQUAL "MEMBER")
            if(memberField EQUAL 0)
                set(currentMember ${argument})
                list(APPEND members ${argument})
                set(memberDefinitions_${argument})
            elseif(memberField EQUAL 1)
                set(memberSource_${currentMember} ${argument})
            else()
                list(APPEND memberDefinitions_${currentMember} ${argument})
            endif()
            math(EXPR memberField "${memberField} + 1")
        elseif(mode STREQUAL "SOURCES")
            list(APPEND helperSources ${argument})
        elseif(mode STREQUAL "FOLDER")
            set(folder ${argument})
        else()
            message(FATAL_ERROR "norves_add_test_bundle(${target}): unexpected argument '${argument}'")
        endif()
    endforeach()

    list(GET members 0 defaultMember)
    if(NOT defaultMember STREQUAL target)
        message(FATAL_ERROR "norves_add_test_bundle(${target}): the first MEMBER must be ${target}")
    endif()

    # 生成するファイルは ASCII だけにする（/utf-8 の無いターゲットでもコードページに依らず読めるように）。
    set(generatedDir ${CMAKE_CURRENT_BINARY_DIR}/TestBundle/${target})
    set(generatedSources)
    set(memberList "")
    foreach(member IN LISTS members)
        if(NOT DEFINED memberSource_${member})
            message(FATAL_ERROR "norves_add_test_bundle(${target}): MEMBER ${member} has no source")
        endif()
        get_filename_component(source "${memberSource_${member}}" ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
        set(content "#include \"NorvesTestBundleInvoke.h\"\n")
        foreach(definition IN LISTS memberDefinitions_${member})
            if(definition MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
                string(APPEND content "#define ${CMAKE_MATCH_1} ${CMAKE_MATCH_2}\n")
            else()
                string(APPEND content "#define ${definition} 1\n")
            endif()
        endforeach()
        string(APPEND content
            "#define main NorvesTestMain_${member}\n"
            "#include \"${source}\"\n"
            "#undef main\n"
            "\n"
            "int NorvesTestBundleRun_${member}(int argumentCount, char** arguments)\n"
            "{\n"
            "    return NorvesLib::Test::InvokeBundledMain(&NorvesTestMain_${member}, argumentCount, arguments);\n"
            "}\n")
        file(GENERATE OUTPUT ${generatedDir}/${member}.cpp CONTENT "${content}")
        list(APPEND generatedSources ${generatedDir}/${member}.cpp)
        string(APPEND memberList "NORVES_TEST_BUNDLE_MEMBER(${member})\n")
    endforeach()
    file(GENERATE OUTPUT ${generatedDir}/Members.inc CONTENT "${memberList}")

    add_executable(${target}
        ${CMAKE_SOURCE_DIR}/Test/TestBundle/NorvesTestBundleMain.cpp
        ${generatedSources}
        ${helperSources}
    )
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/Test/TestBundle)
    target_compile_definitions(${target} PRIVATE
        NORVES_TEST_BUNDLE_DEFAULT="${target}"
        NORVES_TEST_BUNDLE_MEMBERS_FILE="${generatedDir}/Members.inc"
    )
    if(folder)
        set_target_properties(${target} PROPERTIES FOLDER "${folder}")
    endif()
endfunction()
