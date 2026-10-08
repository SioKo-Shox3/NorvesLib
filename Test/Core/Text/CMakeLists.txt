include(CTest)
add_test(NAME IdentityLiteralTest COMMAND LoggerSinkTest --test=IdentityLiteralTest)
add_test(NAME JsonWriterTest COMMAND LoggerSinkTest --test=JsonWriterTest)
add_test(NAME JsonUnicodeScalarTest COMMAND LoggerSinkTest --test=JsonUnicodeScalarTest)
add_test(NAME StringEmptyTest COMMAND CookedMeshTest --test=StringEmptyTest)

add_test(NAME StringReplaceTest COMMAND CookedMeshTest --test=StringReplaceTest)
