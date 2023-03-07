aux_source_directory(. TEST_LISTS)
foreach(TEST ${TEST_LISTS})
    STRING(REGEX REPLACE "^\\./|\\.c[a-zA-Z0-9_]*$" "" TEST_EXE_NAME ${TEST})
    add_executable(${TEST_EXE_NAME}  ${TEST})

    target_link_libraries(${TEST_EXE_NAME}  ZLToolKit pthread myNetWork)

endforeach(TEST ${TEST_LISTS})