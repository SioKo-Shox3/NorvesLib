# 静的ライブラリでは参照されないオブジェクトがリンク時に除外されるため、OBJECT を使う。
add_library(NorvesTestNoDialog OBJECT NorvesTestNoDialog.cpp)
set_target_properties(NorvesTestNoDialog PROPERTIES FOLDER "Test/TestSupport")

add_executable(TestAssertNoDialogTest TestAssertNoDialogTest.cpp)
set_target_properties(TestAssertNoDialogTest PROPERTIES FOLDER "Test/TestSupport")

# 共通処理が外れると Debug は対話窓で止まり、30 秒で Timeout となって WILL_FAIL でも失敗する。
# 共通処理が付いていれば abort() がすぐ非 0 で終了するため、WILL_FAIL により成功する。
add_test(NAME TestAssertNoDialogTest COMMAND TestAssertNoDialogTest)
set_tests_properties(TestAssertNoDialogTest PROPERTIES WILL_FAIL TRUE TIMEOUT 30)
