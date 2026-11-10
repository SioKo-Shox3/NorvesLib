# DirectXTex(MIT)の BC 符号化・復号を独立した静的ライブラリにする。
# 利用先は Tools/AssetCook。取り込んだファイルと出所は UPSTREAM.json に記録する。
set(DIRECTXTEX_BC_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/upstream")

add_library(NorvesThirdParty_DirectXTexBC STATIC
    ${DIRECTXTEX_BC_UPSTREAM_DIR}/BC.cpp
    ${DIRECTXTEX_BC_UPSTREAM_DIR}/BC6HBC7.cpp
)
target_include_directories(NorvesThirdParty_DirectXTexBC
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim
    PUBLIC ${DIRECTXTEX_BC_UPSTREAM_DIR}
)
target_compile_features(NorvesThirdParty_DirectXTexBC PUBLIC cxx_std_23)
set_target_properties(NorvesThirdParty_DirectXTexBC PROPERTIES
    FOLDER "Library/ThirdParty"
)
if(MSVC)
    # 第三者のソースは書き換えないので、警告は出さない。
    target_compile_options(NorvesThirdParty_DirectXTexBC PRIVATE /w)
endif()
