add_executable(StatsManagerTest
    StatsManagerTest.cpp
)
target_link_libraries(StatsManagerTest PRIVATE Core)
target_include_directories(StatsManagerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(StatsManagerTest PROPERTIES FOLDER "Test/Core/Debug")

include(CTest)
add_test(NAME StatsManagerTest COMMAND StatsManagerTest)
