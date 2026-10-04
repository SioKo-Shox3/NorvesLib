# bc7enc_rdo(MIT または パブリックドメイン)の BC1・BC4・BC5・BC7 エンコーダと BC7 デコーダだけを
# 独立した静的ライブラリに閉じ込める。リンクしてよいのは Tools/AssetCook だけで、Core・Game には入れない。
# 取り込んだファイルと出所は UPSTREAM.json に記録している。
set(BC7ENC_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/upstream")

add_library(NorvesThirdParty_Bc7Enc STATIC
    ${BC7ENC_UPSTREAM_DIR}/bc7enc.cpp
    ${BC7ENC_UPSTREAM_DIR}/bc7decomp.cpp
    ${BC7ENC_UPSTREAM_DIR}/rgbcx.cpp
)
target_include_directories(NorvesThirdParty_Bc7Enc PUBLIC ${BC7ENC_UPSTREAM_DIR})
target_compile_features(NorvesThirdParty_Bc7Enc PUBLIC cxx_std_23)
set_target_properties(NorvesThirdParty_Bc7Enc PROPERTIES
    FOLDER "Library/ThirdParty"
)
if(MSVC)
    # 第三者のソースは書き換えないので、警告は出さない。
    target_compile_options(NorvesThirdParty_Bc7Enc PRIVATE /w)
endif()
