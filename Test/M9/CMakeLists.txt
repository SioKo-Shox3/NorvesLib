add_test(
    NAME M9WorldAcceptanceTest
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/Scripts/Test-M9World.ps1"
        -Configuration "$<CONFIG>"
        -BuildDirectory "${CMAKE_BINARY_DIR}"
)
set_tests_properties(M9WorldAcceptanceTest PROPERTIES TIMEOUT 300)
