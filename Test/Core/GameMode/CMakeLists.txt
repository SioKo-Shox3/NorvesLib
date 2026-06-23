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

add_executable(GameModeStateMachineTest
    GameModeStateMachineTest.cpp
)
target_link_libraries(GameModeStateMachineTest PRIVATE Core)
target_include_directories(GameModeStateMachineTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(GameModeStateMachineTest PROPERTIES FOLDER "Test/Core/GameMode")

add_test(NAME GameModeStateMachineTest COMMAND GameModeStateMachineTest)

add_executable(SubRoutineTest
    SubRoutineTest.cpp
)
target_link_libraries(SubRoutineTest PRIVATE Core)
target_include_directories(SubRoutineTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SubRoutineTest PROPERTIES FOLDER "Test/Core/GameMode")

add_test(NAME SubRoutineTest COMMAND SubRoutineTest)
