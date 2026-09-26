# Thread モジュールのテスト（Thread は Core に統合されました）

# テスト用にスレッドライブラリを追加（必要な場合）
find_package(Threads REQUIRED)


norves_add_test_bundle(JobSystemShutdownTest
    FOLDER "Test/Thread"
    MEMBER JobSystemShutdownTest JobSystemShutdownTest.cpp
    MEMBER RingBufferTest RingBufferTest.cpp
    MEMBER ThreadLocalStorageTest ThreadLocalStorageTest.cpp
)
target_link_libraries(JobSystemShutdownTest PRIVATE Core Threads::Threads)
target_include_directories(JobSystemShutdownTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# テストの登録（CTestを使用する場合）
include(CTest)
add_test(NAME RingBufferTest COMMAND JobSystemShutdownTest --test=RingBufferTest)
add_test(NAME JobSystemShutdownTest COMMAND JobSystemShutdownTest)
add_test(NAME ThreadLocalStorageTest COMMAND JobSystemShutdownTest --test=ThreadLocalStorageTest)
