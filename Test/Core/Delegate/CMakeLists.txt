include(CTest)
add_test(NAME DelegateCopyTest COMMAND LoggerSinkTest --test=DelegateCopyTest)
add_test(NAME DelegateIdentityTest COMMAND LoggerSinkTest --test=DelegateIdentityTest)
