add_executable(forumer_core_tests
    test_main.cpp
    test_crypto.cpp
    test_vault.cpp
)

target_link_libraries(forumer_core_tests PRIVATE forumer_core)

add_test(NAME forumer_core_tests COMMAND forumer_core_tests)