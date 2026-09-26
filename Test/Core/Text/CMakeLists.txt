include(CTest)
add_test(NAME IdentityLiteralTest COMMAND LoggerSinkTest --test=IdentityLiteralTest)
add_test(NAME JsonWriterTest COMMAND LoggerSinkTest --test=JsonWriterTest)
