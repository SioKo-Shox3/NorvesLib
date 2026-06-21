add_executable(DummyModuleLinkTest
    DummyModuleLinkTest.cpp
)
target_link_libraries(DummyModuleLinkTest PRIVATE NorvesModule_Dummy Core)
target_include_directories(DummyModuleLinkTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DummyModuleLinkTest PROPERTIES FOLDER "Test/Modules/Dummy")

include(CTest)
add_test(NAME DummyModuleLinkTest COMMAND DummyModuleLinkTest)
