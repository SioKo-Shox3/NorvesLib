include(CTest)
add_test(NAME InputRouterTest COMMAND LoggerSinkTest --test=InputRouterTest)
add_test(NAME InputStateReleaseTest COMMAND LoggerSinkTest --test=InputStateReleaseTest)
