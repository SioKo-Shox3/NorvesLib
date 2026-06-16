add_executable(LoggerSinkTest LoggerSinkTest.cpp)
target_link_libraries(LoggerSinkTest PRIVATE Core)
target_include_directories(LoggerSinkTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(LoggerSinkTest PROPERTIES FOLDER "Test/Core/Logging")

# Link the threading library because the async case spins up JobSystem worker threads.
find_package(Threads REQUIRED)
target_link_libraries(LoggerSinkTest PRIVATE Threads::Threads)

include(CTest)
add_test(NAME LoggerSinkTest COMMAND LoggerSinkTest)
