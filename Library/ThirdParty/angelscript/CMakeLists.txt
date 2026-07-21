set(ANGELSCRIPT_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/upstream/sdk/angelscript")

set(ANGELSCRIPT_CORE_SOURCES
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_atomic.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_builder.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_bytecode.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_callfunc.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_callfunc_mips.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_callfunc_x86.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_callfunc_x64_gcc.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_callfunc_x64_msvc.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_callfunc_x64_mingw.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_compiler.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_configgroup.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_context.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_datatype.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_gc.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_generic.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_globalproperty.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_memory.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_module.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_objecttype.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_outputbuffer.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_parser.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_restore.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_scriptcode.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_scriptengine.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_scriptfunction.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_scriptnode.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_scriptobject.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_string.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_string_util.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_thread.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_tokenizer.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_typeinfo.cpp
    ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_variablescope.cpp
)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM_MASM)
    if(NOT CMAKE_ASM_MASM_COMPILER_WORKS)
        message(FATAL_ERROR "MSVC x64 AngelScript requires a working MASM assembler")
    endif()

    list(APPEND ANGELSCRIPT_CORE_SOURCES
        ${ANGELSCRIPT_UPSTREAM_DIR}/source/as_callfunc_x64_msvc_asm.asm
    )
endif()

add_library(NorvesThirdParty_AngelScript STATIC ${ANGELSCRIPT_CORE_SOURCES})
target_include_directories(NorvesThirdParty_AngelScript
    PUBLIC
        ${ANGELSCRIPT_UPSTREAM_DIR}/include
)
target_compile_definitions(NorvesThirdParty_AngelScript PRIVATE
    ANGELSCRIPT_EXPORT
    _LIB
)
target_compile_features(NorvesThirdParty_AngelScript PUBLIC cxx_std_23)
set_target_properties(NorvesThirdParty_AngelScript PROPERTIES
    FOLDER "Library/ThirdParty"
)
