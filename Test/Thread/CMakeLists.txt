# Thread モジュールのテスト（Thread は Core に統合されました）

# RingBufferテスト
add_executable(RingBufferTest RingBufferTest.cpp)
target_link_libraries(RingBufferTest PRIVATE Core)
target_include_directories(RingBufferTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RingBufferTest PROPERTIES FOLDER "Test/Thread")

# ThreadLocalStorageテスト
add_executable(ThreadLocalStorageTest ThreadLocalStorageTest.cpp)
target_link_libraries(ThreadLocalStorageTest PRIVATE Core)
target_include_directories(ThreadLocalStorageTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ThreadLocalStorageTest PROPERTIES FOLDER "Test/Thread")

# JobSystem shutdownテスト
add_executable(JobSystemShutdownTest JobSystemShutdownTest.cpp)
target_link_libraries(JobSystemShutdownTest PRIVATE Core)
target_include_directories(JobSystemShutdownTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(JobSystemShutdownTest PROPERTIES FOLDER "Test/Thread")

# テスト用にスレッドライブラリを追加（必要な場合）
find_package(Threads REQUIRED)
target_link_libraries(RingBufferTest PRIVATE Threads::Threads)
target_link_libraries(ThreadLocalStorageTest PRIVATE Threads::Threads)
target_link_libraries(JobSystemShutdownTest PRIVATE Threads::Threads)

# テストの登録（CTestを使用する場合）
include(CTest)
add_test(NAME RingBufferTest COMMAND RingBufferTest)
add_test(NAME JobSystemShutdownTest COMMAND JobSystemShutdownTest)
add_test(NAME ThreadLocalStorageTest COMMAND ThreadLocalStorageTest)