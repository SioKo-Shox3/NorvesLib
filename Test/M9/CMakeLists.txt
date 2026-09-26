add_test(
    NAME M9WorldAcceptanceTest
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/Scripts/Test-M9World.ps1"
        -Configuration "$<CONFIG>"
        -BuildDirectory "${CMAKE_BINARY_DIR}"
)
set_tests_properties(M9WorldAcceptanceTest PROPERTIES TIMEOUT 300)
# GPU・音声デバイス・Game.exe を使う受入れの確認なので、常設の段から外す。
set_property(TEST M9WorldAcceptanceTest APPEND PROPERTY LABELS Reference)
