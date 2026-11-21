# stb_vorbis は NorvesAudioDecode だけがリンクする独立した静的ライブラリ。
# ファイル入出力・push API・comment 解析を外し、メモリからの pull API だけを残す。
# 取り込んだファイルと出所は UPSTREAM.json に記録する。
add_library(NorvesThirdParty_StbVorbis STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_vorbis.c
)
target_include_directories(NorvesThirdParty_StbVorbis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(NorvesThirdParty_StbVorbis PUBLIC
    STB_VORBIS_NO_STDIO
    STB_VORBIS_NO_PUSHDATA_API
    STB_VORBIS_NO_COMMENTS
)
set_target_properties(NorvesThirdParty_StbVorbis PROPERTIES
    FOLDER "Library/ThirdParty"
)
if(MSVC)
    target_compile_options(NorvesThirdParty_StbVorbis PRIVATE /w)
endif()
