# Link the threading library because the async case spins up JobSystem worker threads.
find_package(Threads REQUIRED)

norves_add_test_bundle(LoggerSinkTest
    FOLDER "Test/Core/Logging"
    MEMBER LoggerSinkTest LoggerSinkTest.cpp
    MEMBER StatsManagerTest ${CMAKE_SOURCE_DIR}/Test/Core/Debug/StatsManagerTest.cpp
    MEMBER DelegateCopyTest ${CMAKE_SOURCE_DIR}/Test/Core/Delegate/DelegateCopyTest.cpp
    MEMBER InputRouterTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputRouterTest.cpp
    MEMBER ModuleRegistryTest ${CMAKE_SOURCE_DIR}/Test/Core/Module/ModuleRegistryTest.cpp
    MEMBER IdentityLiteralTest ${CMAKE_SOURCE_DIR}/Test/Core/Text/IdentityLiteralTest.cpp
    MEMBER JsonWriterTest ${CMAKE_SOURCE_DIR}/Test/Core/Text/JsonWriterTest.cpp
)
target_link_libraries(LoggerSinkTest PRIVATE Core Threads::Threads)
target_include_directories(LoggerSinkTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

include(CTest)
add_test(NAME LoggerSinkTest COMMAND LoggerSinkTest)
