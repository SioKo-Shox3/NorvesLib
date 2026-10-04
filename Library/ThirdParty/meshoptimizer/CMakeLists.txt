# meshoptimizer(MIT)を独立した静的ライブラリに閉じ込める。リンクしてよいのは Tools/AssetCook だけで、
# Core・Game には入れない。取り込んだファイルと出所は UPSTREAM.json に記録している。
set(MESHOPTIMIZER_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/upstream")

add_library(NorvesThirdParty_MeshOptimizer STATIC
    ${MESHOPTIMIZER_UPSTREAM_DIR}/allocator.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/clusterizer.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/indexanalyzer.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/indexcodec.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/indexgenerator.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/meshletcodec.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/meshletutils.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/opacitymap.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/overdrawoptimizer.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/partition.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/quantization.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/rasterizer.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/remesher.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/simplifier.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/spatialorder.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/stripifier.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/tangentspace.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/vcacheoptimizer.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/vertexcodec.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/vertexfilter.cpp
    ${MESHOPTIMIZER_UPSTREAM_DIR}/vfetchoptimizer.cpp
)
target_include_directories(NorvesThirdParty_MeshOptimizer PUBLIC ${MESHOPTIMIZER_UPSTREAM_DIR})
target_compile_features(NorvesThirdParty_MeshOptimizer PUBLIC cxx_std_23)
set_target_properties(NorvesThirdParty_MeshOptimizer PROPERTIES
    FOLDER "Library/ThirdParty"
)
if(MSVC)
    # 第三者のソースは書き換えないので、警告は出さない。
    target_compile_options(NorvesThirdParty_MeshOptimizer PRIVATE /w)
endif()
