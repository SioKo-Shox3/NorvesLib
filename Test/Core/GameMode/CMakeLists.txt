add_executable(GameModeRegistryTest
    GameModeRegistryTest.cpp
)
target_link_libraries(GameModeRegistryTest PRIVATE Core)
target_include_directories(GameModeRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(GameModeRegistryTest PROPERTIES FOLDER "Test/Core/GameMode")

include(CTest)
add_test(NAME GameModeRegistryTest COMMAND GameModeRegistryTest)
