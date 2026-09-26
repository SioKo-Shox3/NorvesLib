include(CTest)
add_test(NAME StructPropertySerializeTest COMMAND ObjectReflectionTest --test=StructPropertySerializeTest)
add_test(NAME EnumPropertySerializeTest COMMAND ObjectReflectionTest --test=EnumPropertySerializeTest)
