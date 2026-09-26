include(CTest)
add_test(NAME GameModeRegistryTest COMMAND GameModeStateMachineTest --test=GameModeRegistryTest)


norves_add_test_bundle(GameModeStateMachineTest
    FOLDER "Test/Core/GameMode"
    MEMBER GameModeStateMachineTest GameModeStateMachineTest.cpp
    MEMBER GameModeRegistryTest GameModeRegistryTest.cpp
    MEMBER SubRoutineTest SubRoutineTest.cpp
)
target_link_libraries(GameModeStateMachineTest PRIVATE Core)
target_include_directories(GameModeStateMachineTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

add_test(NAME GameModeStateMachineTest COMMAND GameModeStateMachineTest)

add_test(NAME SubRoutineTest COMMAND GameModeStateMachineTest --test=SubRoutineTest)
